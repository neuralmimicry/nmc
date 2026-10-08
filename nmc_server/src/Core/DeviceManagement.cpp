#include "DeviceManagement.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <spawn.h>
#endif

namespace NMC::Server {
namespace {

using Json = nlohmann::json;

#ifndef _WIN32
int createCloseOnExecPipe(int pipeFds[2]) {
#if defined(__linux__)
    return ::pipe2(pipeFds, O_CLOEXEC);
#else
    if (::pipe(pipeFds) != 0) return -1;
    for (int index = 0; index < 2; ++index) {
        const int flags = ::fcntl(pipeFds[index], F_GETFD, 0);
        if (flags < 0 || ::fcntl(pipeFds[index], F_SETFD, flags | FD_CLOEXEC) < 0) {
            const int savedError = errno;
            ::close(pipeFds[0]);
            ::close(pipeFds[1]);
            errno = savedError;
            return -1;
        }
    }
    return 0;
#endif
}
#endif

std::string lower(std::string value);

int64_t nowEpochMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string envValue(const std::string& name) {
    if (name.empty()) return {};
    const char* value = std::getenv(name.c_str());
    return value ? value : "";
}

bool safeIdentifier(const std::string& value) {
    static const std::regex pattern("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,127}$");
    return std::regex_match(value, pattern);
}

bool safeEnvName(const std::string& value) {
    static const std::regex pattern("^[A-Za-z_][A-Za-z0-9_]{0,127}$");
    return std::regex_match(value, pattern);
}

bool privateLanHost(const std::string& host) {
    const std::string normalized = lower(host);
    // Plain HTTP is allowed only for loopback or a private address literal.
    // Hostnames can be rebound to public addresses and must use HTTPS.
    if (normalized == "localhost") {
        return true;
    }
#ifndef _WIN32
    in_addr ipv4{};
    if (::inet_pton(AF_INET, host.c_str(), &ipv4) == 1) {
        const uint32_t address = ntohl(ipv4.s_addr);
        return (address >> 24) == 10 || (address >> 24) == 127
               || (address >> 20) == 0xAC1 || (address >> 16) == 0xC0A8
               || (address >> 16) == 0xA9FE;
    }
    in6_addr ipv6{};
    if (::inet_pton(AF_INET6, host.c_str(), &ipv6) == 1) {
        return IN6_IS_ADDR_LOOPBACK(&ipv6) || (ipv6.s6_addr[0] & 0xFE) == 0xFC
               || (ipv6.s6_addr[0] == 0xFE && (ipv6.s6_addr[1] & 0xC0) == 0x80);
    }
#endif
    return false;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

struct Endpoint {
    bool tls{false};
    std::string host;
    int port{0};
    std::string basePath;
    std::string caFile;
    std::string spkiSha256;
};

bool parseEndpoint(const std::string& value, bool allowHttp, Endpoint& parsed, std::string& error) {
    const size_t schemeEnd = value.find("://");
    if (schemeEnd == std::string::npos) {
        error = "endpoint must use an explicit http:// or https:// scheme";
        return false;
    }
    const std::string scheme = lower(value.substr(0, schemeEnd));
    if (scheme != "https" && (scheme != "http" || !allowHttp)) {
        error = "endpoint scheme is not permitted";
        return false;
    }
    const size_t authorityStart = schemeEnd + 3;
    const size_t pathStart = value.find('/', authorityStart);
    const std::string authority = value.substr(authorityStart,
            pathStart == std::string::npos ? std::string::npos : pathStart - authorityStart);
    if (authority.empty() || authority.find('@') != std::string::npos
        || authority.find('?') != std::string::npos || authority.find('#') != std::string::npos) {
        error = "endpoint authority is invalid";
        return false;
    }

    std::string host;
    int port = scheme == "https" ? 443 : 80;
    if (authority.front() == '[') {
        const size_t close = authority.find(']');
        if (close == std::string::npos) {
            error = "endpoint IPv6 authority is invalid";
            return false;
        }
        host = authority.substr(1, close - 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') {
                error = "endpoint port is invalid";
                return false;
            }
            try { port = std::stoi(authority.substr(close + 2)); }
            catch (...) { error = "endpoint port is invalid"; return false; }
        }
    } else {
        const size_t colon = authority.rfind(':');
        if (colon != std::string::npos) {
            host = authority.substr(0, colon);
            try { port = std::stoi(authority.substr(colon + 1)); }
            catch (...) { error = "endpoint port is invalid"; return false; }
        } else {
            host = authority;
        }
    }
    if (host.empty() || host.find_first_of(" \t\r\n\\") != std::string::npos || port < 1 || port > 65535) {
        error = "endpoint host or port is invalid";
        return false;
    }
    const std::string basePath = pathStart == std::string::npos ? "" : value.substr(pathStart);
    if (basePath.find_first_of("?#") != std::string::npos || basePath.find("..") != std::string::npos) {
        error = "endpoint path is invalid";
        return false;
    }
    parsed = {scheme == "https", host, port, basePath};
    return true;
}

template <typename F>
auto withHttpClient(const Endpoint& endpoint, F&& call) -> decltype(call(std::declval<httplib::Client&>())) {
    if (endpoint.tls) {
        httplib::SSLClient client(endpoint.host, endpoint.port);
        if (!endpoint.caFile.empty()) {
            std::ifstream caInput(endpoint.caFile, std::ios::binary);
            if (!caInput) return {};
            const std::string caCertificate((std::istreambuf_iterator<char>(caInput)), std::istreambuf_iterator<char>());
            if (caCertificate.empty() || caCertificate.size() > 1024 * 1024) return {};
            client.load_ca_cert_store(caCertificate.data(), caCertificate.size());
        }
        if (!endpoint.spkiSha256.empty()) {
            // A pinned key is an explicit trust anchor for fixed BMCs whose
            // factory certificate cannot pass normal CA, date or name checks.
            // The callback accepts only the configured key and rejects every
            // other certificate during the TLS handshake.
            const std::string expectedPin = endpoint.spkiSha256;
            client.set_server_certificate_verifier([expectedPin](SSL* ssl) {
                X509* certificate = ssl ? SSL_get1_peer_certificate(ssl) : nullptr;
                if (!certificate) return httplib::SSLVerifierResponse::CertificateRejected;
                EVP_PKEY* publicKey = X509_get_pubkey(certificate);
                X509_free(certificate);
                if (!publicKey) return httplib::SSLVerifierResponse::CertificateRejected;

                const int derSize = i2d_PUBKEY(publicKey, nullptr);
                if (derSize <= 0 || derSize > 64 * 1024) {
                    EVP_PKEY_free(publicKey);
                    return httplib::SSLVerifierResponse::CertificateRejected;
                }
                std::vector<unsigned char> der(static_cast<size_t>(derSize));
                unsigned char* derCursor = der.data();
                const int encodedSize = i2d_PUBKEY(publicKey, &derCursor);
                EVP_PKEY_free(publicKey);
                if (encodedSize != derSize) return httplib::SSLVerifierResponse::CertificateRejected;

                std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
                unsigned int digestSize = 0;
                if (EVP_Digest(der.data(), der.size(), digest.data(), &digestSize, EVP_sha256(), nullptr) != 1
                    || digestSize != 32) {
                    return httplib::SSLVerifierResponse::CertificateRejected;
                }
                static constexpr char hex[] = "0123456789abcdef";
                std::string actualPin;
                actualPin.reserve(digestSize * 2);
                for (unsigned int index = 0; index < digestSize; ++index) {
                    actualPin.push_back(hex[digest[index] >> 4]);
                    actualPin.push_back(hex[digest[index] & 0x0f]);
                }
                const bool pinMatches = actualPin.size() == expectedPin.size()
                                         && CRYPTO_memcmp(actualPin.data(), expectedPin.data(), expectedPin.size()) == 0;
                return pinMatches ? httplib::SSLVerifierResponse::CertificateAccepted
                                  : httplib::SSLVerifierResponse::CertificateRejected;
            });
        }
        if (!client.is_valid()) return {};
        client.set_connection_timeout(3, 0);
        client.set_read_timeout(6, 0);
        client.set_write_timeout(6, 0);
        client.set_follow_location(false);
        return call(client);
    }
    httplib::Client client(endpoint.host, endpoint.port);
    client.set_connection_timeout(3, 0);
    client.set_read_timeout(6, 0);
    client.set_write_timeout(6, 0);
    client.set_follow_location(false);
    return call(client);
}

bool parseControllerEndpoint(const Json& controller, Endpoint& endpoint, std::string& error) {
    if (!parseEndpoint(controller.value("endpoint", std::string{}), false, endpoint, error)) return false;
    if (controller.contains("tls_spki_sha256")) {
        if (!controller["tls_spki_sha256"].is_string()) {
            error = "tls_spki_sha256 must be a SHA-256 hex digest of the BMC public key";
            return false;
        }
        std::string pin = lower(controller["tls_spki_sha256"].get<std::string>());
        static const std::regex pinPattern("^[0-9a-f]{64}$");
        if (!std::regex_match(pin, pinPattern)) {
            error = "tls_spki_sha256 must be a 64-character SHA-256 hex digest of the BMC public key";
            return false;
        }
        endpoint.spkiSha256 = std::move(pin);
    }
    if (controller.contains("tls_ca_file")) {
        if (!controller["tls_ca_file"].is_string()) {
            error = "tls_ca_file must be an absolute CA bundle path";
            return false;
        }
        const std::string caPath = controller["tls_ca_file"].get<std::string>();
        try {
            const std::filesystem::path path(caPath);
            if (!path.is_absolute() || !std::filesystem::is_regular_file(path)
                || std::filesystem::file_size(path) > 1024 * 1024) {
                error = "tls_ca_file must be a readable regular file of at most 1 MiB";
                return false;
            }
        } catch (...) {
            error = "tls_ca_file could not be inspected";
            return false;
        }
        endpoint.caFile = caPath;
    }
    return true;
}

std::string appendBasePath(const Endpoint& endpoint, const std::string& path) {
    std::string base = endpoint.basePath;
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base + path;
}

bool safeRedfishPath(const std::string& path) {
    return path.size() <= 512 && (path == "/redfish/v1" || path.rfind("/redfish/v1/", 0) == 0)
           && path.rfind("//", 0) != 0 && path.find("..") == std::string::npos
           && path.find_first_of("?#\\\\") == std::string::npos;
}

std::string appendRedfishPath(const Endpoint& endpoint, const std::string& path) {
    std::string base = endpoint.basePath;
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (!base.empty() && path.rfind(base + "/", 0) == 0) return path;
    return base + path;
}

std::string redfishVendor(const std::vector<std::string>& values) {
    std::string identity;
    for (const auto& value : values) {
        if (!identity.empty()) identity.push_back(' ');
        identity += lower(value);
    }
    if (identity.find("hewlett packard") != std::string::npos || identity.find("hewlett-packard") != std::string::npos
        || identity.find("hpe") != std::string::npos || identity.find("ilo") != std::string::npos) {
        return "hp_ilo";
    }
    if (identity.find("dell") != std::string::npos || identity.find("idrac") != std::string::npos) {
        return "dell_idrac";
    }
    if (identity.find("american megatrends") != std::string::npos || identity.find("megarac") != std::string::npos
        || identity.find("ami bmc") != std::string::npos) {
        return "ami_megarac";
    }
    return "generic_redfish";
}

Json errorBody(const std::string& code, const std::string& message) {
    return Json{{"success", false}, {"error", {{"code", code}, {"message", message}}}};
}

Json okBody(Json data) {
    return Json{{"success", true}, {"data", std::move(data)}};
}

bool explicitSuccess(const Json& value, int depth = 0) {
    if (depth > 5) return false;
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number_integer()) return value.get<int64_t>() == 0;
    if (value.is_string()) {
        const std::string text = lower(value.get<std::string>());
        if (text.find("unsuccess") != std::string::npos || text.find("fail") != std::string::npos
            || text.find("error") != std::string::npos) return false;
        return text == "ok" || text == "success" || text == "true" || text.find("success") != std::string::npos;
    }
    if (value.is_array()) {
        for (const auto& item : value) if (explicitSuccess(item, depth + 1)) return true;
        return false;
    }
    if (!value.is_object()) return false;
    for (const char* key : {"success", "ok"}) {
        if (value.contains(key)) return explicitSuccess(value[key], depth + 1);
    }
    if (value.contains("error") && !value["error"].is_null() && value["error"] != false
        && value["error"] != "") return false;
    for (const char* key : {"status", "response", "result"})
        if (value.contains(key)) return explicitSuccess(value[key], depth + 1);
    if (value.contains("code") && value["code"].is_number_integer()) return value["code"].get<int64_t>() == 0;
    return false;
}

Json publicDevice(const Json& source) {
    Json result = Json::object();
    for (const char* key : {"id", "name", "kind", "environment", "criticality", "addresses",
                            "services", "depends_on", "affected_services", "controller_id", "controller_target",
                            "turingpi_slot", "home_assistant_entity", "mac_addresses", "dhcp_client_ids",
                            "source", "last_seen_unix_ms"}) {
        if (source.contains(key)) result[key] = source[key];
    }
    return result;
}

std::string canonicalMac(const std::string& value) {
    static const std::regex pattern("^([0-9A-Fa-f]{2})[:-]([0-9A-Fa-f]{2})[:-]([0-9A-Fa-f]{2})[:-]([0-9A-Fa-f]{2})[:-]([0-9A-Fa-f]{2})[:-]([0-9A-Fa-f]{2})$");
    std::smatch match;
    if (!std::regex_match(value, match, pattern)) return {};
    std::string result;
    for (size_t i = 1; i < match.size(); ++i) {
        if (i > 1) result.push_back(':');
        result += lower(match[i].str());
    }
    return result;
}

bool validDhcpAddress(const std::string& value) {
#ifndef _WIN32
    in_addr ipv4{};
    in6_addr ipv6{};
    return ::inet_pton(AF_INET, value.c_str(), &ipv4) == 1
           || ::inet_pton(AF_INET6, value.c_str(), &ipv6) == 1;
#else
    return !value.empty() && value.size() <= 64;
#endif
}

bool validDhcpOptionValue(const Json& value) {
    if (value.is_string()) return value.get<std::string>().size() <= 4096;
    if (value.is_number() || value.is_boolean() || value.is_null()) return true;
    if (!value.is_array() || value.size() > 128) return false;
    for (const auto& item : value) {
        if (item.is_string()) {
            if (item.get<std::string>().size() > 512) return false;
        } else if (!item.is_number_integer()) {
            return false;
        }
    }
    return true;
}

Json queryDhcpServer(const Json& config) {
    const std::string id = config.value("id", std::string{});
    const std::string expectedServerId = config.value("server_id", std::string{});
    const auto unavailable = [&](const std::string& code, const std::string& message) {
        return Json{{"id", id}, {"server_id", expectedServerId}, {"available", false},
                    {"error", {{"code", code}, {"message", message}}}};
    };
    if (!config.contains("allow_http") || !config["allow_http"].is_boolean()) {
        // Omitted means HTTPS only; this branch handles a malformed explicit value.
        if (config.contains("allow_http")) return unavailable("dhcp_config_invalid", "allow_http must be a boolean");
    }
    const bool allowHttp = config.value("allow_http", false);
    Endpoint endpoint;
    std::string endpointError;
    if (!parseEndpoint(config.value("endpoint", std::string{}), allowHttp, endpoint, endpointError)) {
        return unavailable("dhcp_config_invalid", endpointError);
    }
    if (!endpoint.tls && !privateLanHost(endpoint.host)) {
        return unavailable("dhcp_config_invalid", "unencrypted DHCP collector endpoints must use a loopback or private IP address literal");
    }
    if (config.contains("tls_ca_file")) {
        if (!config["tls_ca_file"].is_string()) {
            return unavailable("dhcp_config_invalid", "tls_ca_file must be an absolute CA bundle path");
        }
        try {
            const std::filesystem::path caPath(config["tls_ca_file"].get<std::string>());
            if (!caPath.is_absolute() || !std::filesystem::is_regular_file(caPath)
                || std::filesystem::file_size(caPath) > 1024 * 1024) {
                return unavailable("dhcp_config_invalid", "tls_ca_file must be a readable regular file of at most 1 MiB");
            }
            endpoint.caFile = caPath.string();
        } catch (...) {
            return unavailable("dhcp_config_invalid", "tls_ca_file could not be inspected");
        }
    }
    const std::string tokenEnv = config.value("token_env", std::string{});
    if (!safeEnvName(tokenEnv) || envValue(tokenEnv).empty()) {
        return unavailable("dhcp_credentials_unavailable", "DHCP collector bearer token reference is unavailable");
    }
    auto response = withHttpClient(endpoint, [&](auto& client) {
        httplib::Headers headers{{"Authorization", "Bearer " + envValue(tokenEnv)}};
        return client.Get(appendBasePath(endpoint, "/api/v1/dhcp/leases"), headers);
    });
    if (!response || response->status != 200) {
        return unavailable("dhcp_collector_unavailable", "DHCP collector did not return HTTP 200");
    }
    if (response->body.size() > 2 * 1024 * 1024) {
        return unavailable("dhcp_response_too_large", "DHCP collector response exceeds 2 MiB");
    }
    try {
        const Json payload = Json::parse(response->body);
        if (!payload.is_object() || payload.value("schema_version", 0) != 1
            || !payload.contains("server_id") || !payload["server_id"].is_string()
            || payload["server_id"].get<std::string>() != expectedServerId
            || !payload.contains("observed_at_unix_ms") || !payload["observed_at_unix_ms"].is_number_integer()
            || !payload.contains("leases") || !payload["leases"].is_array() || payload["leases"].size() > 8192) {
            return unavailable("dhcp_response_invalid", "DHCP collector identity or lease schema does not match the configured source");
        }
        const int64_t observedAt = payload["observed_at_unix_ms"].get<int64_t>();
        const int64_t maxAge = config.value("max_age_ms", int64_t{300000});
        if (observedAt <= 0 || maxAge < 1000 || maxAge > 86400000 || observedAt > nowEpochMs() + 30000) {
            return unavailable("dhcp_response_invalid", "DHCP observation time or configured freshness limit is invalid");
        }
        Json leases = Json::array();
        for (const auto& lease : payload["leases"]) {
            if (!lease.is_object() || !lease.contains("address") || !lease["address"].is_string()
                || !validDhcpAddress(lease["address"].get<std::string>())) {
                return unavailable("dhcp_response_invalid", "DHCP lease contains an invalid address");
            }
            const std::string mac = lease.value("mac_address", std::string{});
            const std::string clientId = lease.value("client_id", std::string{});
            if ((!mac.empty() && canonicalMac(mac).empty()) || (mac.empty() && clientId.empty())
                || clientId.size() > 512 || (lease.contains("hostname")
                    && (!lease["hostname"].is_string() || lease["hostname"].get<std::string>().size() > 253))
                || (lease.contains("lease_expires_at_unix_ms") && !lease["lease_expires_at_unix_ms"].is_number_integer())) {
                return unavailable("dhcp_response_invalid", "DHCP lease identity, hostname or expiry is malformed");
            }
            const Json options = lease.value("options", Json::array());
            if (!options.is_array() || options.size() > 256) {
                return unavailable("dhcp_response_invalid", "DHCP lease options must be an array of at most 256 entries");
            }
            for (const auto& option : options) {
                if (!option.is_object() || !option.contains("code") || !option["code"].is_number_integer()
                    || option["code"].get<int>() < 1 || option["code"].get<int>() > 254
                    || !option.contains("name") || !option["name"].is_string()
                    || option["name"].get<std::string>().empty() || option["name"].get<std::string>().size() > 64
                    || !option.contains("value") || !validDhcpOptionValue(option["value"])
                    || !option.contains("source") || !option["source"].is_string()
                    || (option["source"] != "dhcp_ack" && option["source"] != "server_policy" && option["source"] != "client_request")
                    || !option.contains("observed_at_unix_ms") || !option["observed_at_unix_ms"].is_number_integer()) {
                    return unavailable("dhcp_response_invalid", "DHCP option must include a valid code, name, bounded value, evidence source and observation time");
                }
                const int64_t optionTime = option["observed_at_unix_ms"].get<int64_t>();
                if (optionTime <= 0 || optionTime > nowEpochMs() + 30000 || nowEpochMs() - optionTime > maxAge) {
                    return unavailable("dhcp_response_invalid", "DHCP option evidence is outside the configured freshness window");
                }
            }
            leases.push_back(lease);
        }
        const int64_t age = nowEpochMs() - observedAt;
        return Json{{"id", id}, {"server_id", expectedServerId}, {"available", true},
                    {"observed_at_unix_ms", observedAt}, {"stale", age > maxAge}, {"leases", std::move(leases)}};
    } catch (const std::exception&) {
        return unavailable("dhcp_response_invalid", "DHCP collector returned malformed JSON or data");
    }
}

Json publicController(const Json& source) {
    Json result = Json::object();
    for (const char* key : {"id", "protocol", "endpoint", "vendor_profile", "mac_address",
                            "manages", "power_actions_enabled", "source"}) {
        if (source.contains(key)) result[key] = source[key];
    }
    return result;
}

const Json* findById(const Json& values, const std::string& id) {
    if (!values.is_array()) return nullptr;
    for (const auto& value : values) {
        if (value.is_object() && value.value("id", std::string{}) == id) return &value;
    }
    return nullptr;
}

std::string controllerUsername(const Json& controller);
std::string controllerPassword(const Json& controller);

struct CommandResult {
    int exitCode{1};
    bool timedOut{false};
    std::string output;
};

#ifndef _WIN32
bool trustedIpmitoolOverride(const std::string& configuredExecutable) {
    const std::filesystem::path candidate(configuredExecutable);
    if (!candidate.is_absolute()) return false;
    for (const auto& component : candidate) {
        if (component == "..") return false;
    }

    std::filesystem::path current = candidate.root_path();
    struct stat metadata {};
    if (current.empty() || ::lstat(current.c_str(), &metadata) != 0
        || !S_ISDIR(metadata.st_mode) || (metadata.st_uid != 0 && metadata.st_uid != ::geteuid())) {
        return false;
    }

    auto component = candidate.begin();
    if (component != candidate.end()) ++component; // The root directory was checked above.
    for (; component != candidate.end(); ++component) {
        current /= *component;
        if (::lstat(current.c_str(), &metadata) != 0) return false;
        const bool isExecutable = std::next(component) == candidate.end();
        if (isExecutable) {
            return S_ISREG(metadata.st_mode)
                   && (metadata.st_mode & (S_IWGRP | S_IWOTH)) == 0
                   && (metadata.st_mode & (S_ISUID | S_ISGID)) == 0
                   && (metadata.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0
                   && (metadata.st_uid == 0 || metadata.st_uid == ::geteuid());
        }

        const bool trustedOwner = metadata.st_uid == 0 || metadata.st_uid == ::geteuid();
        const bool writableDirectory = (metadata.st_mode & (S_IWGRP | S_IWOTH)) != 0;
        const bool protectedSharedDirectory = metadata.st_uid == 0 && (metadata.st_mode & S_ISVTX) != 0;
        if (!S_ISDIR(metadata.st_mode) || !trustedOwner || (writableDirectory && !protectedSharedDirectory)) {
            return false;
        }
    }
    return false;
}
#endif

CommandResult runIpmitool(const Json& controller, const std::vector<std::string>& command) {
    CommandResult result;
#ifdef _WIN32
    result.output = "IPMI command execution is not available on this server platform";
    return result;
#else
    const std::string host = controller.value("endpoint", std::string{});
    const std::string username = controllerUsername(controller);
    const std::string password = controllerPassword(controller);
    if (host.empty() || username.empty() || password.empty() || command.empty()) {
        result.output = "IPMI host or credential reference is unavailable";
        return result;
    }
    std::string executable;
    const std::string configuredExecutable = envValue("NMC_IPMITOOL_PATH");
    if (!configuredExecutable.empty()) {
        if (!trustedIpmitoolOverride(configuredExecutable)) {
            result.exitCode = 127;
            result.output = "NMC_IPMITOOL_PATH must name an executable in trusted, non-writable directories without symlinks";
            return result;
        }
        executable = configuredExecutable;
    } else {
        for (const char* candidate : {"/usr/sbin/ipmitool", "/usr/bin/ipmitool"}) {
            if (::access(candidate, X_OK) == 0) {
                executable = candidate;
                break;
            }
        }
    }
    if (executable.empty()) {
        result.exitCode = 127;
        result.output = "ipmitool executable is unavailable in the fixed system paths";
        return result;
    }
    int pipeFds[2];
    if (createCloseOnExecPipe(pipeFds) != 0) {
        result.output = "unable to create IPMI output pipe";
        return result;
    }
    std::vector<std::string> args{"ipmitool", "-I", "lanplus", "-H", host, "-U", username, "-E"};
    args.insert(args.end(), command.begin(), command.end());
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    std::vector<std::string> environmentStorage{
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
        "LC_ALL=C",
        "IPMI_PASSWORD=" + password
    };
    std::vector<char*> environment;
    environment.reserve(environmentStorage.size() + 1);
    for (auto& item : environmentStorage) environment.push_back(item.data());
    environment.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    if (::posix_spawn_file_actions_init(&actions) != 0) {
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        result.output = "unable to initialize IPMI process file actions";
        return result;
    }
    ::posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDOUT_FILENO);
    ::posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDERR_FILENO);
    ::posix_spawn_file_actions_addclose(&actions, pipeFds[0]);
    ::posix_spawn_file_actions_addclose(&actions, pipeFds[1]);
    pid_t child = -1;
    const int spawnError = ::posix_spawn(&child, executable.c_str(), &actions, nullptr, argv.data(), environment.data());
    ::posix_spawn_file_actions_destroy(&actions);
    if (spawnError != 0) {
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        result.exitCode = spawnError == ENOENT ? 127 : 1;
        result.output = spawnError == ENOENT ? "ipmitool executable disappeared before start" : "unable to start ipmitool";
        return result;
    }

