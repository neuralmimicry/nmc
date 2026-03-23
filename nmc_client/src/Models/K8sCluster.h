// client/Models/K8sCluster.h
#ifndef NMC_MODELS_K8S_CLUSTER_H
#define NMC_MODELS_K8S_CLUSTER_H

#include <string>
#include <nlohmann/json.hpp>

namespace NMC {
    namespace Models {

        struct K8sCluster {
            std::string id;
            std::string name;
            std::string region;
            std::string status;
            bool is_vcluster;
            std::string parent_cluster;
            std::string vcluster_namespace;

            nlohmann::json toJson() const {
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
                return c;
            }
        };

    } // namespace Models
} // namespace NMC

#endif // NMC_MODELS_K8S_CLUSTER_H