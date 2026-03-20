// server/APIRoutes.cpp
#include "APIRoutes.h"
#include "K8sHandlers.h"
#include "ConnectionHandlers.h"
#include "Utils.h"
#include <cstdlib>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <iostream>
#include <algorithm> // For std::remove_if, std::find_if
#include <set>       // For unique locations/types
#include <chrono>
#include <thread>
#include <vector>
#include <poll.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>


namespace NMC::Server {

    namespace {
        std::string trim(const std::string& value) {
            const auto start = value.find_first_not_of(" \t");
            if (start == std::string::npos) {
                return "";
            }
            const auto end = value.find_last_not_of(" \t");
            return value.substr(start, end - start + 1);
        }

        std::string toLower(std::string value) {
            for (auto& ch : value) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return value;
        }

        std::string normalizeOpenShiftStatus(const std::string& rawStatus) {
            const std::string status = toLower(rawStatus);
            if (status == "ready" || status == "running") {
                return "Ready";
            }
            if (status == "failed" || status == "error" || status == "unhealthy") {
                return "Failed";
            }
            if (status == "pending" || status == "accepted" || status == "queued" || status == "requested") {
                return "Pending";
            }
            if (status == "provisioning" || status == "gitops-syncing" || status == "gitops_syncing"
                || status == "syncing" || status == "installing" || status == "creating") {
                return "Provisioning";
            }
            return "Unknown";
        }

        std::string normalizeTraceyStatus(const std::string& rawStatus) {
            const std::string status = toLower(rawStatus);
            if (status == "ok" || status == "ready" || status == "running" || status == "healthy") {
                return "healthy";
            }
            if (status == "degraded" || status == "warning" || status == "warn") {
                return "degraded";
            }
            if (status == "offline" || status == "down" || status == "failed" || status == "error") {
                return "offline";
            }
            if (status.empty()) {
                return "unknown";
            }
            return status;
        }

