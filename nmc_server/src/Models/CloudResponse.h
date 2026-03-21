// server/Models/CloudResponse.h
// Reusing the CloudResponse model for consistency between client and server.
#ifndef NMC_SERVER_MODELS_CLOUD_RESPONSE_H
#define NMC_SERVER_MODELS_CLOUD_RESPONSE_H

#include <string>
#include <nlohmann/json.hpp> // Include nlohmann/json

namespace NMC {
    namespace Server {
        namespace Models {

// Generic structure for responses from the simulated cloud API.
            struct CloudResponse {
                bool success;
                std::string message;
                nlohmann::json data; // Now can hold any JSON data
                int statusCode; // HTTP status code

                CloudResponse() : success(false), message(""), data(nlohmann::json::object()), statusCode(200) {}

                // Helper to convert to JSON
                nlohmann::json toJsonString() const {
                    return {
                            {"success", success},
                            {"message", message},
                            {"data", data}
                    };
                }
            };

        } // namespace Models
    } // namespace Server
} // namespace NMC

#endif // NMC_SERVER_MODELS_CLOUD_RESPONSE_H
