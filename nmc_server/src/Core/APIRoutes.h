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

// Include data models
#include "../Models/Bucket.h"
#include "../Models/K8sCluster.h"
#include "../Models/SSHKey.h"
#include "../Models/VM.h"
#include "../Models/Connection.h"
#include "../Models/CloudResponse.h"
#include "OpenShiftClient.h"
#include "OIDCValidator.h"

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
        // data storage for various resources
        std::vector<Models::Bucket> buckets;
        std::vector<Models::K8sCluster> k8sClusters;
        std::vector<Models::SSHKey> sshKeys;
        std::vector<Models::VM> vms;
        std::vector<Models::Connection> connections;
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
            int tracey_guardProbeFailures{0};
            int tracey_guardProbeErrors{0};
            int tracey_guardQuarantined{0};
            int tracey_guardRemoteFaults{0};
            std::string source;
        };
        struct TraceyAgentLogEntry {
            int64_t tsMs{0};
            std::string level;
            std::string category;
            std::string message;
            nlohmann::json context;
        };
        std::unordered_map<std::string, TraceyAgent> traceyAgents;
        std::unordered_map<std::string, TraceyRequirement> traceyRequirements;
        std::unordered_map<std::string, std::deque<TraceyStatusSample>> traceyAgentHistory;
        std::unordered_map<std::string, std::deque<TraceyAgentLogEntry>> traceyAgentLogs;
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
        std::string traceyStatusBearerToken;
        std::thread traceyDiscoveryThread;
        std::atomic<bool> stopTraceyDiscovery;

        std::mutex dataMutex; // Mutex to protect access to data in a multi-threaded environment

        // Compliance-related settings
        std::string authToken;
        bool authRequired;
        size_t maxBodyBytes;
        size_t maxLogBodyBytes;
        bool docsEnabled;
        std::string authMode;
        std::unique_ptr<OIDCValidator> oidcValidator;

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

        // Security helpers (optional auth, request ID, and payload limits)
        void ensureRequestId(const httplib::Request& req, httplib::Response& res) const;
        bool enforceBodyLimit(const httplib::Request& req, httplib::Response& res) const;
        bool authorizeOrReject(const httplib::Request& req, httplib::Response& res) const;
        bool shouldLogBody(const std::string& path) const;
        std::string redactBody(const std::string& body) const;

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
        void handleServerVersion(const httplib::Request& req, httplib::Response& res);
        void handleTraceyHeartbeat(const httplib::Request& req, httplib::Response& res);
        void handleListTraceyAgents(const httplib::Request& req, httplib::Response& res);
        void handleTraceyAnalytics(const httplib::Request& req, httplib::Response& res);
        void handleTraceyAgentAnalysis(const httplib::Request& req, httplib::Response& res);
        void handleTraceyAgentControl(const httplib::Request& req, httplib::Response& res);
        void handleTraceyAgentDeepDive(const httplib::Request& req, httplib::Response& res);
        void handleRecruitNode(const httplib::Request& req, httplib::Response& res);
        void runTraceyDiscoveryLoop();
        void ingestTraceyDiscoveryAnnouncement(const nlohmann::json& payload, const std::string& senderAddress, int64_t receivedAtMs);
        void pollTraceyStatus(const std::string& agentId, const std::string& statusAddr, int64_t nowMs);
        void markTraceyStaleAgents(int64_t nowMs);
        void appendTraceyStatusSampleLocked(const TraceyAgent& agent, int64_t tsMs, const std::string& source);
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

        // Declare an instance of K8sHandlers
        std::unique_ptr<K8sHandlers> k8sHandlers;
        std::unique_ptr<OpenShiftClient> openShiftClient;
    };

} // namespace NMC::Server
