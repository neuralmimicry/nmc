// server/Models/K8sCluster.h
#ifndef NMC_SERVER_MODELS_K8S_CLUSTER_H
#define NMC_SERVER_MODELS_K8S_CLUSTER_H

#include <string>
#include <nlohmann/json.hpp>
#include "VClusterConfig.h"

namespace NMC {
    namespace Server {
        namespace Models {

            struct K8sCluster {
                std::string id;
                std::string name;
                std::string region;
                std::string status;
                bool is_vcluster;
                std::string parent_cluster;
                std::string vcluster_namespace;
                std::string config_id; // Reference to VClusterConfig

                nlohmann::json toJsonString() const {
                    nlohmann::json result = {
                            {"id", id},
                            {"name", name},
                            {"region", region},
                            {"status", status},
                            {"is_vcluster", is_vcluster}
                    };
                    if (is_vcluster) {
                        result["parent_cluster"] = parent_cluster;
                        result["vcluster_namespace"] = vcluster_namespace;
                        if (!config_id.empty()) {
                            result["config_id"] = config_id;
                        }
                    }
                    return result;
                }

                static K8sCluster fromJson(const nlohmann::json& j) {
                    K8sCluster c;
                    c.id = j.value("id", "");
                    c.name = j.value("name", "");
                    c.region = j.value("region", "");
                    c.status = j.value("status", "");
                    c.is_vcluster = j.value("is_vcluster", false);
                    c.parent_cluster = j.value("parent_cluster", "");
                    c.vcluster_namespace = j.value("vcluster_namespace", "");
                    c.config_id = j.value("config_id", "");
                    return c;
                }
            };

        } // namespace Models
    } // namespace Server
} // namespace NMC

#endif // NMC_SERVER_MODELS_K8S_CLUSTER_H