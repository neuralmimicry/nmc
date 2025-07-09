// client/Models/VM.h
#ifndef NMC_MODELS_VM_H
#define NMC_MODELS_VM_H

#include <string>
#include <nlohmann/json.hpp>

namespace NMC {
    namespace Models {

        struct VM {
            std::string id;
            std::string name;
            std::string sku;
            std::string region;
            std::string osImage;
            std::string publicKeyId;
            std::string initScript;
            std::string status;

            nlohmann::json toJson() const {
                return {
                        {"id", id},
                        {"name", name},
                        {"sku", sku},
                        {"region", region},
                        {"osImage", osImage},
                        {"publicKeyId", publicKeyId}, // Should be ID, not actual key
                        {"initScript", initScript.empty() ? "" : "(present)"}, // Don't expose full script usually
                        {"status", status}
                };
            }

            static VM fromJson(const nlohmann::json& j) {
                VM v;
                v.id = j.value("id", "");
                v.name = j.value("name", "");
                v.sku = j.value("sku", "");
                v.region = j.value("region", "");
                v.osImage = j.value("osImage", "");
                v.publicKeyId = j.value("publicKeyId", "");
                v.initScript = j.value("initScript", "");
                v.status = j.value("status", "");
                return v;
            }
        };

    } // namespace Models
} // namespace NMC

#endif // NMC_MODELS_VM_H