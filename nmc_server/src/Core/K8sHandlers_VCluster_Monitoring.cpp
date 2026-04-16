// K8sHandlers_VCluster_Monitoring.cpp
// Phase 3: Monitoring, Metrics, Health Checks, and Configuration Management

#include "K8sHandlers.h"
#include "../Models/VClusterConfig.h"
#include "Utils.h"
#include <ctime>
#include <sstream>

namespace NMC {
    namespace Server {

        // ============================================================================
        // GET VCLUSTER METRICS
        // ============================================================================

        void K8sHandlers::handleGetVClusterMetrics(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = extractClusterIdentifierFromRequest(req);
                if (id.empty()) {
                    return sendErrorResponse(res, 400, "Missing vcluster identifier.");
                }

                // Find the vcluster namespace and pods
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

                // Find vcluster namespace
                if (ns_list.contains("items") && ns_list["items"].is_array()) {
                    for (const auto& ns : ns_list["items"]) {
                        std::string ns_name = ns["metadata"].value("name", "");
                        if (ns_name.find("vcluster") == std::string::npos) {
                            continue;
                        }

                        // Check if this namespace contains our vcluster
                        genericClient_t *stsClient = getGenericClient("apps", "v1", "statefulsets");
                        if (!stsClient) continue;

                        char *sts_read_str = Generic_readNamespacedResource(
                            stsClient,
                            (char*)ns_name.c_str(),
                            (char*)id.c_str()
                        );

                        if (sts_read_str) {
                            auto sts = nlohmann::json::parse(sts_read_str);
                            free(sts_read_str);
                            genericClient_free(stsClient);

                            auto labels = sts["metadata"].value("labels", nlohmann::json::object());
                            if (labels.value("vcluster.loft.sh/managed-by", "") == "nmc-server") {
                                target_namespace = ns_name;
                                found = true;
                                break;
                            }
                        }
                        genericClient_free(stsClient);
                    }
                }

                if (!found) {
                    return sendErrorResponse(res, 404, "VCluster '" + id + "' not found.");
                }

                // Get pods in the namespace
                genericClient_t *podClient = getGenericClient("", "v1", "pods");
                if (!podClient) {
                    return sendErrorResponse(res, 500, "Failed to create Pod client.");
                }

                char *pod_list_str = Generic_listNamespaced(podClient, (char*)target_namespace.c_str());
                if (!pod_list_str) {
                    genericClient_free(podClient);
                    return sendErrorResponse(res, 500, "Failed to list pods.");
                }

                auto pod_list = nlohmann::json::parse(pod_list_str);
                free(pod_list_str);
                genericClient_free(podClient);

                // Collect metrics from pods
                nlohmann::json metrics = {
                    {"namespace", target_namespace},
                    {"vcluster_id", id},
                    {"timestamp", std::time(nullptr)},
                    {"pods", nlohmann::json::array()},
                    {"resource_usage", {
                        {"cpu_requests", "0m"},
                        {"cpu_limits", "0m"},
                        {"memory_requests", "0Mi"},
                        {"memory_limits", "0Mi"}
                    }},
                    {"status_summary", {
                        {"running", 0},
                        {"pending", 0},
                        {"failed", 0},
                        {"unknown", 0}
                    }}
                };

                if (pod_list.contains("items") && pod_list["items"].is_array()) {
                    for (const auto& pod : pod_list["items"]) {
                        std::string pod_name = pod["metadata"].value("name", "");
                        std::string pod_phase = pod["status"].value("phase", "Unknown");

                        nlohmann::json pod_metrics = {
                            {"name", pod_name},
                            {"phase", pod_phase},
                            {"node", pod["spec"].value("nodeName", "")},
                            {"start_time", pod["status"].value("startTime", "")}
                        };

                        // Extract resource requests/limits
                        if (pod.contains("spec") && pod["spec"].contains("containers")) {
                            for (const auto& container : pod["spec"]["containers"]) {
                                if (container.contains("resources")) {
                                    pod_metrics["resources"] = container["resources"];
                                }
                            }
                        }

                        // Extract container status
                        if (pod.contains("status") && pod["status"].contains("containerStatuses")) {
                            nlohmann::json container_statuses = nlohmann::json::array();
                            for (const auto& cs : pod["status"]["containerStatuses"]) {
                                container_statuses.push_back({
                                    {"name", cs.value("name", "")},
                                    {"ready", cs.value("ready", false)},
                                    {"restart_count", cs.value("restartCount", 0)},
                                    {"state", cs.value("state", nlohmann::json::object())}
                                });
                            }
                            pod_metrics["container_statuses"] = container_statuses;
                        }

                        metrics["pods"].push_back(pod_metrics);

                        // Update status summary
                        if (pod_phase == "Running") {
                            metrics["status_summary"]["running"] = metrics["status_summary"]["running"].get<int>() + 1;
                        } else if (pod_phase == "Pending") {
                            metrics["status_summary"]["pending"] = metrics["status_summary"]["pending"].get<int>() + 1;
                        } else if (pod_phase == "Failed") {
                            metrics["status_summary"]["failed"] = metrics["status_summary"]["failed"].get<int>() + 1;
                        } else {
                            metrics["status_summary"]["unknown"] = metrics["status_summary"]["unknown"].get<int>() + 1;
                        }
                    }
                }

                // TODO: Query actual metrics from Prometheus/metrics-server if available
                // For now, return pod-level metrics

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VCluster metrics retrieved successfully.";
                apiResponse.data = metrics;
                sendJsonResponse(res, apiResponse);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        // ============================================================================
        // GET VCLUSTER HEALTH
        // ============================================================================

        void K8sHandlers::handleGetVClusterHealth(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = extractClusterIdentifierFromRequest(req);
                if (id.empty()) {
                    return sendErrorResponse(res, 400, "Missing vcluster identifier.");
                }

                // Find the vcluster StatefulSet
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
                std::string target_namespace;
                nlohmann::json sts_data;

                // Find the vcluster
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
                            sts_data = nlohmann::json::parse(sts_read_str);
                            free(sts_read_str);

                            auto labels = sts_data["metadata"].value("labels", nlohmann::json::object());
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

                // Analyze health
                int desired_replicas = sts_data["spec"].value("replicas", 0);
                int ready_replicas = sts_data["status"].value("readyReplicas", 0);
                int current_replicas = sts_data["status"].value("currentReplicas", 0);
                int updated_replicas = sts_data["status"].value("updatedReplicas", 0);

                std::string health_status = "Unknown";
                std::string health_message = "";
                std::vector<std::string> health_issues;

                if (desired_replicas == 0) {
                    health_status = "Paused";
                    health_message = "VCluster is paused (0 replicas).";
                } else if (ready_replicas == desired_replicas) {
                    health_status = "Healthy";
                    health_message = "All replicas are ready and running.";
                } else if (ready_replicas > 0) {
                    health_status = "Degraded";
                    health_message = "Some replicas are not ready.";
                    health_issues.push_back("Ready replicas: " + std::to_string(ready_replicas) + "/" + std::to_string(desired_replicas));
                } else {
                    health_status = "Unhealthy";
                    health_message = "No replicas are ready.";
                    health_issues.push_back("No ready replicas");
                }

                // Check for update in progress
                if (updated_replicas < desired_replicas && updated_replicas > 0) {
                    health_issues.push_back("Rolling update in progress: " + std::to_string(updated_replicas) + "/" + std::to_string(desired_replicas) + " updated");
                }

                nlohmann::json health_data = {
                    {"vcluster_id", id},
                    {"namespace", target_namespace},
                    {"status", health_status},
                    {"message", health_message},
                    {"timestamp", std::time(nullptr)},
                    {"replicas", {
                        {"desired", desired_replicas},
                        {"current", current_replicas},
                        {"ready", ready_replicas},
                        {"updated", updated_replicas}
                    }},
                    {"issues", health_issues},
                    {"checks", {
                        {"api_server_reachable", ready_replicas > 0},
                        {"all_replicas_ready", ready_replicas == desired_replicas},
                        {"no_pending_updates", updated_replicas == desired_replicas}
                    }}
                };

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VCluster health check completed.";
                apiResponse.data = health_data;
                sendJsonResponse(res, apiResponse);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        // ============================================================================
        // GET VCLUSTER RESOURCES
        // ============================================================================

        void K8sHandlers::handleGetVClusterResources(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = extractClusterIdentifierFromRequest(req);
                if (id.empty()) {
                    return sendErrorResponse(res, 400, "Missing vcluster identifier.");
                }

                // Find vcluster namespace
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

                // Find the vcluster namespace
                if (ns_list.contains("items") && ns_list["items"].is_array()) {
                    for (const auto& ns : ns_list["items"]) {
                        std::string ns_name = ns["metadata"].value("name", "");
                        if (ns_name.find("vcluster") == std::string::npos) {
                            continue;
                        }

                        // Check if this namespace contains our vcluster
                        genericClient_t *stsClient = getGenericClient("apps", "v1", "statefulsets");
                        if (!stsClient) continue;

                        char *sts_read_str = Generic_readNamespacedResource(
                            stsClient,
                            (char*)ns_name.c_str(),
                            (char*)id.c_str()
                        );

                        if (sts_read_str) {
                            free(sts_read_str);
                            genericClient_free(stsClient);
                            target_namespace = ns_name;
                            found = true;
                            break;
                        }
                        genericClient_free(stsClient);
                    }
                }

                if (!found) {
                    return sendErrorResponse(res, 404, "VCluster '" + id + "' not found.");
                }

                // Get all resources in the namespace
                nlohmann::json resources = {
                    {"namespace", target_namespace},
                    {"vcluster_id", id},
                    {"timestamp", std::time(nullptr)},
                    {"pods", nlohmann::json::array()},
                    {"services", nlohmann::json::array()},
                    {"configmaps", nlohmann::json::array()},
                    {"secrets", nlohmann::json::array()},
                    {"pvcs", nlohmann::json::array()},
                    {"statefulsets", nlohmann::json::array()}
                };

                // Get Pods
                genericClient_t *podClient = getGenericClient("", "v1", "pods");
                if (podClient) {
                    char *pod_list_str = Generic_listNamespaced(podClient, (char*)target_namespace.c_str());
                    if (pod_list_str) {
                        auto pod_list = nlohmann::json::parse(pod_list_str);
                        if (pod_list.contains("items")) {
                            for (const auto& item : pod_list["items"]) {
                                resources["pods"].push_back({
                                    {"name", item["metadata"].value("name", "")},
                                    {"phase", item["status"].value("phase", "Unknown")}
                                });
                            }
                        }
                        free(pod_list_str);
                    }
                    genericClient_free(podClient);
                }

                // Get Services
                genericClient_t *svcClient = getGenericClient("", "v1", "services");
                if (svcClient) {
                    char *svc_list_str = Generic_listNamespaced(svcClient, (char*)target_namespace.c_str());
                    if (svc_list_str) {
                        auto svc_list = nlohmann::json::parse(svc_list_str);
                        if (svc_list.contains("items")) {
                            for (const auto& item : svc_list["items"]) {
                                resources["services"].push_back({
                                    {"name", item["metadata"].value("name", "")},
                                    {"type", item["spec"].value("type", "ClusterIP")},
                                    {"cluster_ip", item["spec"].value("clusterIP", "")}
                                });
                            }
                        }
                        free(svc_list_str);
                    }
                    genericClient_free(svcClient);
                }

                // Get PVCs
                genericClient_t *pvcClient = getGenericClient("", "v1", "persistentvolumeclaims");
                if (pvcClient) {
                    char *pvc_list_str = Generic_listNamespaced(pvcClient, (char*)target_namespace.c_str());
                    if (pvc_list_str) {
                        auto pvc_list = nlohmann::json::parse(pvc_list_str);
                        if (pvc_list.contains("items")) {
                            for (const auto& item : pvc_list["items"]) {
                                resources["pvcs"].push_back({
                                    {"name", item["metadata"].value("name", "")},
                                    {"status", item["status"].value("phase", "Unknown")},
                                    {"storage", item["spec"]["resources"]["requests"].value("storage", "")}
                                });
                            }
                        }
                        free(pvc_list_str);
                    }
                    genericClient_free(pvcClient);
                }

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VCluster resources retrieved successfully.";
                apiResponse.data = resources;
                sendJsonResponse(res, apiResponse);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        // ============================================================================
        // GET VCLUSTER CONFIG
        // ============================================================================

        void K8sHandlers::handleGetVClusterConfig(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = extractClusterIdentifierFromRequest(req);
                if (id.empty()) {
                    return sendErrorResponse(res, 400, "Missing vcluster identifier.");
                }

                // Find config by vcluster name or config ID
                Models::VClusterConfig config;
                bool found = false;

                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    // Try direct lookup by config ID
                    if (vclusterConfigsRef.find(id) != vclusterConfigsRef.end()) {
                        config = vclusterConfigsRef[id];
                        found = true;
                    } else {
                        // Search by vcluster name
                        for (const auto& [cfg_id, cfg] : vclusterConfigsRef) {
                            if (cfg.vcluster_name == id) {
                                config = cfg;
                                found = true;
                                break;
                            }
                        }
                    }
                }

                if (!found) {
                    return sendErrorResponse(res, 404, "VCluster configuration not found for '" + id + "'.");
                }

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VCluster configuration retrieved successfully.";
                apiResponse.data = config.toJson();
                sendJsonResponse(res, apiResponse);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        // ============================================================================
        // UPDATE VCLUSTER CONFIG
        // ============================================================================

        void K8sHandlers::handleUpdateVClusterConfig(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = extractClusterIdentifierFromRequest(req);
                if (id.empty()) {
                    return sendErrorResponse(res, 400, "Missing vcluster identifier.");
                }

                auto json_body = nlohmann::json::parse(req.body);
                Models::VClusterConfig new_config = Models::VClusterConfig::fromJson(json_body);

                // Find existing config
                std::string config_id;
                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    // Try direct lookup
                    if (vclusterConfigsRef.find(id) != vclusterConfigsRef.end()) {
                        config_id = id;
                    } else {
                        // Search by vcluster name
                        for (const auto& [cfg_id, cfg] : vclusterConfigsRef) {
                            if (cfg.vcluster_name == id) {
                                config_id = cfg_id;
                                break;
                            }
                        }
                    }
                }

                if (config_id.empty()) {
                    return sendErrorResponse(res, 404, "VCluster configuration not found for '" + id + "'.");
                }

                // Preserve ID and name
                new_config.id = config_id;
                if (new_config.vcluster_name.empty()) {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    new_config.vcluster_name = vclusterConfigsRef[config_id].vcluster_name;
                }

                // Update configuration
                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    vclusterConfigsRef[config_id] = new_config;
                }
                if (scheduleStateSnapshot) {
                    scheduleStateSnapshot(static_cast<int64_t>(std::time(nullptr)) * 1000);
                }

                // TODO: Apply configuration changes to running vcluster
                // This would involve patching the StatefulSet with new resource limits,
                // scaling replicas, updating network config, etc.

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VCluster configuration updated successfully. Note: Some changes may require vcluster restart.";
                apiResponse.data = new_config.toJson();
                sendJsonResponse(res, apiResponse);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

    } // namespace Server
} // namespace NMC
