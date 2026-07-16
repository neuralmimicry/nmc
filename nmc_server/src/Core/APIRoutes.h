// server/APIRoutes.h
#pragma once

#include <httplib.h>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>
#include <memory> // Required for std::unique_ptr
#include <unordered_map>
#include <cstdint>
#include <thread>
#include <atomic>
#include <deque>
#include <functional>

// Include data models
#include "../Models/Bucket.h"
#include "../Models/K8sCluster.h"
#include "../Models/SSHKey.h"
#include "../Models/VM.h"
#include "../Models/Connection.h"
#include "../Models/CloudResponse.h"
#include "OpenStackClient.h"
#include "OpenShiftClient.h"
#include "ProxmoxClient.h"
#include "OIDCValidator.h"
#include "ServerStateStore.h"
#include "TraceyCVEIntel.h"
#include "TraceyStateStore.h"

// Forward declaration for K8sHandlers to avoid circular includes

namespace NMC::Server {
    class K8sHandlers; // Forward declaration
}



namespace NMC::Server {

    /**
     * @brief APIRoutes class manages and registers all API endpoints for the server.
     *
     * This class is responsible for setting up HTTP route handlers, processing requests,
     * and managing data for various cloud resources (Buckets, K8s, Models, SSH, VMs).
     * It also provides utility functions for logging and sending structured JSON responses.
     */
    class APIRoutes {
    public:
        /**
         * @brief Constructor for APIRoutes.
         * @param svr Reference to the httplib::Server instance to register routes on.
         */
        APIRoutes(httplib::Server& svr);

        /**
         * @brief Destructor for APIRoutes.
         *
         * This destructor is explicitly declared here and defined in APIRoutes.cpp
         * to allow for proper destruction of members like std::unique_ptr<K8sHandlers>
         * where the full definition of K8sHandlers is needed.
         */
        ~APIRoutes();

    private:
        using RouteGuard = std::function<bool(const httplib::Request&, httplib::Response&)>;

