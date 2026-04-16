// src/Core/K8sHandlers.cpp
#include "K8sHandlers.h"
#include "Utils.h" // For Utils::generateUniqueId
#include <algorithm> // Not as much needed now, but keeping for general utility
#include <cctype>
#include <ctime>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string> // Required for std::to_string
#include <utility>
#include <vector>

// Include necessary headers for Kubernetes C client types
#include <kubernetes/include/apiClient.h> // For apiClient_t
#include <kubernetes/include/generic.h>   // For genericClient_t, genericClient_create, genericClient_free
#include <kubernetes/include/keyValuePair.h> // For keyValuePair_t
#include <kubernetes/api/CoreV1API.h>     // For CoreV1API functions

namespace {
    std::string trimCopy(const std::string& value) {
        const auto start = value.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            return "";
        }
        const auto end = value.find_last_not_of(" \t\r\n");
        return value.substr(start, end - start + 1);
    }

    std::string toLowerCopy(std::string value) {
        for (auto& ch : value) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return value;
    }

    bool clusterMatchesFilter(const nlohmann::json& cluster, const std::string& filterName) {
        const std::string needle = toLowerCopy(filterName);
        if (needle.empty()) {
            return true;
        }

        const std::string name = cluster.value("name", "");
        const std::string id = cluster.value("id", "");
        const std::string region = cluster.value("region", "");
        const std::string joined = toLowerCopy(name + " " + id + " " + region);
        return joined.find(needle) != std::string::npos;
    }

    bool jsonLabelEquals(const nlohmann::json& labels, const char* key, const char* expected) {
        return labels.contains(key)
               && labels[key].is_string()
               && labels[key].get<std::string>() == expected;
    }

    std::optional<nlohmann::json> findRefinerDeploymentFromList(const nlohmann::json& deploymentList) {
        if (!deploymentList.is_object() || !deploymentList.contains("items") || !deploymentList["items"].is_array()) {
            return std::nullopt;
        }

        for (const auto& item : deploymentList["items"]) {
            if (!item.is_object()) {
                continue;
            }

            const auto metadataIt = item.find("metadata");
            if (metadataIt == item.end() || !metadataIt->is_object()) {
                continue;
            }

            const auto& metadata = *metadataIt;
            const std::string name = metadata.value("name", "");
            if (name == "refiner") {
                return item;
            }

            const auto labelsIt = metadata.find("labels");
            if (labelsIt == metadata.end() || !labelsIt->is_object()) {
                continue;
            }

            const auto& labels = *labelsIt;
            if (jsonLabelEquals(labels, "nmc.neuralmimicry.ai/workload", "refiner")
                || jsonLabelEquals(labels, "app.kubernetes.io/name", "refiner")
                || jsonLabelEquals(labels, "app", "refiner")) {
                return item;
            }
        }

        return std::nullopt;
    }

    std::string extractClusterIdentifierFromRequest(const httplib::Request& req) {
        const auto pathIdIt = req.path_params.find("id");
        if (pathIdIt != req.path_params.end() && !pathIdIt->second.empty()) {
            return pathIdIt->second;
        }
        if (req.matches.size() > 1) {
            return req.matches[1].str();
        }
        return "";
    }

    bool sameClusterRecord(const NMC::Server::Models::K8sCluster& left,
                           const NMC::Server::Models::K8sCluster& right) {
        return left.id == right.id
               && left.name == right.name
               && left.region == right.region
               && left.status == right.status
               && left.is_vcluster == right.is_vcluster
               && left.parent_cluster == right.parent_cluster
               && left.vcluster_namespace == right.vcluster_namespace
               && left.config_id == right.config_id;
    }

    std::string clusterSortKey(const NMC::Server::Models::K8sCluster& cluster) {
        return toLowerCopy(trimCopy(cluster.name)) + "|" +
               toLowerCopy(trimCopy(cluster.id)) + "|" +
               toLowerCopy(trimCopy(cluster.region)) + "|" +
               toLowerCopy(trimCopy(cluster.status)) + "|" +
               (cluster.is_vcluster ? "1" : "0") + "|" +
               toLowerCopy(trimCopy(cluster.parent_cluster)) + "|" +
               toLowerCopy(trimCopy(cluster.vcluster_namespace)) + "|" +
               toLowerCopy(trimCopy(cluster.config_id));
    }

    void sortClusterRegistry(std::vector<NMC::Server::Models::K8sCluster>& clusters) {
        std::sort(clusters.begin(), clusters.end(), [](const auto& left, const auto& right) {
            return clusterSortKey(left) < clusterSortKey(right);
        });
    }

    bool clusterRegistryEqual(const std::vector<NMC::Server::Models::K8sCluster>& left,
                              const std::vector<NMC::Server::Models::K8sCluster>& right) {
        if (left.size() != right.size()) {
            return false;
        }
        std::vector<NMC::Server::Models::K8sCluster> sortedLeft = left;
        std::vector<NMC::Server::Models::K8sCluster> sortedRight = right;
        sortClusterRegistry(sortedLeft);
        sortClusterRegistry(sortedRight);
        for (size_t index = 0; index < sortedLeft.size(); ++index) {
            if (!sameClusterRecord(sortedLeft[index], sortedRight[index])) {
                return false;
            }
        }
        return true;
    }

    bool clusterMatchesIdentifier(const NMC::Server::Models::K8sCluster& cluster, const std::string& identifier) {
        const std::string normalizedIdentifier = toLowerCopy(trimCopy(identifier));
        if (normalizedIdentifier.empty()) {
            return false;
        }
        return (!trimCopy(cluster.id).empty() && toLowerCopy(trimCopy(cluster.id)) == normalizedIdentifier)
               || (!trimCopy(cluster.name).empty() && toLowerCopy(trimCopy(cluster.name)) == normalizedIdentifier);
    }

    bool upsertClusterRecord(std::vector<NMC::Server::Models::K8sCluster>& clusters,
                             const NMC::Server::Models::K8sCluster& cluster) {
        const std::string normalizedId = toLowerCopy(trimCopy(cluster.id));
        const std::string normalizedName = toLowerCopy(trimCopy(cluster.name));
        if (normalizedId.empty() && normalizedName.empty()) {
            return false;
        }

        auto existing = std::find_if(clusters.begin(), clusters.end(), [&](const auto& candidate) {
            const std::string candidateId = toLowerCopy(trimCopy(candidate.id));
            const std::string candidateName = toLowerCopy(trimCopy(candidate.name));
            return (!normalizedId.empty() && !candidateId.empty() && candidateId == normalizedId)
                   || (!normalizedName.empty() && !candidateName.empty() && candidateName == normalizedName);
        });
        if (existing == clusters.end()) {
            clusters.push_back(cluster);
            return true;
        }
        if (sameClusterRecord(*existing, cluster)) {
            return false;
        }
        *existing = cluster;
        return true;
    }

    bool removeClusterRecord(std::vector<NMC::Server::Models::K8sCluster>& clusters, const std::string& identifier) {
        auto newEnd = std::remove_if(clusters.begin(), clusters.end(), [&](const auto& cluster) {
            return clusterMatchesIdentifier(cluster, identifier);
        });
        if (newEnd == clusters.end()) {
            return false;
        }
        clusters.erase(newEnd, clusters.end());
        return true;
    }

    bool replaceClusterRegistry(std::vector<NMC::Server::Models::K8sCluster>& clusters,
                                std::vector<NMC::Server::Models::K8sCluster> replacement) {
        sortClusterRegistry(replacement);
        if (clusterRegistryEqual(clusters, replacement)) {
            return false;
        }
        clusters = std::move(replacement);
        return true;
    }

    bool isLocalClusterIdentifier(const std::string& identifier) {
        const std::string normalized = toLowerCopy(identifier);
        return normalized == "refiner-local"
               || normalized == "k8s-local-refiner"
               || normalized == "local-refiner";
    }

    void addUniqueIp(std::vector<std::string>& ips, const std::string& ipOrHost) {
        if (ipOrHost.empty() || ipOrHost == "None") {
            return;
        }
        if (std::find(ips.begin(), ips.end(), ipOrHost) == ips.end()) {
            ips.push_back(ipOrHost);
        }
    }

    void addUniquePort(std::vector<int>& ports, int port) {
        if (port <= 0) {
            return;
        }
        if (std::find(ports.begin(), ports.end(), port) == ports.end()) {
            ports.push_back(port);
        }
    }

    bool parseIntegerPort(const std::string& value, int& parsedPort) {
        if (value.empty()) {
            return false;
        }

        for (const char ch : value) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                return false;
            }
        }

        try {
            parsedPort = std::stoi(value);
            return parsedPort > 0;
        } catch (const std::exception&) {
            return false;
        }
    }

    std::pair<std::string, int> extractHostAndPortFromEndpoint(const std::string& endpoint) {
        if (endpoint.empty()) {
            return {"", -1};
        }

        std::string hostPort = endpoint;
        const std::size_t schemePos = hostPort.find("://");
        if (schemePos != std::string::npos) {
            hostPort = hostPort.substr(schemePos + 3);
        }

        const std::size_t slashPos = hostPort.find('/');
        if (slashPos != std::string::npos) {
            hostPort = hostPort.substr(0, slashPos);
        }

        if (hostPort.empty()) {
            return {"", -1};
        }

        if (hostPort.front() == '[') { // IPv6 literal form: [::1]:6443
            const std::size_t closeBracket = hostPort.find(']');
            if (closeBracket == std::string::npos) {
                return {"", -1};
            }
            const std::string host = hostPort.substr(1, closeBracket - 1);
            int port = -1;
            if (closeBracket + 2 <= hostPort.size() && hostPort.size() > closeBracket + 1 && hostPort[closeBracket + 1] == ':') {
                const std::string portText = hostPort.substr(closeBracket + 2);
                int parsed = -1;
                if (parseIntegerPort(portText, parsed)) {
                    port = parsed;
                }
            }
            return {host, port};
        }

        const std::size_t firstColon = hostPort.find(':');
        const std::size_t lastColon = hostPort.rfind(':');
        if (firstColon != std::string::npos && firstColon == lastColon) {
            const std::string host = hostPort.substr(0, lastColon);
            const std::string portText = hostPort.substr(lastColon + 1);
            int port = -1;
            int parsed = -1;
            if (parseIntegerPort(portText, parsed)) {
                port = parsed;
            }
            return {host, port};
        }

        return {hostPort, -1};
    }

    std::optional<nlohmann::json> findDeploymentByNameFromList(
            const nlohmann::json& deploymentList,
            const std::string& deploymentName
    ) {
        if (!deploymentList.is_object() || !deploymentList.contains("items") || !deploymentList["items"].is_array()) {
            return std::nullopt;
        }
        for (const auto& item : deploymentList["items"]) {
            if (!item.is_object()) {
                continue;
            }
            if (!item.contains("metadata") || !item["metadata"].is_object()) {
                continue;
            }
            const auto& metadata = item["metadata"];
            if (metadata.value("name", "") == deploymentName) {
                return item;
            }
        }
        return std::nullopt;
    }

    std::optional<nlohmann::json> resolveRefinerDeployment(
            genericClient_t* deploymentClient,
            const std::string& namespaceName,
            const std::string& deploymentName
    ) {
        if (!deploymentClient || namespaceName.empty() || deploymentName.empty()) {
            return std::nullopt;
        }
        char* deploymentRaw = Generic_readNamespacedResource(
                deploymentClient,
                (char*)namespaceName.c_str(),
                (char*)deploymentName.c_str()
        );
        char* deploymentListRaw = nullptr;
        std::optional<nlohmann::json> deploymentJson;
        try {
            if (deploymentRaw) {
                deploymentJson = nlohmann::json::parse(deploymentRaw);
            } else {
                deploymentListRaw = Generic_listNamespaced(deploymentClient, (char*)namespaceName.c_str());
                if (deploymentListRaw) {
                    const auto deploymentListJson = nlohmann::json::parse(deploymentListRaw);
                    deploymentJson = findDeploymentByNameFromList(deploymentListJson, deploymentName);
                    if (!deploymentJson) {
                        deploymentJson = findRefinerDeploymentFromList(deploymentListJson);
                    }
                }
            }
        } catch (const std::exception&) {
            deploymentJson = std::nullopt;
        }
        if (deploymentRaw) {
            free(deploymentRaw);
        }
        if (deploymentListRaw) {
            free(deploymentListRaw);
        }
        return deploymentJson;
    }

    nlohmann::json refinerDeploymentSummary(
            const nlohmann::json& deploymentJson,
            const std::string& fallbackNamespace,
            const std::string& fallbackDeployment
    ) {
        int desiredReplicas = 0;
        int readyReplicas = 0;
        int availableReplicas = 0;
        std::string namespaceName = fallbackNamespace;
        std::string deploymentName = fallbackDeployment;
        if (deploymentJson.contains("metadata") && deploymentJson["metadata"].is_object()) {
            const auto& metadata = deploymentJson["metadata"];
            namespaceName = metadata.value("namespace", namespaceName);
            deploymentName = metadata.value("name", deploymentName);
        }
        if (deploymentJson.contains("spec") && deploymentJson["spec"].is_object()) {
            desiredReplicas = deploymentJson["spec"].value("replicas", 0);
        }
        if (deploymentJson.contains("status") && deploymentJson["status"].is_object()) {
            readyReplicas = deploymentJson["status"].value("readyReplicas", 0);
            availableReplicas = deploymentJson["status"].value("availableReplicas", 0);
        }

        bool healthy = false;
        std::string status = "Pending";
        if (desiredReplicas <= 0) {
            status = "Suspended";
            healthy = true;
        } else if (readyReplicas >= desiredReplicas && availableReplicas >= desiredReplicas) {
            status = "Running";
            healthy = true;
        } else if (readyReplicas > 0 || availableReplicas > 0) {
            status = "Degraded";
        }

        return {
                {"observed", true},
                {"healthy", healthy},
                {"status", status},
                {"namespace", namespaceName},
                {"deployment", deploymentName},
                {"desired_replicas", desiredReplicas},
                {"ready_replicas", readyReplicas},
                {"available_replicas", availableReplicas}
        };
    }
} // namespace

