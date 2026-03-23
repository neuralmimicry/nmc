// K8sHandlers_VCluster_Advanced.cpp
// Advanced VCluster implementation with full feature support
// This file contains the enhanced implementations to be integrated into K8sHandlers.cpp

#include "K8sHandlers.h"
#include "../Models/VClusterConfig.h"
#include "Utils.h"
#include <sstream>

namespace NMC {
    namespace Server {

        // ============================================================================
        // HELPER METHODS
        // ============================================================================

        bool K8sHandlers::createServiceAccount(const std::string& ns, const std::string& name) {
            nlohmann::json saObj = {
                {"apiVersion", "v1"},
                {"kind", "ServiceAccount"},
                {"metadata", {
                    {"name", name},
                    {"namespace", ns},
                    {"labels", {
                        {"app.kubernetes.io/managed-by", "nmc-server"},
                        {"app.kubernetes.io/component", "vcluster"}
                    }}
                }}
            };

            genericClient_t *saClient = getGenericClient("", "v1", "serviceaccounts");
            if (!saClient) return false;

            char *result = Generic_createNamespacedResource(
                saClient,
                (char*)ns.c_str(),
                (char*)saObj.dump().c_str()
            );

            bool success = (result != nullptr || apiClient->response_code == 409); // 409 = already exists
            if (result) free(result);
            genericClient_free(saClient);
            return success;
        }

        bool K8sHandlers::createRBACResources(const std::string& ns, const std::string& sa_name,
                                               const std::vector<std::string>& roles) {
            // Create Role with necessary permissions for vcluster
            nlohmann::json roleObj = {
                {"apiVersion", "rbac.authorization.k8s.io/v1"},
                {"kind", "Role"},
                {"metadata", {
                    {"name", sa_name + "-role"},
                    {"namespace", ns}
                }},
                {"rules", nlohmann::json::array({
                    {
                        {"apiGroups", nlohmann::json::array({"", "apps", "rbac.authorization.k8s.io"})},
                        {"resources", nlohmann::json::array({"*"})},
                        {"verbs", nlohmann::json::array({"*"})}
                    }
                })}
            };

            genericClient_t *roleClient = getGenericClient("rbac.authorization.k8s.io", "v1", "roles");
            if (!roleClient) return false;

            char *role_result = Generic_createNamespacedResource(
                roleClient,
                (char*)ns.c_str(),
                (char*)roleObj.dump().c_str()
            );

            bool role_success = (role_result != nullptr || apiClient->response_code == 409);
            if (role_result) free(role_result);
            genericClient_free(roleClient);

            if (!role_success) return false;

            // Create RoleBinding
            nlohmann::json rbObj = {
                {"apiVersion", "rbac.authorization.k8s.io/v1"},
                {"kind", "RoleBinding"},
                {"metadata", {
                    {"name", sa_name + "-rolebinding"},
                    {"namespace", ns}
                }},
                {"roleRef", {
                    {"apiGroup", "rbac.authorization.k8s.io"},
                    {"kind", "Role"},
                    {"name", sa_name + "-role"}
                }},
                {"subjects", nlohmann::json::array({
                    {
                        {"kind", "ServiceAccount"},
                        {"name", sa_name},
                        {"namespace", ns}
                    }
                })}
            };

            genericClient_t *rbClient = getGenericClient("rbac.authorization.k8s.io", "v1", "rolebindings");
            if (!rbClient) return false;

            char *rb_result = Generic_createNamespacedResource(
                rbClient,
                (char*)ns.c_str(),
                (char*)rbObj.dump().c_str()
            );

            bool rb_success = (rb_result != nullptr || apiClient->response_code == 409);
            if (rb_result) free(rb_result);
            genericClient_free(rbClient);

            return rb_success;
        }

        bool K8sHandlers::createPodDisruptionBudget(const std::string& ns, const std::string& name,
                                                     int minAvailable, const std::map<std::string, std::string>& matchLabels) {
            nlohmann::json pdbObj = {
                {"apiVersion", "policy/v1"},
                {"kind", "PodDisruptionBudget"},
                {"metadata", {
                    {"name", name + "-pdb"},
                    {"namespace", ns}
                }},
                {"spec", {
                    {"minAvailable", minAvailable},
                    {"selector", {
                        {"matchLabels", matchLabels}
                    }}
                }}
            };

            genericClient_t *pdbClient = getGenericClient("policy", "v1", "poddisruptionbudgets");
            if (!pdbClient) return false;

            char *result = Generic_createNamespacedResource(
                pdbClient,
                (char*)ns.c_str(),
                (char*)pdbObj.dump().c_str()
            );

            bool success = (result != nullptr || apiClient->response_code == 409);
            if (result) free(result);
            genericClient_free(pdbClient);
            return success;
        }