        // data storage for various resources
        std::vector<Models::Bucket> buckets;
        std::vector<Models::K8sCluster> k8sClusters;
        std::vector<Models::SSHKey> sshKeys;
        std::vector<Models::VM> vms;
        std::vector<Models::Connection> connections;
        std::string currentConnectionName;
        std::unordered_map<std::string, Models::VClusterConfig> vclusterConfigs; // config_id -> VClusterConfig
        struct TraceyAgent {
            std::string agentId;
            std::string cluster;
            std::string status;
            std::string version;
            std::string host;
            std::string source;
            std::string announceAddr;
            std::string statusAddr;
            std::string linkState;
            std::string linkSecurity;
            std::string lastError;
            nlohmann::json metrics;
            nlohmann::json capabilities;
            int64_t lastSeenEpochMs{0};
            int64_t lastAnnouncementEpochMs{0};
            int64_t lastQueryEpochMs{0};
            int64_t nextQueryEpochMs{0};
            int queryFailures{0};
            bool stale{false};
            bool statusReachable{false};
            bool signaturePresent{false};
            bool coordinator{false};
            int64_t coordinatorEpoch{0};
            int64_t score{0};
            int64_t lastProbeAlertTsMs{0};
            std::string lastProbeAlertFingerprint;
        };
        struct TraceyRequirement {
            std::string key;
            std::string resourceType;
            std::string resourceName;
            std::string region;
            std::string expectedAgentId;
            std::string expectedStatusAddr;
            int64_t createdEpochMs{0};
        };
        struct TraceyStatusSample {
            int64_t tsMs{0};
            std::string status;
            bool stale{false};
            bool statusReachable{false};
            int queryFailures{0};
            bool coordinator{false};
            int64_t score{0};
            int probeWatchTotalObservations{0};
            int probeWatchTotalAlerts{0};
            int probeWatchDistinctSources{0};
            int probeWatchRecentAlerts{0};
            int probeWatchHighAlerts{0};
            int probeWatchMediumAlerts{0};
            int probeWatchUnauthorizedControlAlerts{0};
            int probeWatchCooperativeAlerts{0};
            int probeWatchAlertedSurfaces{0};
            int tracey_guardProbeFailures{0};
            int tracey_guardProbeErrors{0};
            int tracey_guardQuarantined{0};
            int tracey_guardRemoteFaults{0};
            int loaderThreatLocalProviders{0};
            int loaderThreatLocalArtifacts{0};
            int loaderThreatBlockedProviders{0};
            int loaderThreatBlockedArtifacts{0};
            int loaderThreatRemoteReporters{0};
            std::string networkCollectorBackend;
            double networkAttributedTotalBps{0.0};
            double networkCrossNetworkBps{0.0};
            int networkActiveFlows{0};
            int networkListeners{0};
            int networkEstimatedFlows{0};
            int networkUdpActiveFlows{0};
            int64_t networkUdpDropDelta{0};
            double networkUdpEstimatedTotalBps{0.0};
            double networkAttributionConfidence{0.0};
            double networkLatencyPressure{0.0};
            double networkQueuePressure{0.0};
            double networkTrafficGrowthPctPerMin{0.0};
            double networkCrossGrowthPctPerMin{0.0};
            double networkFlowGrowthPctPerMin{0.0};
            double projectedTotalBps5m{0.0};
            double projectedTotalBps15m{0.0};
            double projectedCrossNetworkBps5m{0.0};
            double projectedCrossNetworkBps15m{0.0};
            double projectedActiveFlows5m{0.0};
            double projectedActiveFlows15m{0.0};
            double estimatedNetworkBps{0.0};
            double estimatedPowerW{0.0};
            std::string source;
        };
        struct TraceyAgentLogEntry {
            int64_t tsMs{0};
            std::string level;
            std::string category;
            std::string message;
            nlohmann::json context;
        };
        struct AiLabReport {
            std::string scenarioId;
            std::string scenarioName;
            std::string status;
            std::string evidenceRoot;
            int64_t receivedEpochMs{0};
            int64_t startedEpochMs{0};
            int64_t finishedEpochMs{0};
            int actionsProposed{0};
            int actionsExecuted{0};
            int policyRejections{0};
            int approvalsRequired{0};
            int eventsEmitted{0};
            nlohmann::json payload;
        };
        struct AarnnOrchestratorRecord {
            std::string orchestratorId;
            std::string clusterId;
            std::string grpcUrl;
            std::string host;
            int port{0};
            std::string source;
            std::string discoveryProtocol;
            std::string lastError;
            nlohmann::json statusCache;
            int64_t firstSeenEpochMs{0};
            int64_t lastSeenEpochMs{0};
            int64_t lastPolledEpochMs{0};
            int64_t nextPollEpochMs{0};
            bool stale{false};
            bool statusReachable{false};
        };
        struct RecruitCapacityAssessment {
            bool sameHardware{false};
            bool allowed{true};
            std::string source;
            std::string reason;
            nlohmann::json details;
        };
        std::unordered_map<std::string, TraceyAgent> traceyAgents;
        std::unordered_map<std::string, TraceyRequirement> traceyRequirements;
        std::unordered_map<std::string, std::deque<TraceyStatusSample>> traceyAgentHistory;
        std::unordered_map<std::string, std::deque<TraceyAgentLogEntry>> traceyAgentLogs;
        std::deque<AiLabReport> aiLabReports;
        std::unordered_map<std::string, AarnnOrchestratorRecord> aarnnOrchestrators;
        int64_t traceyStaleAfterSeconds;
        bool traceyEnforceManagedResources;
        bool traceyDiscoveryEnabled;
        bool traceyAllowPublicAddr;
        bool traceyTlsVerify;
        bool traceyRequireSignature;
        int64_t traceyRequirementGraceSeconds;
        std::string traceyDiscoveryBindAddr;
        int traceyDiscoveryPort;
        int64_t traceyDiscoveryMaxAgeMs;
        int64_t traceyStatusPollMs;
        int64_t traceyStatusTimeoutMs;
        int64_t traceyStatusMaxBackoffMs;
        size_t traceyHistoryMaxSamples;
        size_t traceyAgentLogMaxEntries;
        size_t aiLabReportMaxEntries;
        std::string traceyStatusBearerToken;
        std::string traceyLocalAgentId;
        std::thread traceyDiscoveryThread;
        std::atomic<bool> stopTraceyDiscovery;
        TraceyCVEIntel traceyCveIntel;
        std::unique_ptr<ServerStateStore> serverStateStore;
        std::unique_ptr<TraceyStateStore> traceyStateStore;
        bool aarnnDiscoveryEnabled;
        bool aarnnAllowPublicAddr;
        std::string aarnnDiscoveryBindAddr;
        int aarnnDiscoveryPort;
        int64_t aarnnStaleAfterSeconds;
        int64_t aarnnStatusPollMs;
        int64_t aarnnStatusTimeoutMs;
        std::thread aarnnDiscoveryThread;
        std::atomic<bool> stopAarnnDiscovery;