    ::close(pipeFds[1]);
    const int oldFlags = ::fcntl(pipeFds[0], F_GETFL, 0);
    if (oldFlags >= 0) ::fcntl(pipeFds[0], F_SETFL, oldFlags | O_NONBLOCK);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    bool pipeOpen = true;
    bool childExited = false;
    int waitStatus = 0;
    std::array<char, 4096> buffer{};
    while (pipeOpen || !childExited) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) {
            result.timedOut = true;
            ::kill(child, SIGKILL);
            break;
        }
        struct pollfd pollFd { pipeFds[0], static_cast<short>(POLLIN | POLLHUP), 0 };
        const int pollResult = ::poll(&pollFd, 1, static_cast<int>(std::min<int64_t>(remaining, 250)));
        if (pollResult > 0 && pipeOpen) {
            while (true) {
                const ssize_t count = ::read(pipeFds[0], buffer.data(), buffer.size());
                if (count > 0) {
                    const size_t room = result.output.size() < 65536 ? 65536 - result.output.size() : 0;
                    result.output.append(buffer.data(), std::min<size_t>(static_cast<size_t>(count), room));
                    if (result.output.size() >= 65536) {
                        result.output += "\n(output truncated)";
                        ::kill(child, SIGKILL);
                        break;
                    }
                    continue;
                }
                if (count == 0) pipeOpen = false;
                if (count < 0 && errno != EAGAIN && errno != EINTR) pipeOpen = false;
                break;
            }
        }
        const pid_t waited = ::waitpid(child, &waitStatus, WNOHANG);
        if (waited == child) childExited = true;
    }
    if (!childExited) ::waitpid(child, &waitStatus, 0);
    ::close(pipeFds[0]);
    if (!result.timedOut && WIFEXITED(waitStatus)) result.exitCode = WEXITSTATUS(waitStatus);
    return result;