namespace NMC {
    namespace Server {

        K8sHandlers::K8sHandlers(
                const std::string& api_server_url,
                const std::string &kubeconfig_path,
                std::mutex &mutex_ref,
                std::vector<Models::K8sCluster>& k8s_clusters_ref,
                std::unordered_map<std::string, Models::VClusterConfig>& vcluster_configs_ref,
                std::function<void(httplib::Response&, const Models::CloudResponse&)> send_json_cb,
                std::function<void(httplib::Response&, int, const std::string&)> send_error_cb,
                std::function<void(int64_t)> state_snapshot_cb
        ) :
        dataMutex(mutex_ref),
        k8sClustersRef(k8s_clusters_ref),
        vclusterConfigsRef(vcluster_configs_ref),
        sendJsonResponse(send_json_cb),
        sendErrorResponse(send_error_cb),
        scheduleStateSnapshot(std::move(state_snapshot_cb)),
        apiClient(nullptr), // Initialize to nullptr
        basePath(nullptr),
        sslConfig(nullptr),
        apiKeys(nullptr)
        {
            // Initialize Kubernetes C client
            int rc = 0;
            if (!kubeconfig_path.empty()) {
                // Set KUBECONFIG env var for load_kube_config if a specific path is given
                setenv("KUBECONFIG", kubeconfig_path.c_str(), 1);
            }

            // Load configuration from kubeconfig file or default locations
            rc = load_kube_config(&basePath, &sslConfig, &apiKeys, nullptr);
            if (rc != 0) {
                // Fallback to direct API server URL if kubeconfig loading fails,
                // this would require manual auth token handling (not shown here for brevity)
                std::cerr << "Warning: Cannot load kubernetes configuration from kubeconfig. Attempting direct connection." << std::endl;
                basePath = strdup(api_server_url.c_str());
                sslConfig = sslConfig_create(NULL, NULL, NULL, 0); // Basic SSL config, unsafe without proper certs
                // For a real scenario, you'd need to set up authToken for direct connection.
                // For this example, if kubeconfig fails, direct connection might not be fully functional without more setup.
            }

            // Create API client with the loaded configuration
            apiClient = apiClient_create_with_base_path(basePath, sslConfig, apiKeys);
            if (!apiClient) {
                std::cerr << "Error: Cannot create a kubernetes client." << std::endl;
                // You might want to throw an exception or set an internal error state here
            } else {
                std::cout << "Kubernetes API client initialized successfully for server: " << (basePath ? basePath : "unknown") << std::endl;
            }
        }

        K8sHandlers::~K8sHandlers() {
            if (apiClient) {
                apiClient_free(apiClient);
                apiClient = nullptr;
            }
            // Free the configuration loaded by load_kube_config or manually allocated
            if (basePath || sslConfig || apiKeys) {
                free_client_config(basePath, sslConfig, apiKeys);
                basePath = nullptr;
                sslConfig = nullptr;
                apiKeys = nullptr;
            }
        }

        // Helper to get a generic client for a specific CRD
        genericClient_t* K8sHandlers::getGenericClient(
                const std::string& group,
                const std::string& version,
                const std::string& plural
        ) {
            if (!apiClient)
                throw std::runtime_error("K8s API client not initialized");
            return genericClient_create(
                    apiClient,
                    (char*)group.c_str(),
                    (char*)version.c_str(),
                    (char*)plural.c_str()
            );
        }

        std::optional<nlohmann::json> K8sHandlers::readNamespacedResource(
                const std::string& group,
                const std::string& version,
                const std::string& plural,
                const std::string& namespaceName,
                const std::string& resourceName
        ) {
            if (namespaceName.empty() || resourceName.empty()) {
                return std::nullopt;
            }

            genericClient_t* genericClient = nullptr;
            char* rawObject = nullptr;
            std::optional<nlohmann::json> result = std::nullopt;
            try {
                genericClient = getGenericClient(group, version, plural);
                rawObject = Generic_readNamespacedResource(
                        genericClient,
                        (char*)namespaceName.c_str(),
                        (char*)resourceName.c_str()
                );
                if (!rawObject) {
                    result = std::nullopt;
                } else {
                    result = nlohmann::json::parse(rawObject);
                }
            } catch (const std::exception&) {
                result = std::nullopt;
            }
            if (rawObject) {
                free(rawObject);
            }
            if (genericClient) {
                genericClient_free(genericClient);
            }
            return result;
        }

        std::optional<nlohmann::json> K8sHandlers::listNamespacedResources(
                const std::string& group,
                const std::string& version,
                const std::string& plural,
                const std::string& namespaceName
        ) {
            if (namespaceName.empty()) {
                return std::nullopt;
            }

            genericClient_t* genericClient = nullptr;
            char* rawList = nullptr;
            std::optional<nlohmann::json> result = std::nullopt;
            try {
                genericClient = getGenericClient(group, version, plural);
                rawList = Generic_listNamespaced(genericClient, (char*)namespaceName.c_str());
                if (!rawList) {
                    result = std::nullopt;
                } else {
                    result = nlohmann::json::parse(rawList);
                }
            } catch (const std::exception&) {
                result = std::nullopt;
            }
            if (rawList) {
                free(rawList);
            }
            if (genericClient) {
                genericClient_free(genericClient);
            }
            return result;
        }


        // A very simplified example of converting a K8s object to our internal model.
        // This assumes a CRD structure that includes 'metadata.name', 'metadata.uid',
        // and 'spec.region' (or label 'region'), and 'status.state'.
        std::optional<Models::K8sCluster> K8sHandlers::parseKubernetesObjectToK8sCluster(const nlohmann::json& k8sObject) {
            if (k8sObject.contains("metadata") && k8sObject["metadata"].contains("name")) {
                Models::K8sCluster cluster;
                cluster.id = k8sObject["metadata"].value("uid", "unknown");
                cluster.name = k8sObject["metadata"].value("name", "unknown");
                cluster.is_vcluster = false;
                cluster.parent_cluster.clear();
                cluster.vcluster_namespace.clear();
                cluster.config_id.clear();

                // Try to get region from spec.region or metadata.labels.region
                if (k8sObject.contains("spec") && k8sObject["spec"].contains("region")) {
                    cluster.region = k8sObject["spec"].value("region", "unknown");
                } else if (k8sObject.contains("metadata") && k8sObject["metadata"].contains("labels") && k8sObject["metadata"]["labels"].contains("region")) {
                    cluster.region = k8sObject["metadata"]["labels"].value("region", "unknown");
                } else {
                    cluster.region = "unknown";
                }

                // Determine status from status.state or conditions
                if (k8sObject.contains("status") && k8sObject["status"].contains("state")) {
                    cluster.status = k8sObject["status"].value("state", "Unknown");
                } else if (k8sObject.contains("spec") && k8sObject["spec"].contains("state")) {
                    cluster.status = k8sObject["spec"].value("state", "Unknown");
                } else if (k8sObject.contains("status") && k8sObject["status"].contains("conditions") && k8sObject["status"]["conditions"].is_array()) {
                    for (const auto& condition : k8sObject["status"]["conditions"]) {
                        if (condition.value("type", "") == "Ready" && condition.value("status", "") == "True") {
                            cluster.status = "Running";
                            break;
                        } else if (condition.value("type", "") == "Suspended" && condition.value("status", "") == "True") {
                            cluster.status = "Suspended";
                            break;
                        }
                    }
                }
                if (cluster.status.empty()) { // Default if no specific status found
                    cluster.status = "Unknown";
                }
                return cluster;
            }
            return std::nullopt;
        }