        std::mutex dataMutex; // Mutex to protect access to data in a multi-threaded environment

        // Compliance-related settings
        std::string authToken;
        bool authRequired;
        size_t maxBodyBytes;
        size_t maxLogBodyBytes;
        bool docsEnabled;
        std::string authMode;
        std::unique_ptr<OIDCValidator> oidcValidator;
        struct CentralAuthCacheEntry {
            bool authenticated{false};
            std::string user;
            nlohmann::json claims = nlohmann::json::object();
            int64_t expiresAtMs{0};
        };
        std::string centralAuthSessionUrl;
        std::string centralAuthLoginUrl;
        int64_t centralAuthCacheTtlMs{15000};
        int64_t centralAuthTimeoutMs{3000};
        bool centralAuthTlsVerify{true};
        mutable std::unordered_map<std::string, CentralAuthCacheEntry> centralAuthTokenCache;
        mutable std::mutex centralAuthCacheMutex;

        // Utility methods for common server operations
        void logRequest(const httplib::Request& req, const httplib::Response& res) const;

        /**
         * @brief Sends a JSON response to the client.
         * @param res The HTTP response object.
         * @param apiResponse The CloudResponse object containing success status, message, and data.
         */
        void sendJsonResponse(httplib::Response& res, const Models::CloudResponse& apiResponse) const;

        /**
         * @brief Sends an error JSON response to the client with a specific HTTP status code.
         * @param res The HTTP response object.
         * @param status The HTTP status code to send (e.g., 400, 404, 500).
         * @param message The error message.
         */
        void sendErrorResponse(httplib::Response& res, int status, const std::string& message) const;

        void handleGetConnectionStatus(const httplib::Request& req, httplib::Response& res);
        void handleMakeConnection(const httplib::Request& req, httplib::Response& res);
        void handleDropConnection(const httplib::Request& req, httplib::Response& res);
        void handleListConnections(const httplib::Request& req, httplib::Response& res);
        void handleSelectConnection(const httplib::Request& req, httplib::Response& res);

        // Security helpers (optional auth, request ID, and payload limits)
        void ensureRequestId(const httplib::Request& req, httplib::Response& res) const;
        bool enforceBodyLimit(const httplib::Request& req, httplib::Response& res) const;
        bool authorizeOrReject(const httplib::Request& req, httplib::Response& res) const;
        bool shouldLogBody(const std::string& path) const;
        std::string redactBody(const std::string& body) const;
        std::string extractAuthToken(const httplib::Request& req) const;
        nlohmann::json centralAuthClaimsJson(const CentralAuthCacheEntry& entry) const;
        bool validateCentralAuthToken(const std::string& token, nlohmann::json* claimsOut = nullptr) const;
        void registerDocsAndAuthRoutes(httplib::Server& svr, const RouteGuard& guard);
        void registerControlMetadataRoutes(httplib::Server& svr, const RouteGuard& guard);
        void registerDomainCrudRoutes(httplib::Server& svr, const RouteGuard& guard);
        void registerReleaseOperateRoutes(httplib::Server& svr, const RouteGuard& guard);
        void registerDomainProxyRoutes(httplib::Server& svr, const RouteGuard& guard);
        void registerTraceyRoutes(httplib::Server& svr, const RouteGuard& guard);
        void handleAuthLogin(const httplib::Request& req, httplib::Response& res);
        void handleAuthSession(const httplib::Request& req, httplib::Response& res);