        int64_t nowEpochMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
            ).count();
        }

        void normalizeClusterStatus(nlohmann::json& cluster) {
            if (cluster.is_object() && cluster.contains("status") && cluster["status"].is_string()) {
                cluster["status"] = normalizeOpenShiftStatus(cluster["status"].get<std::string>());
            }
        }

        bool parseBoolValue(const char* raw, bool fallback) {
            if (!raw || !*raw) {
                return fallback;
            }
            const std::string value = toLower(trim(raw));
            if (value == "1" || value == "true" || value == "yes" || value == "on") {
                return true;
            }
            if (value == "0" || value == "false" || value == "no" || value == "off") {
                return false;
            }
            return fallback;
        }

        int64_t parseInt64Value(const char* raw, int64_t fallback, int64_t minValue, int64_t maxValue) {
            if (!raw || !*raw) {
                return fallback;
            }
            try {
                int64_t parsed = std::stoll(raw);
                if (parsed < minValue) {
                    parsed = minValue;
                } else if (parsed > maxValue) {
                    parsed = maxValue;
                }
                return parsed;
            } catch (const std::exception&) {
                return fallback;
            }
        }

        struct TraceyEndpoint {
            std::string host;
            int port{80};
            bool https{false};
            std::string basePath;
            std::string normalized;
        };

        bool isPrivateIpv4(const in_addr& addr) {
            const uint32_t ip = ntohl(addr.s_addr);
            if ((ip & 0xFF000000U) == 0x0A000000U) { // 10.0.0.0/8
                return true;
            }
            if ((ip & 0xFFF00000U) == 0xAC100000U) { // 172.16.0.0/12
                return true;
            }
            if ((ip & 0xFFFF0000U) == 0xC0A80000U) { // 192.168.0.0/16
                return true;
            }
            if ((ip & 0xFF000000U) == 0x7F000000U) { // 127.0.0.0/8
                return true;
            }
            if ((ip & 0xFFFF0000U) == 0xA9FE0000U) { // 169.254.0.0/16
                return true;
            }
            if ((ip & 0xFFC00000U) == 0x64400000U) { // 100.64.0.0/10
                return true;
            }
            return false;
        }

        bool isPrivateIpv6(const in6_addr& addr) {
            static const in6_addr loopback = IN6ADDR_LOOPBACK_INIT;
            if (std::memcmp(&addr, &loopback, sizeof(in6_addr)) == 0) {
                return true;
            }
            if ((addr.s6_addr[0] & 0xFEU) == 0xFCU) { // fc00::/7
                return true;
            }
            if (addr.s6_addr[0] == 0xFEU && (addr.s6_addr[1] & 0xC0U) == 0x80U) { // fe80::/10
                return true;
            }
            return false;
        }

        bool hostResolvesToLocal(const std::string& host) {
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags = AI_ADDRCONFIG;
            addrinfo* result = nullptr;
            const int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &result);
            if (rc != 0 || result == nullptr) {
                return false;
            }

            bool foundPrivate = false;
            for (addrinfo* cursor = result; cursor != nullptr; cursor = cursor->ai_next) {
                if (cursor->ai_family == AF_INET) {
                    const auto* in = reinterpret_cast<sockaddr_in*>(cursor->ai_addr);
                    if (!isPrivateIpv4(in->sin_addr)) {
                        ::freeaddrinfo(result);
                        return false;
                    }
                    foundPrivate = true;
                } else if (cursor->ai_family == AF_INET6) {
                    const auto* in6 = reinterpret_cast<sockaddr_in6*>(cursor->ai_addr);
                    if (!isPrivateIpv6(in6->sin6_addr)) {
                        ::freeaddrinfo(result);
                        return false;
                    }
                    foundPrivate = true;
                }
            }

            ::freeaddrinfo(result);
            return foundPrivate;
        }

        bool isLocalOrPrivateHost(const std::string& host, bool allowPublicAddr) {
            if (allowPublicAddr) {
                return true;
            }
            const std::string lowered = toLower(host);
            if (lowered == "localhost") {
                return true;
            }

            in_addr in4{};
            if (::inet_pton(AF_INET, host.c_str(), &in4) == 1) {
                return isPrivateIpv4(in4);
            }

            in6_addr in6{};
            if (::inet_pton(AF_INET6, host.c_str(), &in6) == 1) {
                return isPrivateIpv6(in6);
            }

            return hostResolvesToLocal(host);
        }

        bool parseTraceyEndpoint(const std::string& rawValue, TraceyEndpoint& endpoint) {
            std::string value = trim(rawValue);
            if (value.empty()) {
                return false;
            }

            if (value.rfind("http://", 0) != 0 && value.rfind("https://", 0) != 0) {
                value = "http://" + value;
            }

            std::string rest;
            if (value.rfind("https://", 0) == 0) {
                endpoint.https = true;
                endpoint.port = 443;
                rest = value.substr(8);
            } else if (value.rfind("http://", 0) == 0) {
                endpoint.https = false;
                endpoint.port = 80;
                rest = value.substr(7);
            } else {
                return false;
            }

            const auto slashPos = rest.find('/');
            std::string hostPort = slashPos == std::string::npos ? rest : rest.substr(0, slashPos);
            endpoint.basePath = slashPos == std::string::npos ? "" : rest.substr(slashPos);
            if (hostPort.empty()) {
                return false;
            }
            while (!endpoint.basePath.empty() && endpoint.basePath.back() == '/') {
                endpoint.basePath.pop_back();
            }
            if (endpoint.basePath == "/") {
                endpoint.basePath.clear();
            }

            std::string host;
            std::string portPart;
            if (!hostPort.empty() && hostPort.front() == '[') {
                const auto close = hostPort.find(']');
                if (close == std::string::npos) {
                    return false;
                }
                host = hostPort.substr(1, close - 1);
                if (close + 1 < hostPort.size()) {
                    if (hostPort[close + 1] != ':') {
                        return false;
                    }
                    portPart = hostPort.substr(close + 2);
                }
            } else {
                const auto colonPos = hostPort.rfind(':');
                if (colonPos != std::string::npos && hostPort.find(':') == colonPos) {
                    host = hostPort.substr(0, colonPos);
                    portPart = hostPort.substr(colonPos + 1);
                } else {
                    host = hostPort;
                }
            }

            if (host.empty()) {
                return false;
            }

            if (!portPart.empty()) {
                try {
                    endpoint.port = std::stoi(portPart);
                } catch (const std::exception&) {
                    return false;
                }
            }

            if (endpoint.port <= 0 || endpoint.port > 65535) {
                return false;
            }

            endpoint.host = host;
            const bool hostIsIpv6 = host.find(':') != std::string::npos;
            endpoint.normalized = endpoint.https ? "https://" : "http://";
            endpoint.normalized += hostIsIpv6 ? "[" + host + "]" : host;
            endpoint.normalized += ":" + std::to_string(endpoint.port);
            endpoint.normalized += endpoint.basePath;
            return true;
        }

        std::string buildTraceyStatusPath(const TraceyEndpoint& endpoint) {
            if (endpoint.basePath.empty()) {
                return "/status";
            }
            return endpoint.basePath + "/status";
        }

        std::string deriveLinkSecurity(bool signaturePresent, const TraceyEndpoint* endpoint, bool tlsVerify) {
            std::string linkSecurity;
            if (endpoint == nullptr) {
                linkSecurity = "announcement-only";
            } else if (endpoint->https) {
                linkSecurity = tlsVerify ? "tls-verified" : "tls-unverified";
            } else {
                linkSecurity = "plaintext";
            }
            if (signaturePresent) {
                linkSecurity += "+signed-announcement";
            } else {
                linkSecurity += "+unsigned-announcement";
            }
            return linkSecurity;
        }

        std::string mergeTraceySource(const std::string& current, const std::string& incoming) {
            std::set<std::string> tokens;
            auto collect = [&tokens](const std::string& value) {
                if (value.empty()) {
                    return;
                }
                std::stringstream ss(value);
                std::string token;
                while (std::getline(ss, token, '+')) {
                    const std::string normalized = trim(token);
                    if (!normalized.empty()) {
                        tokens.insert(normalized);
                    }
                }
            };

            collect(current);
            collect(incoming);
            if (tokens.empty()) {
                return "";
            }

            const std::vector<std::string> orderedKnown = {
                    "heartbeat",
                    "discovery",
                    "continuum-provisioning"
            };

            std::vector<std::string> ordered;
            for (const auto& known : orderedKnown) {
                auto it = tokens.find(known);
                if (it != tokens.end()) {
                    ordered.push_back(*it);
                    tokens.erase(it);
                }
            }
            for (const auto& extra : tokens) {
                ordered.push_back(extra);
            }

            std::ostringstream out;
            for (size_t i = 0; i < ordered.size(); ++i) {
                if (i > 0) {
                    out << "+";
                }
                out << ordered[i];
            }
            return out.str();
        }

        int64_t computeBackoffMs(int failures, int64_t baseMs, int64_t maxMs) {
            int64_t value = std::max<int64_t>(baseMs, 1000);
            for (int i = 1; i < failures; ++i) {
                if (value >= maxMs) {
                    break;
                }
                if (value > maxMs / 2) {
                    value = maxMs;
                    break;
                }
                value *= 2;
            }
            return std::clamp(value, std::max<int64_t>(baseMs, 1000), std::max<int64_t>(maxMs, baseMs));
        }
    }

    APIRoutes::APIRoutes(httplib::Server& svr) {
        auto envOr = [](const char* primary, const char* fallback) -> const char* {
            const char* val = std::getenv(primary);
            if (val && *val) {
                return val;
            }
            val = std::getenv(fallback);
            if (val && *val) {
                return val;
            }
            return nullptr;
        };

        const char* authModeEnv = envOr("NMC_AUTH_MODE", "NM_AUTH_MODE");
        authMode = authModeEnv ? toLower(std::string(authModeEnv)) : "";
        if (authMode.empty()) {
            authMode = "token";
        }

        const char* authTokenEnv = envOr("NMC_AUTH_TOKEN", "NM_AUTH_TOKEN");
        authToken = authTokenEnv ? authTokenEnv : "";

        if (authMode == "off") {
            authRequired = false;
        } else if (authMode == "token") {
            authRequired = !authToken.empty();
        } else {
            authRequired = true;
        }

        const char* maxBodyEnv = std::getenv("NMC_MAX_BODY_BYTES");
        maxBodyBytes = 1024 * 1024; // 1 MiB default
        if (maxBodyEnv) {
            try {
                maxBodyBytes = static_cast<size_t>(std::stoul(maxBodyEnv));
            } catch (const std::exception&) {
                maxBodyBytes = 1024 * 1024;
            }
        }

        const char* maxLogBodyEnv = std::getenv("NMC_LOG_BODY_BYTES");
        maxLogBodyBytes = 2048;
        if (maxLogBodyEnv) {
            try {
                maxLogBodyBytes = static_cast<size_t>(std::stoul(maxLogBodyEnv));
            } catch (const std::exception&) {
                maxLogBodyBytes = 2048;
            }
        }

        const char* docsEnv = std::getenv("NMC_DOCS_ENABLED");
        docsEnabled = true;
        if (docsEnv) {
            const std::string val = toLower(std::string(docsEnv));
            docsEnabled = !(val == "0" || val == "false" || val == "no");
        }

        stopTraceyDiscovery.store(false);
        traceyStaleAfterSeconds = parseInt64Value(std::getenv("NMC_TRACEY_STALE_SECONDS"), 90, 5, 86400);
        traceyEnforceManagedResources = parseBoolValue(
                envOr("NMC_TRACEY_ENFORCE_MANAGED_RESOURCES", "NM_TRACEY_ENFORCE_MANAGED_RESOURCES"),
                true
        );
        traceyDiscoveryEnabled = parseBoolValue(envOr("NMC_TRACEY_DISCOVERY_ENABLED", "NM_TRACEY_DISCOVERY_ENABLED"), true);
        traceyAllowPublicAddr = parseBoolValue(envOr("NMC_TRACEY_ALLOW_PUBLIC_ADDR", "NM_TRACEY_ALLOW_PUBLIC_ADDR"), false);
        traceyTlsVerify = parseBoolValue(envOr("NMC_TRACEY_TLS_VERIFY", "NM_TRACEY_TLS_VERIFY"), true);
        traceyRequireSignature = parseBoolValue(envOr("NMC_TRACEY_REQUIRE_SIGNATURE", "NM_TRACEY_REQUIRE_SIGNATURE"), false);
        traceyRequirementGraceSeconds = parseInt64Value(
                envOr("NMC_TRACEY_REQUIREMENT_GRACE_SECONDS", "NM_TRACEY_REQUIREMENT_GRACE_SECONDS"),
                300,
                5,
                86400
        );
        const char* traceyBindAddrEnv = envOr("NMC_TRACEY_DISCOVERY_BIND_ADDR", "NM_TRACEY_DISCOVERY_BIND_ADDR");
        traceyDiscoveryBindAddr = traceyBindAddrEnv ? trim(traceyBindAddrEnv) : "0.0.0.0";
        if (traceyDiscoveryBindAddr.empty()) {
            traceyDiscoveryBindAddr = "0.0.0.0";
        }
        traceyDiscoveryPort = static_cast<int>(
                parseInt64Value(envOr("NMC_TRACEY_DISCOVERY_PORT", "NM_TRACEY_DISCOVERY_PORT"), 47990, 1, 65535)
        );
        traceyDiscoveryMaxAgeMs = parseInt64Value(
                envOr("NMC_TRACEY_DISCOVERY_MAX_AGE_MS", "NM_TRACEY_DISCOVERY_MAX_AGE_MS"),
                15000,
                1000,
                3600000
        );
        traceyStatusPollMs = parseInt64Value(
                envOr("NMC_TRACEY_STATUS_POLL_MS", "NM_TRACEY_STATUS_POLL_MS"),
                10000,
                1000,
                600000
        );
        traceyStatusTimeoutMs = parseInt64Value(
                envOr("NMC_TRACEY_STATUS_TIMEOUT_MS", "NM_TRACEY_STATUS_TIMEOUT_MS"),
                2500,
                500,
                120000
        );
        traceyStatusMaxBackoffMs = parseInt64Value(
                envOr("NMC_TRACEY_STATUS_MAX_BACKOFF_MS", "NM_TRACEY_STATUS_MAX_BACKOFF_MS"),
                120000,
                traceyStatusPollMs,
                3600000
        );
        const char* traceyStatusTokenEnv = envOr("NMC_TRACEY_STATUS_BEARER_TOKEN", "NM_TRACEY_STATUS_BEARER_TOKEN");
        traceyStatusBearerToken = traceyStatusTokenEnv ? trim(traceyStatusTokenEnv) : "";

        const bool traceyBootstrapLocalAgent = parseBoolValue(
                envOr("NMC_TRACEY_BOOTSTRAP_LOCAL_AGENT", "NM_TRACEY_BOOTSTRAP_LOCAL_AGENT"),
                true
        );
        const char* localAgentIdEnv = envOr("NMC_TRACEY_LOCAL_AGENT_ID", "NM_TRACEY_LOCAL_AGENT_ID");
        const std::string localTraceyAgentId = localAgentIdEnv ? trim(localAgentIdEnv) : "tracey-continuum-local";
        const char* localStatusAddrEnv = envOr("NMC_TRACEY_LOCAL_STATUS_ADDR", "NM_TRACEY_LOCAL_STATUS_ADDR");
        std::string localTraceyStatusAddr = localStatusAddrEnv ? trim(localStatusAddrEnv) : "http://127.0.0.1:48000";
        if (!localTraceyStatusAddr.empty()) {
            TraceyEndpoint endpoint;
            if (!parseTraceyEndpoint(localTraceyStatusAddr, endpoint) ||
                !isLocalOrPrivateHost(endpoint.host, traceyAllowPublicAddr)) {
                std::cerr << "[WARN] Ignoring invalid local Tracey status address '" << localTraceyStatusAddr
                          << "' for bootstrap sidecar requirement." << std::endl;
                localTraceyStatusAddr.clear();
            } else {
                localTraceyStatusAddr = endpoint.normalized;
            }
        }
        if (traceyBootstrapLocalAgent && !localTraceyAgentId.empty()) {
            std::lock_guard<std::mutex> lock(dataMutex);
            const int64_t nowMs = nowEpochMs();
            upsertTraceyRequirementLocked(
                    "continuum_server",
                    "nmc_server",
                    "local",
                    localTraceyAgentId,
                    localTraceyStatusAddr,
                    nowMs
            );
            auto& localAgent = traceyAgents[localTraceyAgentId];
            localAgent.cluster = "continuum-local";
            localAgent.host = "localhost";
            localAgent.status = localAgent.status.empty() ? "unknown" : localAgent.status;
            localAgent.linkState = "required";
            if (localAgent.metrics.is_null() || !localAgent.metrics.is_object()) {
                localAgent.metrics = nlohmann::json::object();
            }
            localAgent.metrics["bootstrap_managed"] = true;
            localAgent.metrics["monitors"] = {"nmc_server"};
            localAgent.metrics["bootstrap_epoch_ms"] = nowMs;
            if (localTraceyStatusAddr.empty()) {
                localAgent.lastError = "Bootstrap Tracey sidecar is required but no local status_addr is configured.";
            }
        }

        if (authMode == "oidc") {
            OIDCConfig cfg;
            const char* introspection = envOr("NMC_OIDC_INTROSPECTION_URL", "NM_OIDC_INTROSPECTION_URL");
            cfg.introspectionUrl = introspection ? introspection : "";
            const char* issuer = envOr("NMC_OIDC_ISSUER", "NM_OIDC_ISSUER");
            cfg.issuer = issuer ? issuer : "";
            const char* clientId = envOr("NMC_OIDC_CLIENT_ID", "NM_OIDC_CLIENT_ID");
            cfg.clientId = clientId ? clientId : "";
            const char* clientSecret = envOr("NMC_OIDC_CLIENT_SECRET", "NM_OIDC_CLIENT_SECRET");
            cfg.clientSecret = clientSecret ? clientSecret : "";

            auto appendCsv = [&cfg](const char* raw, const auto& trimFn) {
                if (!raw) return;
                std::stringstream ss(raw);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    const std::string trimmed = trimFn(item);
                    if (!trimmed.empty()) {
                        cfg.audiences.push_back(trimmed);
                    }
                }
            };

            appendCsv(envOr("NMC_OIDC_AUDIENCE", "NM_OIDC_AUDIENCE"), trim);
            appendCsv(envOr("NMC_OIDC_ALLOWED_AUDIENCES", "NM_OIDC_ALLOWED_AUDIENCES"), trim);
            appendCsv(envOr("NMC_OIDC_AUDIENCES", "NM_OIDC_AUDIENCES"), trim);

            const char* requiredScope = envOr("NMC_OIDC_REQUIRED_SCOPE", "NM_OIDC_REQUIRED_SCOPE");
            if (requiredScope) {
                std::stringstream ss(requiredScope);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    const std::string trimmed = trim(item);
                    if (!trimmed.empty()) {
                        cfg.requiredScopes.push_back(trimmed);
                    }
                }
            }

            const char* requiredScopesCsv = envOr("NMC_OIDC_REQUIRED_SCOPES", "NM_OIDC_REQUIRED_SCOPES");
            if (requiredScopesCsv) {
                std::stringstream ss(requiredScopesCsv);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    const std::string trimmed = trim(item);
                    if (!trimmed.empty()) {
                        cfg.requiredScopes.push_back(trimmed);
                    }
                }
            }
            oidcValidator = std::make_unique<OIDCValidator>(cfg);
        }

        // Initialize K8sHandlers, passing necessary references and callbacks
        const char* kubeconfigEnv = envOr("KUBECONFIG", "NMC_KUBECONFIG");
        const std::string kubeconfigPath = kubeconfigEnv ? kubeconfigEnv : "";

        std::string api_server_url_value;
        const char* explicitApiUrl = envOr("NMC_K8S_API_URL", "NM_K8S_API_URL");
        if (explicitApiUrl && *explicitApiUrl) {
            api_server_url_value = explicitApiUrl;
        } else {
            const char* k8sHost = std::getenv("KUBERNETES_SERVICE_HOST");
            const char* k8sPort = std::getenv("KUBERNETES_SERVICE_PORT");
            if (k8sHost && *k8sHost) {
                api_server_url_value = "https://" + std::string(k8sHost) + ":" + (k8sPort && *k8sPort ? std::string(k8sPort) : "443");
            } else {
                api_server_url_value = "https://127.0.0.1:6443";
            }
        }

        k8sHandlers = std::make_unique<K8sHandlers>(
                api_server_url_value,
                kubeconfigPath,
                dataMutex,
                [this](httplib::Response& res, const Models::CloudResponse& apiResponse) {
                    sendJsonResponse(res, apiResponse);
                },
                [this](httplib::Response& res, int status, const std::string& message) {
                    sendErrorResponse(res, status, message);
                }
        );

        // OpenShift portal API base URL (FastAPI in oshift). Defaults to localhost:8000.
        const char* osUrlEnv = std::getenv("NMC_OSHIFT_API_URL");
        std::string osUrl = osUrlEnv ? osUrlEnv : "http://127.0.0.1:8000";
        openShiftClient = std::make_unique<OpenShiftClient>(osUrl);

        // Enable logging of all requests
        svr.set_logger([this](const httplib::Request& req, const httplib::Response& res) {
            logRequest(req, res);
        });

        if (docsEnabled) {
            // Set up static file serving for documentation
            // Assumes a 'docs' directory exists relative to the executable
            // or where the server is run from.
            // This is a robust way to serve static content like documentation.
            // All requests to /docs/* will look for files in the ./docs/ directory.
            // E.g., /docs/index.html will serve ./docs/index.html
            // /docs/bucket.html will serve ./docs/bucket.html
            svr.set_base_dir("./docs"); // Set the base directory for static files

            // --- Documentation Route ---
            // This route will serve the index.html from the docs directory
            // when /docs is accessed directly.
            // Other files like /docs/bucket.html will be served automatically
            // by set_base_dir.
            svr.Get("/", [](const httplib::Request& req, httplib::Response& res) {
                res.set_content(Utils::readFile("./docs/index.html"), "text/html");
            });
            svr.Get("/index.html", [](const httplib::Request& req, httplib::Response& res) {
                res.set_content(Utils::readFile("./docs/index.html"), "text/html");
            });
            svr.Get("/docs", [](const httplib::Request& req, httplib::Response& res) {
                res.set_content(Utils::readFile("./docs/index.html"), "text/html");
            });
            svr.Get("/login", [](const httplib::Request& req, httplib::Response& res) {
                res.set_content(Utils::readFile("./docs/login.html"), "text/html");
            });
            // Legacy launch path used by neuralmimicry.ai control-panel links.
            svr.Get(R"(^/services/health/monitoring/?$)", [](const httplib::Request& req, httplib::Response& res) {
                res.set_content(Utils::readFile("./docs/index.html"), "text/html");
            });
            svr.Get(R"(^/services/health/monitoring/index\.html$)", [](const httplib::Request& req, httplib::Response& res) {
                res.set_content(Utils::readFile("./docs/index.html"), "text/html");
            });
            svr.Get(R"(^/services/health/monitoring/login/?$)", [](const httplib::Request& req, httplib::Response& res) {
                res.set_content(Utils::readFile("./docs/login.html"), "text/html");
            });
            svr.Get("/logout", [](const httplib::Request& req, httplib::Response& res) {
                res.set_redirect("/login");
            });
        }

        auto guard = [this](const httplib::Request& req, httplib::Response& res) -> bool {
            ensureRequestId(req, res);
            if (!enforceBodyLimit(req, res)) {
                return false;
            }
            if (!authorizeOrReject(req, res)) {
                return false;
            }
            return true;
        };

        // --- Bucket Routes ---
        svr.Post("/bucket/create", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleCreateBucket(req, res);
        });
        svr.Delete(R"(/bucket/delete/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleDeleteBucket(req, res);
        });
        svr.Get(R"(/bucket/get/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGetBucket(req, res);
        });
        svr.Get("/bucket/list", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListBuckets(req, res);
        });
        svr.Get("/bucket/list-locations", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListBucketLocations(req, res);
        });
        svr.Get("/bucket/list-types", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListBucketTypes(req, res);
        });
        svr.Post(R"(/bucket/reset-key/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleResetBucketKey(req, res);
        });

        // --- Connection Routes ---
        // GET /connections/status
        svr.Get("/connections/status", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            ConnectionHandlers::handleGetConnectionStatus(req, res);
        });

        // POST /connections/make
        svr.Post("/connections/make", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            ConnectionHandlers::handleMakeConnection(req, res);
        });

        // DELETE /connections/:name
        // svr.Delete(R"(/connections/(?P<name>[^/]+))", ConnectionHandlers::handleDropConnection);
        svr.Delete(R"(^/connections/([^/]+)$)",  [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            ConnectionHandlers::handleDropConnection(req, res);
        });

        // GET /connections
        svr.Get("/connections", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            ConnectionHandlers::handleListConnections(req, res);
        });

        // POST /connections/select
        svr.Post("/connections/select", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            ConnectionHandlers::handleSelectConnection(req, res);
        });

        std::cout << "Connection API routes registered." << std::endl;

        // --- K8s Routes ---
        svr.Post("/k8s/create", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            std::string clusterName = "unknown";
            std::string region;
            std::string traceyAgentId;
            std::string traceyStatusAddr;
            try {
                auto jsonBody = nlohmann::json::parse(req.body);
                clusterName = jsonBody.value("name", clusterName);
                region = jsonBody.value("region", "");
                std::string reason;
                if (!extractTraceyProvisioningRequest(jsonBody, traceyAgentId, traceyStatusAddr, reason)) {
                    return sendErrorResponse(res, 400, reason);
                }
            } catch (const nlohmann::json::parse_error& e) {
                return sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
            }
            k8sHandlers->handleCreateK8sCluster(req, res);
            if (res.status >= 200 && res.status < 300 && !traceyAgentId.empty()) {
                std::lock_guard<std::mutex> lock(dataMutex);
                upsertTraceyRequirementLocked("k8s_cluster", clusterName, region, traceyAgentId, traceyStatusAddr, nowEpochMs());
            }
        });
        svr.Delete(R"(/k8s/delete/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            const std::string clusterId = req.matches.size() > 1 ? req.matches[1].str() : "";
            k8sHandlers->handleDeleteK8sCluster(req, res);
            if (res.status >= 200 && res.status < 300 && !clusterId.empty()) {
                std::lock_guard<std::mutex> lock(dataMutex);
                removeTraceyRequirementLocked("k8s_cluster", clusterId);
            }
        });
        svr.Get(R"(/k8s/get/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetK8sCluster(req, res);
        });
        svr.Get(R"(/k8s/get-config/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetKubeConfig(req, res);
        });
        svr.Get("/k8s/list", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleListK8sClusters(req, res);
        });
        svr.Get("/k8s/list-locations", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleListK8sLocations(req, res);
        });
        svr.Get("/k8s/healthz", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleK8sHealthCheck(req, res);
        });
        svr.Post(R"(/k8s/resume/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleResumeK8sCluster(req, res);
        });
        svr.Post(R"(/k8s/suspend/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleSuspendK8sCluster(req, res);
        });

        // --- Model Routes ---
        svr.Post("/model/upload", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleUploadModel(req, res);
        });

        // --- SSH Routes ---
        svr.Post("/ssh/create", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleCreateSSHKey(req, res);
        });
        svr.Delete(R"(/ssh/delete/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleDeleteSSHKey(req, res);
        });
        svr.Get("/ssh/list", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListSSHKeys(req, res);
        });

        // --- VM Routes ---
        svr.Post("/vm/create", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleCreateVM(req, res);
        });
        svr.Delete(R"(/vm/delete/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleDeleteVM(req, res);
        });
        svr.Get(R"(/vm/get/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGetVM(req, res);
        });
        svr.Get("/vm/list", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListVMs(req, res);
        });
        svr.Get("/vm/list-locations", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListVMLocations(req, res);
        });
        svr.Get("/vm/list-os", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListVMOSImages(req, res);
        });
        svr.Get("/vm/list-sku", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListVMSKUs(req, res);
        });
        svr.Post(R"(/vm/restart/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleRestartVM(req, res);
        });
        svr.Post(R"(/vm/resume/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleResumeVM(req, res);
        });
        svr.Post(R"(/vm/suspend/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleSuspendVM(req, res);
        });

        // --- OpenShift Routes ---
        svr.Get("/openshift/resources", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenShiftResources(req, res);
        });
        svr.Get("/openshift/clusters", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenShiftClusters(req, res);
        });
        svr.Post("/openshift/clusters/request", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenShiftRequestCluster(req, res);
        });

        // --- Tracey Agent Routes ---
        svr.Post("/tracey/agents/heartbeat", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyHeartbeat(req, res);
        });
        svr.Get("/tracey/agents", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListTraceyAgents(req, res);
        });

        if (traceyDiscoveryEnabled) {
            try {
                traceyDiscoveryThread = std::thread(&APIRoutes::runTraceyDiscoveryLoop, this);
            } catch (const std::exception& e) {
                traceyDiscoveryEnabled = false;
                std::cerr << "[WARN] Failed to start Tracey discovery thread: " << e.what() << std::endl;
            }
        }
    }

    // Explicit definition of the destructor for APIRoutes
    // This is crucial because APIRoutes contains a std::unique_ptr to K8sHandlers,
    // and K8sHandlers is only forward-declared in APIRoutes.h.
    // The default destructor for unique_ptr needs the full definition of the type
    // it manages when it's instantiated, which happens when the APIRoutes destructor
    // is defined.
    APIRoutes::~APIRoutes() {
        stopTraceyDiscovery.store(true);
        if (traceyDiscoveryThread.joinable()) {
            traceyDiscoveryThread.join();
        }
    }

    /**
     * @brief Logs incoming HTTP requests to standard output.
     *
     * This method provides basic logging for each request received by the server,
     * including the HTTP method, path, and request body if present.
     *
     * @param req The httplib::Request object representing the incoming request.
     */
    void APIRoutes::logRequest(const httplib::Request& req, const httplib::Response& res) const {
        const std::string requestId = res.get_header_value("X-Request-ID").empty()
                ? req.get_header_value("X-Request-ID")
                : res.get_header_value("X-Request-ID");

        std::cout << "[" << req.method << "] " << req.path;
        if (!requestId.empty()) {
            std::cout << " request_id=" << requestId;
        }
        if (!req.body.empty() && shouldLogBody(req.path)) {
            std::cout << " Body: " << redactBody(req.body);
        } else if (!req.body.empty()) {
            std::cout << " Body: [redacted]";
        }
        std::cout << std::endl;
    }

    /**
     * @brief Sends a JSON response based on a Models::CloudResponse object.
     *
     * Sets the Content-Type header to application/json, determines the HTTP status
     * (200 for success, 400 for client error based on apiResponse.success), and
     * sets the response body to a pretty-printed JSON string.
     *
     * @param res The httplib::Response object to send the response through.
     * @param apiResponse The Models::CloudResponse object containing success status,
     * message, and data to be converted to JSON.
     */
    void APIRoutes::sendJsonResponse(httplib::Response& res, const Models::CloudResponse& apiResponse) const {
        res.set_header("Content-Type", "application/json");
        res.status = apiResponse.success ? 200 : 400; // 200 for success, 400 for client error
        res.body = apiResponse.toJsonString().dump(4); // Pretty print JSON
    }

    /**
     * @brief Sends an error JSON response.
     *
     * Creates a Models::CloudResponse object with success set to false,
     * the provided message, and an empty JSON data object. It then calls
     * sendJsonResponse to format and send this error response, finally
     * setting the HTTP status code explicitly.
     *
     * @param res The httplib::Response object to send the error response through.
     * @param status The HTTP status code for the error (e.g., 400, 404, 500).
     * @param message A string describing the error.
     */
    void APIRoutes::sendErrorResponse(httplib::Response& res, int status, const std::string& message) const {
        Models::CloudResponse apiResponse;
        apiResponse.success = false;
        apiResponse.message = message;
        apiResponse.data = nlohmann::json({});
        sendJsonResponse(res, apiResponse);
        res.status = status; // Set HTTP status code for error
    }

    void APIRoutes::ensureRequestId(const httplib::Request& req, httplib::Response& res) const {
        const std::string existing = req.get_header_value("X-Request-ID");
        if (!existing.empty()) {
            res.set_header("X-Request-ID", existing);
            return;
        }
        res.set_header("X-Request-ID", Utils::generateUniqueId("req"));
    }

    bool APIRoutes::enforceBodyLimit(const httplib::Request& req, httplib::Response& res) const {
        if (req.body.size() > maxBodyBytes) {
            sendErrorResponse(res, 413, "Request body exceeds maximum allowed size.");
            return false;
        }
        return true;
    }

    bool APIRoutes::authorizeOrReject(const httplib::Request& req, httplib::Response& res) const {
        if (!authRequired) {
            return true;
        }
        if (authMode == "token") {
            const std::string bearer = req.get_header_value("Authorization");
            if (!bearer.empty()) {
                const std::string prefix = "Bearer ";
                if (bearer.rfind(prefix, 0) == 0) {
                    const std::string token = bearer.substr(prefix.size());
                    if (!authToken.empty() && token == authToken) {
                        return true;
                    }
                }
            }
            const std::string tokenHeader = req.get_header_value("X-NMC-Token");
            if (!tokenHeader.empty() && !authToken.empty() && tokenHeader == authToken) {
                return true;
            }
            res.set_header("WWW-Authenticate", "Bearer");
            sendErrorResponse(res, 401, "Unauthorized.");
            return false;
        }

        if (authMode == "oidc") {
            if (!oidcValidator || !oidcValidator->isConfigured()) {
                sendErrorResponse(res, 500, "OIDC authentication is misconfigured.");
                return false;
            }
            const std::string bearer = req.get_header_value("Authorization");
            std::string token;
            if (!bearer.empty()) {
                const std::string prefix = "Bearer ";
                if (bearer.rfind(prefix, 0) == 0) {
                    token = bearer.substr(prefix.size());
                }
            }
            if (token.empty()) {
                res.set_header("WWW-Authenticate", "Bearer");
                sendErrorResponse(res, 401, "Unauthorized.");
                return false;
            }

            std::string errorMessage;
            nlohmann::json claims;
            if (!oidcValidator->validateToken(token, errorMessage, &claims)) {
                res.set_header("WWW-Authenticate", "Bearer");
                sendErrorResponse(res, 401, "Unauthorized.");
                return false;
            }
            return true;
        }

        sendErrorResponse(res, 500, "Unsupported authentication mode.");
        return false;
    }

    bool APIRoutes::shouldLogBody(const std::string& path) const {
        // Redact known sensitive endpoints (keys, scripts, credentials, provisioning).
        const std::vector<std::string> redactedPrefixes = {
                "/ssh/create",
                "/vm/create",
                "/model/upload",
                "/connections/make",
                "/openshift/clusters/request"
        };
        for (const auto& prefix : redactedPrefixes) {
            if (path.rfind(prefix, 0) == 0) {
                return false;
            }
        }
        return true;
    }

    std::string APIRoutes::redactBody(const std::string& body) const {
        if (body.size() <= maxLogBodyBytes) {
            return body;
        }
        return body.substr(0, maxLogBodyBytes) + "...(truncated)";
    }

