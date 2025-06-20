// server/Models/SSHKey.h
#ifndef NMC_SERVER_MODELS_SSH_KEY_H
#define NMC_SERVER_MODELS_SSH_KEY_H

#include <string>
#include <nlohmann/json.hpp>

namespace NMC {
    namespace Server {
        namespace Models {

            struct SSHKey {
                std::string id;
                std::string name;
                std::string publicKey;
                std::string description;

                nlohmann::json toJson() const {
                    return {
                            {"id", id},
                            {"name", name},
                            {"publicKey", "REDACTED"}, // Do not expose full public key in list/get
                            {"description", description}
                    };
                }

                static SSHKey fromJson(const nlohmann::json& j) {
                    SSHKey k;
                    k.id = j.value("id", "");
                    k.name = j.value("name", "");
                    k.publicKey = j.value("publicKey", ""); // Full key for internal use
                    k.description = j.value("description", "");
                    return k;
                }
            };

        } // namespace Models
    } // namespace Server
} // namespace NMC

#endif // NMC_SERVER_MODELS_SSH_KEY_H