// server/APIRoutes.cpp
#include "APIRoutes.h"
#include "K8sHandlers.h"
#include "VersionCheck.h"
#include "Utils.h"
#include <cstdlib>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <array>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <algorithm> // For std::remove_if, std::find_if
#include <cmath>
#include <map>
#include <set>       // For unique locations/types
#include <chrono>
#include <thread>
#include <tuple>
#include <vector>
#include <poll.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <unistd.h>


namespace NMC::Server {

    namespace {
#include "APIRoutes_InternalHelpers.inl"
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

        const char* centralSessionEnv = envOr("NMC_CENTRAL_AUTH_SESSION_URL", "NM_CENTRAL_AUTH_SESSION_URL");
        centralAuthSessionUrl = centralSessionEnv ? trim(centralSessionEnv) : "";
        const char* centralLoginEnv = envOr("NMC_CENTRAL_AUTH_LOGIN_URL", "NM_CENTRAL_AUTH_LOGIN_URL");
        centralAuthLoginUrl = centralLoginEnv ? trim(centralLoginEnv) : "";
        centralAuthCacheTtlMs = parseInt64Value(
                envOr("NMC_CENTRAL_AUTH_CACHE_TTL_MS", "NM_CENTRAL_AUTH_CACHE_TTL_MS"),
                15'000,
                1'000,
                300'000
        );
        centralAuthTimeoutMs = parseInt64Value(
                envOr("NMC_CENTRAL_AUTH_TIMEOUT_MS", "NM_CENTRAL_AUTH_TIMEOUT_MS"),
                3'000,
                500,
                120'000
        );
        centralAuthTlsVerify = parseBoolValue(
                envOr("NMC_CENTRAL_AUTH_TLS_VERIFY", "NM_CENTRAL_AUTH_TLS_VERIFY"),
                true
        );

