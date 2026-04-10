#ifndef NMC_CORE_CLOUD_API_CLIENT_H
#define NMC_CORE_CLOUD_API_CLIENT_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <httplib.h>
#include "Models/Bucket.h"
#include "Models/K8sCluster.h"
#include "Models/SSHKey.h"
#include "Models/VM.h"
#include "Models/CloudResponse.h"
#include "Models/Connection.h"
#include <optional>
#include <nlohmann/json.hpp>

namespace httplib {
    class Client;
}

namespace NMC::Core {

    /**
     * HTTP transport facade used by all CLI commands.
     *
     * Responsibilities:
     * - map command operations to concrete server routes
     * - normalize HTTP responses into `Models::CloudResponse`
     * - persist and resolve local connection profiles
     * - apply bearer auth headers with deterministic precedence
     */
    class CloudAPIClient {
    public:
        /**
         * Construct a client with a default fallback endpoint and local config path.
         *
         * If a default saved connection exists in the config, that endpoint overrides
         * the fallback host/port for this process.
         */
        CloudAPIClient(const std::string& host, int port, const std::string& configFilePath);
        ~CloudAPIClient();

        // Bucket Operations
        Models::CloudResponse createBucket(const std::string& name, const std::string& location, const std::string& type);
        Models::CloudResponse deleteBucket(const std::string& name);
        Models::CloudResponse getBucket(const std::string& name);
        Models::CloudResponse listBuckets(const std::string& filterName = "");
        Models::CloudResponse listBucketLocations();
        Models::CloudResponse listBucketTypes();
        Models::CloudResponse resetBucketKey(const std::string& name);

        // K8s Operations
        Models::CloudResponse createK8sCluster(const std::string& name, const std::string& region);
        Models::CloudResponse deleteK8sCluster(const std::string& id);
        Models::CloudResponse getK8sCluster(const std::string& id);
        Models::CloudResponse getKubeConfig(const std::string& id);
        Models::CloudResponse listK8sClusters(const std::string& filterName = "");
        Models::CloudResponse listK8sLocations(const std::string& filterSku = "");
        Models::CloudResponse getK8sHealth();
        Models::CloudResponse getRefinerDeploymentStatus(const std::string& namespaceName = "refiner",
                                                         const std::string& deploymentName = "refiner");
        Models::CloudResponse scaleRefinerDeployment(int replicas,
                                                     const std::string& namespaceName = "refiner",
                                                     const std::string& deploymentName = "refiner");
        Models::CloudResponse resumeK8sCluster(const std::string& id);
        Models::CloudResponse suspendK8sCluster(const std::string& id);

        // VCluster Operations
        Models::CloudResponse createVCluster(const std::string& name, const std::string& vclusterNamespace = "");
        Models::CloudResponse deleteVCluster(const std::string& id);
        Models::CloudResponse getVCluster(const std::string& id);
        Models::CloudResponse listVClusters(const std::string& filterName = "");
        Models::CloudResponse getVClusterKubeConfig(const std::string& id);

        // VCluster Lifecycle Operations
        Models::CloudResponse pauseVCluster(const std::string& id);
        Models::CloudResponse resumeVCluster(const std::string& id);
        Models::CloudResponse backupVCluster(const std::string& id, const std::string& backupName = "");
        Models::CloudResponse restoreVCluster(const std::string& backupName, const std::string& targetName = "");
        Models::CloudResponse upgradeVCluster(const std::string& id, const std::string& newVersion);

        // VCluster Configuration Operations
        Models::CloudResponse getVClusterConfig(const std::string& id);
        Models::CloudResponse updateVClusterConfig(const std::string& id, const nlohmann::json& config);

        // VCluster Monitoring Operations
        Models::CloudResponse getVClusterMetrics(const std::string& id);
        Models::CloudResponse getVClusterHealth(const std::string& id);
        Models::CloudResponse getVClusterResources(const std::string& id);

        // Model Operations
        Models::CloudResponse uploadModel(const std::string& filePath, const std::string& sku, const std::string& registryName, const std::string& tag);

        // SSH Key Operations
        Models::CloudResponse createSSHKey(const std::string& name, const std::string& publicKey, const std::string& description);
        Models::CloudResponse deleteSSHKey(const std::string& id);
        Models::CloudResponse listSSHKeys(const std::string& filterName = "");
        Models::CloudResponse getSSHKey(const std::string& id);

        // VM Operations
        Models::CloudResponse createVM(const std::string& name, const std::string& sku, const std::string& region, const std::string& osImage, const std::string& publicKeyId, const std::string& initScript);
        Models::CloudResponse deleteVM(const std::string& id);
        Models::CloudResponse getVM(const std::string& id);
        Models::CloudResponse listVMs(const std::string& filterName = "");
        Models::CloudResponse listVMLocations(const std::string& filterSku = "");
        Models::CloudResponse listVMOSImages(const std::string& filterSku = "");
        Models::CloudResponse listVMSKUs(const std::string& filterSku = "");
        Models::CloudResponse restartVM(const std::string& id);
        Models::CloudResponse resumeVM(const std::string& id);
        Models::CloudResponse suspendVM(const std::string& id);

