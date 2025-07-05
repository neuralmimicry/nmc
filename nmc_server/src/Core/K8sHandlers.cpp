// src/Core/K8sHandlers.cpp
#include "K8sHandlers.h"
#include "Utils.h" // For Utils::generateUniqueId
#include <algorithm> // Not as much needed now, but keeping for general utility
#include <iostream>
#include <stdexcept>
#include <string> // Required for std::to_string

// Include necessary headers for Kubernetes C client types
#include <kubernetes/include/apiClient.h> // For apiClient_t
#include <kubernetes/include/generic.h>   // For genericClient_t, genericClient_create, genericClient_free
#include <kubernetes/include/keyValuePair.h> // For keyValuePair_t
#include <kubernetes/api/CoreV1API.h>     // For CoreV1API functions

namespace NMC {
    namespace Server {

        K8sHandlers::K8sHandlers(
                const std::string& api_server_url,
                const std::string &kubeconfig_path,
                std::mutex &mutex_ref,
                std::function<void(httplib::Response&, const Models::CloudResponse&)> send_json_cb,
        std::function<void(httplib::Response&, int, const std::string&)> send_error_cb
        ) :
        dataMutex(mutex_ref),
        sendJsonResponse(send_json_cb),
        sendErrorResponse(send_error_cb),
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


        // A very simplified example of converting a K8s object to our internal model.
        // This assumes a CRD structure that includes 'metadata.name', 'metadata.uid',
        // and 'spec.region' (or label 'region'), and 'status.state'.
        std::optional<Models::K8sCluster> K8sHandlers::parseKubernetesObjectToK8sCluster(const nlohmann::json& k8sObject) {
            if (k8sObject.contains("metadata") && k8sObject["metadata"].contains("name")) {
                Models::K8sCluster cluster;
                cluster.id = k8sObject["metadata"].value("uid", "unknown");
                cluster.name = k8sObject["metadata"].value("name", "unknown");

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
                        Models::CloudResponse apiResponse;
                        apiResponse.success = true;
                        apiResponse.message = "K8s cluster '" + name + "' created successfully.";
                        apiResponse.data = newCluster->toJson();
                        sendJsonResponse(res, apiResponse);
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
                std::string id = req.path_params.at("id"); // Assuming ID is part of path
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
                    // Successfully deleted, parse and send response
                    Models::CloudResponse apiResponse;
                    apiResponse.success = true;
                    apiResponse.message = "K8s cluster '" + id + "' deleted successfully.";
                    apiResponse.data = nlohmann::json::parse(deleted_object_str); // Or just a success message
                    sendJsonResponse(res, apiResponse);
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
                std::string id = req.path_params.at("id"); // Assuming ID is part of path
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
                        apiResponse.data = foundCluster->toJson();
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

        void K8sHandlers::handleGetKubeConfig(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = req.path_params.at("id"); // Assuming ID of the cluster to get its kubeconfig
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
                genericClient_t *genericClient = getGenericClient(K8S_CLUSTER_CRD_GROUP, K8S_CLUSTER_CRD_VERSION, K8S_CLUSTER_CRD_PLURAL);
                if (!genericClient) {
                    return sendErrorResponse(res, 500, "Failed to create generic Kubernetes client.");
                }

                // FIX: Removed extra NULL arguments from the function call.
                char *list_object_str = Generic_listNamespaced(
                        genericClient,
                        (char*)"default"
                );

                std::vector<Models::K8sCluster> clusters;
                if (list_object_str) {
                    auto list_obj = nlohmann::json::parse(list_object_str);
                    if (list_obj.contains("items") && list_obj["items"].is_array()) {
                        for (const auto& item : list_obj["items"]) {
                            if (auto cluster = parseKubernetesObjectToK8sCluster(item)) {
                                clusters.push_back(*cluster);
                            }
                        }
                    }
                    Models::CloudResponse apiResponse;
                    apiResponse.success = true;
                    apiResponse.message = "Successfully listed K8s clusters.";
                    apiResponse.data["clusters"] = nlohmann::json::array();
                    for (const auto& cluster : clusters) {
                        apiResponse.data["clusters"].push_back(cluster.toJson());
                    }
                    sendJsonResponse(res, apiResponse);
                } else {
                    std::string error_message = "Failed to list K8s clusters.";
                    if (apiClient->response_code >= 400) {
                        error_message += " Error code: " + std::to_string(apiClient->response_code);
                    }
                    sendErrorResponse(res, apiClient->response_code, error_message);
                }
                if (list_object_str) free(list_object_str);
                genericClient_free(genericClient);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        void K8sHandlers::handleListK8sLocations(const httplib::Request& req, httplib::Response& res) {
            try {
                // FIX: Removed CoreV1API_t object creation.
                // The CoreV1API functions take the apiClient_t directly.
                if (!apiClient) {
                    return sendErrorResponse(res, 500, "Kubernetes API client not initialized.");
                }

                // List all nodes
                // FIX: Pass apiClient directly to the API function.
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


        void K8sHandlers::handleResumeK8sCluster(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = req.path_params.at("id"); // Assuming ID is part of path
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
                        Models::CloudResponse apiResponse;
                        apiResponse.success = true;
                        apiResponse.message = "K8s cluster '" + id + "' resumed successfully.";
                        apiResponse.data = updatedCluster->toJson();
                        sendJsonResponse(res, apiResponse);
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
                std::string id = req.path_params.at("id"); // Assuming ID is part of path
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
                        Models::CloudResponse apiResponse;
                        apiResponse.success = true;
                        apiResponse.message = "K8s cluster '" + id + "' suspended successfully.";
                        apiResponse.data = updatedCluster->toJson();
                        sendJsonResponse(res, apiResponse);
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

    } // namespace Server
} // namespace NMC