#endif
}

class AuditLock {
public:
    AuditLock() = default;
    AuditLock(const AuditLock&) = delete;
    AuditLock& operator=(const AuditLock&) = delete;
    ~AuditLock() {
#ifndef _WIN32
        if (fd_ >= 0) ::close(fd_);
#endif
    }

    bool acquire(const std::string& path, std::string& error) {
#ifdef _WIN32
        error = "durable controller-action audit locking is unavailable on this server platform";
        return false;
#else
        if (path.empty()) {
            error = "NMC_DEVICE_ACTION_AUDIT_PATH is not configured";
            return false;
        }
        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd_ < 0) {
            error = "controller action audit file could not be opened securely";
            return false;
        }
        struct stat info {};
        if (::fstat(fd_, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != ::geteuid()
            || (info.st_mode & 0077) != 0) {
            error = "controller action audit file must be an owner-only regular file owned by the Continuum service user";
            return false;
        }
        if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            error = "another controller action currently holds the durable audit lock";
            return false;
        }
        return true;
#endif
    }

private:
#ifndef _WIN32
    int fd_{-1};
#endif
};

bool appendAuditRecord(const std::string& auditPath, const Json& record, std::string& error) {
#ifdef _WIN32
    error = "durable controller-action audit is unavailable on this server platform";
    return false;
#else
    if (auditPath.empty()) {
        error = "NMC_DEVICE_ACTION_AUDIT_PATH is not configured";
        return false;
    }
    const int fd = ::open(auditPath.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        error = "controller action audit file could not be opened securely";
        return false;
    }
    struct stat info {};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != ::geteuid()
        || (info.st_mode & 0077) != 0) {
        ::close(fd);
        error = "controller action audit target must be an owner-only regular file owned by the Continuum service user";
        return false;
    }
    const std::string line = record.dump() + "\n";
    size_t offset = 0;
    while (offset < line.size()) {
        const ssize_t written = ::write(fd, line.data() + offset, line.size() - offset);
        if (written <= 0) {
            ::close(fd);
            error = "controller action audit record could not be written";
            return false;
        }
        offset += static_cast<size_t>(written);
    }
    const bool durable = ::fsync(fd) == 0;
    ::close(fd);
    if (!durable) {
        error = "controller action audit record could not be synchronized to storage";
        return false;
    }
    return true;
#endif
}

Json findAuditRequest(const std::string& auditPath, const std::string& requestId) {
    std::ifstream input(auditPath);
    if (!input) {
        std::error_code error;
        if (!std::filesystem::exists(auditPath, error) && !error) return Json::object();
        return Json{{"audit_scan_error", true}};
    }
    std::string line;
    Json last = Json::object();
    size_t lines = 0;
    while (std::getline(input, line)) {
        if (++lines > 100000 || line.size() > 1024 * 1024) return Json{{"audit_scan_error", true}};
        try {
            const Json record = Json::parse(line);
            if (record.value("request_id", std::string{}) == requestId) last = record;
        } catch (...) {
            return Json{{"audit_scan_error", true}};
        }
    }
    if (input.bad()) return Json{{"audit_scan_error", true}};
    return last;
}

std::vector<std::string> stringArray(const Json& value) {
    std::vector<std::string> result;
    if (!value.is_array()) return result;
    for (const auto& item : value) if (item.is_string()) result.push_back(item.get<std::string>());
    std::sort(result.begin(), result.end());
    return result;
}

bool trueField(const Json& object, const char* key) {
    return object.is_object() && object.contains(key) && object[key].is_boolean() && object[key].get<bool>();
}

std::string stringField(const Json& object, const char* key) {
    return object.is_object() && object.contains(key) && object[key].is_string()
           ? object[key].get<std::string>() : std::string{};
}