        bool K8sHandlers::createIngress(const std::string& ns, const std::string& name,
                                        const Models::NetworkingConfig& netConfig) {
            if (!netConfig.enable_ingress || netConfig.ingress_host.empty()) {
                return true; // Not creating ingress is not an error
            }

            nlohmann::json ingressObj = {
                {"apiVersion", "networking.k8s.io/v1"},
                {"kind", "Ingress"},
                {"metadata", {
                    {"name", name + "-ingress"},
                    {"namespace", ns},
                    {"annotations", netConfig.ingress_annotations}
                }},
                {"spec", {
                    {"ingressClassName", netConfig.ingress_class.empty() ? "nginx" : netConfig.ingress_class},
                    {"rules", nlohmann::json::array({
                        {
                            {"host", netConfig.ingress_host},
                            {"http", {
                                {"paths", nlohmann::json::array({
                                    {
                                        {"path", "/"},
                                        {"pathType", "Prefix"},
                                        {"backend", {
                                            {"service", {
                                                {"name", name},
                                                {"port", {{"number", 443}}}
                                            }}
                                        }}
                                    }
                                })}
                            }}
                        }
                    })}
                }}
            };

            genericClient_t *ingressClient = getGenericClient("networking.k8s.io", "v1", "ingresses");
            if (!ingressClient) return false;

            char *result = Generic_createNamespacedResource(
                ingressClient,
                (char*)ns.c_str(),
                (char*)ingressObj.dump().c_str()
            );

            bool success = (result != nullptr || apiClient->response_code == 409);
            if (result) free(result);
            genericClient_free(ingressClient);
            return success;
        }

        nlohmann::json K8sHandlers::buildVClusterStatefulSet(const std::string& name, const std::string& ns,
                                                              const Models::VClusterConfig& config) {
            // Build container spec with resources and env
            nlohmann::json containerSpec = {
                {"name", "vcluster"},
                {"image", config.vcluster_image + ":" + config.vcluster_image_tag},
                {"args", nlohmann::json::array({
                    "--name=" + name,
                    "--service-account=" + config.security.service_account_name
                })},
                {"ports", nlohmann::json::array({
                    {{"containerPort", 8443}, {"name", "https"}},
                    {{"containerPort", std::stoi(config.monitoring.metrics_port)}, {"name", "metrics"}}
                })},
                {"volumeMounts", nlohmann::json::array({
                    {{"name", "data"}, {"mountPath", "/data"}}
                })}
            };

            // Add resource limits/requests if specified
            if (!config.resources.cpu_request.empty() || !config.resources.memory_request.empty()) {
                nlohmann::json resources = {{"requests", nlohmann::json::object()}};
                if (!config.resources.cpu_request.empty()) {
                    resources["requests"]["cpu"] = config.resources.cpu_request;
                }
                if (!config.resources.memory_request.empty()) {
                    resources["requests"]["memory"] = config.resources.memory_request;
                }
                if (!config.resources.cpu_limit.empty() || !config.resources.memory_limit.empty()) {
                    resources["limits"] = nlohmann::json::object();
                    if (!config.resources.cpu_limit.empty()) {
                        resources["limits"]["cpu"] = config.resources.cpu_limit;
                    }
                    if (!config.resources.memory_limit.empty()) {
                        resources["limits"]["memory"] = config.resources.memory_limit;
                    }
                }
                containerSpec["resources"] = resources;
            }

            // Add health checks if enabled
            if (config.monitoring.enable_health_checks) {
                containerSpec["livenessProbe"] = {
                    {"httpGet", {
                        {"path", "/healthz"},
                        {"port", 8443},
                        {"scheme", "HTTPS"}
                    }},
                    {"initialDelaySeconds", 30},
                    {"periodSeconds", config.monitoring.health_check_interval_seconds}
                };
                containerSpec["readinessProbe"] = {
                    {"httpGet", {
                        {"path", "/readyz"},
                        {"port", 8443},
                        {"scheme", "HTTPS"}
                    }},
                    {"initialDelaySeconds", 10},
                    {"periodSeconds", 10}
                };
            }

            // Build pod spec
            nlohmann::json podSpec = {
                {"serviceAccountName", config.security.service_account_name},
                {"containers", nlohmann::json::array({containerSpec})}
            };

            // Add node selector if specified
            if (!config.placement.node_selector.empty()) {
                podSpec["nodeSelector"] = config.placement.node_selector;
            }

            // Add tolerations if specified
            if (!config.placement.tolerations.is_null() && config.placement.tolerations.is_array()) {
                podSpec["tolerations"] = config.placement.tolerations;
            }

            // Add security context based on PSS
            if (config.security.pod_security_standard == "restricted") {
                podSpec["securityContext"] = {
                    {"runAsNonRoot", true},
                    {"runAsUser", 1000},
                    {"fsGroup", 1000},
                    {"seccompProfile", {{"type", "RuntimeDefault"}}}
                };
                containerSpec["securityContext"] = {
                    {"allowPrivilegeEscalation", false},
                    {"capabilities", {
                        {"drop", nlohmann::json::array({"ALL"})}
                    }}
                };
            }

            // Build StatefulSet
            nlohmann::json statefulSet = {
                {"apiVersion", "apps/v1"},
                {"kind", "StatefulSet"},
                {"metadata", {
                    {"name", name},
                    {"namespace", ns},
                    {"labels", {
                        {"app", name},
                        {"vcluster.loft.sh/managed-by", "nmc-server"},
                        {"app.kubernetes.io/name", "vcluster"},
                        {"app.kubernetes.io/instance", name}
                    }}
                }},
                {"spec", {
                    {"serviceName", name},
                    {"replicas", config.ha.replicas},
                    {"selector", {
                        {"matchLabels", {{"app", name}}}
                    }},
                    {"template", {
                        {"metadata", {
                            {"labels", {{"app", name}}}
                        }},
                        {"spec", podSpec}
                    }},
                    {"volumeClaimTemplates", nlohmann::json::array({
                        {
                            {"metadata", {{"name", "data"}}},
                            {"spec", {
                                {"accessModes", nlohmann::json::array({"ReadWriteOnce"})},
                                {"storageClassName", config.storage_class},
                                {"resources", {
                                    {"requests", {
                                        {"storage", config.resources.storage_request.empty() ? "5Gi" : config.resources.storage_request}
                                    }}
                                }}
                            }}
                        }
                    })}
                }}
            };

            // Add pod anti-affinity for HA
            if (config.ha.replicas > 1) {
                statefulSet["spec"]["template"]["spec"]["affinity"] = {
                    {"podAntiAffinity", {
                        {"preferredDuringSchedulingIgnoredDuringExecution", nlohmann::json::array({
                            {
                                {"weight", 100},
                                {"podAffinityTerm", {
                                    {"labelSelector", {
                                        {"matchLabels", {{"app", name}}}
                                    }},
                                    {"topologyKey", "kubernetes.io/hostname"}
                                }}
                            }
                        })}
                    }}
                };
            }

            return statefulSet;
        }

