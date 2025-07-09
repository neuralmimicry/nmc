// server/Models/Bucket.h
// Example model definition for server side. Similar to client.
#ifndef NMC_SERVER_MODELS_BUCKET_H
#define NMC_SERVER_MODELS_BUCKET_H

#include <string>
#include <nlohmann/json.hpp>

namespace NMC {
    namespace Server {
        namespace Models {

            struct Bucket {
                std::string name;
                std::string location;
                std::string type;
                std::string status;

                // Convert Bucket object to JSON
                nlohmann::json toJsonString() const {
                    return {
                            {"name", name},
                            {"location", location},
                            {"type", type},
                            {"status", status}
                    };
                }

                // Create Bucket object from JSON
                static Bucket fromJson(const nlohmann::json& j) {
                    Bucket b;
                    b.name = j.value("name", "");
                    b.location = j.value("location", "");
                    b.type = j.value("type", "");
                    b.status = j.value("status", "");
                    return b;
                }
            };

        } // namespace Models
    } // namespace Server
} // namespace NMC

#endif // NMC_SERVER_MODELS_BUCKET_H