std::optional<bool> turingPiPowerState(Json value, int oneBasedSlot) {
    if (oneBasedSlot < 1 || oneBasedSlot > 4) return std::nullopt;
    for (int depth = 0; depth < 6; ++depth) {
        if (value.is_object()) {
            const std::string slotKey = "node" + std::to_string(oneBasedSlot);
            if (value.contains(slotKey)) {
                const Json state = value[slotKey];
                if (state.is_boolean()) return state.get<bool>();
                if (state.is_number_integer()) return state.get<int>() != 0;
                if (state.is_string()) {
                    const std::string text = lower(state.get<std::string>());
                    if (text == "1" || text == "on" || text == "true") return true;
                    if (text == "0" || text == "off" || text == "false") return false;
                }
                return std::nullopt;
            }
            if (value.contains("response")) {
                value = value["response"];
                continue;
            }
            if (value.contains("result")) {
                value = value["result"];
                continue;
            }
            return std::nullopt;
        }
        if (value.is_array()) {
            if (value.size() == 4 && (value[static_cast<size_t>(oneBasedSlot - 1)].is_string()
                || value[static_cast<size_t>(oneBasedSlot - 1)].is_boolean()
                || value[static_cast<size_t>(oneBasedSlot - 1)].is_number_integer())) {
                Json wrapper = Json::object();
                wrapper["node" + std::to_string(oneBasedSlot)] = value[static_cast<size_t>(oneBasedSlot - 1)];
                value = std::move(wrapper);
                continue;
            }
            if (value.empty()) return std::nullopt;
            value = value[0];
            continue;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

bool validateInventory(Json& document, std::string& error) {
    if (!document.is_object() || document.value("schema_version", 0) != 1
        || !document.contains("revision") || !document["revision"].is_string()
        || document["revision"].get<std::string>().empty()
        || !document.contains("dependency_graph_revision") || !document["dependency_graph_revision"].is_string()
        || document["dependency_graph_revision"].get<std::string>().empty()
        || !document.contains("generated_at_unix_ms") || !document["generated_at_unix_ms"].is_number_integer()
        || !document.contains("devices") || !document["devices"].is_array()
        || !document.contains("controllers") || !document["controllers"].is_array()) {
        error = "inventory must use schema_version 1 and include revision, generated_at_unix_ms, devices and controllers";
        return false;
    }
    if (document.contains("max_age_ms") && !document["max_age_ms"].is_number_integer()) {
        error = "inventory max_age_ms must be an integer";
        return false;
    }
    if (document["devices"].size() > 10000 || document["controllers"].size() > 2048) {
        error = "inventory exceeds the supported device or controller count";
        return false;
    }

    std::unordered_set<std::string> deviceIds;
    for (const auto& device : document["devices"]) {
        if (!device.is_object() || !device.contains("id") || !device["id"].is_string()) {
            error = "each device must have a string id";
            return false;
        }
        const std::string id = device["id"].get<std::string>();
        if (!safeIdentifier(id) || !deviceIds.insert(id).second) {
            error = "device ids must be unique safe identifiers";
            return false;
        }
        if (!device.contains("kind") || !device["kind"].is_string()) {
            error = "each device must declare a kind";
            return false;
        }
        for (const char* key : {"services", "depends_on", "affected_services", "addresses",
                                "mac_addresses", "dhcp_client_ids"}) {
            if (device.contains(key) && !device[key].is_array()) {
                error = std::string("device field '") + key + "' must be an array";
                return false;
            }
            if (device.contains(key) && device[key].size() > 256) {
                error = std::string("device field '") + key + "' exceeds the supported entry count";
                return false;
            }
            if (device.contains(key)) {
                for (const auto& item : device[key]) {
                    if (!item.is_string() || item.get<std::string>().empty() || item.get<std::string>().size() > 512) {
                        error = std::string("device field '") + key + "' must contain bounded strings";
                        return false;
                    }
                    if (std::string(key) == "mac_addresses" && canonicalMac(item.get<std::string>()).empty()) {
                        error = "device mac_addresses must contain valid six-octet MAC addresses";
                        return false;
                    }
                }
            }
        }
    }

    std::unordered_set<std::string> graphNodes = deviceIds;
    for (const auto& device : document["devices"]) {
        for (const char* key : {"services", "affected_services"}) {
            if (!device.contains(key)) continue;
            for (const auto& item : device[key]) graphNodes.insert(item.get<std::string>());
        }
    }
    for (const auto& device : document["devices"]) {
        if (!device.contains("depends_on")) {
            error = "each device must declare an explicit depends_on array, which may be empty";
            return false;
        }
        for (const auto& dependency : device["depends_on"]) {
            if (!graphNodes.count(dependency.get<std::string>())) {
                error = "device depends_on contains an unknown host or service id";
                return false;
            }
        }
    }

    std::unordered_set<std::string> controllerIds;
    for (const auto& controller : document["controllers"]) {
        if (!controller.is_object() || !controller.contains("id") || !controller["id"].is_string()
            || !controller.contains("protocol") || !controller["protocol"].is_string()
            || !controller.contains("endpoint") || !controller["endpoint"].is_string()
            || !controller.contains("manages") || !controller["manages"].is_array()) {
            error = "each controller must declare id, protocol, endpoint and manages";
            return false;
        }
        const std::string id = controller["id"].get<std::string>();
        const std::string protocol = lower(controller["protocol"].get<std::string>());
        if (!safeIdentifier(id) || !controllerIds.insert(id).second
            || (protocol != "redfish" && protocol != "ipmi" && protocol != "turingpi")) {
            error = "controller ids must be unique and protocols must be redfish, ipmi or turingpi";
            return false;
        }
        if (controller.contains("vendor_profile")) {
            if (!controller["vendor_profile"].is_string()) {
                error = "controller vendor_profile must be a string";
                return false;
            }
            const std::string profile = lower(controller["vendor_profile"].get<std::string>());
            if ((profile != "generic_redfish" && profile != "hp_ilo" && profile != "dell_idrac"
                 && profile != "ami_megarac") || protocol != "redfish") {
                error = "vendor_profile must be generic_redfish, hp_ilo, dell_idrac or ami_megarac on a Redfish controller";
                return false;
            }
        }
        if (controller.contains("power_actions_enabled") && !controller["power_actions_enabled"].is_boolean()) {
            error = "controller power_actions_enabled must be a boolean";
            return false;
        }
        if (controller.contains("mac_address")
            && (!controller["mac_address"].is_string()
                || canonicalMac(controller["mac_address"].get<std::string>()).empty())) {
            error = "controller mac_address must be a valid six-octet MAC address";
            return false;
        }
        if (controller.contains("cold_restart_mode") && !controller["cold_restart_mode"].is_string()) {
            error = "controller cold_restart_mode must be a string";
            return false;
        }
        Endpoint endpoint;
        std::string endpointError;
        const bool endpointIsUrl = controller["endpoint"].get<std::string>().find("://") != std::string::npos;
        if (protocol == "ipmi") {
            const std::string host = controller["endpoint"].get<std::string>();
            static const std::regex hostPattern("^[A-Za-z0-9][A-Za-z0-9.:%-]{0,253}$");
            if (!std::regex_match(host, hostPattern)) {
                error = "IPMI endpoint must be a host name or address without a URL path";
                return false;
            }
        } else if (!endpointIsUrl || !parseControllerEndpoint(controller, endpoint, endpointError)) {
            error = "controller endpoint must be a valid HTTPS URL";
            return false;
        }
        for (const char* key : {"username_env", "password_env"}) {
            if (controller.contains(key) && (!controller[key].is_string()
                || !safeEnvName(controller[key].get<std::string>()))) {
                error = std::string("controller '") + key + "' must name an environment variable";
                return false;
            }
        }
        if (!controller.contains("username_env") || !controller.contains("password_env")) {
            error = "all controllers require username_env and password_env secret references";
            return false;
        }
        for (const auto& managed : controller["manages"]) {
            if (!managed.is_string() || !deviceIds.count(managed.get<std::string>())) {
                error = "controller manages contains an unknown device id";
                return false;
            }
            const Json* device = findById(document["devices"], managed.get<std::string>());
            if (!device || !device->contains("controller_id") || !(*device)["controller_id"].is_string()
                || (*device)["controller_id"].get<std::string>() != id) {
                error = "controller manages must agree with each device controller_id relationship";
                return false;
            }
        }
    }

    if (document.contains("dhcp_servers")) {
        if (!document["dhcp_servers"].is_array() || document["dhcp_servers"].size() > 8) {
            error = "dhcp_servers must be an array of at most 8 explicitly verified collectors";
            return false;
        }
        std::unordered_set<std::string> dhcpIds;
        std::unordered_set<std::string> dhcpServerIds;
        for (const auto& server : document["dhcp_servers"]) {
            if (!server.is_object() || !server.contains("id") || !server["id"].is_string()
                || !server.contains("server_id") || !server["server_id"].is_string()
                || !server.contains("endpoint") || !server["endpoint"].is_string()
                || !server.contains("token_env") || !server["token_env"].is_string()) {
                error = "each DHCP source must declare id, server_id, HTTPS endpoint and token_env";
                return false;
            }
            const std::string id = server["id"].get<std::string>();
            const std::string serverId = server["server_id"].get<std::string>();
            if (!safeIdentifier(id) || !dhcpIds.insert(id).second || serverId.empty() || serverId.size() > 253
                || !dhcpServerIds.insert(serverId).second || !safeEnvName(server["token_env"].get<std::string>())) {
                error = "DHCP source ids and server identifiers must be unique, bounded and valid";
                return false;
            }
            if (server.contains("allow_http") && !server["allow_http"].is_boolean()) {
                error = "DHCP source allow_http must be a boolean";
                return false;
            }
            if (server.contains("max_age_ms") && (!server["max_age_ms"].is_number_integer()
                || server["max_age_ms"].get<int64_t>() < 1000 || server["max_age_ms"].get<int64_t>() > 86400000)) {
                error = "DHCP source max_age_ms must be between 1000 and 86400000";
                return false;
            }
            Endpoint endpoint;
            std::string endpointError;
            if (!parseEndpoint(server["endpoint"].get<std::string>(), server.value("allow_http", false), endpoint, endpointError)) {
                error = "DHCP collector endpoint is invalid: " + endpointError;
                return false;
            }
            if (!endpoint.tls && !privateLanHost(endpoint.host)) {
        error = "unencrypted DHCP collector endpoints must use a loopback or private IP address literal";
                return false;
            }
            if (server.contains("tls_ca_file")) {
                if (!server["tls_ca_file"].is_string()) {
                    error = "DHCP source tls_ca_file must be an absolute CA bundle path";
                    return false;
                }
                try {
                    const std::filesystem::path caPath(server["tls_ca_file"].get<std::string>());
                    if (!caPath.is_absolute() || !std::filesystem::is_regular_file(caPath)
                        || std::filesystem::file_size(caPath) > 1024 * 1024) {
                        error = "DHCP source tls_ca_file must be a readable regular file of at most 1 MiB";
                        return false;
                    }
                } catch (...) {
                    error = "DHCP source tls_ca_file could not be inspected";
                    return false;
                }
            }
        }
    }

    for (const auto& device : document["devices"]) {
        if (device.contains("controller_id")) {
            if (!device["controller_id"].is_string() || !controllerIds.count(device["controller_id"].get<std::string>())) {
                error = "device controller_id references an unknown controller";
                return false;
            }
            const Json* controller = findById(document["controllers"], device["controller_id"].get<std::string>());
            if (controller && lower(controller->value("protocol", std::string{})) == "redfish"
                && (!device.contains("controller_target") || !device["controller_target"].is_string()
                    || !safeIdentifier(device["controller_target"].get<std::string>()))) {
                error = "Redfish-managed devices require a safe controller_target system id";
                return false;
            }
            if (controller && lower(controller->value("protocol", std::string{})) == "turingpi"
                && (!device.contains("turingpi_slot") || !device["turingpi_slot"].is_number_integer()
                    || device["turingpi_slot"].get<int>() < 1 || device["turingpi_slot"].get<int>() > 4)) {
                error = "Turing Pi-managed devices require turingpi_slot 1 through 4";
                return false;
            }
        }
    }
    return true;
}

std::string controllerUsername(const Json& controller) {
    return envValue(controller.value("username_env", std::string{}));
}

std::string controllerPassword(const Json& controller) {
    return envValue(controller.value("password_env", std::string{}));
}

} // namespace

DeviceManagement::DeviceManagement() {
    const char* path = std::getenv("NMC_DEVICE_INVENTORY_PATH");
    if (path) inventoryPath_ = path;
}

void DeviceManagement::sendJson(httplib::Response& res, int statusCode, const Json& body) const {
    res.status = statusCode;
    res.set_content(body.dump(), "application/json");
    res.set_header("Cache-Control", "no-store");
}

DeviceManagement::Inventory DeviceManagement::loadInventory() const {
    Inventory inventory;
    if (inventoryPath_.empty()) {
        inventory.error = "NMC_DEVICE_INVENTORY_PATH is not configured";
        return inventory;
    }
    try {
        const std::filesystem::path path(inventoryPath_);
        if (!path.is_absolute()) {
            inventory.error = "device inventory path must be absolute";
            return inventory;
        }
#ifndef _WIN32
        // Open the configured leaf without following symlinks, then inspect
        // the opened descriptor to avoid a check-then-open path race.
        const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) {
            inventory.error = "device inventory could not be opened";
            return inventory;
        }
        struct stat fileStatus{};
        if (::fstat(fd, &fileStatus) != 0) {
            ::close(fd);
            inventory.error = "device inventory could not be inspected";
            return inventory;
        }
        if (!S_ISREG(fileStatus.st_mode) || fileStatus.st_nlink != 1) {
            ::close(fd);
            inventory.error = "device inventory must be a regular file without hard links";
            return inventory;
        }
        if (fileStatus.st_size < 0 || fileStatus.st_size > 4 * 1024 * 1024) {
            ::close(fd);
            inventory.error = "device inventory exceeds 4 MiB";
            return inventory;
        }
        if ((fileStatus.st_mode & (S_IWGRP | S_IWOTH)) != 0 || (fileStatus.st_mode & S_IRUSR) == 0) {
            ::close(fd);
            inventory.error = "device inventory must be owner-readable and not writable by group or other users";
            return inventory;
        }
        std::string contents;
        contents.reserve(static_cast<size_t>(fileStatus.st_size));
        std::array<char, 8192> buffer{};
        while (true) {
            const ssize_t count = ::read(fd, buffer.data(), buffer.size());
            if (count == 0) break;
            if (count < 0) {
                if (errno == EINTR) continue;
                ::close(fd);
                inventory.error = "device inventory could not be read";
                return inventory;
            }
            if (contents.size() + static_cast<size_t>(count) > 4 * 1024 * 1024) {
                ::close(fd);
                inventory.error = "device inventory exceeds 4 MiB";
                return inventory;
            }
            contents.append(buffer.data(), static_cast<size_t>(count));
        }
        ::close(fd);
        inventory.document = Json::parse(contents);
#else
        if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > 4 * 1024 * 1024) {
            inventory.error = "device inventory is missing, not a regular file, or exceeds 4 MiB";
            return inventory;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            inventory.error = "device inventory could not be opened";
            return inventory;
        }
        input >> inventory.document;
#endif
        if (!validateInventory(inventory.document, inventory.error)) return inventory;
        inventory.revision = inventory.document["revision"].get<std::string>();
        const int64_t generatedAt = inventory.document["generated_at_unix_ms"].get<int64_t>();
        const int64_t maxAge = inventory.document.value("max_age_ms", int64_t{300000});
        inventory.stale = generatedAt <= 0 || maxAge < 1000 || maxAge > 86400000
                          || nowEpochMs() - generatedAt > maxAge || generatedAt > nowEpochMs() + 30000;
        inventory.valid = true;
    } catch (const std::exception& exception) {
        inventory.error = std::string("device inventory could not be parsed: ") + exception.what();
    }
    return inventory;
}

void DeviceManagement::handleGetInventory(const httplib::Request&, httplib::Response& res) const {
    const Inventory inventory = loadInventory();
    if (!inventory.valid) {
        sendJson(res, 503, errorBody("device_inventory_unavailable", inventory.error));
        return;
    }
    Json devices = Json::array();
    Json controllers = Json::array();
    for (const auto& device : inventory.document["devices"]) devices.push_back(publicDevice(device));
    for (const auto& controller : inventory.document["controllers"]) controllers.push_back(publicController(controller));
    sendJson(res, 200, okBody({
        {"revision", inventory.revision},
        {"dependency_graph_revision", inventory.document["dependency_graph_revision"]},
        {"generated_at_unix_ms", inventory.document["generated_at_unix_ms"]},
        {"stale", inventory.stale},
        {"devices", std::move(devices)},
        {"controllers", std::move(controllers)}
    }));
}