        // --- Bucket Handlers ---
        void handleCreateBucket(const httplib::Request& req, httplib::Response& res);
        void handleDeleteBucket(const httplib::Request& req, httplib::Response& res);
        void handleGetBucket(const httplib::Request& req, httplib::Response& res);
        void handleListBuckets(const httplib::Request& req, httplib::Response& res);
        void handleListBucketLocations(const httplib::Request& req, httplib::Response& res);
        void handleListBucketTypes(const httplib::Request& req, httplib::Response& res);
        void handleResetBucketKey(const httplib::Request& req, httplib::Response& res);

        // --- Model Handlers ---
        void handleUploadModel(const httplib::Request& req, httplib::Response& res);

        // --- SSH Key Handlers ---
        void handleCreateSSHKey(const httplib::Request& req, httplib::Response& res);
        void handleDeleteSSHKey(const httplib::Request& req, httplib::Response& res);
        void handleGetSSHKey(const httplib::Request& req, httplib::Response& res);
        void handleListSSHKeys(const httplib::Request& req, httplib::Response& res);

        // --- VM Handlers ---
        void handleCreateVM(const httplib::Request& req, httplib::Response& res);
        void handleDeleteVM(const httplib::Request& req, httplib::Response& res);
        void handleGetVM(const httplib::Request& req, httplib::Response& res);
        void handleListVMs(const httplib::Request& req, httplib::Response& res);
        void handleListVMLocations(const httplib::Request& req, httplib::Response& res);
        void handleListVMOSImages(const httplib::Request& req, httplib::Response& res);
        void handleListVMSKUs(const httplib::Request& req, httplib::Response& res);
        void handleRestartVM(const httplib::Request& req, httplib::Response& res);
        void handleResumeVM(const httplib::Request& req, httplib::Response& res);
        void handleSuspendVM(const httplib::Request& req, httplib::Response& res);