// --- Bucket Handlers ---

    /**
     * @brief Handles the creation of a new bucket.
     *
     * Expects a JSON body with 'name', 'location', and 'type'.
     * Checks for missing parameters, duplicate bucket names, and creates a bucket.
     * Sends a success or error JSON response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleCreateBucket(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string name = json_body.value("name", "");
            std::string location = json_body.value("location", "");
            std::string type = json_body.value("type", "");

            if (name.empty() || location.empty() || type.empty()) {
                return sendErrorResponse(res, 400, "Missing required parameters: name, location, or type.");
            }

            std::lock_guard<std::mutex> lock(dataMutex);
            // Check if bucket already exists
            auto it = std::find_if(buckets.begin(), buckets.end(),
                                   [&](const Models::Bucket& b) { return b.name == name; });
            if (it != buckets.end()) {
                return sendErrorResponse(res, 409, "Bucket '" + name + "' already exists."); // Conflict
            }

            Models::Bucket newBucket;
            newBucket.name = name;
            newBucket.location = location;
            newBucket.type = type;
            newBucket.status = "Active";
            buckets.push_back(newBucket);

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Bucket '" + name + "' created successfully.";
            apiResponse.data = newBucket.toJsonString();
            sendJsonResponse(res, apiResponse);

        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

    /**
     * @brief Handles the deletion of a bucket.
     *
     * Extracts the bucket name from the URL path.
     * Deletes the corresponding bucket if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleDeleteBucket(const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1]; // Get name from regex capture

        std::lock_guard<std::mutex> lock(dataMutex);
        auto initial_size = buckets.size();
        buckets.erase(std::remove_if(buckets.begin(), buckets.end(),
                                         [&](const Models::Bucket& b) { return b.name == name; }),
                          buckets.end());

        if (buckets.size() < initial_size) {
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Bucket '" + name + "' deleted successfully.";
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "Bucket '" + name + "' not found.");
        }
    }

    /**
     * @brief Handles retrieving details of a specific bucket.
     *
     * Extracts the bucket name from the URL path.
     * Finds and returns the bucket's details if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleGetBucket(const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = std::find_if(buckets.begin(), buckets.end(),
                               [&](const Models::Bucket& b) { return b.name == name; });

        if (it != buckets.end()) {
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Bucket details retrieved.";
            apiResponse.data = it->toJsonString();
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "Bucket '" + name + "' not found.");
        }
    }

    /**
     * @brief Handles listing all buckets, with optional name filtering.
     *
     * Supports a 'filter-name' query parameter to filter buckets by name.
     * Returns a JSON array of bucket objects.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListBuckets(const httplib::Request& req, httplib::Response& res) {
        std::string filterName = req.get_param_value("filter-name");

        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json bucketList = nlohmann::json::array();
        for (const auto& bucket : buckets) {
            if (filterName.empty() || bucket.name.find(filterName) != std::string::npos) {
                bucketList.push_back(bucket.toJsonString());
            }
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Buckets listed successfully.";
        apiResponse.data = bucketList;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles listing available bucket locations.
     *
     * Returns a predefined list of bucket locations.
     * In a real system, these would be fetched from a configuration or cloud provider API.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListBucketLocations(const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        // In a real system, these would come from a configuration or backend service
        nlohmann::json locations = {
                {"name", "eu-central"},
                {"name", "us-east"},
                {"name", "asia-southeast"}
        };

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Bucket locations listed successfully.";
        apiResponse.data = locations;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles listing available bucket types.
     *
     * Returns a predefined list of bucket types.
     * In a real system, these would be fetched from a configuration or cloud provider API.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListBucketTypes(const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json types = {
                {"name", "standard"},
                {"name", "archive"},
                {"name", "coldline"}
        };

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Bucket types listed successfully.";
        apiResponse.data = types;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles resetting access credentials for a bucket.
     *
     * Extracts the bucket name from the URL path.
     * Simulates resetting credentials for the bucket if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleResetBucketKey(const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = std::find_if(buckets.begin(), buckets.end(),
                               [&](const Models::Bucket& b) { return b.name == name; });

        if (it != buckets.end()) {
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Bucket '" + name + "' access credentials reset successfully.";
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "Bucket '" + name + "' not found.");
        }
    }

// --- Model Handlers ---
    /**
     * @brief Handles the upload of a model.
     *
     * Expects a JSON body with 'filePath', 'sku', 'registryName', and 'tag'.
     * This is a implementation that only acknowledges the upload without
     * actual file handling.
     * Sends a success or error JSON response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleUploadModel(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string filePath = json_body.value("filePath", "");
            std::string sku = json_body.value("sku", "");
            std::string registryName = json_body.value("registryName", "");
            std::string tag = json_body.value("tag", "");

            if (filePath.empty() || sku.empty() || registryName.empty() || tag.empty()) {
                return sendErrorResponse(res, 400, "Missing required parameters: filePath, sku, registryName, or tag.");
            }

            // In a real scenario, you'd handle file upload logic here
            // For mock, just acknowledge the upload
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Model '" + sku + "' version '" + tag + "' uploaded to registry '" + registryName + "' successfully.";
            apiResponse.data = {
                    {"filePath", filePath},
                    {"sku", sku},
                    {"registryName", registryName},
                    {"tag", tag}
            };
            sendJsonResponse(res, apiResponse);

        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

// --- SSH Key Handlers ---
    /**
     * @brief Handles the creation of a new SSH key.
     *
     * Expects a JSON body with 'name' and 'publicKey', and optional 'description'.
     * Checks for missing parameters, duplicate key names, and creates a SSH key.
     * Sends a success or error JSON response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleCreateSSHKey(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string name = json_body.value("name", "");
            std::string publicKey = json_body.value("publicKey", "");
            std::string description = json_body.value("description", "");

            if (name.empty() || publicKey.empty()) {
                return sendErrorResponse(res, 400, "Missing required parameters: name or publicKey.");
            }

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(sshKeys.begin(), sshKeys.end(),
                                   [&](const Models::SSHKey& k) { return k.name == name; });
            if (it != sshKeys.end()) {
                return sendErrorResponse(res, 409, "SSH key '" + name + "' already exists.");
            }

            Models::SSHKey newKey;
            newKey.id = Utils::generateUniqueId("sshkey");
            newKey.name = name;
            newKey.publicKey = publicKey;
            newKey.description = description;
            sshKeys.push_back(newKey);

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "SSH key '" + name + "' created successfully.";
            apiResponse.data = newKey.toJsonString();
            sendJsonResponse(res, apiResponse);

        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

    /**
     * @brief Handles the deletion of an SSH key.
     *
     * Extracts the key ID from the URL path.
     * Deletes the corresponding SSH key if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleDeleteSSHKey(const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto initial_size = sshKeys.size();
        sshKeys.erase(std::remove_if(sshKeys.begin(), sshKeys.end(),
                                         [&](const Models::SSHKey& k) { return k.id == id; }),
                          sshKeys.end());

        if (sshKeys.size() < initial_size) {
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "SSH key '" + id + "' deleted successfully.";
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "SSH key '" + id + "' not found.");
        }
    }

    /**
     * @brief Handles listing all SSH keys, with optional name filtering.
     *
     * Supports a 'filter-name' query parameter to filter keys by name.
     * Returns a JSON array of SSH key objects.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListSSHKeys(const httplib::Request& req, httplib::Response& res) {
        std::string filterName = req.get_param_value("filter-name");

        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json keyList = nlohmann::json::array();
        for (const auto& key : sshKeys) {
            if (filterName.empty() || key.name.find(filterName) != std::string::npos) {
                keyList.push_back(key.toJsonString());
            }
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "SSH keys listed successfully.";
        apiResponse.data = keyList;
        sendJsonResponse(res, apiResponse);
    }

    bool APIRoutes::extractTraceyProvisioningRequest(const nlohmann::json& payload,
                                                     std::string& agentIdOut,
                                                     std::string& statusAddrOut,
                                                     std::string& reasonOut) const {
        agentIdOut.clear();
        statusAddrOut.clear();
        reasonOut.clear();

        if (!payload.is_object()) {
            reasonOut = "Invalid provisioning payload.";
            return false;
        }

        if (!payload.contains("tracey")) {
            if (traceyEnforceManagedResources) {
                reasonOut = "Tracey configuration is required. Provide tracey.agent_id and optional tracey.status_addr.";
                return false;
            }
            return true;
        }

        const auto& tracey = payload["tracey"];
        if (!tracey.is_object()) {
            reasonOut = "Invalid tracey configuration. Expected object.";
            return false;
        }

        agentIdOut = trim(tracey.value("agent_id", tracey.value("agentId", "")));
        statusAddrOut = trim(tracey.value("status_addr", tracey.value("statusAddr", "")));
        const bool autoDiscovery = tracey.value("auto_discovery", true);

        if (agentIdOut.empty()) {
            reasonOut = "Missing required tracey.agent_id for managed resource provisioning.";
            return false;
        }
        if (statusAddrOut.empty() && !autoDiscovery) {
            reasonOut = "tracey.status_addr is required when tracey.auto_discovery is false.";
            return false;
        }

        if (!statusAddrOut.empty()) {
            TraceyEndpoint endpoint;
            if (!parseTraceyEndpoint(statusAddrOut, endpoint)) {
                reasonOut = "tracey.status_addr is not a valid HTTP(S) endpoint.";
                return false;
            }
            if (!isLocalOrPrivateHost(endpoint.host, traceyAllowPublicAddr)) {
                reasonOut = "tracey.status_addr is outside allowed network ranges.";
                return false;
            }
            statusAddrOut = endpoint.normalized;
        }

        return true;
    }

    void APIRoutes::upsertTraceyRequirementLocked(const std::string& resourceType,
                                                  const std::string& resourceName,
                                                  const std::string& region,
                                                  const std::string& agentId,
                                                  const std::string& statusAddr,
                                                  int64_t nowMs) {
        if (agentId.empty()) {
            return;
        }

        const std::string key = resourceType + ":" + toLower(trim(resourceName));
        TraceyRequirement requirement;
        requirement.key = key;
        requirement.resourceType = resourceType;
        requirement.resourceName = resourceName;
        requirement.region = region;
        requirement.expectedAgentId = agentId;
        requirement.expectedStatusAddr = statusAddr;
        requirement.createdEpochMs = nowMs;
        if (!key.empty() && key.back() != ':') {
            traceyRequirements[key] = requirement;
        }

        auto& agent = traceyAgents[agentId];
        agent.agentId = agentId;
        agent.source = mergeTraceySource(agent.source, "continuum-provisioning");
        if (agent.cluster.empty()) {
            agent.cluster = resourceName;
        }
        if (agent.status.empty()) {
            agent.status = "unknown";
        }
        if (agent.metrics.is_null() || !agent.metrics.is_object()) {
            agent.metrics = nlohmann::json::object();
        }
        agent.metrics["managed_by_continuum"] = true;
        agent.metrics["managed_resource_type"] = resourceType;
        agent.metrics["managed_resource_name"] = resourceName;
        agent.metrics["managed_resource_region"] = region;
        agent.metrics["managed_since_epoch_ms"] = nowMs;
        if (!statusAddr.empty()) {
            agent.statusAddr = statusAddr;
            agent.nextQueryEpochMs = nowMs;
            TraceyEndpoint endpoint;
            if (parseTraceyEndpoint(statusAddr, endpoint)) {
                agent.linkSecurity = deriveLinkSecurity(agent.signaturePresent, &endpoint, traceyTlsVerify);
            }
        }
        if (agent.linkState.empty()) {
            agent.linkState = "required";
        }
    }

    void APIRoutes::removeTraceyRequirementLocked(const std::string& resourceType, const std::string& resourceName) {
        const std::string normalized = toLower(trim(resourceName));
        if (normalized.empty()) {
            return;
        }

        const std::string directKey = resourceType + ":" + normalized;
        auto direct = traceyRequirements.find(directKey);
        if (direct != traceyRequirements.end()) {
            traceyRequirements.erase(direct);
            return;
        }

        for (auto it = traceyRequirements.begin(); it != traceyRequirements.end();) {
            if (it->second.resourceType == resourceType &&
                toLower(trim(it->second.resourceName)) == normalized) {
                it = traceyRequirements.erase(it);
            } else {
                ++it;
            }
        }
    }

// --- VM Handlers ---
    /**
     * @brief Handles the creation of a new Virtual Machine.
     *
     * Expects a JSON body with 'name', 'sku', 'region', 'osImage', and 'publicKeyId',
     * and optional 'initScript'.
     * Checks for missing parameters, duplicate VM names, valid publicKeyId,
     * and creates a VM.
     * Sends a success or error JSON response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleCreateVM(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string name = json_body.value("name", "");
            std::string sku = json_body.value("sku", "");
            std::string region = json_body.value("region", "");
            std::string osImage = json_body.value("osImage", "");
            std::string publicKeyId = json_body.value("publicKeyId", "");
            std::string initScript = json_body.value("initScript", "");
            std::string traceyAgentId;
            std::string traceyStatusAddr;
            std::string traceyReason;

            if (name.empty() || sku.empty() || region.empty() || osImage.empty() || publicKeyId.empty()) {
                return sendErrorResponse(res, 400, "Missing required parameters: name, sku, region, osImage, or publicKeyId.");
            }
            if (!extractTraceyProvisioningRequest(json_body, traceyAgentId, traceyStatusAddr, traceyReason)) {
                return sendErrorResponse(res, 400, traceyReason);
            }

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(vms.begin(), vms.end(),
                                   [&](const Models::VM& vm) { return vm.name == name; });
            if (it != vms.end()) {
                return sendErrorResponse(res, 409, "VM '" + name + "' already exists.");
            }

            // Check if publicKeyId exists in sshKeys
            auto sshKeyIt = std::find_if(sshKeys.begin(), sshKeys.end(),
                                         [&](const Models::SSHKey& k) { return k.id == publicKeyId; });
            if (sshKeyIt == sshKeys.end()) {
                return sendErrorResponse(res, 400, "Invalid publicKeyId: '" + publicKeyId + "' not found.");
            }


            Models::VM newVM;
            newVM.id = Utils::generateUniqueId("vm");
            newVM.name = name;
            newVM.sku = sku;
            newVM.region = region;
            newVM.osImage = osImage;
            newVM.publicKeyId = publicKeyId;
            newVM.initScript = initScript;
            newVM.status = "Creating";
            vms.push_back(newVM);
            upsertTraceyRequirementLocked("vm", name, region, traceyAgentId, traceyStatusAddr, nowEpochMs());

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM '" + name + "' created successfully.";
            apiResponse.data = newVM.toJsonString();
            if (!traceyAgentId.empty()) {
                apiResponse.data["tracey"] = {
                        {"required", true},
                        {"agent_id", traceyAgentId},
                        {"status_addr", traceyStatusAddr},
                        {"compliance_state", "pending"},
                        {"reason", "Waiting for Tracey agent heartbeat/discovery after provisioning."}
                };
            } else {
                apiResponse.data["tracey"] = {
                        {"required", traceyEnforceManagedResources}
                };
            }
            sendJsonResponse(res, apiResponse);

        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

    /**
     * @brief Handles the deletion of a Virtual Machine.
     *
     * Extracts the VM ID from the URL path.
     * Deletes the corresponding VM if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleDeleteVM(const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto initial_size = vms.size();
        std::string removedVmName;
        vms.erase(std::remove_if(vms.begin(), vms.end(),
                                     [&](const Models::VM& vm) {
                                         if (vm.id == id) {
                                             removedVmName = vm.name;
                                             return true;
                                         }
                                         return false;
                                     }),
                      vms.end());

        if (vms.size() < initial_size) {
            removeTraceyRequirementLocked("vm", removedVmName);
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM '" + id + "' deleted successfully.";
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "VM '" + id + "' not found.");
        }
    }

    /**
     * @brief Handles retrieving details of a specific Virtual Machine.
     *
     * Extracts the VM ID from the URL path.
     * Finds and returns the VM's details if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleGetVM(const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = std::find_if(vms.begin(), vms.end(),
                               [&](const Models::VM& vm) { return vm.id == id; });

        if (it != vms.end()) {
            nlohmann::json vmData = it->toJsonString();
            const std::string reqKey = "vm:" + toLower(trim(it->name));
            auto reqIt = traceyRequirements.find(reqKey);
            if (reqIt != traceyRequirements.end()) {
                const auto& requirement = reqIt->second;
                auto agentIt = traceyAgents.find(requirement.expectedAgentId);
                const int64_t nowMs = nowEpochMs();
                const int64_t ageMs = (requirement.createdEpochMs > 0 && nowMs > requirement.createdEpochMs)
                                      ? (nowMs - requirement.createdEpochMs)
                                      : 0;
                const bool withinGrace = ageMs <= (traceyRequirementGraceSeconds * 1000);
                std::string complianceState = "pending";
                std::string reason = "Waiting for Tracey agent heartbeat/discovery.";
                if (agentIt != traceyAgents.end()) {
                    const auto& agent = agentIt->second;
                    const int64_t lastSignalMs = std::max(agent.lastSeenEpochMs, agent.lastAnnouncementEpochMs);
                    const bool stale = (lastSignalMs <= 0) ||
                                       (nowMs > lastSignalMs && (nowMs - lastSignalMs) > (traceyStaleAfterSeconds * 1000));
                    const bool reporting = agent.statusReachable ||
                                           agent.source.find("heartbeat") != std::string::npos ||
                                           agent.source.find("discovery") != std::string::npos;
                    if (!stale && reporting) {
                        complianceState = "compliant";
                        reason = "Tracey agent is actively reporting.";
                    } else if (!withinGrace) {
                        complianceState = "noncompliant";
                        reason = "Tracey agent is stale or not reporting after grace period.";
                    }
                } else if (!withinGrace) {
                    complianceState = "noncompliant";
                    reason = "Required Tracey agent not discovered after grace period.";
                }
                vmData["tracey"] = {
                        {"required", true},
                        {"agent_id", requirement.expectedAgentId},
                        {"status_addr", requirement.expectedStatusAddr},
                        {"compliance_state", complianceState},
                        {"reason", reason}
                };
            } else {
                vmData["tracey"] = {
                        {"required", traceyEnforceManagedResources},
                        {"compliance_state", "untracked"}
                };
            }

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM details retrieved.";
            apiResponse.data = vmData;
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "VM '" + id + "' not found.");
        }
    }

    /**
     * @brief Handles listing all Virtual Machines, with optional name filtering.
     *
     * Supports a 'filter-name' query parameter to filter VMs by name.
     * Returns a JSON array of VM objects.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListVMs(const httplib::Request& req, httplib::Response& res) {
        std::string filterName = req.get_param_value("filter-name");
        const int64_t nowMs = nowEpochMs();

        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json vmList = nlohmann::json::array();
        for (const auto& vm : vms) {
            if (filterName.empty() || vm.name.find(filterName) != std::string::npos) {
                nlohmann::json vmJson = vm.toJsonString();
                const std::string reqKey = "vm:" + toLower(trim(vm.name));
                auto reqIt = traceyRequirements.find(reqKey);
                if (reqIt != traceyRequirements.end()) {
                    const auto& requirement = reqIt->second;
                    auto agentIt = traceyAgents.find(requirement.expectedAgentId);
                    const int64_t ageMs = (requirement.createdEpochMs > 0 && nowMs > requirement.createdEpochMs)
                                          ? (nowMs - requirement.createdEpochMs)
                                          : 0;
                    const bool withinGrace = ageMs <= (traceyRequirementGraceSeconds * 1000);
                    std::string complianceState = "pending";
                    if (agentIt != traceyAgents.end()) {
                        const auto& agent = agentIt->second;
                        const int64_t lastSignalMs = std::max(agent.lastSeenEpochMs, agent.lastAnnouncementEpochMs);
                        const bool stale = (lastSignalMs <= 0) ||
                                           (nowMs > lastSignalMs && (nowMs - lastSignalMs) > (traceyStaleAfterSeconds * 1000));
                        const bool reporting = agent.statusReachable ||
                                               agent.source.find("heartbeat") != std::string::npos ||
                                               agent.source.find("discovery") != std::string::npos;
                        if (!stale && reporting) {
                            complianceState = "compliant";
                        } else if (!withinGrace) {
                            complianceState = "noncompliant";
                        }
                    } else if (!withinGrace) {
                        complianceState = "noncompliant";
                    }
                    vmJson["tracey"] = {
                            {"required", true},
                            {"agent_id", requirement.expectedAgentId},
                            {"status_addr", requirement.expectedStatusAddr},
                            {"compliance_state", complianceState}
                    };
                } else {
                    vmJson["tracey"] = {
                            {"required", traceyEnforceManagedResources},
                            {"compliance_state", "untracked"}
                    };
                }
                vmList.push_back(vmJson);
            }
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "VMs listed successfully.";
        apiResponse.data = vmList;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles listing available VM locations (regions).
     *
     * Returns a predefined list of VM locations.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListVMLocations(const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json locations = {
                {"name", "gb-mids"},
                {"name", "gb-mids"},
                {"name", "us-central"}
        };

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "VM locations listed successfully.";
        apiResponse.data = locations;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles listing available VM OS images.
     *
     * Returns a predefined list of VM OS images (SKUs).
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListVMOSImages(const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json osImages = {
                {"sku", "ubuntu-22.04"},
                {"sku", "windows-server-2022"},
                {"sku", "debian-11"}
        };

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "VM OS images listed successfully.";
        apiResponse.data = osImages;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles listing available VM SKUs (machine types).
     *
     * Returns a predefined list of VM SKUs.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListVMSKUs(const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json skus = {
                {"sku", "nmx-h100-80"},
                {"sku", "standard-a2"},
                {"sku", "premium-gpu-p100"}
        };

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "VM SKUs listed successfully.";
        apiResponse.data = skus;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles restarting a Virtual Machine.
     *
     * Extracts the VM ID from the URL path.
     * Changes the status of the VM to "Restarting" if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleRestartVM(const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = std::find_if(vms.begin(), vms.end(),
                               [&](const Models::VM& vm) { return vm.id == id; });

        if (it != vms.end()) {
            it->status = "Restarting"; // Change status
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM '" + id + "' is restarting.";
            apiResponse.data = it->toJsonString();
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "VM '" + id + "' not found.");
        }
    }

    /**
     * @brief Handles resuming a suspended Virtual Machine.
     *
     * Extracts the VM ID from the URL path.
     * Changes the status of the VM to "Running" if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleResumeVM(const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = std::find_if(vms.begin(), vms.end(),
                               [&](const Models::VM& vm) { return vm.id == id; });

        if (it != vms.end()) {
            it->status = "Running"; // Change status
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM '" + id + "' resumed successfully.";
            apiResponse.data = it->toJsonString();
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "VM '" + id + "' not found.");
        }
    }

    /**
     * @brief Handles suspending a running Virtual Machine.
     *
     * Extracts the VM ID from the URL path.
     * Changes the status of the VM to "Suspended" if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleSuspendVM(const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = std::find_if(vms.begin(), vms.end(),
                               [&](const Models::VM& vm) { return vm.id == id; });

        if (it != vms.end()) {
            it->status = "Suspended"; // Change status
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM '" + id + "' suspended successfully.";
            apiResponse.data = it->toJsonString();
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "VM '" + id + "' not found.");
        }
    }

// --- OpenShift Handlers ---
    void APIRoutes::handleOpenShiftResources(const httplib::Request& req, httplib::Response& res) {
        Models::CloudResponse apiResponse = openShiftClient->getResources();
        sendJsonResponse(res, apiResponse);
        res.status = apiResponse.statusCode;
    }

    void APIRoutes::handleOpenShiftClusters(const httplib::Request& req, httplib::Response& res) {
        Models::CloudResponse apiResponse = openShiftClient->listClusters();
        if (apiResponse.success) {
            if (apiResponse.data.is_array()) {
                for (auto& cluster : apiResponse.data) {
                    normalizeClusterStatus(cluster);
                }
            } else if (apiResponse.data.is_object()) {
                if (apiResponse.data.contains("data") && apiResponse.data["data"].is_array()) {
                    for (auto& cluster : apiResponse.data["data"]) {
                        normalizeClusterStatus(cluster);
                    }
                }
                if (apiResponse.data.contains("clusters") && apiResponse.data["clusters"].is_array()) {
                    for (auto& cluster : apiResponse.data["clusters"]) {
                        normalizeClusterStatus(cluster);
                    }
                }
            }
        }
        sendJsonResponse(res, apiResponse);
        res.status = apiResponse.statusCode;
    }

    void APIRoutes::handleOpenShiftRequestCluster(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string traceyAgentId;
            std::string traceyStatusAddr;
            std::string traceyReason;

            // Validate minimum required fields before forwarding to oshift.
            const std::string name = json_body.value("name", "");
            const std::string organization = json_body.value("organization", "");
            const int gpuCount = json_body.value("gpu_count", 0);
            const std::string architecture = json_body.value("architecture", "");
            const std::string region = json_body.value("region", "");
            const std::string provider = json_body.value("provider", "on-prem");

            if (name.empty() || organization.empty() || architecture.empty() || region.empty() || gpuCount <= 0) {
                return sendErrorResponse(res, 400, "Missing required parameters: name, organization, gpu_count, architecture, or region.");
            }
            if (provider == "hybrid-burst" && (!json_body.contains("burst_targets") || json_body["burst_targets"].empty())) {
                return sendErrorResponse(res, 400, "burst_targets is required when provider is hybrid-burst.");
            }
            if (!extractTraceyProvisioningRequest(json_body, traceyAgentId, traceyStatusAddr, traceyReason)) {
                return sendErrorResponse(res, 400, traceyReason);
            }

            Models::CloudResponse apiResponse = openShiftClient->requestCluster(json_body);
            if (apiResponse.success && apiResponse.data.is_object()) {
                if (apiResponse.data.contains("cluster")) {
                    normalizeClusterStatus(apiResponse.data["cluster"]);
                }
            }
            if (apiResponse.success && !traceyAgentId.empty()) {
                std::lock_guard<std::mutex> lock(dataMutex);
                upsertTraceyRequirementLocked("openshift_cluster", name, region, traceyAgentId, traceyStatusAddr, nowEpochMs());
                if (!apiResponse.data.is_object()) {
                    apiResponse.data = nlohmann::json::object();
                }
                apiResponse.data["tracey"] = {
                        {"required", true},
                        {"agent_id", traceyAgentId},
                        {"status_addr", traceyStatusAddr},
                        {"compliance_state", "pending"},
                        {"reason", "Waiting for Tracey agent heartbeat/discovery after provisioning."}
                };
            }
            sendJsonResponse(res, apiResponse);
            res.status = apiResponse.statusCode;
        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

// --- Tracey Agent Handlers ---
    void APIRoutes::runTraceyDiscoveryLoop() {
        constexpr size_t maxDatagramBytes = 8192;
        std::vector<char> buffer(maxDatagramBytes);
        int sockFd = -1;
        int64_t nextBindAttemptMs = 0;
        int64_t lastMaintenanceMs = 0;

        while (!stopTraceyDiscovery.load()) {
            const int64_t nowMs = nowEpochMs();

            if (sockFd < 0 && nowMs >= nextBindAttemptMs) {
                sockFd = ::socket(AF_INET, SOCK_DGRAM, 0);
                if (sockFd < 0) {
                    nextBindAttemptMs = nowMs + 3000;
                    std::cerr << "[WARN] Tracey discovery socket() failed: " << std::strerror(errno) << std::endl;
                } else {
                    int enable = 1;
                    (void)::setsockopt(sockFd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
#ifdef SO_REUSEPORT
                    (void)::setsockopt(sockFd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
#endif
                    sockaddr_in bindAddr{};
                    bindAddr.sin_family = AF_INET;
                    bindAddr.sin_port = htons(static_cast<uint16_t>(traceyDiscoveryPort));
                    if (traceyDiscoveryBindAddr == "*" || traceyDiscoveryBindAddr.empty() || traceyDiscoveryBindAddr == "0.0.0.0") {
                        bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
                    } else if (::inet_pton(AF_INET, traceyDiscoveryBindAddr.c_str(), &bindAddr.sin_addr) != 1) {
                        std::cerr << "[WARN] Invalid NMC_TRACEY_DISCOVERY_BIND_ADDR '" << traceyDiscoveryBindAddr
                                  << "': expected IPv4 address" << std::endl;
                        ::close(sockFd);
                        sockFd = -1;
                        nextBindAttemptMs = nowMs + 10000;
                    }

                    if (sockFd >= 0 && ::bind(sockFd, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) != 0) {
                        std::cerr << "[WARN] Tracey discovery bind(" << traceyDiscoveryBindAddr << ":" << traceyDiscoveryPort
                                  << ") failed: " << std::strerror(errno) << std::endl;
                        ::close(sockFd);
                        sockFd = -1;
                        nextBindAttemptMs = nowMs + 5000;
                    }
                }
            }

            if (sockFd >= 0) {
                pollfd pfd{};
                pfd.fd = sockFd;
                pfd.events = POLLIN;
                const int pollRc = ::poll(&pfd, 1, 200);
                if (pollRc > 0 && (pfd.revents & POLLIN)) {
                    sockaddr_storage sender{};
                    socklen_t senderLen = sizeof(sender);
                    const ssize_t bytes = ::recvfrom(sockFd, buffer.data(), buffer.size(), 0,
                                                     reinterpret_cast<sockaddr*>(&sender), &senderLen);
                    if (bytes > 0) {
                        std::string senderAddress = "unknown";
                        char addrBuf[INET6_ADDRSTRLEN] = {0};
                        if (sender.ss_family == AF_INET) {
                            const auto* sin = reinterpret_cast<sockaddr_in*>(&sender);
                            if (::inet_ntop(AF_INET, &sin->sin_addr, addrBuf, sizeof(addrBuf)) != nullptr) {
                                senderAddress = addrBuf;
                            }
                        } else if (sender.ss_family == AF_INET6) {
                            const auto* sin6 = reinterpret_cast<sockaddr_in6*>(&sender);
                            if (::inet_ntop(AF_INET6, &sin6->sin6_addr, addrBuf, sizeof(addrBuf)) != nullptr) {
                                senderAddress = addrBuf;
                            }
                        }

                        auto payload = nlohmann::json::parse(buffer.data(), buffer.data() + bytes, nullptr, false);
                        if (payload.is_object()) {
                            ingestTraceyDiscoveryAnnouncement(payload, senderAddress, nowEpochMs());
                        }
                    } else if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                        std::cerr << "[WARN] Tracey discovery recvfrom() failed: " << std::strerror(errno) << std::endl;
                        ::close(sockFd);
                        sockFd = -1;
                        nextBindAttemptMs = nowEpochMs() + 3000;
                    }
                } else if (pollRc < 0 && errno != EINTR) {
                    std::cerr << "[WARN] Tracey discovery poll() failed: " << std::strerror(errno) << std::endl;
                    ::close(sockFd);
                    sockFd = -1;
                    nextBindAttemptMs = nowEpochMs() + 3000;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }

            const int64_t maintenanceNowMs = nowEpochMs();
            if (maintenanceNowMs - lastMaintenanceMs >= 1000) {
                markTraceyStaleAgents(maintenanceNowMs);

                std::vector<std::pair<std::string, std::string>> dueStatusPolls;
                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    dueStatusPolls.reserve(traceyAgents.size());
                    for (auto& [id, agent] : traceyAgents) {
                        if (agent.statusAddr.empty()) {
                            continue;
                        }
                        if (agent.nextQueryEpochMs <= 0 || maintenanceNowMs >= agent.nextQueryEpochMs) {
                            dueStatusPolls.emplace_back(id, agent.statusAddr);
                            agent.nextQueryEpochMs = maintenanceNowMs + traceyStatusPollMs;
                        }
                    }
                }

                for (const auto& pendingPoll : dueStatusPolls) {
                    pollTraceyStatus(pendingPoll.first, pendingPoll.second, maintenanceNowMs);
                }

                lastMaintenanceMs = maintenanceNowMs;
            }
        }

        if (sockFd >= 0) {
            ::close(sockFd);
        }
    }

    void APIRoutes::ingestTraceyDiscoveryAnnouncement(const nlohmann::json& payload,
                                                      const std::string& senderAddress,
                                                      int64_t receivedAtMs) {
        if (!payload.is_object()) {
            return;
        }

        const std::string agentId = payload.value("agent_id", "");
        if (agentId.empty()) {
            return;
        }

        const int64_t announcedTsMs = payload.value("ts_ms", receivedAtMs);
        if (announcedTsMs > 0 && receivedAtMs >= announcedTsMs && (receivedAtMs - announcedTsMs) > traceyDiscoveryMaxAgeMs) {
            return;
        }

        const bool signaturePresent =
                payload.contains("signature") && payload["signature"].is_string() && !trim(payload["signature"].get<std::string>()).empty();
        if (traceyRequireSignature && !signaturePresent) {
            return;
        }

        const std::string announceAddr = payload.value("addr", "");
        const std::string rawStatusAddr = payload.value("status_addr", "");
        TraceyEndpoint endpoint;
        const bool hasEndpoint = !rawStatusAddr.empty() && parseTraceyEndpoint(rawStatusAddr, endpoint);
        const bool endpointAllowed = hasEndpoint && isLocalOrPrivateHost(endpoint.host, traceyAllowPublicAddr);

        std::lock_guard<std::mutex> lock(dataMutex);
        auto& agent = traceyAgents[agentId];
        agent.agentId = agentId;
        agent.source = mergeTraceySource(agent.source, "discovery");
        agent.signaturePresent = signaturePresent;
        agent.lastAnnouncementEpochMs = receivedAtMs;
        agent.lastSeenEpochMs = std::max(agent.lastSeenEpochMs, receivedAtMs);
        agent.stale = false;
        if (agent.status.empty()) {
            agent.status = "unknown";
        }

        if (!announceAddr.empty()) {
            agent.announceAddr = announceAddr;
        } else if (agent.announceAddr.empty()) {
            agent.announceAddr = senderAddress;
        }

        if (payload.contains("capabilities")) {
            agent.capabilities = payload["capabilities"];
        } else if (agent.capabilities.is_null()) {
            agent.capabilities = nlohmann::json::object();
        }

        if (payload.contains("is_coordinator") && payload["is_coordinator"].is_boolean()) {
            agent.coordinator = payload["is_coordinator"].get<bool>();
        }
        if (payload.contains("coordinator_epoch") && payload["coordinator_epoch"].is_number_integer()) {
            agent.coordinatorEpoch = payload["coordinator_epoch"].get<int64_t>();
        }
        if (payload.contains("score") && payload["score"].is_number_integer()) {
            agent.score = payload["score"].get<int64_t>();
        }

        if (hasEndpoint && endpointAllowed) {
            agent.statusAddr = endpoint.normalized;
            agent.nextQueryEpochMs = receivedAtMs;
            if (agent.linkState.empty() || agent.linkState == "offline") {
                agent.linkState = "discovered";
            }
            agent.linkSecurity = deriveLinkSecurity(signaturePresent, &endpoint, traceyTlsVerify);
            if (agent.lastError == "Status endpoint is invalid or disallowed by policy.") {
                agent.lastError.clear();
            }
        } else if (!rawStatusAddr.empty()) {
            agent.lastError = "Status endpoint is invalid or disallowed by policy.";
            agent.linkState = "discovered";
            agent.linkSecurity = deriveLinkSecurity(signaturePresent, nullptr, traceyTlsVerify);
            if (agent.statusAddr.empty()) {
                agent.statusAddr = rawStatusAddr;
            }
        } else {
            agent.linkState = "announcement-only";
            agent.linkSecurity = deriveLinkSecurity(signaturePresent, nullptr, traceyTlsVerify);
        }
    }

    void APIRoutes::pollTraceyStatus(const std::string& agentId, const std::string& statusAddr, int64_t nowMs) {
        auto markFailure = [&](const std::string& message, const TraceyEndpoint* endpoint = nullptr) {
            std::lock_guard<std::mutex> lock(dataMutex);
            auto& agent = traceyAgents[agentId];
            if (agent.agentId.empty()) {
                agent.agentId = agentId;
            }
            if (!statusAddr.empty()) {
                agent.statusAddr = statusAddr;
            }
            agent.statusReachable = false;
            agent.queryFailures += 1;
            agent.lastQueryEpochMs = nowMs;
            agent.nextQueryEpochMs = nowMs + computeBackoffMs(agent.queryFailures, traceyStatusPollMs, traceyStatusMaxBackoffMs);
            agent.linkState = agent.queryFailures >= 3 ? "offline" : "degraded";
            agent.status = agent.queryFailures >= 3 ? "offline" : "degraded";
            agent.lastError = message;
            agent.linkSecurity = deriveLinkSecurity(agent.signaturePresent, endpoint, traceyTlsVerify);
        };

        TraceyEndpoint endpoint;
        if (!parseTraceyEndpoint(statusAddr, endpoint)) {
            markFailure("Invalid status endpoint.");
            return;
        }
        if (!isLocalOrPrivateHost(endpoint.host, traceyAllowPublicAddr)) {
            markFailure("Status endpoint is outside allowed network ranges.", &endpoint);
            return;
        }

        const std::string statusPath = buildTraceyStatusPath(endpoint);
        const int timeoutSec = static_cast<int>(std::max<int64_t>(1, (traceyStatusTimeoutMs + 999) / 1000));
        httplib::Headers headers{{"Accept", "application/json"}};
        if (!traceyStatusBearerToken.empty()) {
            headers.emplace("Authorization", "Bearer " + traceyStatusBearerToken);
        }

        httplib::Result result;
        if (endpoint.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient client(endpoint.host, endpoint.port);
            client.enable_server_certificate_verification(traceyTlsVerify);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Get(statusPath.c_str(), headers);
#else
            markFailure("HTTPS status polling is unavailable: httplib lacks OpenSSL support.", &endpoint);
            return;
#endif
        } else {
            httplib::Client client(endpoint.host, endpoint.port);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Get(statusPath.c_str(), headers);
        }

        if (!result) {
            markFailure("Status poll failed: " + httplib::to_string(result.error()), &endpoint);
            return;
        }
        if (result->status < 200 || result->status >= 300) {
            markFailure("Status endpoint returned HTTP " + std::to_string(result->status) + ".", &endpoint);
            return;
        }

        auto statusPayload = nlohmann::json::parse(result->body, nullptr, false);
        if (statusPayload.is_discarded()) {
            markFailure("Status endpoint returned non-JSON payload.", &endpoint);
            return;
        }

        std::string resolvedStatus = "healthy";
        if (statusPayload.is_object()) {
            if (statusPayload.contains("status") && statusPayload["status"].is_string()) {
                resolvedStatus = normalizeTraceyStatus(statusPayload["status"].get<std::string>());
            } else if (statusPayload.contains("posture") && statusPayload["posture"].is_string()) {
                const std::string posture = toLower(statusPayload["posture"].get<std::string>());
                if (posture.find("incident") != std::string::npos || posture.find("critical") != std::string::npos) {
                    resolvedStatus = "offline";
                } else if (posture.find("elevated") != std::string::npos || posture.find("warning") != std::string::npos) {
                    resolvedStatus = "degraded";
                } else {
                    resolvedStatus = "healthy";
                }
            }
        }

        std::lock_guard<std::mutex> lock(dataMutex);
        auto& agent = traceyAgents[agentId];
        agent.agentId = agentId;
        agent.status = normalizeTraceyStatus(resolvedStatus);
        agent.statusReachable = true;
        agent.queryFailures = 0;
        agent.lastQueryEpochMs = nowMs;
        agent.nextQueryEpochMs = nowMs + traceyStatusPollMs;
        agent.lastSeenEpochMs = std::max(agent.lastSeenEpochMs, nowMs);
        agent.stale = false;
        agent.linkState = agent.status == "healthy" ? "healthy" : "degraded";
        agent.statusAddr = endpoint.normalized;
        agent.linkSecurity = deriveLinkSecurity(agent.signaturePresent, &endpoint, traceyTlsVerify);
        agent.lastError.clear();

        if (agent.metrics.is_null() || !agent.metrics.is_object()) {
            agent.metrics = nlohmann::json::object();
        }
        agent.metrics["status_snapshot"] = statusPayload;

        if (statusPayload.is_object()) {
            if (statusPayload.contains("is_coordinator") && statusPayload["is_coordinator"].is_boolean()) {
                agent.coordinator = statusPayload["is_coordinator"].get<bool>();
            }
        }
    }

    void APIRoutes::markTraceyStaleAgents(int64_t nowMs) {
        std::lock_guard<std::mutex> lock(dataMutex);
        const int64_t staleAfterMs = std::max<int64_t>(5, traceyStaleAfterSeconds) * 1000;
        for (auto& [id, agent] : traceyAgents) {
            (void)id;
            const int64_t lastSignalMs = std::max(agent.lastSeenEpochMs, agent.lastAnnouncementEpochMs);
            if (lastSignalMs <= 0) {
                continue;
            }
            const bool staleNow = nowMs > lastSignalMs && (nowMs - lastSignalMs) > staleAfterMs;
            agent.stale = staleNow;
            if (staleNow) {
                agent.status = "offline";
                agent.linkState = "offline";
                if (agent.lastError.empty()) {
                    agent.lastError = "Agent is stale (missed heartbeat/discovery window).";
                }
            }
        }
    }

    void APIRoutes::handleTraceyHeartbeat(const httplib::Request& req, httplib::Response& res) {
        try {
            const auto body = nlohmann::json::parse(req.body);
            std::string agentId = body.value("agent_id", "");
            if (agentId.empty()) {
                agentId = body.value("id", "");
            }
            if (agentId.empty()) {
                return sendErrorResponse(res, 400, "Missing required parameter: agent_id.");
            }

            const int64_t nowMs = nowEpochMs();
            const std::string rawStatusAddr = body.value("status_addr", "");
            TraceyEndpoint endpoint;
            const bool hasEndpoint = !rawStatusAddr.empty() && parseTraceyEndpoint(rawStatusAddr, endpoint);
            const bool endpointAllowed = hasEndpoint && isLocalOrPrivateHost(endpoint.host, traceyAllowPublicAddr);

            std::lock_guard<std::mutex> lock(dataMutex);
            auto& agent = traceyAgents[agentId];
            agent.agentId = agentId;
            agent.source = mergeTraceySource(agent.source, "heartbeat");
            const std::string cluster = body.value("cluster", body.value("cluster_id", ""));
            if (!cluster.empty()) {
                agent.cluster = cluster;
            }
            agent.status = normalizeTraceyStatus(body.value("status", agent.status.empty() ? "healthy" : agent.status));
            agent.version = body.value("version", agent.version);
            agent.host = body.value("host", body.value("hostname", agent.host));
            if (body.contains("metrics")) {
                agent.metrics = body["metrics"];
            } else if (agent.metrics.is_null()) {
                agent.metrics = nlohmann::json::object();
            }
            if (body.contains("capabilities")) {
                agent.capabilities = body["capabilities"];
            } else if (agent.capabilities.is_null()) {
                agent.capabilities = nlohmann::json::object();
            }
            if (body.contains("is_coordinator") && body["is_coordinator"].is_boolean()) {
                agent.coordinator = body["is_coordinator"].get<bool>();
            }
            if (body.contains("coordinator_epoch") && body["coordinator_epoch"].is_number_integer()) {
                agent.coordinatorEpoch = body["coordinator_epoch"].get<int64_t>();
            }
            if (body.contains("score") && body["score"].is_number_integer()) {
                agent.score = body["score"].get<int64_t>();
            }

            agent.lastSeenEpochMs = nowMs;
            agent.stale = false;
            agent.statusReachable = true;
            agent.lastError.clear();

            if (hasEndpoint && endpointAllowed) {
                agent.statusAddr = endpoint.normalized;
                agent.nextQueryEpochMs = nowMs;
                agent.linkState = "healthy";
                agent.linkSecurity = deriveLinkSecurity(agent.signaturePresent, &endpoint, traceyTlsVerify);
            } else if (!rawStatusAddr.empty()) {
                agent.lastError = "Heartbeat provided invalid or disallowed status_addr.";
                agent.linkState = "degraded";
                agent.linkSecurity = deriveLinkSecurity(agent.signaturePresent, nullptr, traceyTlsVerify);
            } else if (agent.linkState.empty()) {
                agent.linkState = "heartbeat-only";
                agent.linkSecurity = deriveLinkSecurity(agent.signaturePresent, nullptr, traceyTlsVerify);
            }

            nlohmann::json payload = {
                    {"agent_id", agent.agentId},
                    {"cluster", agent.cluster},
                    {"status", agent.status},
                    {"version", agent.version},
                    {"host", agent.host},
                    {"source", agent.source},
                    {"status_addr", agent.statusAddr},
                    {"link_state", agent.linkState},
                    {"link_security", agent.linkSecurity},
                    {"query_failures", agent.queryFailures},
                    {"metrics", agent.metrics.is_null() ? nlohmann::json::object() : agent.metrics},
                    {"last_seen_epoch_ms", agent.lastSeenEpochMs},
                    {"last_seen_seconds_ago", 0},
                    {"stale", false}
            };

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Tracey heartbeat accepted.";
            apiResponse.data = payload;
            sendJsonResponse(res, apiResponse);
        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

    void APIRoutes::handleListTraceyAgents(const httplib::Request& req, httplib::Response& res) {
        (void)req;
        const int64_t nowMs = nowEpochMs();
        markTraceyStaleAgents(nowMs);

        std::vector<TraceyAgent> snapshot;
        std::vector<TraceyRequirement> requirementSnapshot;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            snapshot.reserve(traceyAgents.size());
            for (const auto& pair : traceyAgents) {
                snapshot.push_back(pair.second);
            }
            requirementSnapshot.reserve(traceyRequirements.size());
            for (const auto& pair : traceyRequirements) {
                requirementSnapshot.push_back(pair.second);
            }
        }

        std::sort(snapshot.begin(), snapshot.end(), [](const TraceyAgent& a, const TraceyAgent& b) {
            return a.agentId < b.agentId;
        });
        std::sort(requirementSnapshot.begin(), requirementSnapshot.end(), [](const TraceyRequirement& a, const TraceyRequirement& b) {
            if (a.resourceType == b.resourceType) {
                return a.resourceName < b.resourceName;
            }
            return a.resourceType < b.resourceType;
        });

        int healthy = 0;
        int degraded = 0;
        int offline = 0;
        int stale = 0;
        int unknown = 0;
        int discovered = 0;
        int heartbeatOnly = 0;
        int reachable = 0;
        int secureLinks = 0;
        int signedAnnouncements = 0;
        nlohmann::json agents = nlohmann::json::array();

        for (const auto& agent : snapshot) {
            const int64_t lastSignalMs = std::max(agent.lastSeenEpochMs, agent.lastAnnouncementEpochMs);
            int64_t ageSeconds = 0;
            if (lastSignalMs > 0 && nowMs > lastSignalMs) {
                ageSeconds = (nowMs - lastSignalMs) / 1000;
            }
            int64_t lastAnnouncementSecondsAgo = 0;
            if (agent.lastAnnouncementEpochMs > 0 && nowMs > agent.lastAnnouncementEpochMs) {
                lastAnnouncementSecondsAgo = (nowMs - agent.lastAnnouncementEpochMs) / 1000;
            }
            int64_t lastQuerySecondsAgo = 0;
            if (agent.lastQueryEpochMs > 0 && nowMs > agent.lastQueryEpochMs) {
                lastQuerySecondsAgo = (nowMs - agent.lastQueryEpochMs) / 1000;
            }

            const bool isStale = agent.stale || ageSeconds > traceyStaleAfterSeconds;
            const std::string status = isStale ? "offline" : normalizeTraceyStatus(agent.status);

            if (isStale) {
                stale++;
            }
            if (status == "healthy") {
                healthy++;
            } else if (status == "degraded") {
                degraded++;
            } else if (status == "offline") {
                offline++;
            } else {
                unknown++;
            }

            const bool hasDiscoverySource = agent.source.find("discovery") != std::string::npos;
            const bool hasHeartbeatSource = agent.source.find("heartbeat") != std::string::npos;
            if (hasDiscoverySource) {
                discovered++;
            }
            if (hasHeartbeatSource && !hasDiscoverySource) {
                heartbeatOnly++;
            }
            if (agent.statusReachable) {
                reachable++;
            }
            if (agent.linkSecurity.find("tls-verified") != std::string::npos) {
                secureLinks++;
            }
            if (agent.signaturePresent) {
                signedAnnouncements++;
            }

            agents.push_back({
                    {"agent_id", agent.agentId},
                    {"cluster", agent.cluster},
                    {"status", status},
                    {"version", agent.version},
                    {"host", agent.host},
                    {"source", agent.source},
                    {"announce_addr", agent.announceAddr},
                    {"status_addr", agent.statusAddr},
                    {"link_state", agent.linkState},
                    {"link_security", agent.linkSecurity},
                    {"last_error", agent.lastError},
                    {"metrics", agent.metrics.is_null() ? nlohmann::json::object() : agent.metrics},
                    {"capabilities", agent.capabilities.is_null() ? nlohmann::json::object() : agent.capabilities},
                    {"last_seen_epoch_ms", agent.lastSeenEpochMs},
                    {"last_seen_seconds_ago", ageSeconds},
                    {"last_announcement_epoch_ms", agent.lastAnnouncementEpochMs},
                    {"last_announcement_seconds_ago", lastAnnouncementSecondsAgo},
                    {"last_query_epoch_ms", agent.lastQueryEpochMs},
                    {"last_query_seconds_ago", lastQuerySecondsAgo},
                    {"next_query_epoch_ms", agent.nextQueryEpochMs},
                    {"query_failures", agent.queryFailures},
                    {"status_reachable", agent.statusReachable},
                    {"signature_present", agent.signaturePresent},
                    {"is_coordinator", agent.coordinator},
                    {"coordinator_epoch", agent.coordinatorEpoch},
                    {"score", agent.score},
                    {"stale", isStale}
            });
        }

        int managedCompliant = 0;
        int managedPending = 0;
        int managedNonCompliant = 0;
        nlohmann::json requirements = nlohmann::json::array();
        const int64_t graceMs = std::max<int64_t>(5, traceyRequirementGraceSeconds) * 1000;

        for (const auto& requirement : requirementSnapshot) {
            const TraceyAgent* matchedAgent = nullptr;
            if (!requirement.expectedAgentId.empty()) {
                auto agentIt = std::find_if(snapshot.begin(), snapshot.end(), [&](const TraceyAgent& agent) {
                    return agent.agentId == requirement.expectedAgentId;
                });
                if (agentIt != snapshot.end()) {
                    matchedAgent = &(*agentIt);
                }
            }

            const int64_t resourceAgeMs = (requirement.createdEpochMs > 0 && nowMs > requirement.createdEpochMs)
                                          ? (nowMs - requirement.createdEpochMs)
                                          : 0;
            const bool withinGrace = resourceAgeMs <= graceMs;
            std::string complianceState = "pending";
            std::string reason = "Waiting for required Tracey agent signal.";
            int64_t agentLastSeenSecondsAgo = -1;
            std::string agentStatus = "missing";

            if (matchedAgent != nullptr) {
                const int64_t lastSignalMs = std::max(matchedAgent->lastSeenEpochMs, matchedAgent->lastAnnouncementEpochMs);
                const bool stale = (lastSignalMs <= 0) ||
                                   (nowMs > lastSignalMs && (nowMs - lastSignalMs) > (traceyStaleAfterSeconds * 1000));
                if (lastSignalMs > 0 && nowMs >= lastSignalMs) {
                    agentLastSeenSecondsAgo = (nowMs - lastSignalMs) / 1000;
                }
                agentStatus = matchedAgent->status.empty() ? "unknown" : matchedAgent->status;
                const bool hasSignal = matchedAgent->statusReachable ||
                                       matchedAgent->source.find("heartbeat") != std::string::npos ||
                                       matchedAgent->source.find("discovery") != std::string::npos;
                if (!stale && hasSignal) {
                    complianceState = "compliant";
                    reason = "Tracey agent is reporting for this managed resource.";
                } else if (withinGrace) {
                    complianceState = "pending";
                    reason = stale
                             ? "Tracey agent discovered but currently stale; grace window still active."
                             : "Tracey agent registered but has not reported yet; grace window still active.";
                } else {
                    complianceState = "noncompliant";
                    reason = stale
                             ? "Tracey agent is stale or offline after grace window."
                             : "Tracey agent is not reporting after grace window.";
                }
            } else if (!withinGrace) {
                complianceState = "noncompliant";
                reason = "Required Tracey agent has not been discovered after grace window.";
            }

            if (complianceState == "compliant") {
                managedCompliant++;
            } else if (complianceState == "noncompliant") {
                managedNonCompliant++;
            } else {
                managedPending++;
            }

            requirements.push_back({
                    {"key", requirement.key},
                    {"resource_type", requirement.resourceType},
                    {"resource_name", requirement.resourceName},
                    {"region", requirement.region},
                    {"created_epoch_ms", requirement.createdEpochMs},
                    {"resource_age_seconds", resourceAgeMs / 1000},
                    {"expected_agent_id", requirement.expectedAgentId},
                    {"expected_status_addr", requirement.expectedStatusAddr},
                    {"agent_status", agentStatus},
                    {"agent_last_seen_seconds_ago", agentLastSeenSecondsAgo},
                    {"compliance_state", complianceState},
                    {"reason", reason}
            });
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Tracey agents listed successfully.";
        apiResponse.data = {
                {"summary", {
                        {"total", static_cast<int>(snapshot.size())},
                        {"healthy", healthy},
                        {"degraded", degraded},
                        {"offline", offline},
                        {"stale", stale},
                        {"unknown", unknown},
                        {"discovered", discovered},
                        {"heartbeat_only", heartbeatOnly},
                        {"reachable", reachable},
                        {"secure_links", secureLinks},
                        {"signed_announcements", signedAnnouncements}
                }},
                {"tracey_policy", {
                        {"enforce_managed_resources", traceyEnforceManagedResources},
                        {"requirement_grace_seconds", traceyRequirementGraceSeconds}
                }},
                {"requirement_summary", {
                        {"total", static_cast<int>(requirementSnapshot.size())},
                        {"compliant", managedCompliant},
                        {"pending", managedPending},
                        {"noncompliant", managedNonCompliant}
                }},
                {"requirements", requirements},
                {"discovery", {
                        {"enabled", traceyDiscoveryEnabled},
                        {"bind_addr", traceyDiscoveryBindAddr},
                        {"port", traceyDiscoveryPort},
                        {"max_age_ms", traceyDiscoveryMaxAgeMs},
                        {"status_poll_ms", traceyStatusPollMs},
                        {"status_timeout_ms", traceyStatusTimeoutMs},
                        {"status_max_backoff_ms", traceyStatusMaxBackoffMs},
                        {"allow_public_addr", traceyAllowPublicAddr},
                        {"tls_verify", traceyTlsVerify},
                        {"require_signature", traceyRequireSignature}
                }},
                {"stale_after_seconds", traceyStaleAfterSeconds},
                {"agents", agents}
        };
        sendJsonResponse(res, apiResponse);
    }

} // namespace NMC::Server