        if (authMode == "off") {
            authRequired = false;
        } else if (authMode == "token") {
            authRequired = !authToken.empty() || !centralAuthSessionUrl.empty();
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
        stopAarnnDiscovery.store(false);
        aarnnDiscoveryEnabled = parseBoolValue(
                envOr("NMC_AARNN_DISCOVERY_ENABLED", "NM_AARNN_DISCOVERY_ENABLED"),
                true
        );
        aarnnAllowPublicAddr = parseBoolValue(
                envOr("NMC_AARNN_ALLOW_PUBLIC_ADDR", "NM_AARNN_ALLOW_PUBLIC_ADDR"),
                false
        );
        const char* aarnnBindAddrEnv = envOr("NMC_AARNN_DISCOVERY_BIND_ADDR", "NM_AARNN_DISCOVERY_BIND_ADDR");
        aarnnDiscoveryBindAddr = aarnnBindAddrEnv ? trim(aarnnBindAddrEnv) : "0.0.0.0";
        if (aarnnDiscoveryBindAddr.empty()) {
            aarnnDiscoveryBindAddr = "0.0.0.0";
        }
        aarnnDiscoveryPort = static_cast<int>(
                parseInt64Value(envOr("NMC_AARNN_DISCOVERY_PORT", "NM_AARNN_DISCOVERY_PORT"), 50050, 1, 65535)
        );
        aarnnStaleAfterSeconds = parseInt64Value(
                envOr("NMC_AARNN_STALE_SECONDS", "NM_AARNN_STALE_SECONDS"),
                15,
                4,
                86400
        );
        aarnnStatusPollMs = parseInt64Value(
                envOr("NMC_AARNN_STATUS_POLL_MS", "NM_AARNN_STATUS_POLL_MS"),
                5000,
                1000,
                600000
        );
        aarnnStatusTimeoutMs = parseInt64Value(
                envOr("NMC_AARNN_STATUS_TIMEOUT_MS", "NM_AARNN_STATUS_TIMEOUT_MS"),
                2500,
                500,
                120000
        );

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
                parseInt64Value(envOr("NMC_TRACEY_DISCOVERY_PORT", "NM_TRACEY_DISCOVERY_PORT"), 47991, 1, 65535)
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
        traceyHistoryMaxSamples = static_cast<size_t>(parseInt64Value(
                envOr("NMC_TRACEY_HISTORY_MAX_SAMPLES", "NM_TRACEY_HISTORY_MAX_SAMPLES"),
                1440,
                60,
                200000
        ));
        traceyAgentLogMaxEntries = static_cast<size_t>(parseInt64Value(
                envOr("NMC_TRACEY_AGENT_LOG_MAX_ENTRIES", "NM_TRACEY_AGENT_LOG_MAX_ENTRIES"),
                400,
                50,
                50000
        ));
        const char* traceyStatusTokenEnv = envOr("NMC_TRACEY_STATUS_BEARER_TOKEN", "NM_TRACEY_STATUS_BEARER_TOKEN");
        traceyStatusBearerToken = traceyStatusTokenEnv ? trim(traceyStatusTokenEnv) : "";

        const char* serverStateRootEnv = envOr("NMC_SERVER_STATE_ROOT", "NM_SERVER_STATE_ROOT");
        if (!serverStateRootEnv) {
            serverStateRootEnv = envOr("NMC_STATE_ROOT", "NM_STATE_ROOT");
        }
        if (!serverStateRootEnv) {
            serverStateRootEnv = envOr("NMC_TRACEY_STATE_ROOT", "NM_TRACEY_STATE_ROOT");
        }
        std::filesystem::path serverStateSnapshotPath;
        if (serverStateRootEnv && !trim(serverStateRootEnv).empty()) {
            serverStateSnapshotPath = std::filesystem::path(trim(serverStateRootEnv)) / "server_state_snapshot.json";
        }

        std::string serverPostgresDsn;
        if (const char* serverDsnEnv = envOr("NMC_SERVER_POSTGRES_DSN", "NM_SERVER_POSTGRES_DSN")) {
            serverPostgresDsn = trim(serverDsnEnv);
        }
        if (serverPostgresDsn.empty()) {
            if (const char* genericPostgresEnv = envOr("NMC_POSTGRES_DSN", "NM_POSTGRES_DSN")) {
                serverPostgresDsn = trim(genericPostgresEnv);
            }
        }
        if (serverPostgresDsn.empty()) {
            if (const char* postgresUrlEnv = envOr("NMC_POSTGRES_URL", "NM_POSTGRES_URL")) {
                serverPostgresDsn = trim(postgresUrlEnv);
            }
        }
        if (serverPostgresDsn.empty()) {
            if (const char* traceyDsnEnv = envOr("NMC_TRACEY_POSTGRES_DSN", "NM_TRACEY_POSTGRES_DSN")) {
                serverPostgresDsn = trim(traceyDsnEnv);
            }
        }
        if (serverPostgresDsn.empty()) {
            const char* databaseUrlEnv = std::getenv("DATABASE_URL");
            serverPostgresDsn = databaseUrlEnv ? trim(databaseUrlEnv) : "";
        }

        const char* serverPersistFlushEnv = envOr("NMC_SERVER_PERSIST_FLUSH_MS", "NM_SERVER_PERSIST_FLUSH_MS");
        if (!serverPersistFlushEnv) {
            serverPersistFlushEnv = envOr("NMC_PERSIST_FLUSH_MS", "NM_PERSIST_FLUSH_MS");
        }
        if (!serverPersistFlushEnv) {
            serverPersistFlushEnv = envOr("NMC_TRACEY_PERSIST_FLUSH_MS", "NM_TRACEY_PERSIST_FLUSH_MS");
        }
        const int64_t serverStateSnapshotFlushMs = parseInt64Value(
                serverPersistFlushEnv,
                5000,
                250,
                60000
        );
        serverStateStore = std::make_unique<ServerStateStore>(ServerStateStore::Config{
                serverStateSnapshotPath,
                serverStateSnapshotFlushMs,
                serverPostgresDsn,
                "primary",
                [this](int64_t snapshotTs) {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    return buildServerStateSnapshotLocked(snapshotTs);
                }
        });
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            const nlohmann::json restoredServerState = serverStateStore->loadSnapshot();
            if (restoredServerState.is_object()) {
                restoreServerStateSnapshotLocked(restoredServerState);
            }
        }

