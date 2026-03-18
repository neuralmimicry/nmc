// nmc_server/src/Core/OIDCValidator.h
#pragma once

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace NMC::Server {

    struct OIDCConfig {
        std::string introspectionUrl;
        std::string issuer;
        std::vector<std::string> audiences;
        std::vector<std::string> requiredScopes;
        std::string clientId;
        std::string clientSecret;
    };

    /**
     * @brief OIDC validator using RFC 7662 token introspection.
     *
     * This is a lightweight validation path intended for server-side enforcement
     * of bearer tokens. It validates token activity and optionally checks
     * issuer, audience, and scope.
     */
    class OIDCValidator {
    public:
        explicit OIDCValidator(const OIDCConfig& config);

        bool isConfigured() const;
        bool validateToken(const std::string& bearerToken,
                           std::string& errorMessage,
                           nlohmann::json* claimsOut) const;

    private:
        struct ParsedUrl {
            std::string host;
            int port;
            std::string path;
            bool https;
        };

        ParsedUrl parseUrl(const std::string& url) const;
        std::string urlEncode(const std::string& value) const;
        std::string base64Encode(const std::string& value) const;
        bool hasAudience(const nlohmann::json& claims, const std::vector<std::string>& audiences) const;
        bool hasRequiredScopes(const std::string& scopeString) const;

        OIDCConfig cfg;
        ParsedUrl endpoint;
        std::unique_ptr<httplib::Client> client;
        mutable std::mutex clientMutex;
    };

} // namespace NMC::Server
