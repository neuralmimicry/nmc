// server/Models/VClusterConfig.h
#ifndef NMC_SERVER_MODELS_VCLUSTER_CONFIG_H
#define NMC_SERVER_MODELS_VCLUSTER_CONFIG_H

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace NMC {
    namespace Server {
        namespace Models {

            // Resource limits/requests configuration
            struct ResourceRequirements {
                std::string cpu_request;
                std::string cpu_limit;
                std::string memory_request;
                std::string memory_limit;
                std::string storage_request;

                nlohmann::json toJson() const {
                    nlohmann::json j = nlohmann::json::object();
                    if (!cpu_request.empty()) j["cpu_request"] = cpu_request;
                    if (!cpu_limit.empty()) j["cpu_limit"] = cpu_limit;
                    if (!memory_request.empty()) j["memory_request"] = memory_request;
                    if (!memory_limit.empty()) j["memory_limit"] = memory_limit;
                    if (!storage_request.empty()) j["storage_request"] = storage_request;
                    return j;
                }

                static ResourceRequirements fromJson(const nlohmann::json& j) {
                    ResourceRequirements r;
                    if (!j.is_object()) return r;
                    r.cpu_request = j.value("cpu_request", "");
                    r.cpu_limit = j.value("cpu_limit", "");
                    r.memory_request = j.value("memory_request", "");
                    r.memory_limit = j.value("memory_limit", "");
                    r.storage_request = j.value("storage_request", "");
                    return r;
                }
            };

            // Node selector and tolerations
            struct PlacementConfig {
                std::map<std::string, std::string> node_selector;
                nlohmann::json tolerations; // Array of toleration objects

                nlohmann::json toJson() const {
                    nlohmann::json j = nlohmann::json::object();
                    if (!node_selector.empty()) {
                        j["node_selector"] = node_selector;
                    }
                    if (!tolerations.is_null() && tolerations.is_array()) {
                        j["tolerations"] = tolerations;
                    }
                    return j;
                }

                static PlacementConfig fromJson(const nlohmann::json& j) {
                    PlacementConfig p;
                    if (!j.is_object()) return p;
                    if (j.contains("node_selector") && j["node_selector"].is_object()) {
                        p.node_selector = j["node_selector"].get<std::map<std::string, std::string>>();
                    }
                    if (j.contains("tolerations") && j["tolerations"].is_array()) {
                        p.tolerations = j["tolerations"];
                    }
                    return p;
                }
            };

            // High availability configuration
            struct HAConfig {
                int replicas;
                bool enable_pod_disruption_budget;
                int min_available;

                nlohmann::json toJson() const {
                    return {
                        {"replicas", replicas},
                        {"enable_pod_disruption_budget", enable_pod_disruption_budget},
                        {"min_available", min_available}
                    };
                }

                static HAConfig fromJson(const nlohmann::json& j) {
                    HAConfig h;
                    if (!j.is_object()) {
                        h.replicas = 1;
                        h.enable_pod_disruption_budget = false;
                        h.min_available = 1;
                        return h;
                    }
                    h.replicas = j.value("replicas", 1);
                    h.enable_pod_disruption_budget = j.value("enable_pod_disruption_budget", false);
                    h.min_available = j.value("min_available", 1);
                    return h;
                }
            };

            // Networking configuration
            struct NetworkingConfig {
                std::string service_type; // ClusterIP, NodePort, LoadBalancer
                bool enable_ingress;
                std::string ingress_host;
                std::string ingress_class;
                std::map<std::string, std::string> ingress_annotations;
                std::string networking_mode; // default, host-network, calico, cilium

                nlohmann::json toJson() const {
                    nlohmann::json j = {
                        {"service_type", service_type},
                        {"enable_ingress", enable_ingress},
                        {"networking_mode", networking_mode}
                    };
                    if (!ingress_host.empty()) j["ingress_host"] = ingress_host;
                    if (!ingress_class.empty()) j["ingress_class"] = ingress_class;
                    if (!ingress_annotations.empty()) j["ingress_annotations"] = ingress_annotations;
                    return j;
                }

                static NetworkingConfig fromJson(const nlohmann::json& j) {
                    NetworkingConfig n;
                    if (!j.is_object()) {
                        n.service_type = "ClusterIP";
                        n.enable_ingress = false;
                        n.networking_mode = "default";
                        return n;
                    }
                    n.service_type = j.value("service_type", "ClusterIP");
                    n.enable_ingress = j.value("enable_ingress", false);
                    n.ingress_host = j.value("ingress_host", "");
                    n.ingress_class = j.value("ingress_class", "");
                    n.networking_mode = j.value("networking_mode", "default");
                    if (j.contains("ingress_annotations") && j["ingress_annotations"].is_object()) {
                        n.ingress_annotations = j["ingress_annotations"].get<std::map<std::string, std::string>>();
                    }
                    return n;
                }
            };

            // RBAC and security configuration
            struct SecurityConfig {
                bool auto_create_service_account;
                std::string service_account_name;
                std::vector<std::string> rbac_roles;
                std::string ca_cert; // Base64 encoded custom CA certificate
                bool enable_pod_security_policy;
                std::string pod_security_standard; // restricted, baseline, privileged

                nlohmann::json toJson() const {
                    nlohmann::json j = {
                        {"auto_create_service_account", auto_create_service_account},
                        {"enable_pod_security_policy", enable_pod_security_policy},
                        {"pod_security_standard", pod_security_standard}
                    };
                    if (!service_account_name.empty()) j["service_account_name"] = service_account_name;
                    if (!rbac_roles.empty()) j["rbac_roles"] = rbac_roles;
                    if (!ca_cert.empty()) j["ca_cert"] = "(present)"; // Don't expose full cert
                    return j;
                }

                static SecurityConfig fromJson(const nlohmann::json& j) {
                    SecurityConfig s;
                    if (!j.is_object()) {
                        s.auto_create_service_account = true;
                        s.enable_pod_security_policy = false;
                        s.pod_security_standard = "baseline";
                        return s;
                    }
                    s.auto_create_service_account = j.value("auto_create_service_account", true);
                    s.service_account_name = j.value("service_account_name", "");
                    s.enable_pod_security_policy = j.value("enable_pod_security_policy", false);
                    s.pod_security_standard = j.value("pod_security_standard", "baseline");
                    s.ca_cert = j.value("ca_cert", "");
                    if (j.contains("rbac_roles") && j["rbac_roles"].is_array()) {
                        s.rbac_roles = j["rbac_roles"].get<std::vector<std::string>>();
                    }
                    return s;
                }
            };

            // Sync options configuration
            struct SyncConfig {
                std::vector<std::string> sync_resources; // pods, services, configmaps, etc.
                std::map<std::string, std::string> sync_labels;
                std::string host_namespace_mapping; // single, multi, custom
                bool enable_persistent_volumes;

                nlohmann::json toJson() const {
                    nlohmann::json j = {
                        {"host_namespace_mapping", host_namespace_mapping},
                        {"enable_persistent_volumes", enable_persistent_volumes}
                    };
                    if (!sync_resources.empty()) j["sync_resources"] = sync_resources;
                    if (!sync_labels.empty()) j["sync_labels"] = sync_labels;
                    return j;
                }

                static SyncConfig fromJson(const nlohmann::json& j) {
                    SyncConfig s;
                    if (!j.is_object()) {
                        s.host_namespace_mapping = "single";
                        s.enable_persistent_volumes = true;
                        return s;
                    }
                    s.host_namespace_mapping = j.value("host_namespace_mapping", "single");
                    s.enable_persistent_volumes = j.value("enable_persistent_volumes", true);
                    if (j.contains("sync_resources") && j["sync_resources"].is_array()) {
                        s.sync_resources = j["sync_resources"].get<std::vector<std::string>>();
                    }
                    if (j.contains("sync_labels") && j["sync_labels"].is_object()) {
                        s.sync_labels = j["sync_labels"].get<std::map<std::string, std::string>>();
                    }
                    return s;
                }
            };

            // Monitoring configuration
            struct MonitoringConfig {
                bool enable_metrics;
                std::string metrics_port;
                bool enable_health_checks;
                int health_check_interval_seconds;

                nlohmann::json toJson() const {
                    return {
                        {"enable_metrics", enable_metrics},
                        {"metrics_port", metrics_port},
                        {"enable_health_checks", enable_health_checks},
                        {"health_check_interval_seconds", health_check_interval_seconds}
                    };
                }

                static MonitoringConfig fromJson(const nlohmann::json& j) {
                    MonitoringConfig m;
                    if (!j.is_object()) {
                        m.enable_metrics = true;
                        m.metrics_port = "8443";
                        m.enable_health_checks = true;
                        m.health_check_interval_seconds = 30;
                        return m;
                    }
                    m.enable_metrics = j.value("enable_metrics", true);
                    m.metrics_port = j.value("metrics_port", "8443");
                    m.enable_health_checks = j.value("enable_health_checks", true);
                    m.health_check_interval_seconds = j.value("health_check_interval_seconds", 30);
                    return m;
                }
            };

            // Tracey integration configuration
            struct TraceyConfig {
                bool enabled;
                std::string agent_id;
                std::string status_addr;
                bool enforce_compliance;
                std::vector<std::string> required_capabilities;

                nlohmann::json toJson() const {
                    nlohmann::json j = {
                        {"enabled", enabled},
                        {"enforce_compliance", enforce_compliance}
                    };
                    if (!agent_id.empty()) j["agent_id"] = agent_id;
                    if (!status_addr.empty()) j["status_addr"] = status_addr;
                    if (!required_capabilities.empty()) j["required_capabilities"] = required_capabilities;
                    return j;
                }

                static TraceyConfig fromJson(const nlohmann::json& j) {
                    TraceyConfig t;
                    if (!j.is_object()) {
                        t.enabled = false;
                        t.enforce_compliance = false;
                        return t;
                    }
                    t.enabled = j.value("enabled", false);
                    t.agent_id = j.value("agent_id", "");
                    t.status_addr = j.value("status_addr", "");
                    t.enforce_compliance = j.value("enforce_compliance", false);
                    if (j.contains("required_capabilities") && j["required_capabilities"].is_array()) {
                        t.required_capabilities = j["required_capabilities"].get<std::vector<std::string>>();
                    }
                    return t;
                }
            };

            // Complete VCluster configuration
            struct VClusterConfig {
                std::string id;
                std::string vcluster_name;
                std::string vcluster_image;
                std::string vcluster_image_tag;
                std::string storage_class;

                ResourceRequirements resources;
                PlacementConfig placement;
                HAConfig ha;
                NetworkingConfig networking;
                SecurityConfig security;
                SyncConfig sync;
                MonitoringConfig monitoring;
                TraceyConfig tracey;

                nlohmann::json toJson() const {
                    nlohmann::json j = {
                        {"id", id},
                        {"vcluster_name", vcluster_name},
                        {"vcluster_image", vcluster_image},
                        {"vcluster_image_tag", vcluster_image_tag},
                        {"storage_class", storage_class},
                        {"resources", resources.toJson()},
                        {"placement", placement.toJson()},
                        {"ha", ha.toJson()},
                        {"networking", networking.toJson()},
                        {"security", security.toJson()},
                        {"sync", sync.toJson()},
                        {"monitoring", monitoring.toJson()},
                        {"tracey", tracey.toJson()}
                    };
                    return j;
                }

                static VClusterConfig fromJson(const nlohmann::json& j) {
                    VClusterConfig c;
                    if (!j.is_object()) {
                        // Set defaults
                        c.vcluster_image = "ghcr.io/loft-sh/vcluster";
                        c.vcluster_image_tag = "0.19.0";
                        c.storage_class = "default";
                        c.ha = HAConfig::fromJson(nlohmann::json::object());
                        c.networking = NetworkingConfig::fromJson(nlohmann::json::object());
                        c.security = SecurityConfig::fromJson(nlohmann::json::object());
                        c.sync = SyncConfig::fromJson(nlohmann::json::object());
                        c.monitoring = MonitoringConfig::fromJson(nlohmann::json::object());
                        c.tracey = TraceyConfig::fromJson(nlohmann::json::object());
                        return c;
                    }
                    c.id = j.value("id", "");
                    c.vcluster_name = j.value("vcluster_name", "");
                    c.vcluster_image = j.value("vcluster_image", "ghcr.io/loft-sh/vcluster");
                    c.vcluster_image_tag = j.value("vcluster_image_tag", "0.19.0");
                    c.storage_class = j.value("storage_class", "default");

                    c.resources = ResourceRequirements::fromJson(j.value("resources", nlohmann::json::object()));
                    c.placement = PlacementConfig::fromJson(j.value("placement", nlohmann::json::object()));
                    c.ha = HAConfig::fromJson(j.value("ha", nlohmann::json::object()));
                    c.networking = NetworkingConfig::fromJson(j.value("networking", nlohmann::json::object()));
                    c.security = SecurityConfig::fromJson(j.value("security", nlohmann::json::object()));
                    c.sync = SyncConfig::fromJson(j.value("sync", nlohmann::json::object()));
                    c.monitoring = MonitoringConfig::fromJson(j.value("monitoring", nlohmann::json::object()));
                    c.tracey = TraceyConfig::fromJson(j.value("tracey", nlohmann::json::object()));

                    return c;
                }
            };

        } // namespace Models
    } // namespace Server
} // namespace NMC

#endif // NMC_SERVER_MODELS_VCLUSTER_CONFIG_H