        nlohmann::json K8sHandlers::buildVClusterService(const std::string& name, const std::string& ns,
                                                          const Models::NetworkingConfig& netConfig) {
            nlohmann::json service = {
                {"apiVersion", "v1"},
                {"kind", "Service"},
                {"metadata", {
                    {"name", name},
                    {"namespace", ns},
                    {"labels", {
                        {"app", name},
                        {"vcluster.loft.sh/managed-by", "nmc-server"}
                    }}
                }},
                {"spec", {
                    {"type", netConfig.service_type},
                    {"selector", {{"app", name}}},
                    {"ports", nlohmann::json::array({
                        {
                            {"name", "https"},
                            {"port", 443},
                            {"targetPort", 8443},
                            {"protocol", "TCP"}
                        }
                    })}
                }}
            };

            return service;
        }

        std::string K8sHandlers::extractClusterIdentifierFromRequest(const httplib::Request& req) {
            if (req.matches.size() > 1) {
                return req.matches[1].str();
            }
            return "";
        }

        bool K8sHandlers::clusterMatchesFilter(const nlohmann::json& cluster, const std::string& filterName) {
            if (filterName.empty()) return true;
            std::string name = cluster.value("name", "");
            return name.find(filterName) != std::string::npos;
        }

        // ============================================================================
        // ENHANCED CREATE VCLUSTER HANDLER
        // ============================================================================