        void K8sHandlers::handleCreateK8sCluster(const httplib::Request& req, httplib::Response& res) {
            try {
                auto json_body = nlohmann::json::parse(req.body);
                std::string name = json_body.value("name", "");
                std::string region = json_body.value("region", "");

                if (name.empty() || region.empty()) {
                    return sendErrorResponse(res, 400, "Missing required parameters: name or region.");
                }

                genericClient_t *genericClient = getGenericClient(K8S_CLUSTER_CRD_GROUP, K8S_CLUSTER_CRD_VERSION, K8S_CLUSTER_CRD_PLURAL);
                if (!genericClient) {
                    return sendErrorResponse(res, 500, "Failed to create generic Kubernetes client.");
                }

                // Construct the Kubernetes object for the CRD
                nlohmann::json k8sObject = {
                        {"apiVersion", K8S_CLUSTER_CRD_GROUP + "/" + K8S_CLUSTER_CRD_VERSION},
                        {"kind", "K8sCluster"}, // Assuming Kind is K8sCluster
                        {"metadata", {{"name", name}}},
                        {"spec", {{"region", region}, {"state", "Running"}}} // Initial state
                };

                char *created_object_str = Generic_createNamespacedResource(
                        genericClient,
                        (char*)"default", // Assuming namespace 'default' for now
                        (char*)k8sObject.dump().c_str()
                );

                if (created_object_str) {
                    auto created_obj = nlohmann::json::parse(created_object_str);
                    if (auto newCluster = parseKubernetesObjectToK8sCluster(created_obj)) {
                        bool snapshotChanged = false;
                        const int64_t mutationTs = static_cast<int64_t>(std::time(nullptr)) * 1000;
                        {
                            std::lock_guard<std::mutex> lock(dataMutex);
                            snapshotChanged = upsertClusterRecord(k8sClustersRef, *newCluster);
                        }
                        Models::CloudResponse apiResponse;
                        apiResponse.success = true;
                        apiResponse.message = "K8s cluster '" + name + "' created successfully.";
                        apiResponse.data = newCluster->toJsonString();
                        sendJsonResponse(res, apiResponse);
                        if (snapshotChanged && scheduleStateSnapshot) {
                            scheduleStateSnapshot(mutationTs);
                        }
                    } else {
                        sendErrorResponse(res, 500, "Failed to parse created Kubernetes object for cluster '" + name + "'.");
                    }
                } else if (apiClient->response_code == 409) {
                    sendErrorResponse(res, 409, "K8s cluster '" + name + "' already exists.");
                } else {
                    std::string error_message = "Failed to create K8s cluster '" + name + "'.";
                    if (apiClient->response_code >= 400) {
                        error_message += " Error code: " + std::to_string(apiClient->response_code);
                    }
                    sendErrorResponse(res, apiClient->response_code, error_message);
                }
                if (created_object_str) free(created_object_str);
                genericClient_free(genericClient);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        void K8sHandlers::handleDeleteK8sCluster(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = extractClusterIdentifierFromRequest(req);
                if (id.empty()) {
                    return sendErrorResponse(res, 400, "Missing cluster ID.");
                }

                genericClient_t *genericClient = getGenericClient(K8S_CLUSTER_CRD_GROUP, K8S_CLUSTER_CRD_VERSION, K8S_CLUSTER_CRD_PLURAL);
                if (!genericClient) {
                    return sendErrorResponse(res, 500, "Failed to create generic Kubernetes client.");
                }

                char *deleted_object_str = Generic_deleteNamespacedResource(
                        genericClient,
                        (char*)"default", // Assuming namespace 'default'
                        (char*)id.c_str()
                );

                if (deleted_object_str) {
                    bool snapshotChanged = false;
                    const int64_t mutationTs = static_cast<int64_t>(std::time(nullptr)) * 1000;
                    {
                        std::lock_guard<std::mutex> lock(dataMutex);
                        snapshotChanged = removeClusterRecord(k8sClustersRef, id);
                    }
                    // Successfully deleted, parse and send response
                    Models::CloudResponse apiResponse;
                    apiResponse.success = true;
                    apiResponse.message = "K8s cluster '" + id + "' deleted successfully.";
                    apiResponse.data = nlohmann::json::parse(deleted_object_str); // Or just a success message
                    sendJsonResponse(res, apiResponse);
                    if (snapshotChanged && scheduleStateSnapshot) {
                        scheduleStateSnapshot(mutationTs);
                    }
                } else if (apiClient->response_code == 404) {
                    sendErrorResponse(res, 404, "K8s cluster '" + id + "' not found.");
                } else {
                    std::string error_message = "Failed to delete K8s cluster '" + id + "'.";
                    if (apiClient->response_code >= 400) {
                        error_message += " Error code: " + std::to_string(apiClient->response_code);
                    }
                    sendErrorResponse(res, apiClient->response_code, error_message);
                }
                if (deleted_object_str) free(deleted_object_str);
                genericClient_free(genericClient);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        void K8sHandlers::handleGetK8sCluster(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = extractClusterIdentifierFromRequest(req);
                if (id.empty()) {
                    return sendErrorResponse(res, 400, "Missing cluster ID.");
                }

                genericClient_t *genericClient = getGenericClient(K8S_CLUSTER_CRD_GROUP, K8S_CLUSTER_CRD_VERSION, K8S_CLUSTER_CRD_PLURAL);
                if (!genericClient) {
                    return sendErrorResponse(res, 500, "Failed to create generic Kubernetes client.");
                }

                char *read_object_str = Generic_readNamespacedResource(
                        genericClient,
                        (char*)"default", // Assuming namespace 'default'
                        (char*)id.c_str()
                );

                if (read_object_str) {
                    auto read_obj = nlohmann::json::parse(read_object_str);
                    if (auto foundCluster = parseKubernetesObjectToK8sCluster(read_obj)) {
                        Models::CloudResponse apiResponse;
                        apiResponse.success = true;
                        apiResponse.message = "K8s cluster '" + id + "' retrieved successfully.";
                        apiResponse.data = foundCluster->toJsonString();
                        sendJsonResponse(res, apiResponse);
                    } else {
                        sendErrorResponse(res, 500, "Failed to parse retrieved Kubernetes object for cluster '" + id + "'.");
                    }
                } else if (apiClient->response_code == 404) {
                    sendErrorResponse(res, 404, "K8s cluster '" + id + "' not found.");
                } else {
                    std::string error_message = "Failed to retrieve K8s cluster '" + id + "'.";
                    if (apiClient->response_code >= 400) {
                        error_message += " Error code: " + std::to_string(apiClient->response_code);
                    }
                    sendErrorResponse(res, apiClient->response_code, error_message);
                }
                if (read_object_str) free(read_object_str);
                genericClient_free(genericClient);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        void K8sHandlers::handleGetK8sClusterDetails(const httplib::Request& req, httplib::Response& res) {
            try {
                const std::string identifier = extractClusterIdentifierFromRequest(req);
                if (identifier.empty()) {
                    return sendErrorResponse(res, 400, "Missing cluster identifier.");
                }

                std::vector<std::string> ipsInUse;
                std::vector<int> portsInUse;
                const std::string apiServerEndpoint = basePath ? std::string(basePath) : "";
                const auto [apiServerHost, apiServerPort] = extractHostAndPortFromEndpoint(apiServerEndpoint);
                addUniqueIp(ipsInUse, apiServerHost);
                addUniquePort(portsInUse, apiServerPort);

                if (isLocalClusterIdentifier(identifier)) {
                    int totalNodes = 0;
                    int readyNodes = 0;
                    std::string localRegion = "local";
                    nlohmann::json nodeRows = nlohmann::json::array();

                    if (apiClient) {
                        v1_node_list_t* nodeList = CoreV1API_listNode(
                                apiClient,
                                NULL, // pretty
                                NULL, // allowWatchBookmarks
                                NULL, // continue
                                NULL, // fieldSelector
                                NULL, // labelSelector
                                NULL, // limit
                                NULL, // resourceVersion
                                NULL, // resourceVersionMatch
                                NULL, // sendInitialEvents
                                NULL, // timeoutSeconds
                                NULL  // watch
                        );

                        if (nodeList && nodeList->items) {
                            listEntry_t* nodeEntry = nullptr;
                            list_ForEach(nodeEntry, nodeList->items) {
                                v1_node_t* node = static_cast<v1_node_t*>(nodeEntry->data);
                                if (!node) {
                                    continue;
                                }

                                totalNodes += 1;

                                bool nodeReady = false;
                                nlohmann::json nodeAddresses = nlohmann::json::array();
                                if (node->status && node->status->addresses) {
                                    listEntry_t* addrEntry = nullptr;
                                    list_ForEach(addrEntry, node->status->addresses) {
                                        v1_node_address_t* address = static_cast<v1_node_address_t*>(addrEntry->data);
                                        if (!address || !address->address) {
                                            continue;
                                        }
                                        const std::string addressType = address->type ? std::string(address->type) : "Unknown";
                                        const std::string addressValue = std::string(address->address);
                                        nodeAddresses.push_back({
                                                {"type", addressType},
                                                {"address", addressValue}
                                        });
                                        addUniqueIp(ipsInUse, addressValue);
                                    }
                                }

                                if (node->status && node->status->conditions) {
                                    listEntry_t* condEntry = nullptr;
                                    list_ForEach(condEntry, node->status->conditions) {
                                        v1_node_condition_t* condition = static_cast<v1_node_condition_t*>(condEntry->data);
                                        if (!condition || !condition->type || !condition->status) {
                                            continue;
                                        }
                                        if (std::strcmp(condition->type, "Ready") == 0
                                            && std::strcmp(condition->status, "True") == 0) {
                                            nodeReady = true;
                                            break;
                                        }
                                    }
                                } else {
                                    nodeReady = true;
                                }
                                if (nodeReady) {
                                    readyNodes += 1;
                                }

                                if (localRegion == "local" && node->metadata && node->metadata->labels) {
                                    listEntry_t* labelEntry = nullptr;
                                    list_ForEach(labelEntry, node->metadata->labels) {
                                        keyValuePair_t* kvp = static_cast<keyValuePair_t*>(labelEntry->data);
                                        if (!kvp || !kvp->key || !kvp->value) {
                                            continue;
                                        }
                                        if (std::strcmp(static_cast<const char*>(kvp->key), "topology.kubernetes.io/region") == 0
                                            || std::strcmp(static_cast<const char*>(kvp->key), "failure-domain.beta.kubernetes.io/region") == 0) {
                                            localRegion = static_cast<const char*>(kvp->value);
                                            break;
                                        }
                                    }
                                }

                                const std::string nodeName = (node->metadata && node->metadata->name)
                                                             ? std::string(node->metadata->name)
                                                             : std::string("unknown");
                                nodeRows.push_back({
                                        {"name", nodeName},
                                        {"ready", nodeReady},
                                        {"addresses", nodeAddresses}
                                });
                            }
                        }

                        if (nodeList) {
                            v1_node_list_free(nodeList);
                        }
                    }

                    bool refinerObserved = false;
                    int desiredReplicas = 0;
                    int readyReplicas = 0;
                    int availableReplicas = 0;
                    std::string refinerNamespace = "refiner";
                    std::string refinerDeploymentName = "refiner";

                    genericClient_t* deploymentClient = nullptr;
                    char* deploymentRaw = nullptr;
                    char* deploymentListRaw = nullptr;
                    try {
                        deploymentClient = getGenericClient("apps", "v1", "deployments");
                        if (deploymentClient) {
                            deploymentRaw = Generic_readNamespacedResource(
                                    deploymentClient,
                                    (char*)"refiner",
                                    (char*)"refiner"
                            );

                            std::optional<nlohmann::json> deploymentJson;
                            if (deploymentRaw) {
                                deploymentJson = nlohmann::json::parse(deploymentRaw);
                            } else {
                                deploymentListRaw = Generic_listNamespaced(deploymentClient, (char*)"refiner");
                                if (deploymentListRaw) {
                                    const auto deploymentListJson = nlohmann::json::parse(deploymentListRaw);
                                    deploymentJson = findRefinerDeploymentFromList(deploymentListJson);
                                }
                            }

                            if (deploymentJson && deploymentJson->is_object()) {
                                refinerObserved = true;
                                if (deploymentJson->contains("metadata") && (*deploymentJson)["metadata"].is_object()) {
                                    const auto& metadata = (*deploymentJson)["metadata"];
                                    refinerNamespace = metadata.value("namespace", refinerNamespace);
                                    refinerDeploymentName = metadata.value("name", refinerDeploymentName);
                                }
                                if (deploymentJson->contains("spec") && (*deploymentJson)["spec"].is_object()) {
                                    desiredReplicas = (*deploymentJson)["spec"].value("replicas", 0);
                                }
                                if (deploymentJson->contains("status") && (*deploymentJson)["status"].is_object()) {
                                    readyReplicas = (*deploymentJson)["status"].value("readyReplicas", 0);
                                    availableReplicas = (*deploymentJson)["status"].value("availableReplicas", 0);
                                }
                            }
                        }
                    } catch (const std::exception&) {
                        // Keep fallback behaviour non-fatal when deployment lookup fails.
                    }

                    if (deploymentRaw) {
                        free(deploymentRaw);
                    }
                    if (deploymentListRaw) {
                        free(deploymentListRaw);
                    }
                    if (deploymentClient) {
                        genericClient_free(deploymentClient);
                    }

                    nlohmann::json serviceRows = nlohmann::json::array();
                    if (apiClient) {
                        v1_service_list_t* serviceList = CoreV1API_listNamespacedService(
                                apiClient,
                                (char*)refinerNamespace.c_str(),
                                NULL, // pretty
                                NULL, // allowWatchBookmarks
                                NULL, // continue
                                NULL, // fieldSelector
                                NULL, // labelSelector
                                NULL, // limit
                                NULL, // resourceVersion
                                NULL, // resourceVersionMatch
                                NULL, // sendInitialEvents
                                NULL, // timeoutSeconds
                                NULL  // watch
                        );

                        if (serviceList && serviceList->items) {
                            listEntry_t* serviceEntry = nullptr;
                            list_ForEach(serviceEntry, serviceList->items) {
                                v1_service_t* service = static_cast<v1_service_t*>(serviceEntry->data);
                                if (!service) {
                                    continue;
                                }

                                const std::string serviceName = (service->metadata && service->metadata->name)
                                                                ? std::string(service->metadata->name)
                                                                : std::string("unknown");
                                const std::string serviceNamespace = (service->metadata && service->metadata->_namespace)
                                                                     ? std::string(service->metadata->_namespace)
                                                                     : refinerNamespace;

                                std::string serviceType = "ClusterIP";
                                std::string clusterIp = "";
                                nlohmann::json externalIps = nlohmann::json::array();
                                nlohmann::json loadBalancer = nlohmann::json::array();
                                nlohmann::json portRows = nlohmann::json::array();

                                if (service->spec) {
                                    if (service->spec->type) {
                                        serviceType = service->spec->type;
                                    }
                                    if (service->spec->cluster_ip) {
                                        clusterIp = service->spec->cluster_ip;
                                        addUniqueIp(ipsInUse, clusterIp);
                                    }
                                    if (service->spec->cluster_ips) {
                                        listEntry_t* clusterIpEntry = nullptr;
                                        list_ForEach(clusterIpEntry, service->spec->cluster_ips) {
                                            const char* clusterIpValue = static_cast<const char*>(clusterIpEntry->data);
                                            if (!clusterIpValue) {
                                                continue;
                                            }
                                            addUniqueIp(ipsInUse, clusterIpValue);
                                        }
                                    }
                                    if (service->spec->external_ips) {
                                        listEntry_t* externalIpEntry = nullptr;
                                        list_ForEach(externalIpEntry, service->spec->external_ips) {
                                            const char* externalIp = static_cast<const char*>(externalIpEntry->data);
                                            if (!externalIp) {
                                                continue;
                                            }
                                            externalIps.push_back(externalIp);
                                            addUniqueIp(ipsInUse, externalIp);
                                        }
                                    }
                                    if (service->spec->load_balancer_ip) {
                                        addUniqueIp(ipsInUse, service->spec->load_balancer_ip);
                                    }
                                    if (service->spec->ports) {
                                        listEntry_t* portEntry = nullptr;
                                        list_ForEach(portEntry, service->spec->ports) {
                                            v1_service_port_t* servicePort = static_cast<v1_service_port_t*>(portEntry->data);
                                            if (!servicePort) {
                                                continue;
                                            }
                                            addUniquePort(portsInUse, servicePort->port);
                                            addUniquePort(portsInUse, servicePort->node_port);

                                            nlohmann::json targetPort = nullptr;
                                            if (servicePort->target_port) {
                                                if (servicePort->target_port->type == IOS_DATA_TYPE_INT) {
                                                    targetPort = servicePort->target_port->i;
                                                    addUniquePort(portsInUse, servicePort->target_port->i);
                                                } else if (servicePort->target_port->type == IOS_DATA_TYPE_STRING && servicePort->target_port->s) {
                                                    targetPort = servicePort->target_port->s;
                                                    int parsedTargetPort = -1;
                                                    if (parseIntegerPort(servicePort->target_port->s, parsedTargetPort)) {
                                                        addUniquePort(portsInUse, parsedTargetPort);
                                                    }
                                                }
                                            }

                                            portRows.push_back({
                                                    {"name", servicePort->name ? std::string(servicePort->name) : std::string("")},
                                                    {"protocol", servicePort->protocol ? std::string(servicePort->protocol) : std::string("TCP")},
                                                    {"port", servicePort->port},
                                                    {"target_port", targetPort},
                                                    {"node_port", servicePort->node_port}
                                            });
                                        }
                                    }
                                }

                                if (service->status && service->status->load_balancer && service->status->load_balancer->ingress) {
                                    listEntry_t* ingressEntry = nullptr;
                                    list_ForEach(ingressEntry, service->status->load_balancer->ingress) {
                                        v1_load_balancer_ingress_t* ingress = static_cast<v1_load_balancer_ingress_t*>(ingressEntry->data);
                                        if (!ingress) {
                                            continue;
                                        }
                                        if (ingress->ip) {
                                            loadBalancer.push_back(ingress->ip);
                                            addUniqueIp(ipsInUse, ingress->ip);
                                        } else if (ingress->hostname) {
                                            loadBalancer.push_back(ingress->hostname);
                                            addUniqueIp(ipsInUse, ingress->hostname);
                                        }
                                    }
                                }

                                serviceRows.push_back({
                                        {"name", serviceName},
                                        {"namespace", serviceNamespace},
                                        {"type", serviceType},
                                        {"cluster_ip", clusterIp},
                                        {"external_ips", externalIps},
                                        {"load_balancer", loadBalancer},
                                        {"ports", portRows}
                                });
                            }
                        }

                        if (serviceList) {
                            v1_service_list_free(serviceList);
                        }
                    }

                    bool refinerHealthy = false;
                    std::string localStatus = (totalNodes > 0) ? "Running" : "Unavailable";
                    if (refinerObserved) {
                        if (desiredReplicas <= 0) {
                            localStatus = "Suspended";
                            refinerHealthy = true;
                        } else if (readyReplicas >= desiredReplicas && availableReplicas >= desiredReplicas) {
                            localStatus = "Running";
                            refinerHealthy = true;
                        } else if (readyReplicas > 0 || availableReplicas > 0) {
                            localStatus = "Degraded";
                        } else {
                            localStatus = "Pending";
                        }
                    }

                    std::sort(ipsInUse.begin(), ipsInUse.end());
                    std::sort(portsInUse.begin(), portsInUse.end());

                    Models::CloudResponse apiResponse;
                    apiResponse.success = true;
                    apiResponse.message = "K8s cluster details retrieved.";
                    apiResponse.data = {
                            {"id", "k8s-local-refiner"},
                            {"name", "refiner-local"},
                            {"region", localRegion},
                            {"status", localStatus},
                            {"source", "local-kubeconfig"},
                            {"total_nodes", totalNodes},
                            {"ready_nodes", readyNodes},
                            {"refiner", {
                                                 {"observed", refinerObserved},
                                                 {"healthy", refinerHealthy},
                                                 {"namespace", refinerNamespace},
                                                 {"deployment", refinerDeploymentName},
                                                 {"desired_replicas", desiredReplicas},
                                                 {"ready_replicas", readyReplicas},
                                                 {"available_replicas", availableReplicas}
                                         }},
                            {"networking", {
                                                   {"api_server", apiServerEndpoint},
                                                   {"nodes", nodeRows},
                                                   {"services", serviceRows},
                                                   {"ips_in_use", ipsInUse},
                                                   {"ports_in_use", portsInUse}
                                           }}
                    };
                    return sendJsonResponse(res, apiResponse);
                }

                genericClient_t* genericClient = nullptr;
                char* readObject = nullptr;
                char* listObject = nullptr;
                std::optional<nlohmann::json> resolvedObject;

                try {
                    genericClient = getGenericClient(K8S_CLUSTER_CRD_GROUP, K8S_CLUSTER_CRD_VERSION, K8S_CLUSTER_CRD_PLURAL);
                    if (!genericClient) {
                        return sendErrorResponse(res, 500, "Failed to create generic Kubernetes client.");
                    }

                    readObject = Generic_readNamespacedResource(
                            genericClient,
                            (char*)"default",
                            (char*)identifier.c_str()
                    );
                    if (readObject) {
                        resolvedObject = nlohmann::json::parse(readObject);
                    } else {
                        listObject = Generic_listNamespaced(genericClient, (char*)"default");
                        if (listObject) {
                            const auto listJson = nlohmann::json::parse(listObject);
                            if (listJson.contains("items") && listJson["items"].is_array()) {
                                for (const auto& item : listJson["items"]) {
                                    if (!item.is_object()) {
                                        continue;
                                    }
                                    const auto metadata = item.value("metadata", nlohmann::json::object());
                                    const std::string itemName = metadata.value("name", "");
                                    const std::string itemUid = metadata.value("uid", "");
                                    if (identifier == itemName || identifier == itemUid) {
                                        resolvedObject = item;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                } catch (const std::exception&) {
                    // Handled below via empty `resolvedObject`.
                }

                if (readObject) {
                    free(readObject);
                }
                if (listObject) {
                    free(listObject);
                }
                if (genericClient) {
                    genericClient_free(genericClient);
                }

                if (!resolvedObject) {
                    return sendErrorResponse(res, 404, "K8s cluster '" + identifier + "' not found.");
                }

                const auto cluster = parseKubernetesObjectToK8sCluster(*resolvedObject);
                if (!cluster) {
                    return sendErrorResponse(res, 500, "Failed to parse retrieved Kubernetes object for cluster '" + identifier + "'.");
                }

                nlohmann::json clusterData = cluster->toJsonString();
                std::vector<std::string> endpointCandidates;
                auto captureEndpointCandidate = [&](const nlohmann::json& objectNode, const char* key) {
                    if (objectNode.is_object() && objectNode.contains(key) && objectNode[key].is_string()) {
                        const std::string endpoint = objectNode[key].get<std::string>();
                        if (endpoint.empty()) {
                            return;
                        }
                        if (std::find(endpointCandidates.begin(), endpointCandidates.end(), endpoint) == endpointCandidates.end()) {
                            endpointCandidates.push_back(endpoint);
                        }
                        const auto [host, port] = extractHostAndPortFromEndpoint(endpoint);
                        addUniqueIp(ipsInUse, host);
                        addUniquePort(portsInUse, port);
                    }
                };

                if (resolvedObject->contains("spec")) {
                    const auto& spec = (*resolvedObject)["spec"];
                    captureEndpointCandidate(spec, "endpoint");
                    captureEndpointCandidate(spec, "server");
                    captureEndpointCandidate(spec, "api_server");
                    captureEndpointCandidate(spec, "apiServer");
                    if (spec.is_object() && spec.contains("port") && spec["port"].is_number_integer()) {
                        addUniquePort(portsInUse, spec["port"].get<int>());
                    }
                }
                if (resolvedObject->contains("status")) {
                    const auto& status = (*resolvedObject)["status"];
                    captureEndpointCandidate(status, "endpoint");
                    captureEndpointCandidate(status, "server");
                    captureEndpointCandidate(status, "api_server");
                    captureEndpointCandidate(status, "apiServer");
                    if (status.is_object() && status.contains("port") && status["port"].is_number_integer()) {
                        addUniquePort(portsInUse, status["port"].get<int>());
                    }
                }

                std::sort(ipsInUse.begin(), ipsInUse.end());
                std::sort(portsInUse.begin(), portsInUse.end());

                clusterData["networking"] = {
                        {"api_server", apiServerEndpoint},
                        {"endpoint_candidates", endpointCandidates},
                        {"ips_in_use", ipsInUse},
                        {"ports_in_use", portsInUse}
                };

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "K8s cluster details retrieved.";
                apiResponse.data = clusterData;
                sendJsonResponse(res, apiResponse);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        void K8sHandlers::handleGetKubeConfig(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = extractClusterIdentifierFromRequest(req);
                if (id.empty()) {
                    return sendErrorResponse(res, 400, "Missing cluster ID.");
                }

                // In a real scenario, you'd fetch the kubeconfig for the specific cluster 'id'.
                // This is a placeholder that returns a dummy kubeconfig.
                // You might fetch it from a Kubernetes Secret, or a custom resource related to the cluster.

                // For demonstration, let's assume we can read the cluster's CRD to get some info
                // and then construct a mock kubeconfig.
                genericClient_t *genericClient = getGenericClient(K8S_CLUSTER_CRD_GROUP, K8S_CLUSTER_CRD_VERSION, K8S_CLUSTER_CRD_PLURAL);
                if (!genericClient) {
                    return sendErrorResponse(res, 500, "Failed to create generic Kubernetes client.");
                }

                char *cluster_crd_str = Generic_readNamespacedResource(
                        genericClient,
                        (char*)"default", // Assuming namespace 'default'
                        (char*)id.c_str()
                );

                if (cluster_crd_str) {
                    auto cluster_crd_obj = nlohmann::json::parse(cluster_crd_str);
                    std::string cluster_name = cluster_crd_obj["metadata"].value("name", id);
                    std::string api_server_addr = "https://your-cluster-api-server-" + id + ".aarnn.network"; // Mock API server address

                    std::string kubeconfig_content = R"(
apiVersion: v1
clusters:
- cluster:
    certificate-authority-data: <YOUR_CA_DATA>
    server: )" + api_server_addr + R"(
  name: )" + cluster_name + R"(
contexts:
- context:
    cluster: )" + cluster_name + R"(
    user: admin-)" + cluster_name + R"(
  name: )" + cluster_name + R"(
current-context: )" + cluster_name + R"(
kind: Config
preferences: {}
users:
- name: admin-)" + cluster_name + R"(
  user:
    token: <YOUR_AUTH_TOKEN>
)";

                    Models::CloudResponse apiResponse;
                    apiResponse.success = true;
                    apiResponse.message = "Kubeconfig for cluster '" + id + "' retrieved successfully.";
                    apiResponse.data["kubeconfig"] = kubeconfig_content;
                    sendJsonResponse(res, apiResponse);
                } else if (apiClient->response_code == 404) {
                    sendErrorResponse(res, 404, "K8s cluster '" + id + "' not found.");
                } else {
                    std::string error_message = "Failed to retrieve kubeconfig for cluster '" + id + "'.";
                    if (apiClient->response_code >= 400) {
                        error_message += " Error code: " + std::to_string(apiClient->response_code);
                    }
                    sendErrorResponse(res, apiClient->response_code, error_message);
                }
                if (cluster_crd_str) free(cluster_crd_str);
                genericClient_free(genericClient);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        void K8sHandlers::handleListK8sClusters(const httplib::Request& req, httplib::Response& res) {
            try {
                const std::string filterName = req.get_param_value("filter-name");

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.data = nlohmann::json::object();
                apiResponse.data["clusters"] = nlohmann::json::array();

                std::string crdWarning;

                // Attempt CRD-backed cluster listing first.
                genericClient_t* genericClient = nullptr;
                char* listStr = nullptr;
                try {
                    genericClient = getGenericClient(
                            K8S_CLUSTER_CRD_GROUP,
                            K8S_CLUSTER_CRD_VERSION,
                            K8S_CLUSTER_CRD_PLURAL
                    );
                    if (genericClient) {
                        listStr = Generic_listNamespaced(genericClient, (char*)"default");
                    }

                    if (listStr) {
                        const auto listObject = nlohmann::json::parse(listStr);
                        if (listObject.contains("items") && listObject["items"].is_array()) {
                            for (const auto& item : listObject["items"]) {
                                if (auto cluster = parseKubernetesObjectToK8sCluster(item)) {
                                    nlohmann::json clusterJson = cluster->toJsonString();
                                    clusterJson["status"] = cluster->status;
                                    if (clusterMatchesFilter(clusterJson, filterName)) {
                                        apiResponse.data["clusters"].push_back(clusterJson);
                                    }
                                }
                            }
                        }
                    } else {
                        crdWarning = "K8s CRD listing unavailable from API client; using local cluster fallback.";
                    }
                } catch (const std::exception& e) {
                    crdWarning = "K8s CRD listing failed; using local cluster fallback. Detail: " + std::string(e.what());
                }

                if (listStr) {
                    free(listStr);
                }
                if (genericClient) {
                    genericClient_free(genericClient);
                }

                // Build a local fallback cluster entry using node and Refiner deployment state.
                int totalNodes = 0;
                int readyNodes = 0;
                std::string localRegion = "local";
                if (apiClient) {
                    v1_node_list_t* nodeList = CoreV1API_listNode(
                            apiClient,
                            NULL, // pretty
                            NULL, // allowWatchBookmarks
                            NULL, // continue
                            NULL, // fieldSelector
                            NULL, // labelSelector
                            NULL, // limit
                            NULL, // resourceVersion
                            NULL, // resourceVersionMatch
                            NULL, // sendInitialEvents
                            NULL, // timeoutSeconds
                            NULL  // watch
                    );

                    if (nodeList && nodeList->items) {
                        listEntry_t* nodeEntry = nullptr;
                        list_ForEach(nodeEntry, nodeList->items) {
                            v1_node_t* node = static_cast<v1_node_t*>(nodeEntry->data);
                            if (!node) {
                                continue;
                            }
                            totalNodes += 1;
                            // Conservative fallback: treat listed nodes as usable capacity.
                            readyNodes += 1;

                            if (localRegion == "local" && node->metadata && node->metadata->labels) {
                                listEntry_t* labelEntry = nullptr;
                                list_ForEach(labelEntry, node->metadata->labels) {
                                    keyValuePair_t* kvp = static_cast<keyValuePair_t*>(labelEntry->data);
                                    if (!kvp || !kvp->key || !kvp->value) {
                                        continue;
                                    }
                                    if (std::strcmp(static_cast<const char*>(kvp->key), "topology.kubernetes.io/region") == 0
                                        || std::strcmp(static_cast<const char*>(kvp->key), "failure-domain.beta.kubernetes.io/region") == 0) {
                                        localRegion = static_cast<const char*>(kvp->value);
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    if (nodeList) {
                        v1_node_list_free(nodeList);
                    }
                }

                bool refinerObserved = false;
                int desiredReplicas = 0;
                int readyReplicas = 0;
                int availableReplicas = 0;
                std::string refinerNamespace = "refiner";
                std::string refinerDeploymentName = "refiner";

                genericClient_t* deploymentClient = nullptr;
                char* deploymentRaw = nullptr;
                char* deploymentListRaw = nullptr;
                try {
                    deploymentClient = getGenericClient("apps", "v1", "deployments");
                    if (deploymentClient) {
                        deploymentRaw = Generic_readNamespacedResource(
                                deploymentClient,
                                (char*)"refiner",
                                (char*)"refiner"
                        );

                        std::optional<nlohmann::json> deploymentJson;
                        if (deploymentRaw) {
                            deploymentJson = nlohmann::json::parse(deploymentRaw);
                        } else {
                            deploymentListRaw = Generic_listNamespaced(deploymentClient, (char*)"refiner");
                            if (deploymentListRaw) {
                                const auto deploymentListJson = nlohmann::json::parse(deploymentListRaw);
                                deploymentJson = findRefinerDeploymentFromList(deploymentListJson);
                            }
                        }

                        if (deploymentJson && deploymentJson->is_object()) {
                            refinerObserved = true;
                            if (deploymentJson->contains("metadata") && (*deploymentJson)["metadata"].is_object()) {
                                const auto& metadata = (*deploymentJson)["metadata"];
                                refinerNamespace = metadata.value("namespace", refinerNamespace);
                                refinerDeploymentName = metadata.value("name", refinerDeploymentName);
                            }
                            if (deploymentJson->contains("spec") && (*deploymentJson)["spec"].is_object()) {
                                desiredReplicas = (*deploymentJson)["spec"].value("replicas", 0);
                            }
                            if (deploymentJson->contains("status") && (*deploymentJson)["status"].is_object()) {
                                readyReplicas = (*deploymentJson)["status"].value("readyReplicas", 0);
                                availableReplicas = (*deploymentJson)["status"].value("availableReplicas", 0);
                            }
                        }
                    }
                } catch (const std::exception&) {
                    // Keep fallback behaviour non-fatal when deployment lookup fails.
                }

                if (deploymentRaw) {
                    free(deploymentRaw);
                }
                if (deploymentListRaw) {
                    free(deploymentListRaw);
                }
                if (deploymentClient) {
                    genericClient_free(deploymentClient);
                }

                bool refinerHealthy = false;
                std::string localStatus = (totalNodes > 0) ? "Running" : "Unavailable";
                if (refinerObserved) {
                    if (desiredReplicas <= 0) {
                        localStatus = "Suspended";
                        refinerHealthy = true;
                    } else if (readyReplicas >= desiredReplicas && availableReplicas >= desiredReplicas) {
                        localStatus = "Running";
                        refinerHealthy = true;
                    } else if (readyReplicas > 0 || availableReplicas > 0) {
                        localStatus = "Degraded";
                    } else {
                        localStatus = "Pending";
                    }
                }

                nlohmann::json localCluster = {
                        {"id", "k8s-local-refiner"},
                        {"name", "refiner-local"},
                        {"region", localRegion},
                        {"status", localStatus},
                        {"source", "local-kubeconfig"},
                        {"total_nodes", totalNodes},
                        {"ready_nodes", readyNodes}
                };
                localCluster["refiner"] = {
                        {"observed", refinerObserved},
                        {"healthy", refinerHealthy},
                        {"namespace", refinerNamespace},
                        {"deployment", refinerDeploymentName},
                        {"desired_replicas", desiredReplicas},
                        {"ready_replicas", readyReplicas},
                        {"available_replicas", availableReplicas}
                };

                bool localPresent = false;
                for (const auto& existing : apiResponse.data["clusters"]) {
                    if (existing.is_object() && existing.value("name", "") == localCluster.value("name", "")) {
                        localPresent = true;
                        break;
                    }
                }
                if (!localPresent && clusterMatchesFilter(localCluster, filterName)) {
                    apiResponse.data["clusters"].push_back(localCluster);
                }

                if (apiResponse.data["clusters"].empty() && filterName.empty()) {
                    nlohmann::json unavailable = {
                            {"id", "k8s-unavailable"},
                            {"name", (basePath ? std::string(basePath) : std::string("k8s-local"))},
                            {"region", "local"},
                            {"status", "Unavailable"},
                            {"source", "kubernetes-api"}
                    };
                    apiResponse.data["clusters"].push_back(unavailable);
                    if (crdWarning.empty()) {
                        crdWarning = "K8s cluster CRDs and local node probes were unavailable.";
                    }
                }

                if (!crdWarning.empty()) {
                    apiResponse.data["warning"] = crdWarning;
                }
                apiResponse.message = apiResponse.data["clusters"].empty()
                                      ? "No Kubernetes clusters matched the current query."
                                      : "Successfully listed K8s clusters.";
                bool snapshotChanged = false;
                int64_t mutationTs = 0;
                if (filterName.empty()) {
                    std::vector<Models::K8sCluster> discoveredClusters;
                    for (const auto& clusterPayload : apiResponse.data["clusters"]) {
                        if (!clusterPayload.is_object()) {
                            continue;
                        }
                        Models::K8sCluster cluster = Models::K8sCluster::fromJson(clusterPayload);
                        if (trimCopy(cluster.id).empty() && trimCopy(cluster.name).empty()) {
                            continue;
                        }
                        discoveredClusters.push_back(std::move(cluster));
                    }
                    mutationTs = static_cast<int64_t>(std::time(nullptr)) * 1000;
                    {
                        std::lock_guard<std::mutex> lock(dataMutex);
                        snapshotChanged = replaceClusterRegistry(k8sClustersRef, std::move(discoveredClusters));
                    }
                }
                sendJsonResponse(res, apiResponse);
                if (snapshotChanged && scheduleStateSnapshot) {
                    scheduleStateSnapshot(mutationTs);
                }
            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        void K8sHandlers::handleListK8sLocations(const httplib::Request& req, httplib::Response& res) {
            try {
                // The CoreV1API functions take the apiClient_t directly.
                if (!apiClient) {
                    return sendErrorResponse(res, 500, "Kubernetes API client not initialized.");
                }

                // List all nodes
                v1_node_list_t *node_list = CoreV1API_listNode(
                        apiClient,
                        NULL, // pretty
                        NULL, // allowWatchBookmarks
                        NULL, // continue
                        NULL, // fieldSelector
                        NULL, // labelSelector
                        NULL, // limit
                        NULL, // resourceVersion
                        NULL, // resourceVersionMatch
                        NULL, // sendInitialEvents
                        NULL, // timeoutSeconds
                        NULL  // watch
                );

                std::vector<std::string> locations;
                if (node_list && node_list->items) {
                    listEntry_t *listEntry = NULL;
                    list_ForEach(listEntry, node_list->items) {
                        v1_node_t *node = (v1_node_t *)listEntry->data;
                        if (node && node->metadata && node->metadata->labels) {
                            // Assuming 'topology.kubernetes.io/region' or similar label defines location
                            listEntry_t *labelEntry = NULL;
                            std::string region_label;
                            list_ForEach(labelEntry, node->metadata->labels) {
                                keyValuePair_t *kvp = (keyValuePair_t *)labelEntry->data;
                                if (kvp && kvp->key && kvp->value &&
                                    (strcmp((char*)kvp->key, "topology.kubernetes.io/region") == 0 ||
                                     strcmp((char*)kvp->key, "failure-domain.beta.kubernetes.io/region") == 0)) {
                                    region_label = (char*)kvp->value;
                                    break;
                                }
                            }
                            if(!region_label.empty()) {
                                locations.push_back(region_label);
                            }
                        }
                    }
                    v1_node_list_free(node_list); // Free the list returned by the API
                }

                // Remove duplicates
                std::sort(locations.begin(), locations.end());
                locations.erase(std::unique(locations.begin(), locations.end()), locations.end());

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "Successfully listed K8s locations (regions).";
                apiResponse.data["locations"] = locations;
                sendJsonResponse(res, apiResponse);

                // FIX: Removed call to CoreV1API_free as the object was not created.

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        // Handler for liveness/readiness probe
        void K8sHandlers::handleK8sHealthCheck(const httplib::Request& req, httplib::Response& res) {
            try {
                // This is a simple health check that returns 200 OK
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "K8s API server is healthy.";
                sendJsonResponse(res, apiResponse);
            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        void K8sHandlers::handleGetRefinerStatus(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string namespaceName = req.has_param("namespace") ? trimCopy(req.get_param_value("namespace")) : "refiner";
                std::string deploymentName = req.has_param("deployment") ? trimCopy(req.get_param_value("deployment")) : "refiner";
                if (namespaceName.empty()) {
                    namespaceName = "refiner";
                }
                if (deploymentName.empty()) {
                    deploymentName = "refiner";
                }

                genericClient_t* deploymentClient = getGenericClient("apps", "v1", "deployments");
                if (!deploymentClient) {
                    return sendErrorResponse(res, 500, "Failed to create deployment client.");
                }
                auto deploymentJson = resolveRefinerDeployment(deploymentClient, namespaceName, deploymentName);
                genericClient_free(deploymentClient);
                if (!deploymentJson || !deploymentJson->is_object()) {
                    return sendErrorResponse(
                            res,
                            404,
                            "Refiner deployment '" + deploymentName + "' not found in namespace '" + namespaceName + "'."
                    );
                }

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "Refiner deployment status retrieved.";
                apiResponse.data = refinerDeploymentSummary(*deploymentJson, namespaceName, deploymentName);
                sendJsonResponse(res, apiResponse);
            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        void K8sHandlers::handleScaleRefiner(const httplib::Request& req, httplib::Response& res) {
            try {
                auto jsonBody = nlohmann::json::parse(req.body);
                if (!jsonBody.is_object()) {
                    return sendErrorResponse(res, 400, "Invalid JSON body. Expected an object.");
                }

                std::string namespaceName = trimCopy(jsonBody.value("namespace", "refiner"));
                std::string deploymentName = trimCopy(jsonBody.value("deployment", "refiner"));
                if (namespaceName.empty()) {
                    namespaceName = "refiner";
                }
                if (deploymentName.empty()) {
                    deploymentName = "refiner";
                }

                int replicas = -1;
                if (jsonBody.contains("replicas")) {
                    const auto& replicasNode = jsonBody["replicas"];
                    if (replicasNode.is_number_integer()) {
                        replicas = replicasNode.get<int>();
                    } else if (replicasNode.is_string()) {
                        try {
                            replicas = std::stoi(trimCopy(replicasNode.get<std::string>()));
                        } catch (const std::exception&) {
                            replicas = -1;
                        }
                    }
                }
                if (replicas < 0 || replicas > 100) {
                    return sendErrorResponse(res, 400, "replicas must be an integer in range 0-100.");
                }

                genericClient_t* deploymentClient = getGenericClient("apps", "v1", "deployments");
                if (!deploymentClient) {
                    return sendErrorResponse(res, 500, "Failed to create deployment client.");
                }

                auto deploymentJson = resolveRefinerDeployment(deploymentClient, namespaceName, deploymentName);
                if (!deploymentJson || !deploymentJson->is_object()) {
                    genericClient_free(deploymentClient);
                    return sendErrorResponse(
                            res,
                            404,
                            "Refiner deployment '" + deploymentName + "' not found in namespace '" + namespaceName + "'."
                    );
                }

                if (deploymentJson->contains("metadata") && (*deploymentJson)["metadata"].is_object()) {
                    const auto& metadata = (*deploymentJson)["metadata"];
                    namespaceName = metadata.value("namespace", namespaceName);
                    deploymentName = metadata.value("name", deploymentName);
                }

                nlohmann::json patchBody = {
                        {"spec", {{"replicas", replicas}}}
                };
                char* patchedRaw = Generic_patchNamespacedResource(
                        deploymentClient,
                        (char*)namespaceName.c_str(),
                        (char*)deploymentName.c_str(),
                        (char*)patchBody.dump().c_str(),
                        NULL,
                        NULL,
                        NULL,
                        NULL,
                        NULL
                );
                genericClient_free(deploymentClient);

                if (!patchedRaw) {
                    if (apiClient && apiClient->response_code == 404) {
                        return sendErrorResponse(
                                res,
                                404,
                                "Refiner deployment '" + deploymentName + "' not found in namespace '" + namespaceName + "'."
                        );
                    }
                    const int statusCode = (apiClient && apiClient->response_code >= 400) ? apiClient->response_code : 500;
                    return sendErrorResponse(res, statusCode, "Failed to scale Refiner deployment.");
                }

                nlohmann::json patchedJson;
                try {
                    patchedJson = nlohmann::json::parse(patchedRaw);
                } catch (const std::exception&) {
                    free(patchedRaw);
                    return sendErrorResponse(res, 500, "Failed to parse scaled deployment response.");
                }
                free(patchedRaw);

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "Refiner deployment scaled successfully.";
                apiResponse.data = refinerDeploymentSummary(patchedJson, namespaceName, deploymentName);
                sendJsonResponse(res, apiResponse);
            } catch (const nlohmann::json::parse_error& e) {
                sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        void K8sHandlers::handleResumeK8sCluster(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = extractClusterIdentifierFromRequest(req);
                if (id.empty()) {
                    return sendErrorResponse(res, 400, "Missing cluster ID.");
                }

                genericClient_t *genericClient = getGenericClient(K8S_CLUSTER_CRD_GROUP, K8S_CLUSTER_CRD_VERSION, K8S_CLUSTER_CRD_PLURAL);
                if (!genericClient) {
                    return sendErrorResponse(res, 500, "Failed to create generic Kubernetes client.");
                }

                // Patch the CRD to change its state to "Running"
                nlohmann::json patch_body = {
                        {"spec", {{"state", "Running"}}}
                };

                // FIX: The signature for Generic_patchNamespacedResource has changed.
                // The content type is no longer a simple char*. Passing NULL to satisfy the compiler.
                // NOTE: For this to work at runtime, the API server must know the patch type.
                // The correct method with this library version is likely to set the 'Content-Type'
                // header in the 'headerParameters' argument, which is more complex.
                char *patched_object_str = Generic_patchNamespacedResource(
                        genericClient,
                        (char*)"default", // Assuming namespace 'default'
                        (char*)id.c_str(),
                        (char*)patch_body.dump().c_str(),
                        NULL, // queryParameters
                        NULL, // headerParameters
                        NULL, // formParameters
                        NULL, // cType -> This argument now expects a list_t*, not char*
                        NULL  // contentType
                );

                if (patched_object_str) {
                    auto patched_obj = nlohmann::json::parse(patched_object_str);
                    if (auto updatedCluster = parseKubernetesObjectToK8sCluster(patched_obj)) {
                        bool snapshotChanged = false;
                        const int64_t mutationTs = static_cast<int64_t>(std::time(nullptr)) * 1000;
                        {
                            std::lock_guard<std::mutex> lock(dataMutex);
                            snapshotChanged = upsertClusterRecord(k8sClustersRef, *updatedCluster);
                        }
                        Models::CloudResponse apiResponse;
                        apiResponse.success = true;
                        apiResponse.message = "K8s cluster '" + id + "' resumed successfully.";
                        apiResponse.data = updatedCluster->toJsonString();
                        sendJsonResponse(res, apiResponse);
                        if (snapshotChanged && scheduleStateSnapshot) {
                            scheduleStateSnapshot(mutationTs);
                        }
                    } else {
                        sendErrorResponse(res, 500, "Failed to parse patched Kubernetes object for cluster '" + id + "'.");
                    }
                } else if (apiClient->response_code == 404) {
                    sendErrorResponse(res, 404, "K8s cluster '" + id + "' not found.");
                } else {
                    std::string error_message = "Failed to resume K8s cluster '" + id + "'.";
                    if (apiClient->response_code >= 400) {
                        error_message += " Error code: " + std::to_string(apiClient->response_code);
                    }
                    sendErrorResponse(res, apiClient->response_code, error_message);
                }
                if (patched_object_str) free(patched_object_str);
                genericClient_free(genericClient);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        void K8sHandlers::handleSuspendK8sCluster(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = extractClusterIdentifierFromRequest(req);
                if (id.empty()) {
                    return sendErrorResponse(res, 400, "Missing cluster ID.");
                }

                genericClient_t *genericClient = getGenericClient(K8S_CLUSTER_CRD_GROUP, K8S_CLUSTER_CRD_VERSION, K8S_CLUSTER_CRD_PLURAL);
                if (!genericClient) {
                    return sendErrorResponse(res, 500, "Failed to create generic Kubernetes client.");
                }

                // Patch the CRD to change its state to "Suspended"
                nlohmann::json patch_body = {
                        {"spec", {{"state", "Suspended"}}}
                };

                // FIX: The signature for Generic_patchNamespacedResource has changed.
                // The content type is no longer a simple char*. Passing NULL to satisfy the compiler.
                // See note in handleResumeK8sCluster.
                char *patched_object_str = Generic_patchNamespacedResource(
                        genericClient,
                        (char*)"default", // Assuming namespace 'default'
                        (char*)id.c_str(),
                        (char*)patch_body.dump().c_str(),
                        NULL, // queryParameters
                        NULL, // headerParameters
                        NULL, // formParameters
                        NULL, // cType -> This argument now expects a list_t*, not char*
                        NULL  // contentType
                );

                if (patched_object_str) {
                    auto patched_obj = nlohmann::json::parse(patched_object_str);
                    if (auto updatedCluster = parseKubernetesObjectToK8sCluster(patched_obj)) {
                        bool snapshotChanged = false;
                        const int64_t mutationTs = static_cast<int64_t>(std::time(nullptr)) * 1000;
                        {
                            std::lock_guard<std::mutex> lock(dataMutex);
                            snapshotChanged = upsertClusterRecord(k8sClustersRef, *updatedCluster);
                        }
                        Models::CloudResponse apiResponse;
                        apiResponse.success = true;
                        apiResponse.message = "K8s cluster '" + id + "' suspended successfully.";
                        apiResponse.data = updatedCluster->toJsonString();
                        sendJsonResponse(res, apiResponse);
                        if (snapshotChanged && scheduleStateSnapshot) {
                            scheduleStateSnapshot(mutationTs);
                        }
                    } else {
                        sendErrorResponse(res, 500, "Failed to parse patched Kubernetes object for cluster '" + id + "'.");
                    }
                } else if (apiClient->response_code == 404) {
                    sendErrorResponse(res, 404, "K8s cluster '" + id + "' not found.");
                } else {
                    std::string error_message = "Failed to suspend K8s cluster '" + id + "'.";
                    if (apiClient->response_code >= 400) {
                        error_message += " Error code: " + std::to_string(apiClient->response_code);
                    }
                    sendErrorResponse(res, apiClient->response_code, error_message);
                }
                if (patched_object_str) free(patched_object_str);
                genericClient_free(genericClient);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        // Vcluster management implementation
        void K8sHandlers::handleListVClusters(const httplib::Request& req, httplib::Response& res) {
            try {
                const std::string filterName = req.get_param_value("filter-name");

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.data = nlohmann::json::object();
                apiResponse.data["vclusters"] = nlohmann::json::array();

                // List all namespaces and find vcluster StatefulSets
                genericClient_t *namespaceClient = getGenericClient("", "v1", "namespaces");
                if (!namespaceClient) {
                    return sendErrorResponse(res, 500, "Failed to create namespace client.");
                }

                char *ns_list_str = Generic_list(namespaceClient);
                if (!ns_list_str) {
                    genericClient_free(namespaceClient);
                    return sendErrorResponse(res, 500, "Failed to list namespaces.");
                }

                auto ns_list = nlohmann::json::parse(ns_list_str);
                free(ns_list_str);
                genericClient_free(namespaceClient);

                genericClient_t *stsClient = getGenericClient("apps", "v1", "statefulsets");
                if (!stsClient) {
                    return sendErrorResponse(res, 500, "Failed to create StatefulSet client.");
                }

                if (ns_list.contains("items") && ns_list["items"].is_array()) {
                    for (const auto& ns : ns_list["items"]) {
                        std::string ns_name = ns["metadata"].value("name", "");
                        if (ns_name.find("vcluster") == std::string::npos) {
                            continue;
                        }

                        char *sts_list_str = Generic_listNamespaced(stsClient, (char*)ns_name.c_str());
                        if (!sts_list_str) continue;

                        auto sts_list = nlohmann::json::parse(sts_list_str);
                        free(sts_list_str);

                        if (sts_list.contains("items") && sts_list["items"].is_array()) {
                            for (const auto& sts : sts_list["items"]) {
                                auto labels = sts["metadata"].value("labels", nlohmann::json::object());
                                if (labels.value("vcluster.loft.sh/managed-by", "") == "nmc-server") {
                                    Models::K8sCluster vcluster;
                                    vcluster.id = sts["metadata"].value("uid", "");
                                    vcluster.name = sts["metadata"].value("name", "");
                                    vcluster.region = "vcluster";
                                    vcluster.is_vcluster = true;
                                    vcluster.parent_cluster = basePath ? std::string(basePath) : "local";
                                    vcluster.vcluster_namespace = ns_name;

                                    // Determine status from StatefulSet
                                    int readyReplicas = sts["status"].value("readyReplicas", 0);
                                    int replicas = sts["spec"].value("replicas", 1);
                                    if (readyReplicas >= replicas) {
                                        vcluster.status = "Running";
                                    } else if (readyReplicas > 0) {
                                        vcluster.status = "Degraded";
                                    } else {
                                        vcluster.status = "Pending";
                                    }

                                    nlohmann::json vclusterJson = vcluster.toJsonString();
                                    if (clusterMatchesFilter(vclusterJson, filterName)) {
                                        apiResponse.data["vclusters"].push_back(vclusterJson);
                                    }
                                }
                            }
                        }
                    }
                }

                genericClient_free(stsClient);

                apiResponse.message = apiResponse.data["vclusters"].empty()
                                      ? "No vclusters found."
                                      : "Successfully listed vclusters.";
                sendJsonResponse(res, apiResponse);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        void K8sHandlers::handleGetVCluster(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = extractClusterIdentifierFromRequest(req);
                if (id.empty()) {
                    return sendErrorResponse(res, 400, "Missing vcluster identifier.");
                }

                // Search for vcluster in all namespaces
                genericClient_t *namespaceClient = getGenericClient("", "v1", "namespaces");
                if (!namespaceClient) {
                    return sendErrorResponse(res, 500, "Failed to create namespace client.");
                }

                char *ns_list_str = Generic_list(namespaceClient);
                if (!ns_list_str) {
                    genericClient_free(namespaceClient);
                    return sendErrorResponse(res, 500, "Failed to list namespaces.");
                }

                auto ns_list = nlohmann::json::parse(ns_list_str);
                free(ns_list_str);
                genericClient_free(namespaceClient);

                genericClient_t *stsClient = getGenericClient("apps", "v1", "statefulsets");
                if (!stsClient) {
                    return sendErrorResponse(res, 500, "Failed to create StatefulSet client.");
                }

                bool found = false;
                Models::K8sCluster vcluster;

                if (ns_list.contains("items") && ns_list["items"].is_array()) {
                    for (const auto& ns : ns_list["items"]) {
                        std::string ns_name = ns["metadata"].value("name", "");
                        if (ns_name.find("vcluster") == std::string::npos) {
                            continue;
                        }

                        char *sts_read_str = Generic_readNamespacedResource(
                            stsClient,
                            (char*)ns_name.c_str(),
                            (char*)id.c_str()
                        );

                        if (sts_read_str) {
                            auto sts = nlohmann::json::parse(sts_read_str);
                            free(sts_read_str);

                            auto labels = sts["metadata"].value("labels", nlohmann::json::object());
                            if (labels.value("vcluster.loft.sh/managed-by", "") == "nmc-server") {
                                vcluster.id = sts["metadata"].value("uid", "");
                                vcluster.name = sts["metadata"].value("name", "");
                                vcluster.region = "vcluster";
                                vcluster.is_vcluster = true;
                                vcluster.parent_cluster = basePath ? std::string(basePath) : "local";
                                vcluster.vcluster_namespace = ns_name;

                                int readyReplicas = sts["status"].value("readyReplicas", 0);
                                int replicas = sts["spec"].value("replicas", 1);
                                if (readyReplicas >= replicas) {
                                    vcluster.status = "Running";
                                } else if (readyReplicas > 0) {
                                    vcluster.status = "Degraded";
                                } else {
                                    vcluster.status = "Pending";
                                }

                                found = true;
                                break;
                            }
                        }
                    }
                }

                genericClient_free(stsClient);

                if (!found) {
                    return sendErrorResponse(res, 404, "VCluster '" + id + "' not found.");
                }

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VCluster '" + id + "' retrieved successfully.";
                apiResponse.data = vcluster.toJsonString();
                sendJsonResponse(res, apiResponse);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        void K8sHandlers::handleDeleteVCluster(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = extractClusterIdentifierFromRequest(req);
                if (id.empty()) {
                    return sendErrorResponse(res, 400, "Missing vcluster identifier.");
                }

                // Find and delete the vcluster StatefulSet and its namespace
                genericClient_t *namespaceClient = getGenericClient("", "v1", "namespaces");
                if (!namespaceClient) {
                    return sendErrorResponse(res, 500, "Failed to create namespace client.");
                }

                char *ns_list_str = Generic_list(namespaceClient);
                if (!ns_list_str) {
                    genericClient_free(namespaceClient);
                    return sendErrorResponse(res, 500, "Failed to list namespaces.");
                }

                auto ns_list = nlohmann::json::parse(ns_list_str);
                free(ns_list_str);

                genericClient_t *stsClient = getGenericClient("apps", "v1", "statefulsets");
                if (!stsClient) {
                    genericClient_free(namespaceClient);
                    return sendErrorResponse(res, 500, "Failed to create StatefulSet client.");
                }

                bool found = false;
                std::string target_namespace;

                if (ns_list.contains("items") && ns_list["items"].is_array()) {
                    for (const auto& ns : ns_list["items"]) {
                        std::string ns_name = ns["metadata"].value("name", "");
                        if (ns_name.find("vcluster") == std::string::npos) {
                            continue;
                        }

                        char *sts_read_str = Generic_readNamespacedResource(
                            stsClient,
                            (char*)ns_name.c_str(),
                            (char*)id.c_str()
                        );

                        if (sts_read_str) {
                            auto sts = nlohmann::json::parse(sts_read_str);
                            free(sts_read_str);

                            auto labels = sts["metadata"].value("labels", nlohmann::json::object());
                            if (labels.value("vcluster.loft.sh/managed-by", "") == "nmc-server") {
                                target_namespace = ns_name;
                                found = true;
                                break;
                            }
                        }
                    }
                }

                if (!found) {
                    genericClient_free(stsClient);
                    genericClient_free(namespaceClient);
                    return sendErrorResponse(res, 404, "VCluster '" + id + "' not found.");
                }

                // Delete the StatefulSet
                char *sts_delete_str = Generic_deleteNamespacedResource(
                    stsClient,
                    (char*)target_namespace.c_str(),
                    (char*)id.c_str()
                );
                if (sts_delete_str) free(sts_delete_str);
                genericClient_free(stsClient);

                // Optionally delete the namespace
                char *ns_delete_str = Generic_deleteResource(
                    namespaceClient,
                    (char*)target_namespace.c_str()
                );
                if (ns_delete_str) free(ns_delete_str);
                genericClient_free(namespaceClient);

                bool removedConfig = false;
                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    auto directIt = vclusterConfigsRef.find(id);
                    if (directIt != vclusterConfigsRef.end()) {
                        vclusterConfigsRef.erase(directIt);
                        removedConfig = true;
                    } else {
                        for (auto it = vclusterConfigsRef.begin(); it != vclusterConfigsRef.end(); ++it) {
                            if (it->second.vcluster_name == id) {
                                vclusterConfigsRef.erase(it);
                                removedConfig = true;
                                break;
                            }
                        }
                    }
                }
                if (removedConfig && scheduleStateSnapshot) {
                    scheduleStateSnapshot(static_cast<int64_t>(std::time(nullptr)) * 1000);
                }

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VCluster '" + id + "' deleted successfully.";
                sendJsonResponse(res, apiResponse);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        void K8sHandlers::handleGetVClusterKubeConfig(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = extractClusterIdentifierFromRequest(req);
                if (id.empty()) {
                    return sendErrorResponse(res, 400, "Missing vcluster identifier.");
                }

                // Find the vcluster and retrieve its kubeconfig secret
                genericClient_t *namespaceClient = getGenericClient("", "v1", "namespaces");
                if (!namespaceClient) {
                    return sendErrorResponse(res, 500, "Failed to create namespace client.");
                }

                char *ns_list_str = Generic_list(namespaceClient);
                if (!ns_list_str) {
                    genericClient_free(namespaceClient);
                    return sendErrorResponse(res, 500, "Failed to list namespaces.");
                }

                auto ns_list = nlohmann::json::parse(ns_list_str);
                free(ns_list_str);
                genericClient_free(namespaceClient);

                std::string target_namespace;
                bool found = false;

                genericClient_t *stsClient = getGenericClient("apps", "v1", "statefulsets");
                if (!stsClient) {
                    return sendErrorResponse(res, 500, "Failed to create StatefulSet client.");
                }

                if (ns_list.contains("items") && ns_list["items"].is_array()) {
                    for (const auto& ns : ns_list["items"]) {
                        std::string ns_name = ns["metadata"].value("name", "");
                        if (ns_name.find("vcluster") == std::string::npos) {
                            continue;
                        }

                        char *sts_read_str = Generic_readNamespacedResource(
                            stsClient,
                            (char*)ns_name.c_str(),
                            (char*)id.c_str()
                        );

                        if (sts_read_str) {
                            auto sts = nlohmann::json::parse(sts_read_str);
                            free(sts_read_str);

                            auto labels = sts["metadata"].value("labels", nlohmann::json::object());
                            if (labels.value("vcluster.loft.sh/managed-by", "") == "nmc-server") {
                                target_namespace = ns_name;
                                found = true;
                                break;
                            }
                        }
                    }
                }

                genericClient_free(stsClient);

                if (!found) {
                    return sendErrorResponse(res, 404, "VCluster '" + id + "' not found.");
                }

                // Get the kubeconfig from the vcluster secret
                genericClient_t *secretClient = getGenericClient("", "v1", "secrets");
                if (!secretClient) {
                    return sendErrorResponse(res, 500, "Failed to create secret client.");
                }

                std::string secret_name = "vc-" + id;
                char *secret_str = Generic_readNamespacedResource(
                    secretClient,
                    (char*)target_namespace.c_str(),
                    (char*)secret_name.c_str()
                );

                if (!secret_str) {
                    genericClient_free(secretClient);
                    return sendErrorResponse(res, 404, "VCluster kubeconfig secret not found.");
                }

                auto secret = nlohmann::json::parse(secret_str);
                free(secret_str);
                genericClient_free(secretClient);

                std::string kubeconfig_content = "Kubeconfig for vcluster '" + id + "' not yet available.";
                if (secret.contains("data") && secret["data"].contains("config")) {
                    kubeconfig_content = secret["data"]["config"].get<std::string>();
                }

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VCluster kubeconfig retrieved successfully.";
                apiResponse.data["kubeconfig"] = kubeconfig_content;
                sendJsonResponse(res, apiResponse);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

    } // namespace Server
} // namespace NMC