        const char* traceyStateRootEnv = envOr("NMC_TRACEY_STATE_ROOT", "NM_TRACEY_STATE_ROOT");
        std::filesystem::path traceyStateSnapshotPath;
        if (traceyStateRootEnv && !trim(traceyStateRootEnv).empty()) {
            traceyStateSnapshotPath = std::filesystem::path(trim(traceyStateRootEnv)) / "tracey_state_snapshot.json";
        }
        std::string traceyPostgresDsn;
        if (const char* traceyDsnEnv = envOr("NMC_TRACEY_POSTGRES_DSN", "NM_TRACEY_POSTGRES_DSN")) {
            traceyPostgresDsn = trim(traceyDsnEnv);
        }
        if (traceyPostgresDsn.empty()) {
            if (const char* genericPostgresEnv = envOr("NMC_POSTGRES_DSN", "NM_POSTGRES_DSN")) {
                traceyPostgresDsn = trim(genericPostgresEnv);
            }
        }
        if (traceyPostgresDsn.empty()) {
            if (const char* postgresUrlEnv = envOr("NMC_POSTGRES_URL", "NM_POSTGRES_URL")) {
                traceyPostgresDsn = trim(postgresUrlEnv);
            }
        }
        if (traceyPostgresDsn.empty()) {
            const char* databaseUrlEnv = std::getenv("DATABASE_URL");
            traceyPostgresDsn = databaseUrlEnv ? trim(databaseUrlEnv) : "";
        }
        const int64_t traceyStateSnapshotFlushMs = parseInt64Value(
                envOr("NMC_TRACEY_PERSIST_FLUSH_MS", "NM_TRACEY_PERSIST_FLUSH_MS"),
                5000,
                250,
                60000
        );
        traceyStateStore = std::make_unique<TraceyStateStore>(TraceyStateStore::Config{
                traceyStateSnapshotPath,
                traceyStateSnapshotFlushMs,
                traceyPostgresDsn,
                [this](int64_t snapshotTs) {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    return buildTraceyStateSnapshotLocked(snapshotTs);
                }
        });
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            const nlohmann::json restoredTraceyState = traceyStateStore->loadSnapshot();
            if (restoredTraceyState.is_object()) {
                restoreTraceyStateSnapshotLocked(restoredTraceyState);
            }
        }