        void K8sHandlers::handleCreateVCluster(const httplib::Request& req, httplib::Response& res) {
            try {
                auto json_body = nlohmann::json::parse(req.body);

                // Parse basic parameters
                std::string name = json_body.value("name", "");
                std::string vcluster_namespace = json_body.value("namespace", "vcluster-" + name);

                if (name.empty()) {
                    return sendErrorResponse(res, 400, "Missing required parameter: name.");
                }

                // Parse full VClusterConfig
                Models::VClusterConfig config = Models::VClusterConfig::fromJson(json_body.value("config", nlohmann::json::object()));
                config.id = Utils::generateUniqueId("vc");
                config.vcluster_name = name;

                // Set default service account name if not specified
                if (config.security.service_account_name.empty()) {
                    config.security.service_account_name = name;
                }

                // Store config
                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    vclusterConfigsRef[config.id] = config;
                }

                // Step 1: Create namespace
                nlohmann::json namespaceObj = {
                    {"apiVersion", "v1"},
                    {"kind", "Namespace"},
                    {"metadata", {
                        {"name", vcluster_namespace},
                        {"labels", {
                            {"vcluster.loft.sh/managed-by", "nmc-server"},
                            {"app.kubernetes.io/managed-by", "nmc-server"}
                        }}
                    }}
                };

                genericClient_t *namespaceClient = getGenericClient("", "v1", "namespaces");
                if (!namespaceClient) {
                    return sendErrorResponse(res, 500, "Failed to create namespace client.");
                }

                char *ns_result = Generic_createResource(namespaceClient, (char*)namespaceObj.dump().c_str());
                if (!ns_result && apiClient->response_code != 409) {
                    genericClient_free(namespaceClient);
                    return sendErrorResponse(res, 500, "Failed to create namespace for vcluster.");
                }
                if (ns_result) free(ns_result);
                genericClient_free(namespaceClient);

                // Step 2: Create ServiceAccount if enabled
                if (config.security.auto_create_service_account) {
                    if (!createServiceAccount(vcluster_namespace, config.security.service_account_name)) {
                        return sendErrorResponse(res, 500, "Failed to create ServiceAccount.");
                    }

                    // Step 3: Create RBAC resources
                    if (!createRBACResources(vcluster_namespace, config.security.service_account_name, config.security.rbac_roles)) {
                        return sendErrorResponse(res, 500, "Failed to create RBAC resources.");
                    }
                }

                // Step 4: Deploy vcluster StatefulSet
                nlohmann::json vclusterStatefulSet = buildVClusterStatefulSet(name, vcluster_namespace, config);

                genericClient_t *stsClient = getGenericClient("apps", "v1", "statefulsets");
                if (!stsClient) {
                    return sendErrorResponse(res, 500, "Failed to create StatefulSet client.");
                }

                char *sts_result = Generic_createNamespacedResource(
                    stsClient,
                    (char*)vcluster_namespace.c_str(),
                    (char*)vclusterStatefulSet.dump().c_str()
                );

                if (!sts_result) {
                    genericClient_free(stsClient);
                    return sendErrorResponse(res, apiClient->response_code,
                        "Failed to create vcluster StatefulSet: " + std::to_string(apiClient->response_code));
                }

                auto created_sts = nlohmann::json::parse(sts_result);
                free(sts_result);
                genericClient_free(stsClient);

                // Step 5: Create service
                nlohmann::json vclusterService = buildVClusterService(name, vcluster_namespace, config.networking);

                genericClient_t *svcClient = getGenericClient("", "v1", "services");
                if (svcClient) {
                    char *svc_result = Generic_createNamespacedResource(
                        svcClient,
                        (char*)vcluster_namespace.c_str(),
                        (char*)vclusterService.dump().c_str()
                    );
                    if (svc_result) free(svc_result);
                    genericClient_free(svcClient);
                }

                // Step 6: Create Ingress if enabled
                if (config.networking.enable_ingress) {
                    createIngress(vcluster_namespace, name, config.networking);
                }

                // Step 7: Create PodDisruptionBudget if HA is enabled
                if (config.ha.enable_pod_disruption_budget && config.ha.replicas > 1) {
                    std::map<std::string, std::string> matchLabels = {{"app", name}};
                    createPodDisruptionBudget(vcluster_namespace, name, config.ha.min_available, matchLabels);
                }

                // Step 8: Build response
                Models::K8sCluster vcluster;
                vcluster.id = created_sts["metadata"].value("uid", config.id);
                vcluster.name = name;
                vcluster.region = "vcluster";
                vcluster.status = "Provisioning";
                vcluster.is_vcluster = true;
                vcluster.parent_cluster = basePath ? std::string(basePath) : "local";
                vcluster.vcluster_namespace = vcluster_namespace;
                vcluster.config_id = config.id;

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VCluster '" + name + "' created successfully with advanced configuration.";
                apiResponse.data = vcluster.toJsonString();

                // Add Tracey info if enabled
                if (config.tracey.enabled && !config.tracey.agent_id.empty()) {
                    apiResponse.data["tracey"] = {
                        {"enabled", true},
                        {"agent_id", config.tracey.agent_id},
                        {"status_addr", config.tracey.status_addr},
                        {"enforce_compliance", config.tracey.enforce_compliance}
                    };
                }

                sendJsonResponse(res, apiResponse);

            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        // ============================================================================
        // TO BE CONTINUED IN NEXT PHASE...
        // ============================================================================
        // Lifecycle, monitoring, and configuration handlers will be implemented
        // in subsequent phases following the same patterns

    } // namespace Server
} // namespace NMC