void DeviceManagement::handleGetDhcpObservations(const httplib::Request&, httplib::Response& res) const {
    const Inventory inventory = loadInventory();
    if (!inventory.valid) {
        sendJson(res, 503, errorBody("device_inventory_unavailable", inventory.error));
        return;
    }
    const Json sources = inventory.document.value("dhcp_servers", Json::array());
    if (!sources.is_array() || sources.empty()) {
        sendJson(res, 200, okBody({{"configured", false}, {"inventory_stale", inventory.stale},
                                   {"servers", Json::array()}}));
        return;
    }

    std::vector<std::future<Json>> requests;
    requests.reserve(sources.size());
    for (const auto& source : sources) {
        requests.push_back(std::async(std::launch::async, [source] { return queryDhcpServer(source); }));
    }

    Json servers = Json::array();
    for (size_t sourceIndex = 0; sourceIndex < requests.size(); ++sourceIndex) {
        Json result;
        try {
            result = requests[sourceIndex].get();
        } catch (...) {
            result = {{"id", sources[sourceIndex].value("id", std::string{})},
                      {"server_id", sources[sourceIndex].value("server_id", std::string{})},
                      {"available", false},
                      {"error", {{"code", "dhcp_collector_unavailable"},
                                  {"message", "DHCP collector request failed"}}}};
        }
        if (!result.value("available", false)) {
            servers.push_back(std::move(result));
            continue;
        }

        Json leases = Json::array();
        std::unordered_map<std::string, size_t> addressCounts;
        for (const auto& lease : result["leases"]) ++addressCounts[lease.value("address", std::string{})];
        const int64_t observedAt = result.value("observed_at_unix_ms", int64_t{0});
        const bool freshObservation = !result.value("stale", true);
        for (const auto& lease : result["leases"]) {
            const std::string mac = lease.value("mac_address", std::string{});
            const std::string canonicalLeaseMac = canonicalMac(mac);
            const std::string clientId = lease.value("client_id", std::string{});
            std::unordered_set<std::string> matchingDeviceIds;
            for (const auto& device : inventory.document["devices"]) {
                bool matched = false;
                if (!canonicalLeaseMac.empty() && device.contains("mac_addresses") && device["mac_addresses"].is_array()) {
                    for (const auto& deviceMac : device["mac_addresses"]) {
                        if (deviceMac.is_string() && canonicalMac(deviceMac.get<std::string>()) == canonicalLeaseMac) {
                            matched = true;
                        }
                    }
                }
                if (!clientId.empty() && device.contains("dhcp_client_ids") && device["dhcp_client_ids"].is_array()) {
                    for (const auto& deviceClientId : device["dhcp_client_ids"]) {
                        if (deviceClientId.is_string() && deviceClientId.get<std::string>() == clientId) matched = true;
                    }
                }
                if (matched) matchingDeviceIds.insert(device.value("id", std::string{}));
            }
            std::string identityStatus = "unknown";
            if (addressCounts[lease.value("address", std::string{})] > 1) identityStatus = "conflict";
            else if (matchingDeviceIds.size() > 1) identityStatus = "conflict";
            else if (inventory.stale) identityStatus = "registry_stale";
            else if (matchingDeviceIds.size() == 1) identityStatus = "verified";

            Json receivedOptions = Json::array();
            Json advertisedOptions = Json::array();
            Json requestedOptions = Json::array();
            for (const auto& option : lease["options"]) {
                Json publicOption{{"code", option["code"]}, {"name", option["name"]},
                                  {"value", option["value"]}, {"source", option["source"]},
                                  {"observed_at_unix_ms", option["observed_at_unix_ms"]}};
                if (option["source"] == "dhcp_ack") receivedOptions.push_back(std::move(publicOption));
                else if (option["source"] == "server_policy") advertisedOptions.push_back(std::move(publicOption));
                else requestedOptions.push_back(std::move(publicOption));
            }
            Json entry{{"server_id", result["server_id"]},
                       {"address", lease["address"]},
                       {"mac_address", canonicalLeaseMac.empty() ? mac : canonicalLeaseMac},
                       {"client_id", clientId},
                       {"hostname", lease.value("hostname", std::string{})},
                       {"lease_expires_at_unix_ms", lease.value("lease_expires_at_unix_ms", int64_t{0})},
                       {"lease_observed_at_unix_ms", observedAt},
                       {"observation_fresh", freshObservation},
                       {"identity_status", identityStatus},
                       {"received_options", std::move(receivedOptions)},
                       {"advertised_options", std::move(advertisedOptions)},
                       {"requested_options", std::move(requestedOptions)}};
            if (identityStatus == "verified") entry["device_id"] = *matchingDeviceIds.begin();
            leases.push_back(std::move(entry));
        }
        result.erase("leases");
        result["leases"] = std::move(leases);
        servers.push_back(std::move(result));
    }
    sendJson(res, 200, okBody({{"configured", true}, {"inventory_revision", inventory.revision},
                               {"inventory_stale", inventory.stale}, {"servers", std::move(servers)}}));
}

void DeviceManagement::handleGetHomeAssistant(const httplib::Request&, httplib::Response& res) const {
    const Inventory inventory = loadInventory();
    if (!inventory.valid) {
        sendJson(res, 503, errorBody("device_inventory_unavailable", inventory.error));
        return;
    }
    const Json config = inventory.document.value("home_assistant", Json::object());
    if (!config.is_object() || !config.contains("base_url") || !config["base_url"].is_string()
        || !config.contains("token_env") || !config["token_env"].is_string()
        || !safeEnvName(config["token_env"].get<std::string>())) {
        sendJson(res, 200, okBody({{"configured", false}, {"available", false},
                                   {"inventory_revision", inventory.revision},
                                   {"stale_inventory", inventory.stale}, {"entities", Json::array()}}));
        return;
    }
    if (config.contains("allow_http") && !config["allow_http"].is_boolean()) {
        sendJson(res, 503, errorBody("home_assistant_config_invalid", "allow_http must be a boolean"));
        return;
    }
    const std::string token = envValue(config["token_env"].get<std::string>());
    Endpoint endpoint;
    std::string endpointError;
    const std::string baseUrl = config["base_url"].get<std::string>();
    const bool allowHttp = config.value("allow_http", false);
    if (token.empty() || !parseEndpoint(baseUrl, allowHttp, endpoint, endpointError)) {
        sendJson(res, 503, errorBody("home_assistant_unavailable", token.empty() ? "Home Assistant token is unavailable" : endpointError));
        return;
    }
    if (!endpoint.tls && !privateLanHost(endpoint.host)) {
        sendJson(res, 503, errorBody("home_assistant_config_invalid", "HTTP Home Assistant endpoints must use a loopback or private IP address literal"));
        return;
    }
    if (config.contains("tls_ca_file")) {
        if (!config["tls_ca_file"].is_string()) {
            sendJson(res, 503, errorBody("home_assistant_config_invalid", "tls_ca_file must be an absolute CA bundle path"));
            return;
        }
        try {
            const std::filesystem::path caPath(config["tls_ca_file"].get<std::string>());
            if (!caPath.is_absolute() || !std::filesystem::is_regular_file(caPath)
                || std::filesystem::file_size(caPath) > 1024 * 1024) {
                sendJson(res, 503, errorBody("home_assistant_config_invalid", "tls_ca_file must be a readable regular file of at most 1 MiB"));
                return;
            }
            endpoint.caFile = caPath.string();
        } catch (...) {
            sendJson(res, 503, errorBody("home_assistant_config_invalid", "tls_ca_file could not be inspected"));
            return;
        }
    }
    const Json entityAllowlist = config.value("entities", Json::array());
    if (!entityAllowlist.is_array() || entityAllowlist.size() > 512) {
        sendJson(res, 503, errorBody("home_assistant_config_invalid", "Home Assistant entities must be an array of at most 512 entity ids"));
        return;
    }
    std::unordered_set<std::string> allowed;
    for (const auto& entity : entityAllowlist) {
        if (!entity.is_string() || entity.get<std::string>().find('.') == std::string::npos) {
            sendJson(res, 503, errorBody("home_assistant_config_invalid", "Home Assistant entity allowlist contains an invalid id"));
            return;
        }
        allowed.insert(entity.get<std::string>());
    }

    auto response = withHttpClient(endpoint, [&](auto& client) {
        httplib::Headers headers{{"Authorization", "Bearer " + token}};
        return client.Get(appendBasePath(endpoint, "/api/states"), headers);
    });
    if (!response || response->status != 200) {
        sendJson(res, 503, errorBody("home_assistant_unavailable", "Home Assistant state API did not return HTTP 200"));
        return;
    }
    try {
        const Json states = Json::parse(response->body);
        if (!states.is_array()) throw std::runtime_error("state API response is not an array");
        Json entities = Json::array();
        for (const auto& state : states) {
            if (!state.is_object() || !state.contains("entity_id") || !state["entity_id"].is_string()) continue;
            const std::string id = state["entity_id"].get<std::string>();
            if (!allowed.count(id)) continue;
            Json entry{{"entity_id", id}};
            if (state.contains("state") && state["state"].is_string()) entry["state"] = state["state"];
            if (state.contains("last_changed") && state["last_changed"].is_string()) entry["last_changed"] = state["last_changed"];
            if (state.contains("last_updated") && state["last_updated"].is_string()) entry["last_updated"] = state["last_updated"];
            entities.push_back(std::move(entry));
        }
        sendJson(res, 200, okBody({{"configured", true}, {"available", true},
                                   {"inventory_revision", inventory.revision},
                                   {"stale_inventory", inventory.stale}, {"entities", std::move(entities)}}));
    } catch (const std::exception& exception) {
        sendJson(res, 502, errorBody("home_assistant_response_invalid", exception.what()));
    }
}

