// nmc_client/src/Models/CloudResponse.h
#ifndef NMC_CLIENT_MODELS_CLOUD_RESPONSE_H
#define NMC_CLIENT_MODELS_CLOUD_RESPONSE_H

#include <string>
#include <nlohmann/json.hpp> // Include nlohmann/json for JSON data handling

namespace NMC {
    namespace Models {

// Generic structure for responses from the simulated cloud API.
        struct CloudResponse {
            bool success;
            std::string message;
            nlohmann::json data; // Change this to nlohmann::json to match server

            // Helper to convert to JSON (if needed, mainly for debugging or re-serializing)
            nlohmann::json toJson() const {
                return {
                        {"success", success},
                        {"message", message},
                        {"data", data}
                };
            }
        };

    } // namespace Models
} // namespace NMC

#endif // NMC_CLIENT_MODELS_CLOUD_RESPONSE_H
