// nmc_server/src/Models/Connection.h
// This file defines the Connection model for the server, mirroring the client.
#ifndef NMC_SERVER_CONNECTION_H
#define NMC_SERVER_CONNECTION_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp> // For JSON serialization, if needed

namespace NMC {
    namespace Server {
        namespace Models {

            struct Connection {
                std::string name;
                std::string endpoint;
                bool isActive;
                // Add a health status for the server to report back
                std::string healthStatus; // e.g., "healthy", "unhealthy", "unknown"

                Connection() : name(""), endpoint(""), isActive(false), healthStatus("unknown") {}

                Connection(std::string name, std::string endpoint, bool isActive, std::string healthStatus = "unknown")
                        : name(std::move(name)), endpoint(std::move(endpoint)), isActive(isActive),
                          healthStatus(std::move(healthStatus)) {}

                // Helper to convert to a simple JSON string
                std::string toJsonString() const {
                    std::string json = "{";
                    json += "\"name\": \"" + name + "\",";
                    json += "\"endpoint\": \"" + endpoint + "\",";
                    json += "\"isActive\": " + std::string(isActive ? "true" : "false") + ",";
                    json += "\"healthStatus\": \"" + healthStatus + "\"";
                    json += "}";
                    return json;
                }
            };
        }
    }

} // namespace NMC::Server::Models

#endif // NMC_SERVER_CONNECTION_H