void DeviceManagement::handleGetHomeAssistantReconciliation(const httplib::Request& req,
                                                            httplib::Response& res) const {
    // Reuse the state endpoint so the same URL, TLS, token and entity allowlist
    // checks protect both views. The revision check below prevents mixing two
    // different inventory snapshots if the registry changes between reads.
    httplib::Response stateResponse;
    handleGetHomeAssistant(req, stateResponse);
    if (stateResponse.status != 200) {
        res.status = stateResponse.status;
        res.set_header("Cache-Control", "no-store");
        res.set_content(stateResponse.body, "application/json");
        return;
    }

    try {
        const Json statePayload = Json::parse(stateResponse.body);
        if (!statePayload.is_object() || statePayload.value("success", false) != true
            || !statePayload.contains("data") || !statePayload["data"].is_object()) {
            sendJson(res, 502, errorBody("home_assistant_response_invalid",
                                         "Home Assistant state response did not match the expected envelope"));
            return;
        }

        const Json stateData = statePayload["data"];
        Inventory inventory = loadInventory();
        if (!inventory.valid) {
            sendJson(res, 503, errorBody("device_inventory_unavailable", inventory.error));
            return;
        }
        if (!stateData.contains("inventory_revision") || !stateData["inventory_revision"].is_string()
            || stateData["inventory_revision"].get<std::string>() != inventory.revision) {
            sendJson(res, 409, errorBody("device_inventory_changed",
                                         "Inventory changed while Home Assistant evidence was collected; retry reconciliation"));
            return;
        }

        const bool configured = stateData.value("configured", false);
        const bool available = stateData.value("available", false);
        std::unordered_set<std::string> allowlistedEntities;
        const Json homeAssistantConfig = inventory.document.value("home_assistant", Json::object());
        if (homeAssistantConfig.is_object()) {
            const Json configuredEntities = homeAssistantConfig.value("entities", Json::array());
            if (configuredEntities.is_array()) {
                for (const auto& entity : configuredEntities) {
                    if (entity.is_string()) allowlistedEntities.insert(entity.get<std::string>());
                }
            }
        }

        std::unordered_map<std::string, Json> observedStates;
        std::unordered_map<std::string, size_t> observedCounts;
        const Json entities = stateData.value("entities", Json::array());
        if (!entities.is_array()) {
            sendJson(res, 502, errorBody("home_assistant_response_invalid",
                                         "Home Assistant state response entities must be an array"));
            return;
        }
        for (const auto& entity : entities) {
            if (!entity.is_object() || !entity.contains("entity_id") || !entity["entity_id"].is_string()) continue;
            const std::string entityId = entity["entity_id"].get<std::string>();
            if (!allowlistedEntities.count(entityId)) continue;
            ++observedCounts[entityId];
            observedStates[entityId] = entity;
        }

        std::unordered_map<std::string, size_t> inventoryMappingCounts;
        for (const auto& device : inventory.document["devices"]) {
            if (device.is_object() && device.contains("home_assistant_entity")
                && device["home_assistant_entity"].is_string()) {
                ++inventoryMappingCounts[device["home_assistant_entity"].get<std::string>()];
            }
        }

        Json reconciledDevices = Json::array();
        std::unordered_set<std::string> mappedEntities;
        size_t matchedCount = 0;
        size_t missingCount = 0;
        size_t notConfiguredCount = 0;
        size_t ambiguousCount = 0;
        for (const auto& device : inventory.document["devices"]) {
            if (!device.is_object() || !device.contains("home_assistant_entity")
                || !device["home_assistant_entity"].is_string()) continue;
            const std::string deviceId = device.value("id", std::string{});
            const std::string entityId = device["home_assistant_entity"].get<std::string>();
            mappedEntities.insert(entityId);

            std::string status;
            const size_t mappingCount = inventoryMappingCounts[entityId];
            const size_t stateCount = observedCounts[entityId];
            if (mappingCount != 1 || stateCount > 1) {
                status = "ambiguous";
                ++ambiguousCount;
            } else if (!configured || !allowlistedEntities.count(entityId)) {
                status = "not_configured";
                ++notConfiguredCount;
            } else if (!available || stateCount == 0) {
                status = "missing";
                ++missingCount;
            } else {
                status = "matched";
                ++matchedCount;
            }

            Json entry{{"device_id", deviceId}, {"entity_id", entityId}, {"status", status}};
            if (status == "matched") {
                const Json& state = observedStates.at(entityId);
                for (const char* key : {"state", "last_changed", "last_updated"}) {
                    if (state.contains(key) && state[key].is_string()) entry[key] = state[key];
                }
            }
            reconciledDevices.push_back(std::move(entry));
        }

        Json unmappedEntities = Json::array();
        Json ambiguousEntities = Json::array();
        std::unordered_set<std::string> reportedUnmappedEntities;
        for (const auto& entity : entities) {
            if (!entity.is_object() || !entity.contains("entity_id") || !entity["entity_id"].is_string()) continue;
            const std::string entityId = entity["entity_id"].get<std::string>();
            if (allowlistedEntities.count(entityId) && !mappedEntities.count(entityId)
                && reportedUnmappedEntities.insert(entityId).second) {
                Json unmapped{{"entity_id", entityId}};
                if (observedCounts[entityId] == 1) {
                    unmapped["status"] = "unmapped";
                    unmappedEntities.push_back(std::move(unmapped));
                } else {
                    unmapped["status"] = "ambiguous";
                    ambiguousEntities.push_back(std::move(unmapped));
                }
            }
        }

        sendJson(res, 200, okBody({
            {"configured", configured},
            {"available", available},
            {"inventory_revision", inventory.revision},
            {"stale_inventory", inventory.stale},
            {"observed_at_unix_ms", nowEpochMs()},
            {"summary", {{"matched", matchedCount}, {"missing", missingCount},
                          {"not_configured", notConfiguredCount}, {"ambiguous", ambiguousCount},
                          {"unmapped_entities", unmappedEntities.size()},
                          {"ambiguous_entities", ambiguousEntities.size()}}},
            {"devices", std::move(reconciledDevices)},
            {"unmapped_entities", std::move(unmappedEntities)},
            {"ambiguous_entities", std::move(ambiguousEntities)}}));
    } catch (const std::exception& exception) {
        sendJson(res, 502, errorBody("home_assistant_response_invalid", exception.what()));
    }
}

Json DeviceManagement::getControllerDiagnostics(const Inventory& inventory,
                                                const std::string& controllerId,
                                                int& statusCode) const {
    const Json* controller = findById(inventory.document["controllers"], controllerId);
    if (!controller) {
        statusCode = 404;
        return errorBody("controller_not_found", "controller id is not present in the verified inventory");
    }
    const std::string protocol = lower(controller->value("protocol", std::string{}));
    const int64_t observedAt = nowEpochMs();

    if (protocol == "redfish") {
        Endpoint endpoint;
        std::string endpointError;
        if (!parseControllerEndpoint(*controller, endpoint, endpointError)) {
            statusCode = 503;
            return errorBody("controller_endpoint_invalid", endpointError);
        }
        const std::string username = controllerUsername(*controller);
        const std::string password = controllerPassword(*controller);
        if (username.empty() || password.empty()) {
            statusCode = 503;
            return errorBody("controller_credentials_unavailable", "Redfish credential references are unavailable");
        }
        const auto getResource = [&](const std::string& path) -> std::optional<Json> {
            if (!safeRedfishPath(path)) return std::nullopt;
            auto response = withHttpClient(endpoint, [&](auto& client) {
                client.set_basic_auth(username, password);
                return client.Get(appendRedfishPath(endpoint, path));
            });
            if (!response || response->status != 200 || response->body.size() > 2 * 1024 * 1024) {
                return std::nullopt;
            }
            Json document = Json::parse(response->body);
            if (!document.is_object()) return std::nullopt;
            return document;
        };
        auto rootResponse = withHttpClient(endpoint, [&](auto& client) {
            client.set_basic_auth(username, password);
            return client.Get(appendBasePath(endpoint, "/redfish/v1/"));
        });
        if (!rootResponse || rootResponse->status != 200) {
            statusCode = 502;
            return errorBody("redfish_unavailable", "Redfish service root did not return HTTP 200");
        }
        try {
            const Json root = Json::parse(rootResponse->body);
            if (!root.contains("Systems") || !root["Systems"].is_object()
                || !root["Systems"].contains("@odata.id") || !root["Systems"]["@odata.id"].is_string()) {
                statusCode = 502;
                return errorBody("redfish_response_invalid", "Redfish service root has no Systems resource link");
            }
            const std::string systemsPath = root["Systems"]["@odata.id"].get<std::string>();
            if (!safeRedfishPath(systemsPath)) {
                statusCode = 502;
                return errorBody("redfish_response_invalid", "Redfish Systems resource link is not a safe local path");
            }
            const std::optional<Json> systemsDocument = getResource(systemsPath);
            if (!systemsDocument) {
                statusCode = 502;
                return errorBody("redfish_systems_unavailable", "Redfish Systems collection did not return HTTP 200");
            }
            const Json& systems = *systemsDocument;
            if (!systems.contains("Members") || !systems["Members"].is_array() || systems["Members"].size() > 32) {
                statusCode = 502;
                return errorBody("redfish_response_invalid", "Redfish Systems collection is malformed or too large");
            }
            Json resources = Json::array();
            std::vector<std::string> vendorEvidence;
            for (const char* key : {"Vendor", "Manufacturer", "Product"}) {
                if (root.contains(key) && root[key].is_string()) vendorEvidence.push_back(root[key].get<std::string>());
            }
            const auto addString = [](Json& target, const Json& source, const char* sourceKey, const char* targetKey) {
                if (source.contains(sourceKey) && source[sourceKey].is_string()) target[targetKey] = source[sourceKey];
            };
            for (const auto& member : systems["Members"]) {
                if (!member.is_object() || !member.contains("@odata.id") || !member["@odata.id"].is_string()) continue;
                const std::string resourcePath = member["@odata.id"].get<std::string>();
                if (!safeRedfishPath(resourcePath)) continue;
                const std::optional<Json> systemDocument = getResource(resourcePath);
                if (!systemDocument) {
                    resources.push_back({{"resource", resourcePath}, {"available", false}});
                    continue;
                }
                const Json& system = *systemDocument;
                Json status = Json::object();
                if (system.contains("Status") && system["Status"].is_object()) {
                    for (const char* key : {"State", "Health", "HealthRollup"}) {
                        if (system["Status"].contains(key) && system["Status"][key].is_string()) status[key] = system["Status"][key];
                    }
                }
                Json item{{"id", system.value("Id", std::string{})},
                          {"name", system.value("Name", std::string{})},
                          {"power_state", system.value("PowerState", std::string{})},
                          {"status", std::move(status)},
                          {"available", true},
                          {"resource", resourcePath}};
                addString(item, system, "Manufacturer", "manufacturer");
                addString(item, system, "Model", "model");
                for (const char* key : {"Manufacturer", "Model"}) {
                    if (system.contains(key) && system[key].is_string()) vendorEvidence.push_back(system[key].get<std::string>());
                }
                Json resetCapabilities = Json::array();
                bool resetActionAvailable = false;
                if (system.contains("Actions") && system["Actions"].is_object()
                    && system["Actions"].contains("#ComputerSystem.Reset")
                    && system["Actions"]["#ComputerSystem.Reset"].is_object()) {
                    const Json& resetAction = system["Actions"]["#ComputerSystem.Reset"];
                    resetActionAvailable = resetAction.contains("target") && resetAction["target"].is_string()
                                           && safeRedfishPath(resetAction["target"].get<std::string>());
                    const char* allowableKey = "ResetType@Redfish.AllowableValues";
                    if (resetAction.contains(allowableKey) && resetAction[allowableKey].is_array()
                        && resetAction[allowableKey].size() <= 32) {
                        for (const auto& value : resetAction[allowableKey]) {
                            if (value.is_string()) resetCapabilities.push_back(value);
                        }
                    }
                }
                item["reset_action_available"] = resetActionAvailable;
                item["reset_types"] = std::move(resetCapabilities);
                resources.push_back(std::move(item));
            }

            Json managers = Json::array();
            bool managersAvailable = false;
            if (root.contains("Managers") && root["Managers"].is_object()
                && root["Managers"].contains("@odata.id") && root["Managers"]["@odata.id"].is_string()) {
                const std::string managersPath = root["Managers"]["@odata.id"].get<std::string>();
                if (safeRedfishPath(managersPath)) {
                    const std::optional<Json> managersDocument = getResource(managersPath);
                    if (managersDocument && managersDocument->contains("Members")
                        && (*managersDocument)["Members"].is_array() && (*managersDocument)["Members"].size() <= 32) {
                        managersAvailable = true;
                        for (const auto& member : (*managersDocument)["Members"]) {
                            if (!member.is_object() || !member.contains("@odata.id") || !member["@odata.id"].is_string()) continue;
                            const std::string managerPath = member["@odata.id"].get<std::string>();
                            if (!safeRedfishPath(managerPath)) continue;
                            const std::optional<Json> managerDocument = getResource(managerPath);
                            if (!managerDocument) {
                                managers.push_back({{"resource", managerPath}, {"available", false}});
                                continue;
                            }
                            const Json& manager = *managerDocument;
                            Json managerStatus = Json::object();
                            if (manager.contains("Status") && manager["Status"].is_object()) {
                                for (const char* key : {"State", "Health", "HealthRollup"}) {
                                    if (manager["Status"].contains(key) && manager["Status"][key].is_string()) managerStatus[key] = manager["Status"][key];
                                }
                            }
                            Json item{{"id", manager.value("Id", std::string{})},
                                      {"name", manager.value("Name", std::string{})},
                                      {"available", true}, {"resource", managerPath}, {"status", std::move(managerStatus)}};
                            addString(item, manager, "ManagerType", "manager_type");
                            addString(item, manager, "Manufacturer", "manufacturer");
                            addString(item, manager, "Model", "model");
                            addString(item, manager, "FirmwareVersion", "firmware_version");
                            for (const char* key : {"Manufacturer", "Model", "Name"}) {
                                if (manager.contains(key) && manager[key].is_string()) vendorEvidence.push_back(manager[key].get<std::string>());
                            }
                            managers.push_back(std::move(item));
                        }
                    }
                }
            }
            Json controllerData{{"controller_id", controllerId}, {"protocol", protocol},
                                {"vendor_profile", controller->value("vendor_profile", std::string("generic_redfish"))},
                                {"detected_vendor", redfishVendor(vendorEvidence)},
                                {"observed_at_unix_ms", observedAt}, {"inventory_stale", inventory.stale},
                                {"systems", std::move(resources)}, {"managers_available", managersAvailable},
                                {"managers", std::move(managers)}};
            addString(controllerData, root, "Vendor", "service_vendor");
            addString(controllerData, root, "Product", "service_product");
            addString(controllerData, root, "RedfishVersion", "redfish_version");
            statusCode = 200;
            return okBody(std::move(controllerData));
        } catch (const std::exception& exception) {
            statusCode = 502;
            return errorBody("redfish_response_invalid", exception.what());
        }
    }

    if (protocol == "ipmi") {
        const CommandResult chassis = runIpmitool(*controller, {"chassis", "status"});
        if (chassis.timedOut) {
            statusCode = 504;
            return errorBody("ipmi_timeout", "IPMI chassis status exceeded its 8 second deadline");
        }
        if (chassis.exitCode != 0) {
            statusCode = chassis.exitCode == 127 ? 503 : 502;
            return errorBody(chassis.exitCode == 127 ? "ipmitool_unavailable" : "ipmi_diagnostics_failed",
                             chassis.output.empty() ? "IPMI chassis status failed" : chassis.output.substr(0, 4096));
        }
        const CommandResult sensors = runIpmitool(*controller, {"sensor", "list"});
        const CommandResult eventLog = runIpmitool(*controller, {"sel", "list", "last", "20"});
        statusCode = 200;
        return okBody({{"controller_id", controllerId}, {"protocol", protocol},
                       {"observed_at_unix_ms", observedAt}, {"inventory_stale", inventory.stale},
                       {"chassis_status", chassis.output},
                       {"sensors", {{"available", sensors.exitCode == 0 && !sensors.timedOut},
                                    {"output", sensors.output.substr(0, 32768)}}},
                       {"system_event_log", {{"available", eventLog.exitCode == 0 && !eventLog.timedOut},
                                             {"output", eventLog.output.substr(0, 32768)}}}});
    }

    if (protocol == "turingpi") {
        Endpoint endpoint;
        std::string endpointError;
        if (!parseControllerEndpoint(*controller, endpoint, endpointError)) {
            statusCode = 503;
            return errorBody("controller_endpoint_invalid", endpointError);
        }
        const std::string username = controllerUsername(*controller);
        const std::string password = controllerPassword(*controller);
        if (username.empty() || password.empty()) {
            statusCode = 503;
            return errorBody("controller_credentials_unavailable", "Turing Pi credential references are unavailable");
        }
        auto authResponse = withHttpClient(endpoint, [&](auto& client) {
            return client.Post(appendBasePath(endpoint, "/api/bmc/authenticate"),
                               Json{{"username", username}, {"password", password}}.dump(), "application/json");
        });
        if (!authResponse || authResponse->status != 200) {
            statusCode = 502;
            return errorBody("turingpi_authentication_failed", "Turing Pi BMC authentication failed");
        }
        try {
            const Json auth = Json::parse(authResponse->body);
            std::string token;
            if (auth.is_object() && auth.contains("id") && auth["id"].is_string()) token = auth["id"].get<std::string>();
            if (auth.is_array() && !auth.empty() && auth[0].is_object()
                && auth[0].contains("response") && auth[0]["response"].is_string()) token = auth[0]["response"].get<std::string>();
            if (token.empty() || token.size() > 4096) {
                statusCode = 502;
                return errorBody("turingpi_response_invalid", "Turing Pi BMC did not return a valid access token");
            }
            httplib::Params params{{"opt", "get"}, {"type", "power"}};
            auto powerResponse = withHttpClient(endpoint, [&](auto& client) {
                return client.Get(appendBasePath(endpoint, "/api/bmc"), params,
                                  {{"Authorization", "Bearer " + token}});
            });
            if (!powerResponse || powerResponse->status != 200) {
                statusCode = 502;
                return errorBody("turingpi_status_failed", "Turing Pi BMC power status request failed");
            }
            const Json powerPayload = Json::parse(powerResponse->body);
            Json nodeStates = Json::object();
            for (int slot = 1; slot <= 4; ++slot) {
                const auto state = turingPiPowerState(powerPayload, slot);
                if (state.has_value()) nodeStates["slot" + std::to_string(slot)] = *state ? "On" : "Off";
            }
            statusCode = 200;
            return okBody({{"controller_id", controllerId}, {"protocol", protocol},
                           {"observed_at_unix_ms", observedAt}, {"inventory_stale", inventory.stale},
                           {"nodes", std::move(nodeStates)}});
        } catch (const std::exception& exception) {
            statusCode = 502;
            return errorBody("turingpi_response_invalid", exception.what());
        }
    }

    statusCode = 422;
    return errorBody("controller_protocol_unsupported", "controller protocol is not supported");
}