        const bool traceyBootstrapLocalAgent = parseBoolValue(
                envOr("NMC_TRACEY_BOOTSTRAP_LOCAL_AGENT", "NM_TRACEY_BOOTSTRAP_LOCAL_AGENT"),
                true
        );
        const char* localAgentIdEnv = envOr("NMC_TRACEY_LOCAL_AGENT_ID", "NM_TRACEY_LOCAL_AGENT_ID");
        const std::string localTraceyAgentId = localAgentIdEnv ? trim(localAgentIdEnv) : "tracey-continuum-local";
        traceyLocalAgentId = localTraceyAgentId;
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
            scheduleTraceyStateSnapshotLocked(nowMs);
        }
        if (serverStateStore) {
            serverStateStore->start();
            scheduleServerStateSnapshot(nowEpochMs());
        }
        if (traceyStateStore) {
            traceyStateStore->start();
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
                k8sClusters,
                vclusterConfigs,
                [this](httplib::Response& res, const Models::CloudResponse& apiResponse) {
                    sendJsonResponse(res, apiResponse);
                },
                [this](httplib::Response& res, int status, const std::string& message) {
                    sendErrorResponse(res, status, message);
                },
                [this](int64_t snapshotTs) {
                    scheduleServerStateSnapshot(snapshotTs);
                }
        );

        // OpenShift portal API base URL (FastAPI in oshift). Defaults to localhost:8000.
        const char* osUrlEnv = std::getenv("NMC_OSHIFT_API_URL");
        std::string osUrl = osUrlEnv ? osUrlEnv : "http://127.0.0.1:8000";
        openShiftClient = std::make_unique<OpenShiftClient>(osUrl);
        // OpenStack portal API base URL. Defaults to the OpenShift URL so a single
        // multi-provider backend can satisfy both routes when desired.
        const char* openStackUrlEnv = std::getenv("NMC_OPENSTACK_API_URL");
        std::string openStackUrl = openStackUrlEnv ? openStackUrlEnv : osUrl;
        openStackClient = std::make_unique<OpenStackClient>(openStackUrl);
        // Proxmox portal API base URL. Defaults to the OpenStack URL so the same
        // unified provider backend can satisfy all portal routes when desired.
        const char* proxmoxUrlEnv = std::getenv("NMC_PROXMOX_API_URL");
        std::string proxmoxUrl = proxmoxUrlEnv ? proxmoxUrlEnv : openStackUrl;
        proxmoxClient = std::make_unique<ProxmoxClient>(proxmoxUrl);

        // Enable logging of all requests
        svr.set_logger([this](const httplib::Request& req, const httplib::Response& res) {
            logRequest(req, res);
        });

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

        registerDocsAndAuthRoutes(svr, guard);
        registerControlMetadataRoutes(svr, guard);

        registerDomainCrudRoutes(svr, guard);
        registerReleaseOperateRoutes(svr, guard);
        registerDomainProxyRoutes(svr, guard);

        registerTraceyRoutes(svr, guard);

        if (traceyDiscoveryEnabled) {
            try {
                traceyDiscoveryThread = std::thread(&APIRoutes::runTraceyDiscoveryLoop, this);
            } catch (const std::exception& e) {
                traceyDiscoveryEnabled = false;
                std::cerr << "[WARN] Failed to start Tracey discovery thread: " << e.what() << std::endl;
            }
        }
        if (aarnnDiscoveryEnabled) {
            try {
                aarnnDiscoveryThread = std::thread(&APIRoutes::runAarnnDiscoveryLoop, this);
            } catch (const std::exception& e) {
                aarnnDiscoveryEnabled = false;
                std::cerr << "[WARN] Failed to start AARNN discovery thread: " << e.what() << std::endl;
            }
        }

        traceyCveIntel.start();
    }

    // Explicit definition of the destructor for APIRoutes
    // This is crucial because APIRoutes contains a std::unique_ptr to K8sHandlers,
    // and K8sHandlers is only forward-declared in APIRoutes.h.
    // The default destructor for unique_ptr needs the full definition of the type
    // it manages when it's instantiated, which happens when the APIRoutes destructor
    // is defined.
    APIRoutes::~APIRoutes() {
        stopTraceyDiscovery.store(true);
        stopAarnnDiscovery.store(true);
        traceyCveIntel.stop();
        if (traceyDiscoveryThread.joinable()) {
            traceyDiscoveryThread.join();
        }
        if (aarnnDiscoveryThread.joinable()) {
            aarnnDiscoveryThread.join();
        }
        if (serverStateStore) {
            serverStateStore->stop();
        }
        if (traceyStateStore) {
            traceyStateStore->stop();
        }
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

    nlohmann::json APIRoutes::buildTraceyFleetViewFromAgents(const std::vector<nlohmann::json>& agentViews,
                                                             int64_t nowMs) const {
        return buildTraceyFleetViewFromAgentsImpl(agentViews, nowMs);
    }


} // namespace NMC::Server
