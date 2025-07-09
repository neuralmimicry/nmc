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

            nlohmann::json toJson() const {
                return {
                        {"id", id},
                        {"name", name},
                        {"region", region},
                        {"status", status}
                };
            }

            static K8sCluster fromJson(const nlohmann::json& j) {
                K8sCluster c;
                c.id = j.value("id", "");
                c.name = j.value("name", "");
                c.region = j.value("region", "");
                c.status = j.value("status", "");
                return c;
            }
        };

    } // namespace Models
} // namespace NMC

#endif // NMC_MODELS_K8S_CLUSTER_H