void DeviceManagement::handleGetControllerDiagnostics(const httplib::Request& req, httplib::Response& res) const {
    const std::string controllerId = req.get_param_value("controller_id");
    if (!safeIdentifier(controllerId)) {
        sendJson(res, 400, errorBody("controller_id_invalid", "controller_id must be a safe inventory identifier"));
        return;
    }
    const Inventory inventory = loadInventory();
    if (!inventory.valid) {
        sendJson(res, 503, errorBody("device_inventory_unavailable", inventory.error));
        return;
    }
    int statusCode = 500;
    const Json response = getControllerDiagnostics(inventory, controllerId, statusCode);
    sendJson(res, statusCode, response);
}

Json DeviceManagement::performControllerAction(const Inventory& inventory,
                                               const std::string& controllerId,
                                               const Json& request,
                                               int& statusCode) {
    if (inventory.stale) {
        statusCode = 409;
        return errorBody("device_inventory_stale", "controller actions require a fresh device inventory");
    }
    const char* enabled = std::getenv("NMC_DEVICE_POWER_CONTROL_ENABLED");
    if (!enabled || lower(enabled) != "true") {
        statusCode = 423;
        return errorBody("controller_actions_disabled", "NMC_DEVICE_POWER_CONTROL_ENABLED is not true");
    }
    if (!request.is_object()) {
        statusCode = 400;
        return errorBody("controller_action_invalid", "request body must be a JSON object");
    }
    const std::string deviceId = stringField(request, "device_id");
    const std::string action = lower(stringField(request, "action"));
    const std::string requestId = stringField(request, "request_id");
    const std::string changeId = stringField(request, "change_id");
    const std::string reason = stringField(request, "reason");
    const std::string expectedRevision = stringField(request, "expected_inventory_revision");
    if (!safeIdentifier(deviceId) || !safeIdentifier(controllerId) || !safeIdentifier(requestId)
        || !safeIdentifier(changeId) || reason.size() < 8 || reason.size() > 512
        || (action != "power_on" && action != "power_off" && action != "warm_restart" && action != "cold_restart")) {
        statusCode = 400;
        return errorBody("controller_action_invalid", "device_id, controller_id, request_id, change_id, reason or action is invalid");
    }
    if (expectedRevision != inventory.revision) {
        statusCode = 409;
        return errorBody("inventory_revision_mismatch", "expected_inventory_revision does not match the active verified inventory");
    }

    const Json* controller = findById(inventory.document["controllers"], controllerId);
    const Json* device = findById(inventory.document["devices"], deviceId);
    if (!controller || !device) {
        statusCode = 404;
        return errorBody("controller_target_not_found", "controller or device is not present in the verified inventory");
    }
    if (!device->contains("controller_id") || !(*device)["controller_id"].is_string()
        || (*device)["controller_id"].get<std::string>() != controllerId
        || !controller->contains("manages") || !controller->at("manages").is_array()
        || std::find(controller->at("manages").begin(), controller->at("manages").end(), deviceId) == controller->at("manages").end()) {
        statusCode = 409;
        return errorBody("controller_target_relationship_mismatch", "controller-to-device relationship is missing or ambiguous");
    }
    if (!controller->value("power_actions_enabled", false)) {
        statusCode = 423;
        return errorBody("controller_actions_disabled", "power_actions_enabled is not true for this inventory controller");
    }
    const Json preflight = request.value("preflight", Json::object());
    if (!preflight.is_object()) {
        statusCode = 400;
        return errorBody("controller_preflight_missing", "preflight must be a structured object");
    }
    if (!preflight.contains("observed_at_unix_ms") || !preflight["observed_at_unix_ms"].is_number_integer()) {
        statusCode = 409;
        return errorBody("controller_preflight_blocked", "fresh identity, dependency, affected-service and monitoring evidence is required");
    }
    const int64_t observedAt = preflight["observed_at_unix_ms"].get<int64_t>();
    const int64_t age = nowEpochMs() - observedAt;
    const std::string expectedPowerState = stringField(preflight, "expected_power_state");
    const std::string requiredPowerState = action == "power_on" ? "Off" : "On";
    const std::vector<std::string> expectedServices = stringArray(device->value("affected_services", Json::array()));
    const std::vector<std::string> expectedDependencies = stringArray(device->value("depends_on", Json::array()));
    const std::vector<std::string> suppliedServices = stringArray(preflight.value("affected_services", Json::array()));
    const std::vector<std::string> suppliedDependencies = stringArray(preflight.value("dependency_ids", Json::array()));
    if (age < -30000 || age > 60000
        || stringField(preflight, "inventory_revision") != inventory.revision
        || stringField(preflight, "dependency_graph_revision") != inventory.document["dependency_graph_revision"].get<std::string>()
        || !trueField(preflight, "target_identity_verified")
        || !trueField(preflight, "dependencies_healthy")
        || !trueField(preflight, "dependents_known")
        || !trueField(preflight, "affected_services_healthy")
        || !trueField(preflight, "monitoring_healthy")
        || expectedPowerState != requiredPowerState
        || expectedServices.empty() || suppliedServices != expectedServices
        || suppliedDependencies != expectedDependencies) {
        statusCode = 409;
        return errorBody("controller_preflight_blocked", "fresh identity, dependency, affected-service and monitoring evidence must match the active inventory");
    }
    const std::string auditPath = envValue("NMC_DEVICE_ACTION_AUDIT_PATH");
    if (auditPath.empty()) {
        statusCode = 503;
        return errorBody("controller_audit_unavailable", "NMC_DEVICE_ACTION_AUDIT_PATH is required before controller actions can run");
    }
    AuditLock auditLock;
    std::string auditError;
    if (!auditLock.acquire(auditPath, auditError)) {
        statusCode = 503;
        return errorBody("controller_audit_unavailable", auditError);
    }
    const Json prior = findAuditRequest(auditPath, requestId);
    if (prior.value("audit_scan_error", false)) {
        statusCode = 503;
        return errorBody("controller_audit_unavailable", "controller action audit history could not be read completely; action is blocked until the audit file is repaired");
    }
    if (!prior.is_null() && !prior.empty()) {
        if (prior.value("phase", std::string{}) == "complete") {
            statusCode = prior.value("http_status", 200);
            return prior.value("response", Json::object());
        }
        statusCode = 409;
        return errorBody("controller_action_outcome_unknown", "request_id already has a durable intent without a recorded outcome; inspect the controller before submitting a new request_id");
    }

    Json auditBase{{"request_id", requestId}, {"controller_id", controllerId}, {"device_id", deviceId},
                   {"action", action}, {"change_id", changeId}, {"inventory_revision", inventory.revision},
                   {"reason", reason}, {"started_at_unix_ms", nowEpochMs()}};
    Json intent = auditBase;
    intent["phase"] = "intent";
    if (!appendAuditRecord(auditPath, intent, auditError)) {
        statusCode = 503;
        return errorBody("controller_audit_unavailable", auditError);
    }

    const std::string protocol = lower(controller->value("protocol", std::string{}));
    Json outcome;
    bool executed = false;
    bool verified = false;
    std::string executionMessage;

    if (protocol == "redfish") {
        Endpoint endpoint;
        std::string endpointError;
        const std::string target = device->value("controller_target", std::string{});
        const std::string username = controllerUsername(*controller);
        const std::string password = controllerPassword(*controller);
        if (!parseControllerEndpoint(*controller, endpoint, endpointError)
            || !safeIdentifier(target) || username.empty() || password.empty()) {
            executionMessage = "Redfish endpoint, target mapping or credentials are invalid";
        } else {
            const auto getResource = [&](const std::string& path) -> std::optional<Json> {
                if (!safeRedfishPath(path)) return std::nullopt;
                auto response = withHttpClient(endpoint, [&](auto& client) {
                    client.set_basic_auth(username, password);
                    return client.Get(appendRedfishPath(endpoint, path));
                });
                if (!response || response->status != 200 || response->body.size() > 2 * 1024 * 1024) return std::nullopt;
                Json document = Json::parse(response->body);
                if (!document.is_object()) return std::nullopt;
                return document;
            };
            std::string systemPath;
            std::optional<Json> targetSystem;
            try {
                const std::optional<Json> root = getResource("/redfish/v1/");
                if (root && root->contains("Systems") && (*root)["Systems"].is_object()
                    && (*root)["Systems"].contains("@odata.id") && (*root)["Systems"]["@odata.id"].is_string()) {
                    const std::string systemsPath = (*root)["Systems"]["@odata.id"].get<std::string>();
                    const std::optional<Json> systems = getResource(systemsPath);
                    if (systems && systems->contains("Members") && (*systems)["Members"].is_array()
                        && (*systems)["Members"].size() <= 32) {
                        bool ambiguousTarget = false;
                        for (const auto& member : (*systems)["Members"]) {
                            if (!member.is_object() || !member.contains("@odata.id") || !member["@odata.id"].is_string()) continue;
                            const std::string candidatePath = member["@odata.id"].get<std::string>();
                            if (!safeRedfishPath(candidatePath)) continue;
                            const std::optional<Json> candidate = getResource(candidatePath);
                            if (!candidate || candidate->value("Id", std::string{}) != target) continue;
                            if (targetSystem) {
                                ambiguousTarget = true;
                                break;
                            }
                            systemPath = candidatePath;
                            targetSystem = candidate;
                        }
                        if (ambiguousTarget) {
                            targetSystem.reset();
                            executionMessage = "Redfish target identity is ambiguous in the advertised Systems collection";
                        }
                    }
                }
                if (!targetSystem && executionMessage.empty()) {
                    executionMessage = "Redfish target identity could not be found through the advertised Systems collection";
                }
                if (targetSystem) {
                    const Json& system = *targetSystem;
                    const std::string powerState = system.value("PowerState", std::string{});
                    if (system.value("Id", std::string{}) != target || powerState != expectedPowerState) {
                        executionMessage = "Redfish target identity or power state differs from preflight evidence";
                    }
                    std::string resetType;
                    if (action == "warm_restart") resetType = "GracefulRestart";
                    else if (action == "cold_restart") resetType = "PowerCycle";
                    else if (action == "power_on") resetType = "On";
                    else resetType = "GracefulShutdown";
                    std::string resetTarget;
                    bool resetTypeSupported = false;
                    if (system.contains("Actions") && system["Actions"].is_object()
                        && system["Actions"].contains("#ComputerSystem.Reset")
                        && system["Actions"]["#ComputerSystem.Reset"].is_object()) {
                        const Json& resetAction = system["Actions"]["#ComputerSystem.Reset"];
                        const char* allowableKey = "ResetType@Redfish.AllowableValues";
                        if (resetAction.contains("target") && resetAction["target"].is_string()) {
                            resetTarget = resetAction["target"].get<std::string>();
                        }
                        if (resetAction.contains(allowableKey) && resetAction[allowableKey].is_array()) {
                            const auto& allowed = resetAction[allowableKey];
                            for (const auto& item : allowed) {
                                if (item.is_string() && item.get<std::string>() == resetType) resetTypeSupported = true;
                            }
                        }
                    }
                    if (!resetTypeSupported || resetTarget.empty()) {
                        executionMessage = "Redfish reset action target or allowable reset types are absent or do not permit this operation";
                    }
                    if (!safeRedfishPath(resetTarget)) {
                        executionMessage = "Redfish reset action target is not a safe local Redfish resource path";
                    }
                    const std::string resetPath = appendRedfishPath(endpoint, resetTarget);
                    Json reset{{"ResetType", resetType}};
                    httplib::Result actionResponse;
                    if (executionMessage.empty()) {
                        actionResponse = withHttpClient(endpoint, [&](auto& client) {
                            client.set_basic_auth(username, password);
                            return client.Post(resetPath, reset.dump(), "application/json");
                        });
                    }
                    executed = actionResponse && (actionResponse->status == 200 || actionResponse->status == 202 || actionResponse->status == 204);
                    if (executionMessage.empty()) executionMessage = executed ? "Redfish reset action accepted" : "Redfish reset action was rejected";
                    if (executed) {
                        const std::optional<Json> verify = getResource(systemPath);
                        if (verify) {
                            const std::string resultingState = verify->value("PowerState", std::string{});
                            verified = action == "power_off" ? resultingState == "Off" : resultingState == "On";
                            outcome = {{"controller_power_state_before", powerState}, {"controller_power_state_after", resultingState}};
                        }
                    }
                }
            } catch (const std::exception& exception) {
                executionMessage = std::string("Redfish action response could not be verified: ") + exception.what();
            }
        }
    } else if (protocol == "ipmi") {
        std::vector<std::string> command{"chassis", "power"};
        if (action == "power_on") command.push_back("on");
        else if (action == "power_off") command.push_back("off");
        else if (action == "warm_restart") command.push_back("reset");
        else command.push_back("cycle");
        const CommandResult before = runIpmitool(*controller, {"chassis", "power", "status"});
        const std::string beforeState = lower(before.output);
        const bool stateMatches = before.exitCode == 0 && !before.timedOut
                && (expectedPowerState == "On" ? beforeState.find("chassis power is on") != std::string::npos
                                                : beforeState.find("chassis power is off") != std::string::npos);
        if (!stateMatches) {
            executionMessage = before.timedOut ? "IPMI pre-action power-state check timed out"
                    : "IPMI target power state differs from preflight evidence";
        } else {
            const CommandResult result = runIpmitool(*controller, command);
            executed = result.exitCode == 0 && !result.timedOut;
            executionMessage = result.timedOut ? "IPMI action timed out" : result.output.substr(0, 2048);
            if (executed) {
                const CommandResult verify = runIpmitool(*controller, {"chassis", "power", "status"});
                const std::string state = lower(verify.output);
                verified = verify.exitCode == 0 && !verify.timedOut
                           && (action == "power_off" ? state.find("chassis power is off") != std::string::npos
                                                      : state.find("chassis power is on") != std::string::npos);
                outcome = {{"controller_power_status", verify.output.substr(0, 2048)}};
            }
        }
    } else if (protocol == "turingpi") {
        const int slot = device->value("turingpi_slot", 0);
        Endpoint endpoint;
        std::string endpointError;
        const std::string username = controllerUsername(*controller);
        const std::string password = controllerPassword(*controller);
        if (!parseControllerEndpoint(*controller, endpoint, endpointError)
            || slot < 1 || slot > 4 || username.empty() || password.empty()) {
            executionMessage = "Turing Pi endpoint, slot mapping or credentials are invalid";
        } else if (action == "warm_restart" || (action == "cold_restart"
                   && controller->value("cold_restart_mode", std::string{}) != "bmc_reset")) {
            executionMessage = "Turing Pi restart semantics are not explicitly enabled for this controller";
        } else {
            auto authResponse = withHttpClient(endpoint, [&](auto& client) {
                return client.Post(appendBasePath(endpoint, "/api/bmc/authenticate"),
                                   Json{{"username", username}, {"password", password}}.dump(), "application/json");
            });
            try {
                std::string token;
                if (authResponse && authResponse->status == 200) {
                    const Json auth = Json::parse(authResponse->body);
                    if (auth.is_object() && auth.contains("id") && auth["id"].is_string()) token = auth["id"].get<std::string>();
                    if (auth.is_array() && !auth.empty() && auth[0].is_object()
                        && auth[0].contains("response") && auth[0]["response"].is_string()) token = auth[0]["response"].get<std::string>();
                }
                if (token.empty()) {
                    executionMessage = "Turing Pi BMC authentication failed";
                } else {
                    auto getSlotPower = [&]() -> std::optional<bool> {
                        httplib::Params statusParams{{"opt", "get"}, {"type", "power"}};
                        auto statusResponse = withHttpClient(endpoint, [&](auto& client) {
                            return client.Get(appendBasePath(endpoint, "/api/bmc"), statusParams,
                                              {{"Authorization", "Bearer " + token}});
                        });
                        if (!statusResponse || statusResponse->status != 200) return std::nullopt;
                        return turingPiPowerState(Json::parse(statusResponse->body), slot);
                    };
                    const auto beforePower = getSlotPower();
                    if (!beforePower.has_value() || (*beforePower ? "On" : "Off") != expectedPowerState) {
                        executionMessage = "Turing Pi target power state differs from preflight evidence";
                    } else {
                    httplib::Params params{{"opt", "set"}};
                    if (action == "cold_restart") {
                        params.emplace("type", "reset");
                        params.emplace("node", std::to_string(slot - 1));
                    } else {
                        params.emplace("type", "power");
                        params.emplace("node" + std::to_string(slot), action == "power_on" ? "1" : "0");
                    }
                    auto actionResponse = withHttpClient(endpoint, [&](auto& client) {
                        return client.Get(appendBasePath(endpoint, "/api/bmc"), params,
                                          {{"Authorization", "Bearer " + token}});
                    });
                    bool operationConfirmed = false;
                    if (actionResponse && actionResponse->status == 200 && !actionResponse->body.empty()) {
                        try {
                            operationConfirmed = explicitSuccess(Json::parse(actionResponse->body));
                        } catch (...) {
                            operationConfirmed = false;
                        }
                    }
                    executed = actionResponse && actionResponse->status == 200 && operationConfirmed;
                    executionMessage = executed ? "Turing Pi BMC action accepted and explicitly acknowledged"
                                                : "Turing Pi BMC action was not explicitly acknowledged";
                    if (executed) {
                        const auto afterPower = getSlotPower();
                        if (afterPower.has_value()) {
                            outcome = {{"power_state_before", *beforePower ? "On" : "Off"},
                                       {"power_state_after", *afterPower ? "On" : "Off"}};
                            verified = action == "power_off" ? !*afterPower : *afterPower;
                        }
                    }
                    }
                }
            } catch (const std::exception& exception) {
                executionMessage = std::string("Turing Pi response could not be verified: ") + exception.what();
            }
        }
    } else {
        executionMessage = "controller protocol is not supported";
    }

    statusCode = executed ? (verified ? 200 : 502) : 502;
    Json response = executed && verified
            ? okBody({{"request_id", requestId}, {"controller_id", controllerId}, {"device_id", deviceId},
                      {"action", action}, {"verified", true}, {"controller", std::move(outcome)},
                      {"message", executionMessage}})
            : errorBody(executed ? "controller_action_unverified" : "controller_action_failed",
                        executionMessage.empty() ? "controller action failed" : executionMessage);
    Json completion = auditBase;
    completion["phase"] = "complete";
    completion["completed_at_unix_ms"] = nowEpochMs();
    completion["http_status"] = statusCode;
    completion["response"] = response;
    if (!appendAuditRecord(auditPath, completion, auditError)) {
        statusCode = 503;
        return errorBody("controller_audit_unavailable", "action outcome occurred but its durable audit record could not be committed: " + auditError);
    }
    return response;
}

