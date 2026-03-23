// K8sHandlers_VCluster_Lifecycle.cpp
// Phase 2: Lifecycle Management Handlers
// Pause, Resume, Backup, Restore, Upgrade operations

#include "K8sHandlers.h"
#include "../Models/VClusterConfig.h"
#include "Utils.h"
#include <sstream>

namespace NMC {
    namespace Server {

        // ============================================================================
        // PAUSE VCLUSTER
        // ============================================================================

        void K8sHandlers::handlePauseVCluster(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = extractClusterIdentifierFromRequest(req);
                if (id.empty()) {
                    return sendErrorResponse(res, 400, "Missing vcluster identifier.");
                }

                // Find the vcluster
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
                std::string vcluster_name;
                int current_replicas = 0;

                // Find the vcluster StatefulSet
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
                                vcluster_name = sts["metadata"].value("name", "");
                                current_replicas = sts["spec"].value("replicas", 1);
                                found = true;
                                break;
                            }
                        }
                    }
                }

                if (!found) {
                    genericClient_free(stsClient);
                    return sendErrorResponse(res, 404, "VCluster '" + id + "' not found.");
                }

                // Check if already paused
                if (current_replicas == 0) {
                    genericClient_free(stsClient);
                    Models::CloudResponse apiResponse;
                    apiResponse.success = true;
                    apiResponse.message = "VCluster '" + id + "' is already paused.";
                    apiResponse.data = {{"status", "Paused"}};
                    return sendJsonResponse(res, apiResponse);
                }

                // Create annotation to store original replica count
                nlohmann::json patchObj = {
                    {"metadata", {
                        {"annotations", {
                            {"vcluster.nmc/original-replicas", std::to_string(current_replicas)},
                            {"vcluster.nmc/paused-at", std::to_string(std::time(nullptr))}
                        }}
                    }},
                    {"spec", {
                        {"replicas", 0}
                    }}
                };

                // Patch the StatefulSet (scale to 0)
                char *patch_result = Generic_patchNamespacedResource(
                    stsClient,
                    (char*)target_namespace.c_str(),
                    (char*)vcluster_name.c_str(),
                    (char*)patchObj.dump().c_str(),
                    nullptr,  // queryParameters
                    nullptr,  // headerParameters
                    nullptr,  // formParameters
                    nullptr,  // headerType
                    nullptr   // contentType
                );

                if (!patch_result) {
                    genericClient_free(stsClient);
                    return sendErrorResponse(res, 500, "Failed to pause vcluster: " + std::to_string(apiClient->response_code));
                }

                free(patch_result);
                genericClient_free(stsClient);

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VCluster '" + id + "' paused successfully.";
                apiResponse.data = {
                    {"status", "Paused"},
                    {"original_replicas", current_replicas},
                    {"namespace", target_namespace}
                };
                sendJsonResponse(res, apiResponse);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        // ============================================================================
        // RESUME VCLUSTER
        // ============================================================================

        void K8sHandlers::handleResumeVCluster(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = extractClusterIdentifierFromRequest(req);
                if (id.empty()) {
                    return sendErrorResponse(res, 400, "Missing vcluster identifier.");
                }

                // Find the vcluster
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
                std::string vcluster_name;
                int original_replicas = 1;
                int current_replicas = 0;

                // Find the vcluster StatefulSet
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
                                vcluster_name = sts["metadata"].value("name", "");
                                current_replicas = sts["spec"].value("replicas", 0);

                                // Get original replica count from annotation
                                auto annotations = sts["metadata"].value("annotations", nlohmann::json::object());
                                std::string orig_str = annotations.value("vcluster.nmc/original-replicas", "1");
                                try {
                                    original_replicas = std::stoi(orig_str);
                                } catch (...) {
                                    original_replicas = 1;
                                }

                                found = true;
                                break;
                            }
                        }
                    }
                }

                if (!found) {
                    genericClient_free(stsClient);
                    return sendErrorResponse(res, 404, "VCluster '" + id + "' not found.");
                }

                // Check if already running
                if (current_replicas > 0) {
                    genericClient_free(stsClient);
                    Models::CloudResponse apiResponse;
                    apiResponse.success = true;
                    apiResponse.message = "VCluster '" + id + "' is already running.";
                    apiResponse.data = {
                        {"status", "Running"},
                        {"replicas", current_replicas}
                    };
                    return sendJsonResponse(res, apiResponse);
                }

                // Patch the StatefulSet (restore replicas)
                nlohmann::json patchObj = {
                    {"metadata", {
                        {"annotations", {
                            {"vcluster.nmc/resumed-at", std::to_string(std::time(nullptr))}
                        }}
                    }},
                    {"spec", {
                        {"replicas", original_replicas}
                    }}
                };

                char *patch_result = Generic_patchNamespacedResource(
                    stsClient,
                    (char*)target_namespace.c_str(),
                    (char*)vcluster_name.c_str(),
                    (char*)patchObj.dump().c_str(),
                    nullptr,  // queryParameters
                    nullptr,  // headerParameters
                    nullptr,  // formParameters
                    nullptr,  // headerType
                    nullptr   // contentType
                );

                if (!patch_result) {
                    genericClient_free(stsClient);
                    return sendErrorResponse(res, 500, "Failed to resume vcluster: " + std::to_string(apiClient->response_code));
                }

                free(patch_result);
                genericClient_free(stsClient);

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VCluster '" + id + "' resumed successfully.";
                apiResponse.data = {
                    {"status", "Resuming"},
                    {"replicas", original_replicas},
                    {"namespace", target_namespace}
                };
                sendJsonResponse(res, apiResponse);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        // ============================================================================
        // BACKUP VCLUSTER
        // ============================================================================

        void K8sHandlers::handleBackupVCluster(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = extractClusterIdentifierFromRequest(req);
                if (id.empty()) {
                    return sendErrorResponse(res, 400, "Missing vcluster identifier.");
                }

                auto json_body = nlohmann::json::parse(req.body);
                std::string backup_name = json_body.value("backup_name", id + "-backup-" + std::to_string(std::time(nullptr)));

                // Find vcluster and get its configuration
                std::string config_id;
                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    // Search for vcluster with matching ID to get config_id
                    // In production, this would query from a persistent store
                    for (const auto& [cfg_id, cfg] : vclusterConfigsRef) {
                        if (cfg.vcluster_name == id) {
                            config_id = cfg_id;
                            break;
                        }
                    }
                }

                if (config_id.empty()) {
                    return sendErrorResponse(res, 404, "VCluster configuration not found for '" + id + "'.");
                }

                // Get the VClusterConfig
                Models::VClusterConfig config;
                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    if (vclusterConfigsRef.find(config_id) == vclusterConfigsRef.end()) {
                        return sendErrorResponse(res, 404, "VCluster configuration not found.");
                    }
                    config = vclusterConfigsRef[config_id];
                }

                // Create backup ConfigMap with vcluster config and metadata
                nlohmann::json backupData = {
                    {"backup_name", backup_name},
                    {"vcluster_id", id},
                    {"vcluster_name", config.vcluster_name},
                    {"config_id", config_id},
                    {"config", config.toJson()},
                    {"backup_timestamp", std::time(nullptr)},
                    {"backup_version", "1.0"}
                };

                nlohmann::json configMapObj = {
                    {"apiVersion", "v1"},
                    {"kind", "ConfigMap"},
                    {"metadata", {
                        {"name", backup_name},
                        {"namespace", "vcluster-backups"},
                        {"labels", {
                            {"vcluster.nmc/backup", "true"},
                            {"vcluster.nmc/source-id", id}
                        }}
                    }},
                    {"data", {
                        {"backup.json", backupData.dump(2)}
                    }}
                };

                // Create backup namespace if it doesn't exist
                nlohmann::json backupNsObj = {
                    {"apiVersion", "v1"},
                    {"kind", "Namespace"},
                    {"metadata", {{"name", "vcluster-backups"}}}
                };

                genericClient_t *nsClient = getGenericClient("", "v1", "namespaces");
                if (nsClient) {
                    char *ns_result = Generic_createResource(nsClient, (char*)backupNsObj.dump().c_str());
                    if (ns_result) free(ns_result);
                    genericClient_free(nsClient);
                }

                // Create backup ConfigMap
                genericClient_t *cmClient = getGenericClient("", "v1", "configmaps");
                if (!cmClient) {
                    return sendErrorResponse(res, 500, "Failed to create ConfigMap client.");
                }

                char *cm_result = Generic_createNamespacedResource(
                    cmClient,
                    (char*)"vcluster-backups",
                    (char*)configMapObj.dump().c_str()
                );

                if (!cm_result) {
                    genericClient_free(cmClient);
                    return sendErrorResponse(res, 500, "Failed to create backup: " + std::to_string(apiClient->response_code));
                }

                free(cm_result);
                genericClient_free(cmClient);

                // TODO: In production, also snapshot PVCs using VolumeSnapshot API

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VCluster '" + id + "' backed up successfully as '" + backup_name + "'.";
                apiResponse.data = {
                    {"backup_name", backup_name},
                    {"vcluster_id", id},
                    {"timestamp", std::time(nullptr)},
                    {"location", "ConfigMap: vcluster-backups/" + backup_name}
                };
                sendJsonResponse(res, apiResponse);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        // ============================================================================
        // RESTORE VCLUSTER
        // ============================================================================

        void K8sHandlers::handleRestoreVCluster(const httplib::Request& req, httplib::Response& res) {
            try {
                auto json_body = nlohmann::json::parse(req.body);
                std::string backup_name = json_body.value("backup_name", "");
                std::string new_name = json_body.value("new_name", "");

                if (backup_name.empty()) {
                    return sendErrorResponse(res, 400, "Missing required parameter: backup_name.");
                }

                // Retrieve backup ConfigMap
                genericClient_t *cmClient = getGenericClient("", "v1", "configmaps");
                if (!cmClient) {
                    return sendErrorResponse(res, 500, "Failed to create ConfigMap client.");
                }

                char *cm_read_str = Generic_readNamespacedResource(
                    cmClient,
                    (char*)"vcluster-backups",
                    (char*)backup_name.c_str()
                );

                if (!cm_read_str) {
                    genericClient_free(cmClient);
                    return sendErrorResponse(res, 404, "Backup '" + backup_name + "' not found.");
                }

                auto cm = nlohmann::json::parse(cm_read_str);
                free(cm_read_str);
                genericClient_free(cmClient);

                // Parse backup data
                std::string backup_json_str = cm["data"].value("backup.json", "{}");
                auto backup_data = nlohmann::json::parse(backup_json_str);

                // Extract config
                Models::VClusterConfig config = Models::VClusterConfig::fromJson(backup_data.value("config", nlohmann::json::object()));

                // Use new name if provided
                if (!new_name.empty()) {
                    config.vcluster_name = new_name;
                }

                // Generate new ID for restored vcluster
                config.id = Utils::generateUniqueId("vc");

                // Create the vcluster using the restored config
                nlohmann::json create_request = {
                    {"name", config.vcluster_name},
                    {"namespace", "vcluster-" + config.vcluster_name},
                    {"config", config.toJson()}
                };

                // Simulate create request
                httplib::Request create_req;
                create_req.body = create_request.dump();

                handleCreateVCluster(create_req, res);

                // If successful, add restore metadata to response
                if (res.status >= 200 && res.status < 300) {
                    try {
                        auto response_data = nlohmann::json::parse(res.body);
                        if (response_data.contains("data")) {
                            response_data["data"]["restored_from"] = backup_name;
                            response_data["data"]["restore_timestamp"] = std::time(nullptr);
                            response_data["message"] = "VCluster restored from backup '" + backup_name + "' as '" + config.vcluster_name + "'.";
                            res.body = response_data.dump();
                        }
                    } catch (...) {
                        // If parsing fails, leave response as-is
                    }
                }

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        // ============================================================================
        // UPGRADE VCLUSTER
        // ============================================================================

        void K8sHandlers::handleUpgradeVCluster(const httplib::Request& req, httplib::Response& res) {
            try {
                std::string id = extractClusterIdentifierFromRequest(req);
                if (id.empty()) {
                    return sendErrorResponse(res, 400, "Missing vcluster identifier.");
                }

                auto json_body = nlohmann::json::parse(req.body);
                std::string new_version = json_body.value("version", "");

                if (new_version.empty()) {
                    return sendErrorResponse(res, 400, "Missing required parameter: version.");
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
                std::string vcluster_name;
                std::string current_image;

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
                            auto sts = nlohmann::json::parse(sts_read_str);
                            free(sts_read_str);

                            auto labels = sts["metadata"].value("labels", nlohmann::json::object());
                            if (labels.value("vcluster.loft.sh/managed-by", "") == "nmc-server") {
                                target_namespace = ns_name;
                                vcluster_name = sts["metadata"].value("name", "");

                                // Get current image
                                if (sts.contains("spec") && sts["spec"].contains("template") &&
                                    sts["spec"]["template"].contains("spec") &&
                                    sts["spec"]["template"]["spec"].contains("containers") &&
                                    sts["spec"]["template"]["spec"]["containers"].is_array() &&
                                    !sts["spec"]["template"]["spec"]["containers"].empty()) {
                                    current_image = sts["spec"]["template"]["spec"]["containers"][0].value("image", "");
                                }

                                found = true;
                                break;
                            }
                        }
                    }
                }

                if (!found) {
                    genericClient_free(stsClient);
                    return sendErrorResponse(res, 404, "VCluster '" + id + "' not found.");
                }

                // Build new image tag
                std::string base_image = "ghcr.io/loft-sh/vcluster";
                if (current_image.find(":") != std::string::npos) {
                    base_image = current_image.substr(0, current_image.find_last_of(":"));
                }
                std::string new_image = base_image + ":" + new_version;

                // Patch the StatefulSet with new image
                nlohmann::json patchObj = {
                    {"metadata", {
                        {"annotations", {
                            {"vcluster.nmc/upgraded-at", std::to_string(std::time(nullptr))},
                            {"vcluster.nmc/previous-version", current_image}
                        }}
                    }},
                    {"spec", {
                        {"template", {
                            {"spec", {
                                {"containers", nlohmann::json::array({
                                    {
                                        {"name", "vcluster"},
                                        {"image", new_image}
                                    }
                                })}
                            }}
                        }}
                    }}
                };

                char *patch_result = Generic_patchNamespacedResource(
                    stsClient,
                    (char*)target_namespace.c_str(),
                    (char*)vcluster_name.c_str(),
                    (char*)patchObj.dump().c_str(),
                    nullptr,  // queryParameters
                    nullptr,  // headerParameters
                    nullptr,  // formParameters
                    nullptr,  // headerType
                    nullptr   // contentType
                );

                if (!patch_result) {
                    genericClient_free(stsClient);
                    return sendErrorResponse(res, 500, "Failed to upgrade vcluster: " + std::to_string(apiClient->response_code));
                }

                free(patch_result);
                genericClient_free(stsClient);

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VCluster '" + id + "' upgrade initiated to version " + new_version + ".";
                apiResponse.data = {
                    {"status", "Upgrading"},
                    {"previous_image", current_image},
                    {"new_image", new_image},
                    {"new_version", new_version},
                    {"namespace", target_namespace}
                };
                sendJsonResponse(res, apiResponse);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

    } // namespace Server
} // namespace NMC
