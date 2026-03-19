// nmc_server/src/Core/OIDCValidator.cpp
#include "OIDCValidator.h"
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <algorithm>

namespace NMC::Server {

    OIDCValidator::OIDCValidator(const OIDCConfig& config)
            : cfg(config), endpoint(parseUrl(config.introspectionUrl)) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        if (endpoint.https) {
            httpsClient = std::make_unique<httplib::SSLClient>(endpoint.host, endpoint.port);
        } else {
            httpClient = std::make_unique<httplib::Client>(endpoint.host, endpoint.port);
        }
#else
        if (endpoint.https) {
            httpClient.reset();
        } else {
            httpClient = std::make_unique<httplib::Client>(endpoint.host, endpoint.port);
        }
#endif
        if (httpClient) {
            httpClient->set_connection_timeout(5);
            httpClient->set_read_timeout(10);
            httpClient->set_write_timeout(10);
        }
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        if (httpsClient) {
            httpsClient->set_connection_timeout(5);
            httpsClient->set_read_timeout(10);
            httpsClient->set_write_timeout(10);
        }
#endif
    }

    bool OIDCValidator::isConfigured() const {
        if (cfg.introspectionUrl.empty()) {
            return false;
        }
        if (endpoint.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            return httpsClient != nullptr;
#else
            return false;
#endif
        }
        return httpClient != nullptr;
    }

    OIDCValidator::ParsedUrl OIDCValidator::parseUrl(const std::string& url) const {
        ParsedUrl parsed{"", 443, "/", false};
        if (url.empty()) {
            return parsed;
        }

        std::string s = url;
        if (s.rfind("https://", 0) == 0) {
            parsed.https = true;
            parsed.port = 443;
            s.erase(0, 8);
        } else if (s.rfind("http://", 0) == 0) {
            parsed.https = false;
            parsed.port = 80;
            s.erase(0, 7);
        }

        auto slash = s.find('/');
        std::string hostPort = slash == std::string::npos ? s : s.substr(0, slash);
        parsed.path = slash == std::string::npos ? "/" : s.substr(slash);

        auto colon = hostPort.find(':');
        parsed.host = hostPort.substr(0, colon);
        if (colon != std::string::npos) {
            try {
                parsed.port = std::stoi(hostPort.substr(colon + 1));
            } catch (const std::exception&) {
                parsed.port = parsed.https ? 443 : 80;
            }
        }
        return parsed;
    }

    std::string OIDCValidator::urlEncode(const std::string& value) const {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;
        for (unsigned char c : value) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
            } else {
                escaped << '%' << std::uppercase << std::setw(2)
                        << static_cast<int>(c) << std::nouppercase;
            }
        }
        return escaped.str();
    }

    std::string OIDCValidator::base64Encode(const std::string& value) const {
        if (value.empty()) {
            return "";
        }
        std::string out;
        out.resize(4 * ((value.size() + 2) / 3));
        const int len = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&out[0]),
                                        reinterpret_cast<const unsigned char*>(value.data()),
                                        static_cast<int>(value.size()));
        out.resize(len);
        return out;
    }

    bool OIDCValidator::hasAudience(const nlohmann::json& claims, const std::vector<std::string>& audiences) const {
        if (audiences.empty()) {
            return true;
        }
        if (!claims.contains("aud")) {
            if (claims.contains("client_id") && claims["client_id"].is_string()) {
                const auto clientId = claims["client_id"].get<std::string>();
                return std::find(audiences.begin(), audiences.end(), clientId) != audiences.end();
            }
            return false;
        }
        if (claims["aud"].is_string()) {
            const auto audValue = claims["aud"].get<std::string>();
            return std::find(audiences.begin(), audiences.end(), audValue) != audiences.end();
        }
        if (claims["aud"].is_array()) {
            for (const auto& entry : claims["aud"]) {
                if (!entry.is_string()) {
                    continue;
                }
                const auto audValue = entry.get<std::string>();
                if (std::find(audiences.begin(), audiences.end(), audValue) != audiences.end()) {
                    return true;
                }
            }
        }
        return false;
    }

    bool OIDCValidator::hasRequiredScopes(const std::string& scopeString) const {
        if (cfg.requiredScopes.empty()) {
            return true;
        }
        std::vector<std::string> scopes;
        std::stringstream ss(scopeString);
        std::string item;
        while (ss >> item) {
            scopes.push_back(item);
        }
        for (const auto& required : cfg.requiredScopes) {
            bool found = false;
            for (const auto& scope : scopes) {
                if (scope == required) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
        return true;
    }

    bool OIDCValidator::validateToken(const std::string& bearerToken,
                                      std::string& errorMessage,
                                      nlohmann::json* claimsOut) const {
        if (!isConfigured()) {
            errorMessage = "OIDC validator is not configured.";
            return false;
        }
        if (bearerToken.empty()) {
            errorMessage = "Missing bearer token.";
            return false;
        }

        std::string body = "token=" + urlEncode(bearerToken);
        if (!cfg.clientId.empty() && cfg.clientSecret.empty()) {
            body += "&client_id=" + urlEncode(cfg.clientId);
        }

        httplib::Headers headers{
                {"Content-Type", "application/x-www-form-urlencoded"}
        };
        if (!cfg.clientId.empty() && !cfg.clientSecret.empty()) {
            const std::string basic = cfg.clientId + ":" + cfg.clientSecret;
            headers.emplace("Authorization", "Basic " + base64Encode(basic));
        }

        httplib::Result res;
        {
            std::lock_guard<std::mutex> lock(clientMutex);
            if (endpoint.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                if (!httpsClient) {
                    errorMessage = "HTTPS introspection requires OpenSSL-enabled httplib.";
                    return false;
                }
                res = httpsClient->Post(endpoint.path.c_str(), headers, body, "application/x-www-form-urlencoded");
#else
                errorMessage = "HTTPS introspection requires OpenSSL-enabled httplib.";
                return false;
#endif
            } else {
                if (!httpClient) {
                    errorMessage = "HTTP introspection client is not initialized.";
                    return false;
                }
                res = httpClient->Post(endpoint.path.c_str(), headers, body, "application/x-www-form-urlencoded");
            }
        }

        if (!res) {
            errorMessage = "OIDC introspection request failed: " + httplib::to_string(res.error());
            return false;
        }
        if (res->status < 200 || res->status >= 300) {
            errorMessage = "OIDC introspection endpoint returned status " + std::to_string(res->status);
            return false;
        }

        nlohmann::json claims;
        try {
            claims = nlohmann::json::parse(res->body);
        } catch (const nlohmann::json::parse_error& e) {
            errorMessage = "OIDC introspection response is not valid JSON.";
            return false;
        }

        const bool active = claims.value("active", false);
        if (!active) {
            errorMessage = "Token is not active.";
            return false;
        }

        if (!cfg.issuer.empty()) {
            if (!claims.contains("iss") || !claims["iss"].is_string()) {
                errorMessage = "Missing issuer claim.";
                return false;
            }
            if (claims["iss"].get<std::string>() != cfg.issuer) {
                errorMessage = "Issuer mismatch.";
                return false;
            }
        }

        if (!cfg.audiences.empty()) {
            if (!hasAudience(claims, cfg.audiences)) {
                errorMessage = "Audience mismatch.";
                return false;
            }
        }

        if (!cfg.requiredScopes.empty()) {
            if (!claims.contains("scope") || !claims["scope"].is_string()) {
                errorMessage = "Required scope missing.";
                return false;
            }
            if (!hasRequiredScopes(claims["scope"].get<std::string>())) {
                errorMessage = "Scope mismatch.";
                return false;
            }
        }

        if (claimsOut) {
            *claimsOut = claims;
        }
        return true;
    }

} // namespace NMC::Server
