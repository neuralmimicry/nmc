// src/Core/K8sHandlers.h
#pragma once

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <vector>
#include <mutex>
#include <functional> // For std::function
#include <optional>   // For std::optional

#include "../Models/K8sCluster.h"
#include "../Models/CloudResponse.h"
#include "Utils.h" // For Utils::generateUniqueId

#ifdef __cplusplus
extern "C" {
#endif
// Include Kubernetes C client headers
#include <kubernetes/include/apiClient.h>
#include <kubernetes/include/generic.h> // For genericClient_t, Generic_create, genericClient_free
#include <kubernetes/include/keyValuePair.h> // Correct header for keyValuePair_t
#include <kubernetes/include/list.h>
#include <kubernetes/config/kube_config.h>
#include <kubernetes/api/CoreV1API.h>     // For CoreV1API_t, CoreV1API_create, CoreV1API_free
// Required for Kubernetes C client data structures
#include <kubernetes/model/v1_object_meta.h>
#include <kubernetes/model/v1_node.h>
#include <kubernetes/model/v1_node_list.h>
#include <kubernetes/model/v1_label_selector.h> // Potentially needed for labels
#ifdef __cplusplus
} // End of extern "C" block
#endif

namespace NMC {
    namespace Server {

        /**
         * @brief A class to encapsulate Kubernetes (K8s) API handler logic.
         *
         * This class manages the data for K8s clusters and provides
         * methods to handle API requests related to K8s operations by communicating
         * with a live Kubernetes API server using the official C client.
         */
        class K8sHandlers {
        public:
            /**
             * @brief Constructor for K8sHandlers.
             *
             * Initializes the K8sHandlers with Kubernetes API client configuration,
             * a mutex for thread safety.
             */
            K8sHandlers(
                    const std::string& api_server_url,
                    const std::string& kubeconfig_path,
                    std::mutex& mutex_ref,
                    std::unordered_map<std::string, Models::VClusterConfig>& vcluster_configs_ref,
                    std::function<void(httplib::Response&, const Models::CloudResponse&)> send_json_cb,
            std::function<void(httplib::Response&, int, const std::string&)> send_error_cb
            );

            ~K8sHandlers();

            // Handlers for K8s API operations
            void handleCreateK8sCluster(const httplib::Request& req, httplib::Response& res);
            void handleDeleteK8sCluster(const httplib::Request& req, httplib::Response& res);
            void handleGetK8sCluster(const httplib::Request& req, httplib::Response& res);
            void handleGetK8sClusterDetails(const httplib::Request& req, httplib::Response& res);
            void handleGetKubeConfig(const httplib::Request& req, httplib::Response& res);
            void handleListK8sClusters(const httplib::Request& req, httplib::Response& res);
            void handleListK8sLocations(const httplib::Request& req, httplib::Response& res);
            void handleK8sHealthCheck(const httplib::Request& req, httplib::Response& res);
            void handleResumeK8sCluster(const httplib::Request& req, httplib::Response& res);
            void handleSuspendK8sCluster(const httplib::Request& req, httplib::Response& res);

            // Vcluster management handlers
            void handleCreateVCluster(const httplib::Request& req, httplib::Response& res);
            void handleDeleteVCluster(const httplib::Request& req, httplib::Response& res);
            void handleGetVCluster(const httplib::Request& req, httplib::Response& res);
            void handleListVClusters(const httplib::Request& req, httplib::Response& res);
            void handleGetVClusterKubeConfig(const httplib::Request& req, httplib::Response& res);

            // Vcluster lifecycle handlers
            void handlePauseVCluster(const httplib::Request& req, httplib::Response& res);
            void handleResumeVCluster(const httplib::Request& req, httplib::Response& res);
            void handleBackupVCluster(const httplib::Request& req, httplib::Response& res);
            void handleRestoreVCluster(const httplib::Request& req, httplib::Response& res);
            void handleUpgradeVCluster(const httplib::Request& req, httplib::Response& res);

            // Vcluster configuration handlers
            void handleGetVClusterConfig(const httplib::Request& req, httplib::Response& res);
            void handleUpdateVClusterConfig(const httplib::Request& req, httplib::Response& res);

            // Vcluster monitoring handlers
            void handleGetVClusterMetrics(const httplib::Request& req, httplib::Response& res);
            void handleGetVClusterHealth(const httplib::Request& req, httplib::Response& res);
            void handleGetVClusterResources(const httplib::Request& req, httplib::Response& res);

        private:
            // Kubernetes C client related members
            apiClient_t *apiClient;
            char *basePath;
            sslConfig_t *sslConfig;
            list_t *apiKeys; // List of apiKey_t for authentication

            // Internal helper to convert K8s JSON (from generic client) to Models::K8sCluster
            std::optional<Models::K8sCluster> parseKubernetesObjectToK8sCluster(const nlohmann::json& k8sObject);
            // Internal helper for generic client calls
            genericClient_t *getGenericClient(const std::string& group, const std::string& version, const std::string& plural);

            // VCluster helper methods
            bool createServiceAccount(const std::string& ns, const std::string& name);
            bool createRBACResources(const std::string& ns, const std::string& sa_name, const std::vector<std::string>& roles);
            bool createPodDisruptionBudget(const std::string& ns, const std::string& name, int minAvailable, const std::map<std::string, std::string>& matchLabels);
            bool createIngress(const std::string& ns, const std::string& name, const Models::NetworkingConfig& netConfig);
            nlohmann::json buildVClusterStatefulSet(const std::string& name, const std::string& ns, const Models::VClusterConfig& config);
            nlohmann::json buildVClusterService(const std::string& name, const std::string& ns, const Models::NetworkingConfig& netConfig);
            std::string extractClusterIdentifierFromRequest(const httplib::Request& req);
            bool clusterMatchesFilter(const nlohmann::json& cluster, const std::string& filterName);

            std::mutex& dataMutex; /**< Reference to the mutex (still useful for general thread safety). */
            std::unordered_map<std::string, Models::VClusterConfig>& vclusterConfigsRef; /**< Reference to VCluster configs storage */

            // Callbacks for sending responses, provided by APIRoutes
            std::function<void(httplib::Response&, const Models::CloudResponse&)> sendJsonResponse;
            std::function<void(httplib::Response&, int, const std::string&)> sendErrorResponse;

            // CRD Group, Version, Plural for our hypothetical K8sCluster
            const std::string K8S_CLUSTER_CRD_GROUP = "aarnn.network";
            const std::string K8S_CLUSTER_CRD_VERSION = "v1";
            const std::string K8S_CLUSTER_CRD_PLURAL = "k8sclusters";
        };

    } // namespace Server
} // namespace NMC