        // OpenShift / OpenStack / Proxmox Continuum Operations (via provider portal APIs)
        Models::CloudResponse getServerHealth();
        Models::CloudResponse getServerVersion();
        Models::CloudResponse listOpenShiftResources();
        Models::CloudResponse listOpenShiftClusters();
        Models::CloudResponse getOpenShiftClusterDetails(const std::string& idOrName);
        Models::CloudResponse requestOpenShiftCluster(const std::string& name,
                                                      const std::string& organization,
                                                      int gpuCount,
                                                      const std::string& architecture,
                                                      const std::string& region,
                                                      const std::string& provider,
                                                      const std::vector<std::string>& burstTargets);
        Models::CloudResponse listOpenStackResources();
        Models::CloudResponse listOpenStackClusters();
        Models::CloudResponse getOpenStackClusterDetails(const std::string& idOrName);
        Models::CloudResponse requestOpenStackCluster(const std::string& name,
                                                      const std::string& organization,
                                                      int gpuCount,
                                                      const std::string& architecture,
                                                      const std::string& region,
                                                      const std::string& provider,
                                                      const std::vector<std::string>& burstTargets);
        Models::CloudResponse listProxmoxResources();
        Models::CloudResponse listProxmoxClusters();
        Models::CloudResponse getProxmoxClusterDetails(const std::string& idOrName);
        Models::CloudResponse requestProxmoxCluster(const std::string& name,
                                                    const std::string& organization,
                                                    int gpuCount,
                                                    const std::string& architecture,
                                                    const std::string& region,
                                                    const std::string& provider,
                                                    const std::vector<std::string>& burstTargets);

        // Node Recruitment
        Models::CloudResponse recruitNode(const nlohmann::json& requestPayload);

        // Tracey Operations
        Models::CloudResponse traceyHeartbeat(const nlohmann::json& heartbeatPayload);
        Models::CloudResponse listTraceyAgents();
        Models::CloudResponse getTraceyAnalytics(int windowSeconds = -1, int bucketSeconds = -1, int logLimit = -1);
        Models::CloudResponse getTraceyFleet();
        Models::CloudResponse getTraceyAdaptive(const std::string& policy = "");
        Models::CloudResponse getTraceyCveStatus();
        Models::CloudResponse getTraceyAssessmentFleet();
        Models::CloudResponse getTraceyAssessmentPlan(const std::string& agentId);
        Models::CloudResponse submitTraceyAssessmentReport(const std::string& agentId, const nlohmann::json& reportPayload);
        Models::CloudResponse listTraceyRacks();
        Models::CloudResponse getTraceyRackDetails(const std::string& rackId);
        Models::CloudResponse getTraceyAgentAnalysis(const std::string& agentId,
                                                     int windowSeconds = -1,
                                                     int bucketSeconds = -1,
                                                     int logLimit = -1);
        Models::CloudResponse getTraceyAgentServer(const std::string& agentId);
        Models::CloudResponse getTraceyAgentGpu(const std::string& agentId, const std::string& gpuId);
        Models::CloudResponse getTraceyAgentCompromise(const std::string& agentId);
        Models::CloudResponse controlTraceyAgent(const std::string& agentId, const nlohmann::json& controlPayload);
        Models::CloudResponse getTraceyAgentDeepDive(const std::string& agentId);

        // Server-side Connection Management
        Models::CloudResponse getServerConnectionStatus();
        Models::CloudResponse makeServerConnection(const std::string& name, const std::string& endpoint, bool setDefault);
        Models::CloudResponse dropServerConnection(const std::string& name);
        Models::CloudResponse listServerConnections();
        Models::CloudResponse selectServerConnection(const std::string& name);

        // Connection Management
        Models::CloudResponse getConnectionStatus();
        Models::CloudResponse makeConnection(const std::string& name, const std::string& endpoint, bool setDefault);
        Models::CloudResponse makeConnection(const std::string& name, const std::string& endpoint, bool setDefault, const std::string& token);
        Models::CloudResponse dropConnection(const std::string& name);
        Models::CloudResponse listConnections();
        Models::CloudResponse selectConnection(const std::string& name);
        Models::CloudResponse unsetDefaultConnection();
        bool hasDefaultConnection() const;
        std::optional<Models::Connection> getDefaultConnection() const;
        Models::CloudResponse setConnectionToken(const std::string& name, const std::string& token);
        Models::CloudResponse clearConnectionToken(const std::string& name);

    private:
        std::unique_ptr<httplib::Client> cli;

        // Normalize transport results and server payloads into CLI-facing responses.
        Models::CloudResponse processHttpResponse(httplib::Result& res, const std::string& successMessage) const;
        Models::CloudResponse processHttpResponse(httplib::Result& res, const std::string& successMessage, const nlohmann::json& dataPayload) const;

        std::vector<Models::Connection> connections;
        std::string currentConnectionName;
        std::string configFilePath;

        // Local connection profile persistence (`~/.nmc/config.json`).
        void loadConnections(bool showMessage);
        void saveConnections(bool showMessage);

        /**
         * Resolve bearer token precedence:
         * 1) OIDC env vars
         * 2) generic bearer env vars
         * 3) active/default connection token
         * 4) legacy auth env vars
         */
        std::string resolveAuthToken() const;

        // Apply default Authorization header to current client / temporary clients.
        void applyAuthHeaders();
        void applyAuthHeaders(httplib::Client& client, const std::string& token) const;

        // Parsed, in-memory copy of serialized connection state.
        nlohmann::json connectionsConfig;

        // Fallback endpoint when no default saved connection is active.
        std::string host;
        int port;
    };

} // namespace NMC::Core

#endif // NMC_CORE_CLOUD_API_CLIENT_H
