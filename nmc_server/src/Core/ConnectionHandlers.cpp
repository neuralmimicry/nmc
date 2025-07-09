// nmc_server/src/Core/ConnectionHandlers.cpp
#include "ConnectionHandlers.h"
#include <iostream>
#include <algorithm> // For std::find_if, std::remove_if

namespace NMC::Server {

// Initialize static members
    std::vector<NMC::Server::Models::Connection> ConnectionHandlers::s_connections;
    std::string ConnectionHandlers::s_currentConnectionName = "";

// Helper to parse JSON from request body (robust version using nlohmann/json)
    nlohmann::json ConnectionHandlers::parseJsonBody(const std::string& body) {
        try {
            return nlohmann::json::parse(body);
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "JSON parsing error: " << e.what() << std::endl;
            // Throw a more general exception or handle it as per application's error strategy
            throw std::runtime_error("Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            std::cerr << "An unexpected error occurred during JSON parsing: " << e.what() << std::endl;
            throw std::runtime_error("An unexpected error occurred during JSON parsing: " + std::string(e.what()));
        }
    }

// Helper to simulate health check
    std::string ConnectionHandlers::simulateHealthCheck(const std::string& endpoint) {
        // In a real scenario, this would involve making an actual HTTP request
        // to the endpoint's health check API.
        // For demonstration, we'll just return "healthy" if the endpoint looks valid.
        if (endpoint.rfind("https://", 0) == 0 || endpoint.rfind("http://", 0) == 0) {
            return "healthy";
        }
        return "unhealthy";
    }

    void ConnectionHandlers::handleGetConnectionStatus(const httplib::Request& req, httplib::Response& res) {
        NMC::Server::Models::CloudResponse apiResponse;
        if (s_currentConnectionName.empty()) {
            apiResponse.success = false;
            apiResponse.message = "No active connection.";
            apiResponse.statusCode = 404;
        } else {
            auto it = std::find_if(s_connections.begin(), s_connections.end(),
                                   [](const NMC::Server::Models::Connection& conn) { return conn.name == s_currentConnectionName; });
            if (it != s_connections.end()) {
                it->healthStatus = simulateHealthCheck(it->endpoint); // Update health status
                apiResponse.success = true;
                apiResponse.message = "Current connection status.";
                apiResponse.data = it->toJsonString(); // Return connection details as JSON string
                apiResponse.statusCode = 200;
            } else {
                apiResponse.success = false;
                apiResponse.message = "Active connection '" + s_currentConnectionName + "' not found in stored connections. Inconsistency detected.";
                apiResponse.statusCode = 500; // Internal server error
            }
        }
        res.set_content(apiResponse.toJsonString(), "application/json");
        res.status = apiResponse.statusCode;
    }

    void ConnectionHandlers::handleMakeConnection(const httplib::Request& req, httplib::Response& res) {
        NMC::Server::Models::CloudResponse apiResponse;
        try {
            // Parse the request body using the robust JSON parser
            nlohmann::json params = parseJsonBody(req.body);

            // Extract parameters robustly, providing default values or checking existence
            // using .value() with a default, or .at() with a try-catch for missing mandatory fields
            std::string name;
            if (params.contains("name") && params["name"].is_string()) {
                name = params["name"].get<std::string>();
            } else {
                apiResponse.success = false;
                apiResponse.message = "Connection name is required and must be a string.";
                apiResponse.statusCode = 400; // Bad request
                res.set_content(apiResponse.toJsonString(), "application/json");
                res.status = apiResponse.statusCode;
                return;
            }

            std::string endpoint;
            if (params.contains("endpoint") && params["endpoint"].is_string()) {
                endpoint = params["endpoint"].get<std::string>();
            } else {
                apiResponse.success = false;
                apiResponse.message = "Endpoint is required and must be a string.";
                apiResponse.statusCode = 400; // Bad request
                res.set_content(apiResponse.toJsonString(), "application/json");
                res.status = apiResponse.statusCode;
                return;
            }

            bool setDefault = false; // Default to false if not provided or not boolean
            if (params.contains("setDefault") && params["setDefault"].is_boolean()) {
                setDefault = params["setDefault"].get<bool>();
            } else if (params.contains("setDefault") && params["setDefault"].is_string()) {
                // Handle cases where 'true'/'false' might be sent as strings for backward compatibility
                std::string setDefaultStr = params["setDefault"].get<std::string>();
                if (setDefaultStr == "true") setDefault = true;
                else if (setDefaultStr == "false") setDefault = false;
                // Else, it's a string but not 'true'/'false', default to false
            }


            if (name.empty() || endpoint.empty()) {
                // This check is partly redundant due to the checks above, but good as a final safeguard
                apiResponse.success = false;
                apiResponse.message = "Connection name and endpoint cannot be empty.";
                apiResponse.statusCode = 400; // Bad request
                res.set_content(apiResponse.toJsonString(), "application/json");
                res.status = apiResponse.statusCode;
                return;
            }

            auto it = std::find_if(s_connections.begin(), s_connections.end(),
                                   [&name](const NMC::Server::Models::Connection& conn) { return conn.name == name; });

            if (it != s_connections.end()) {
                apiResponse.success = false;
                apiResponse.message = "Connection with name '" + name + "' already exists.";
                apiResponse.statusCode = 409; // Conflict
            } else {
                NMC::Server::Models::Connection newConn(name, endpoint, setDefault, simulateHealthCheck(endpoint));
                s_connections.push_back(newConn);

                if (setDefault) {
                    // Deactivate all other connections
                    for (auto& conn : s_connections) {
                        if (conn.name != name) {
                            conn.isActive = false;
                        }
                    }
                    s_currentConnectionName = name;
                    apiResponse.message = "Connection '" + name + "' created and set as default.";
                } else {
                    apiResponse.message = "Connection '" + name + "' created.";
                }
                apiResponse.success = true;
                apiResponse.statusCode = 201; // Created
                apiResponse.data = newConn.toJsonString();
            }
        } catch (const std::runtime_error& e) {
            // Catch parsing errors from parseJsonBody or other runtime issues
            apiResponse.success = false;
            apiResponse.message = std::string("Error processing request: ") + e.what();
            apiResponse.statusCode = 400; // Bad Request for parsing errors, or 500 for internal
        } catch (const nlohmann::json::exception& e) {
            // Catch nlohmann::json specific exceptions (e.g., .at() on missing key)
            apiResponse.success = false;
            apiResponse.message = std::string("Error extracting JSON parameters: ") + e.what();
            apiResponse.statusCode = 400; // Bad Request
        } catch (const std::exception& e) {
            apiResponse.success = false;
            apiResponse.message = std::string("An unexpected error occurred: ") + e.what();
            apiResponse.statusCode = 500; // Internal Server Error
        }
        res.set_content(apiResponse.toJsonString(), "application/json");
        res.status = apiResponse.statusCode;
    }

    void ConnectionHandlers::handleDropConnection(const httplib::Request& req, httplib::Response& res) {
        NMC::Server::Models::CloudResponse apiResponse;
        // Assuming name is retrieved from URL path parameters, which httplib handles.
        // No JSON parsing needed here for 'name'.
        std::string name;
        if (req.matches.size() > 1) {
            // matches[0] is the whole path, [1] is our ([^/]+) group
            name = req.matches[1];
        }
        if (name.empty()) {
            apiResponse.success   = false;
            apiResponse.message   = "Connection name missing or empty in URL path.";
            apiResponse.statusCode = 400;
            res.set_content(apiResponse.toJsonString(), "application/json");
            res.status = apiResponse.statusCode;
            return;
        }

        auto initial_size = s_connections.size();
        s_connections.erase(std::remove_if(s_connections.begin(), s_connections.end(),
                                           [&name](const NMC::Server::Models::Connection& conn) { return conn.name == name; }),
                            s_connections.end());

        if (s_connections.size() < initial_size) {
            if (s_currentConnectionName == name) {
                s_currentConnectionName = ""; // Dropped the current active connection
                apiResponse.message = "Connection '" + name + "' dropped. No active connection now.";
            } else {
                apiResponse.message = "Connection '" + name + "' dropped.";
            }
            apiResponse.success = true;
            apiResponse.statusCode = 200;
        } else {
            apiResponse.success = false;
            apiResponse.message = "Connection with name '" + name + "' not found.";
            apiResponse.statusCode = 404; // Not found
        }
        res.set_content(apiResponse.toJsonString(), "application/json");
        res.status = apiResponse.statusCode;
    }

    void ConnectionHandlers::handleListConnections(const httplib::Request& req, httplib::Response& res) {
        NMC::Server::Models::CloudResponse apiResponse;
        // Construct the JSON array using nlohmann::json for robustness
        nlohmann::json connectionsJsonArray = nlohmann::json::array();

        for (auto& conn : s_connections) { // Iterate by reference to update health status
            conn.healthStatus = simulateHealthCheck(conn.endpoint); // Update health status
            // Assuming NMC::Server::Models::Connection::toJsonString() returns a valid JSON string
            // that can be parsed back into a nlohmann::json object.
            // A better approach would be for Connection::toJson() to return nlohmann::json directly.
            try {
                connectionsJsonArray.push_back(nlohmann::json::parse(conn.toJsonString()));
            } catch (const nlohmann::json::parse_error& e) {
                std::cerr << "Error converting Connection to JSON for listing: " << e.what() << std::endl;
                // Decide how to handle this: skip, add an error object, etc.
                // For robustness, perhaps add a placeholder or log the issue.
                // For now, we'll just log and continue, which might lead to incomplete JSON.
            }
        }

        apiResponse.success = true;
        apiResponse.message = "Connections listed successfully.";
        apiResponse.data = connectionsJsonArray.dump(); // Convert nlohmann::json array to string
        apiResponse.statusCode = 200;

        res.set_content(apiResponse.toJsonString(), "application/json");
        res.status = apiResponse.statusCode;
    }

    void ConnectionHandlers::handleSelectConnection(const httplib::Request& req, httplib::Response& res) {
        NMC::Server::Models::CloudResponse apiResponse;
        try {
            nlohmann::json params = parseJsonBody(req.body);

            std::string name;
            if (params.contains("name") && params["name"].is_string()) {
                name = params["name"].get<std::string>();
            } else {
                apiResponse.success = false;
                apiResponse.message = "Connection name is required and must be a string.";
                apiResponse.statusCode = 400;
                res.set_content(apiResponse.toJsonString(), "application/json");
                res.status = apiResponse.statusCode;
                return;
            }

            if (name.empty()) {
                apiResponse.success = false;
                apiResponse.message = "Connection name cannot be empty.";
                apiResponse.statusCode = 400;
                res.set_content(apiResponse.toJsonString(), "application/json");
                res.status = apiResponse.statusCode;
                return;
            }

            auto it = std::find_if(s_connections.begin(), s_connections.end(),
                                   [&name](const NMC::Server::Models::Connection& conn) { return conn.name == name; });

            if (it == s_connections.end()) {
                apiResponse.success = false;
                apiResponse.message = "Connection with name '" + name + "' not found.";
                apiResponse.statusCode = 404;
            } else {
                // Deactivate old active connection
                for (auto& conn : s_connections) {
                    conn.isActive = false;
                }
                // Set new active connection
                it->isActive = true;
                s_currentConnectionName = name;
                apiResponse.success = true;
                apiResponse.message = "Connection '" + name + "' selected as default.";
                apiResponse.statusCode = 200;
            }
        } catch (const std::runtime_error& e) {
            apiResponse.success = false;
            apiResponse.message = std::string("Error processing request: ") + e.what();
            apiResponse.statusCode = 400;
        } catch (const nlohmann::json::exception& e) {
            apiResponse.success = false;
            apiResponse.message = std::string("Error extracting JSON parameters: ") + e.what();
            apiResponse.statusCode = 400;
        } catch (const std::exception& e) {
            apiResponse.success = false;
            apiResponse.message = std::string("An unexpected error occurred: ") + e.what();
            apiResponse.statusCode = 500;
        }
        res.set_content(apiResponse.toJsonString(), "application/json");
        res.status = apiResponse.statusCode;
    }

} // namespace NMC::Server