        // --- OpenShift Handlers ---
        void handleOpenShiftResources(const httplib::Request& req, httplib::Response& res);
        void handleOpenShiftClusters(const httplib::Request& req, httplib::Response& res);
        void handleOpenShiftClusterDetails(const httplib::Request& req, httplib::Response& res);
        void handleOpenShiftRequestCluster(const httplib::Request& req, httplib::Response& res);
        void handleOpenStackResources(const httplib::Request& req, httplib::Response& res);
        void handleOpenStackClusters(const httplib::Request& req, httplib::Response& res);
        void handleOpenStackClusterDetails(const httplib::Request& req, httplib::Response& res);
        void handleOpenStackRequestCluster(const httplib::Request& req, httplib::Response& res);
        void handleProxmoxResources(const httplib::Request& req, httplib::Response& res);
        void handleProxmoxClusters(const httplib::Request& req, httplib::Response& res);
        void handleProxmoxClusterDetails(const httplib::Request& req, httplib::Response& res);
        void handleProxmoxRequestCluster(const httplib::Request& req, httplib::Response& res);
        void handleServerVersion(const httplib::Request& req, httplib::Response& res);
        void handleTraceyHeartbeat(const httplib::Request& req, httplib::Response& res);
        void handleListTraceyAgents(const httplib::Request& req, httplib::Response& res);
        void handleTraceyAnalytics(const httplib::Request& req, httplib::Response& res);
        void handleTraceyFleet(const httplib::Request& req, httplib::Response& res);
        void handleTraceyAdaptive(const httplib::Request& req, httplib::Response& res);
        void handleListTraceyRacks(const httplib::Request& req, httplib::Response& res);
        void handleTraceyRackDetails(const httplib::Request& req, httplib::Response& res);
        void handleTraceyAgentAnalysis(const httplib::Request& req, httplib::Response& res);
        void handleTraceyAgentServer(const httplib::Request& req, httplib::Response& res);
        void handleTraceyAgentGpu(const httplib::Request& req, httplib::Response& res);
        void handleTraceyCveStatus(const httplib::Request& req, httplib::Response& res);
        void handleTraceyAssessmentFleet(const httplib::Request& req, httplib::Response& res);
        void handleTraceyAssessmentPlan(const httplib::Request& req, httplib::Response& res);
        void handleTraceyAssessmentReport(const httplib::Request& req, httplib::Response& res);
        void handleAiLabStatus(const httplib::Request& req, httplib::Response& res);
        void handleAiLabReport(const httplib::Request& req, httplib::Response& res);
        void handleTraceyAgentCompromise(const httplib::Request& req, httplib::Response& res);
        void handleTraceyAgentControl(const httplib::Request& req, httplib::Response& res);
        void handleTraceyAgentDeepDive(const httplib::Request& req, httplib::Response& res);
        void handleAarnnEndpoints(const httplib::Request& req, httplib::Response& res);
        void handleAarnnInventory(const httplib::Request& req, httplib::Response& res);
        void handleAarnnProxy(const httplib::Request& req, httplib::Response& res, const std::string& plane);
        nlohmann::json buildAarnnEndpointsPayload(std::string& errorOut);
        nlohmann::json buildAarnnInventoryPayload(const std::string& clusterId,
                                                 const std::string& orchestratorId,
                                                 std::string& errorOut);
        bool resolveAarnnEndpoint(const std::string& plane,
                                  std::string& endpointOut,
                                  nlohmann::json& endpointInfoOut,
                                  std::string& errorOut);
        bool resolveAarnnOrchestratorTarget(const std::string& clusterId,
                                           const std::string& orchestratorId,
                                           nlohmann::json& targetOut,
                                           std::string& errorOut);
        bool fetchAarnnStatusViaControlPlane(const std::string& grpcUrl,
                                             int64_t nowMs,
                                             nlohmann::json& statusOut,
                                             std::string& errorOut);
        nlohmann::json buildTraceyContinuumAgentView(const TraceyAgent& agent, int64_t nowMs) const;
        nlohmann::json buildTraceyFleetViewFromAgents(const std::vector<nlohmann::json>& agentViews,
                                                      int64_t nowMs) const;
        RecruitCapacityAssessment assessRecruitCapacity(const std::string& host);
        void handleRecruitNode(const httplib::Request& req, httplib::Response& res);
        void runTraceyDiscoveryLoop();
        void runAarnnDiscoveryLoop();
        void ingestAarnnDiscoveryAnnouncement(const std::string& payload,
                                              const std::string& senderAddress,
                                              int64_t receivedAtMs);
        void pollAarnnOrchestratorStatus(const std::string& mapKey, const std::string& grpcUrl, int64_t nowMs);
        void markAarnnStaleOrchestrators(int64_t nowMs);
        void ingestTraceyDiscoveryAnnouncement(const nlohmann::json& payload, const std::string& senderAddress, int64_t receivedAtMs);
        void pollTraceyStatus(const std::string& agentId, const std::string& statusAddr, int64_t nowMs);
        void markTraceyStaleAgents(int64_t nowMs);
        void appendTraceyStatusSampleLocked(const TraceyAgent& agent, int64_t tsMs, const std::string& source);
        void appendTraceyProbeWatchLogsLocked(TraceyAgent& agent, int64_t nowMs);
        void appendTraceyAgentLogLocked(const std::string& agentId,
                                        int64_t tsMs,
                                        const std::string& level,
                                        const std::string& category,
                                        const std::string& message,
                                        nlohmann::json context = nlohmann::json::object());
        bool extractTraceyProvisioningRequest(const nlohmann::json& payload,
                                             std::string& agentIdOut,
                                             std::string& statusAddrOut,
                                             std::string& reasonOut) const;
        void upsertTraceyRequirementLocked(const std::string& resourceType,
                                           const std::string& resourceName,
                                           const std::string& region,
                                           const std::string& agentId,
                                           const std::string& statusAddr,
                                           int64_t nowMs);
        void removeTraceyRequirementLocked(const std::string& resourceType, const std::string& resourceName);
        nlohmann::json buildServerStateSnapshotLocked(int64_t nowMs) const;
        void restoreServerStateSnapshotLocked(const nlohmann::json& snapshot);
        void scheduleServerStateSnapshot(int64_t nowMs);
        void recordServerStateEvent(const std::string& entityType,
                                    const std::string& entityKey,
                                    const std::string& action,
                                    int64_t tsMs,
                                    nlohmann::json payload);
        nlohmann::json buildTraceyStateSnapshotLocked(int64_t nowMs) const;
        void restoreTraceyStateSnapshotLocked(const nlohmann::json& snapshot);
        void scheduleTraceyStateSnapshotLocked(int64_t nowMs);
        nlohmann::json traceyAgentToJson(const TraceyAgent& agent) const;
        nlohmann::json traceyRequirementToJson(const TraceyRequirement& requirement) const;
        nlohmann::json traceyStatusSampleToJson(const TraceyStatusSample& sample) const;
        nlohmann::json traceyAgentLogToJson(const TraceyAgentLogEntry& entry) const;
        nlohmann::json aiLabReportToJson(const AiLabReport& report) const;
        TraceyAgent traceyAgentFromJson(const nlohmann::json& payload) const;
        TraceyRequirement traceyRequirementFromJson(const nlohmann::json& payload) const;
        TraceyStatusSample traceyStatusSampleFromJson(const nlohmann::json& payload) const;
        TraceyAgentLogEntry traceyAgentLogFromJson(const nlohmann::json& payload) const;
        AiLabReport aiLabReportFromJson(const nlohmann::json& payload) const;