void DeviceManagement::handleControllerAction(const httplib::Request& req, httplib::Response& res) {
    Json request;
    try {
        request = Json::parse(req.body);
    } catch (const std::exception& exception) {
        sendJson(res, 400, errorBody("controller_action_invalid_json", exception.what()));
        return;
    }
    if (!request.is_object()) {
        sendJson(res, 400, errorBody("controller_action_invalid", "request body must be a JSON object"));
        return;
    }
    const std::string controllerId = stringField(request, "controller_id");
    if (!safeIdentifier(controllerId)) {
        sendJson(res, 400, errorBody("controller_id_invalid", "controller_id must be a safe inventory identifier"));
        return;
    }
    std::lock_guard<std::mutex> lock(actionMutex_);
    const Inventory inventory = loadInventory();
    if (!inventory.valid) {
        sendJson(res, 503, errorBody("device_inventory_unavailable", inventory.error));
        return;
    }
    int statusCode = 500;
    try {
        const Json response = performControllerAction(inventory, controllerId, request, statusCode);
        sendJson(res, statusCode, response);
    } catch (const std::exception& exception) {
        std::cerr << "[ERROR] Controller action failed internally: " << exception.what() << std::endl;
        sendJson(res, 500, errorBody("controller_action_internal_error", "controller action could not be completed due to an internal error"));
    }
}

} // namespace NMC::Server