        // Declare an instance of K8sHandlers
        std::unique_ptr<K8sHandlers> k8sHandlers;
        std::unique_ptr<OpenStackClient> openStackClient;
        std::unique_ptr<OpenShiftClient> openShiftClient;
        std::unique_ptr<ProxmoxClient> proxmoxClient;

        // --- Gail Trading Bridge Proxy Handlers ---
        // These proxy Gail's /v1/trading/* endpoints through the NMC server so the
        // dashboard and CLI can access trading data without direct Gail connectivity.
        std::string resolveGailBaseUrl() const;
        std::string resolveGailApiToken() const;
        bool proxyGailTradingRequest(
            const httplib::Request& req,
            httplib::Response& res,
            const std::string& gailPath,
            const std::string& method,
            const std::string& body = ""
        );
        void handleGailTradingStatus(const httplib::Request& req, httplib::Response& res);
        void handleGailTradingPortfolio(const httplib::Request& req, httplib::Response& res);
        void handleGailTradingPositions(const httplib::Request& req, httplib::Response& res);
        void handleGailTradingHistory(const httplib::Request& req, httplib::Response& res);
        void handleGailTradingLogs(const httplib::Request& req, httplib::Response& res);
        void handleGailTradingExchanges(const httplib::Request& req, httplib::Response& res);
        void handleGailTradingCurrencies(const httplib::Request& req, httplib::Response& res);
        void handleGailTradingGetConfig(const httplib::Request& req, httplib::Response& res);
        void handleGailTradingSetConfig(const httplib::Request& req, httplib::Response& res);
        void handleGailTradingPause(const httplib::Request& req, httplib::Response& res);
        void handleGailTradingResume(const httplib::Request& req, httplib::Response& res);
        void handleGailTradingOverride(const httplib::Request& req, httplib::Response& res);
        void handleGailTradingEvaluate(const httplib::Request& req, httplib::Response& res);
        void handleGailApiIssues(const httplib::Request& req, httplib::Response& res);
    };

} // namespace NMC::Server
