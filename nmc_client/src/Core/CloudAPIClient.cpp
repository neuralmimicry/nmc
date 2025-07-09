// nmc_client/src/Core/CloudAPIClient.cpp
#include "CloudAPIClient.h"
#include "httplib.h"
#include <iostream>
#include <nlohmann/json.hpp> // For JSON parsing and creation
#include <algorithm> // For std::find_if, std::remove_if
#include <fstream>   // For file operations (std::ifstream, std::ofstream)
#include <filesystem> // For creating directories (C++17)


namespace NMC::Core {

    //------------------------------------------------------------------------------
// Helper: parse "http://host:port/..." into host+port and rebuild cli
    static void initCliFromUrl(std::unique_ptr<httplib::Client>& cli,
                               const std::string& url,
                               int fallbackPort)
    {
        // strip scheme
        std::string s = url;
        if (s.rfind("http://",0)==0)  s.erase(0,7);
        else if (s.rfind("https://",0)==0) s.erase(0,8);

        // drop any path
        auto slash = s.find('/');
        if (slash != std::string::npos) s.resize(slash);

        // split host:port
        auto colon = s.find(':');
        std::string host = s.substr(0, colon);
        int port = (colon==std::string::npos)
                   ? fallbackPort
                   : std::stoi(s.substr(colon+1));

        cli = std::make_unique<httplib::Client>(host, port);
        cli->set_connection_timeout(5);
        cli->set_read_timeout(180);
        cli->set_write_timeout(180);
    }

    // Constructor: Initializes the httplib client with the server's host and port.
    CloudAPIClient::CloudAPIClient(const std::string& host, int port, const std::string& filePath)
            : host(host), port(port), configFilePath(filePath) { // Initialize configFilePath
        loadConnections(false); // Load connections on construction
        cli = std::make_unique<httplib::Client>(host, port);
        for (const auto& conn : connectionsConfig["connections"]) {
            if (conn["name"] == currentConnectionName) {
                // Assuming endpoint is a full URL like "http://192.168.1.72:8080"
                initCliFromUrl(cli, conn["endpoint"].get<std::string>(), port);
                break;
            }
        }
    }

// Destructor: Cleans up the httplib client.
    CloudAPIClient::~CloudAPIClient() {
        // std::unique_ptr automatically handles deletion, so nothing explicit needed here
        // connections are saved automatically on modification, so no need to save here.
    }


// Helper function to process the HTTP response and convert it into a CloudResponse model.
    Models::CloudResponse CloudAPIClient::processHttpResponse(httplib::Result& res, const std::string& successMessage) const {
        Models::CloudResponse apiResponse;
        if (res) {
            apiResponse.success = res->status == 200; // HTTP 200 OK means success
            apiResponse.message = successMessage; // Default success message

            if (res->body.empty()) {
                apiResponse.data = nlohmann::json(); // Empty JSON object if no body
            } else {
                try {
                    apiResponse.data = nlohmann::json::parse(res->body);
                } catch (const nlohmann::json::parse_error& e) {
                    // If parsing fails, store the raw body and mark as unsuccessful
                    std::cerr << "JSON Parse Error for response body: " << e.what() << std::endl;
                    apiResponse.success = false;
                    apiResponse.message = "Failed to parse server response: " + std::string(e.what());
                    apiResponse.data = res->body; // Store raw body for debugging
                    return apiResponse;
                }
            }

            if (!apiResponse.success) {
                // If server returned an error status (e.g., 400, 404, 500)
                if (apiResponse.data.contains("message")) {
                    apiResponse.message = apiResponse.data["message"].get<std::string>();
                } else {
                    apiResponse.message = "Server responded with status " + std::to_string(res->status);
                }
            }
        } else {
            // HTTP request itself failed (e.g., connection error, timeout)
            apiResponse.success = false;
            apiResponse.message = "API Call Failed: " + httplib::to_string(res.error());
            apiResponse.data = nlohmann::json();
        }
        return apiResponse;
    }

    // Overload for processHttpResponse to allow custom data payload (e.g., for local operations)
    Models::CloudResponse CloudAPIClient::processHttpResponse(httplib::Result& res, const std::string& successMessage, const nlohmann::json& dataPayload) const {
        Models::CloudResponse apiResponse;
        apiResponse.success = true; // Assume success for local data processing, errors handled by message
        apiResponse.message = successMessage;
        apiResponse.data = dataPayload;
        // If the original 'res' was not successful, propagate that status if applicable,
        // but for locally generated data, 'success' typically comes from the logic, not HTTP status.
        if (res && res->status != 200) {
            apiResponse.success = false;
            apiResponse.message = "Operation completed with warnings: " + successMessage + " (HTTP Status: " + std::to_string(res->status) + ")";
            if (res->body.empty()) {
                apiResponse.data = nlohmann::json(); // Empty JSON object if no body
            } else {
                try {
                    apiResponse.data = nlohmann::json::parse(res->body); // Still try to parse the actual response body for more info
                } catch (const nlohmann::json::parse_error& e) {
                    apiResponse.data = res->body;
                }
            }
        } else if (!res) {
            apiResponse.success = false; // The original res was an error
            apiResponse.message = "Operation failed: " + successMessage + " (HTTP error: " + httplib::to_string(res.error()) + ")";
        }

        return apiResponse;
    }


// --- Bucket Operations ---

    Models::CloudResponse CloudAPIClient::createBucket(const std::string& name, const std::string& location, const std::string& type) {
        nlohmann::json request_body;
        request_body["name"] = name;
        request_body["location"] = location;
        request_body["type"] = type;

        auto res = cli->Post("/bucket/create", request_body.dump(), "application/json");
        return processHttpResponse(res, "Bucket '" + name + "' created successfully.");
    }

    Models::CloudResponse CloudAPIClient::deleteBucket(const std::string& name) {
        auto res = cli->Delete("/bucket/delete/" + name);
        return processHttpResponse(res, "Bucket '" + name + "' deleted successfully.");
    }

    Models::CloudResponse CloudAPIClient::getBucket(const std::string& name) {
        auto res = cli->Get("/bucket/get/" + name);
        return processHttpResponse(res, "Bucket details retrieved.");
    }

    Models::CloudResponse CloudAPIClient::listBuckets(const std::string& filterName) {
        std::string path = "/bucket/list";
        if (!filterName.empty()) {
            path += "?filter-name=" + filterName;
        }
        auto res = cli->Get(path);
        return processHttpResponse(res, "Buckets listed successfully.");
    }

    Models::CloudResponse CloudAPIClient::listBucketLocations() {
        auto res = cli->Get("/bucket/list-locations");
        return processHttpResponse(res, "Bucket locations listed.");
    }

    Models::CloudResponse CloudAPIClient::listBucketTypes() {
        auto res = cli->Get("/bucket/list-types");
        return processHttpResponse(res, "Bucket types listed.");
    }

    Models::CloudResponse CloudAPIClient::resetBucketKey(const std::string& name) {
        auto res = cli->Post("/bucket/reset-key/" + name);
        return processHttpResponse(res, "Bucket access credentials for '" + name + "' reset successfully.");
    }

// --- K8s Operations ---
    Models::CloudResponse CloudAPIClient::createK8sCluster(const std::string& name, const std::string& region) {
        nlohmann::json request_body;
        request_body["name"] = name;
        request_body["region"] = region;

        auto res = cli->Post("/k8s/create", request_body.dump(), "application/json");
        return processHttpResponse(res, "K8s cluster '" + name + "' created successfully.");
    }

    Models::CloudResponse CloudAPIClient::deleteK8sCluster(const std::string& id) {
        auto res = cli->Delete("/k8s/delete/" + id);
        return processHttpResponse(res, "K8s cluster '" + id + "' deleted successfully.");
    }

    Models::CloudResponse CloudAPIClient::getK8sCluster(const std::string& id) {
        auto res = cli->Get("/k8s/get/" + id);
        return processHttpResponse(res, "K8s cluster details retrieved.");
    }

    Models::CloudResponse CloudAPIClient::getKubeConfig(const std::string& id) {
        auto res = cli->Get("/k8s/get-config/" + id);
        return processHttpResponse(res, "KubeConfig retrieved.");
    }

    Models::CloudResponse CloudAPIClient::listK8sClusters(const std::string& filterName) {
        std::string path = "/k8s/list";
        if (!filterName.empty()) {
            path += "?filter-name=" + filterName;
        }
        // 1) do the call
        auto raw = cli->Get(path);
        auto apiResp = processHttpResponse(raw, "K8s clusters listed.");

        // 2) extract the array
        nlohmann::json clusters = nlohmann::json::array();
        if (apiResp.success) {
            // success case: may be old shape or new envelope
            if (apiResp.data.contains("clusters") && apiResp.data["clusters"].is_array()) {
                clusters = apiResp.data["clusters"];
            }
            else if (apiResp.data.contains("data")
                && apiResp.data["data"].contains("clusters")
                && apiResp.data["data"]["clusters"].is_array()) {
                    clusters = apiResp.data["data"]["clusters"];
                }
            } else {
                // even on failure we might still have data.data.clusters
                if (apiResp.data.contains("data")
                    && apiResp.data["data"].contains("clusters")
                    && apiResp.data["data"]["clusters"].is_array()) {
                        clusters = apiResp.data["data"]["clusters"];
                    }
            }

            // 3) overwrite .data so everyone downstream just sees an array
            apiResp.data = std::move(clusters);
            return apiResp;
        }

    Models::CloudResponse CloudAPIClient::listK8sLocations(const std::string& filterSku) {
        // The server-side listK8sLocations does not use filterSku, but keeping it for now
        // for API consistency if the server API might evolve to use it.
        std::string path = "/k8s/list-locations";
        // if (!filterSku.empty()) {
        //     path += "?filter-sku=" + filterSku; // Server's current implementation doesn't use this
        // }
        auto res = cli->Get(path);
        return processHttpResponse(res, "K8s locations listed.");
    }

    Models::CloudResponse CloudAPIClient::resumeK8sCluster(const std::string& id) {
        auto res = cli->Post("/k8s/resume/" + id, "", "application/json"); // Empty body for POST
        return processHttpResponse(res, "K8s cluster '" + id + "' resumed.");
    }

    Models::CloudResponse CloudAPIClient::suspendK8sCluster(const std::string& id) {
        auto res = cli->Post("/k8s/suspend/" + id, "", "application/json"); // Empty body for POST
        return processHttpResponse(res, "K8s cluster '" + id + "' suspended.");
    }

// --- Model Operations ---
    Models::CloudResponse CloudAPIClient::uploadModel(const std::string& filePath, const std::string& sku, const std::string& registryName, const std::string& tag) {
        nlohmann::json request_body;
        request_body["filePath"] = filePath;
        request_body["sku"] = sku;
        request_body["registryName"] = registryName;
        request_body["tag"] = tag;

        auto res = cli->Post("/model/upload", request_body.dump(), "application/json");
        return processHttpResponse(res, "Model uploaded successfully.");
    }

// --- SSH Key Operations ---
    Models::CloudResponse CloudAPIClient::createSSHKey(const std::string& name, const std::string& publicKey, const std::string& description) {
        nlohmann::json request_body;
        request_body["name"] = name;
        request_body["publicKey"] = publicKey;
        request_body["description"] = description;

        auto res = cli->Post("/ssh/create", request_body.dump(), "application/json");
        return processHttpResponse(res, "SSH key '" + name + "' created successfully.");
    }

    Models::CloudResponse CloudAPIClient::deleteSSHKey(const std::string& id) {
        auto res = cli->Delete("/ssh/delete/" + id);
        return processHttpResponse(res, "SSH key '" + id + "' deleted successfully.");
    }

    Models::CloudResponse CloudAPIClient::listSSHKeys(const std::string& filterName) {
        std::string path = "/ssh/list";
        if (!filterName.empty()) {
            path += "?filter-name=" + filterName;
        }
        auto res = cli->Get(path);
        return processHttpResponse(res, "SSH keys listed.");
    }

    Models::CloudResponse CloudAPIClient::getSSHKey(const std::string& id) {
        auto res = cli->Get("/ssh/get/" + id);
        return processHttpResponse(res, "SSH key details retrieved.");
    }

// --- VM Operations ---
    Models::CloudResponse CloudAPIClient::createVM(const std::string& name, const std::string& sku, const std::string& region, const std::string& osImage, const std::string& publicKeyId, const std::string& initScript) {
        nlohmann::json request_body;
        request_body["name"] = name;
        request_body["sku"] = sku;
        request_body["region"] = region;
        request_body["osImage"] = osImage;
        request_body["publicKeyId"] = publicKeyId;
        request_body["initScript"] = initScript;

        auto res = cli->Post("/vm/create", request_body.dump(), "application/json");
        return processHttpResponse(res, "VM '" + name + "' created successfully.");
    }

    Models::CloudResponse CloudAPIClient::deleteVM(const std::string& id) {
        auto res = cli->Delete("/vm/delete/" + id);
        return processHttpResponse(res, "VM '" + id + "' deleted successfully.");
    }

    Models::CloudResponse CloudAPIClient::getVM(const std::string& id) {
        auto res = cli->Get("/vm/get/" + id);
        return processHttpResponse(res, "VM details retrieved.");
    }

    Models::CloudResponse CloudAPIClient::listVMs(const std::string& filterName) {
        std::string path = "/vm/list";
        if (!filterName.empty()) {
            path += "?filter-name=" + filterName;
        }
        auto res = cli->Get(path);
        return processHttpResponse(res, "VMs listed.");
    }

    Models::CloudResponse CloudAPIClient::listVMLocations(const std::string& filterSku) {
        // The server-side listVMLocations does not use filterSku, but keeping it for API consistency
        std::string path = "/vm/list-locations";
        // if (!filterSku.empty()) {
        //     path += "?filter-sku=" + filterSku; // Server's current implementation doesn't use this
        // }
        auto res = cli->Get(path);
        return processHttpResponse(res, "VM locations listed.");
    }

    Models::CloudResponse CloudAPIClient::listVMOSImages(const std::string& filterSku) {
        std::string path = "/vm/list-os"; // Corrected path to match server
        if (!filterSku.empty()) {
            path += "?filter-sku=" + filterSku;
        }
        auto res = cli->Get(path);
        return processHttpResponse(res, "VM OS images listed.");
    }

    Models::CloudResponse CloudAPIClient::listVMSKUs(const std::string& filterSku) {
        std::string path = "/vm/list-sku"; // Corrected path to match server
        if (!filterSku.empty()) {
            path += "?filter-sku=" + filterSku;
        }
        auto res = cli->Get(path);
        return processHttpResponse(res, "VM SKUs listed.");
    }

    Models::CloudResponse CloudAPIClient::restartVM(const std::string& id) {
        auto res = cli->Post("/vm/restart/" + id, "", "application/json");
        return processHttpResponse(res, "VM '" + id + "' restarted successfully.");
    }

    Models::CloudResponse CloudAPIClient::resumeVM(const std::string& id) {
        auto res = cli->Post("/vm/resume/" + id, "", "application/json");
        return processHttpResponse(res, "VM '" + id + "' resumed successfully.");
    }

    Models::CloudResponse CloudAPIClient::suspendVM(const std::string& id) {
        auto res = cli->Post("/vm/suspend/" + id, "", "application/json");
        return processHttpResponse(res, "VM '" + id + "' suspended successfully.");
    }

// --- Connection Management ---
    Models::CloudResponse CloudAPIClient::getConnectionStatus() {
        if (currentConnectionName.empty()) {
            return {false, "No default connection set. Please use 'nmc connection make --default' or 'nmc connection select' to set one.", nlohmann::json::parse("{\"isActive\": false, \"status\": \"unset\"}")};
        }

        auto it = std::find_if(connections.begin(), connections.end(),
                               [this](const Models::Connection& conn) { return conn.name == currentConnectionName; });

        if (it == connections.end()) {
            // This indicates an inconsistency where currentConnectionName is set but the connection itself is not in the list.
            currentConnectionName = ""; // Clear inconsistent state
            saveConnections(true); // Save to reflect the cleared state
            return {false, "Current connection details not found, though a name was set. This indicates an inconsistency. Resetting default. Please select a connection.", nlohmann::json::parse("{\"isActive\": false, \"status\": \"inconsistent\"}")};
        }

        // Now, attempt to ping the endpoint to check health
        // Create a temporary httplib client for the specific connection's endpoint
        // Use a host and port derived from the connection's endpoint URL
        httplib::Client temp_cli(it->endpoint);
        temp_cli.set_connection_timeout(5);
        temp_cli.set_read_timeout(10);
        temp_cli.set_write_timeout(10);

        httplib::Result res = temp_cli.Get("/health"); // Assuming a /health endpoint for basic check

        Models::CloudResponse response;
        if (res && res->status == 200) {
            response.success = true;
            response.message = "Current connection: " + it->name + " (" + it->endpoint + ") is active and healthy.";
            response.data = nlohmann::json::parse("{\"name\": \"" + it->name + "\", \"endpoint\": \"" + it->endpoint + "\", \"isActive\": " + (it->isActive ? "true" : "false") + ", \"status\": \"healthy\"}");
        } else {
            response.success = false;
            response.message = "Connection to " + it->name + " (" + it->endpoint + ") failed or is unhealthy. Error: " + (res ? std::to_string(res->status) : httplib::to_string(res.error()));
            response.data = nlohmann::json::parse("{\"name\": \"" + it->name + "\", \"endpoint\": \"" + it->endpoint + "\", \"isActive\": " + (it->isActive ? "true" : "false") + ", \"status\": \"unhealthy\"}");
        }
        return response;
    }

    Models::CloudResponse CloudAPIClient::makeConnection(const std::string& name, const std::string& endpoint, bool setDefault) {
        auto it = std::find_if(connections.begin(), connections.end(),
                               [&name](const Models::Connection& conn) { return conn.name == name; });

        if (it != connections.end()) {
            return {false, "Connection with name '" + name + "' already exists. Use 'drop' first or choose a different name.", nlohmann::json()};
        }

        connections.emplace_back(name, endpoint, setDefault); // New connection
        if (setDefault) {
            // Deactivate old default connection and set new one
            for (auto& conn : connections) {
                if (conn.name != name) {
                    conn.isActive = false;
                }
            }
            currentConnectionName = name;
        }
        saveConnections(true); // Save changes after making a connection
        auto& newConn = connections.back();
        initCliFromUrl(cli, newConn.endpoint, port);
        return {true, "Connection '" + name + "' created" + (setDefault ? " and set as default." : "."), nlohmann::json::parse("{\"name\": \"" + name + "\", \"endpoint\": \"" + endpoint + "\", \"default_set\": " + (setDefault ? "true" : "false") + "}")};
    }

    Models::CloudResponse CloudAPIClient::dropConnection(const std::string& name) {
        auto initial_size = connections.size();
        connections.erase(std::remove_if(connections.begin(), connections.end(),
                                         [&name](const Models::Connection& conn) { return conn.name == name; }),
                          connections.end());

        if (connections.size() < initial_size) {
            if (currentConnectionName == name) {
                currentConnectionName = ""; // Dropped the current active connection
                saveConnections(true); // Save to reflect the cleared state
                return {true, "Connection '" + name + "' dropped. No default connection now.", nlohmann::json::parse("{\"dropped_name\": \"" + name + "\", \"default_unset\": true}")};
            }
            saveConnections(true); // Save changes after dropping a connection
            return {true, "Connection '" + name + "' dropped.", nlohmann::json::parse("{\"dropped_name\": \"" + name + "\", \"default_unset\": false}")};
        }
        return {false, "Connection with name '" + name + "' not found.", nlohmann::json()};
    }

    Models::CloudResponse CloudAPIClient::listConnections() {
        /// loadConnections(false); // Load latest connections from file
        nlohmann::json connections_json = nlohmann::json::array();
        for (const auto& conn : connections) {
            connections_json.push_back({
                                               {"name", conn.name},
                                               {"endpoint", conn.endpoint},
                                               {"isActive", conn.isActive},
                                               {"isDefault", conn.isActive && (conn.name == currentConnectionName)}
                                       });
        }
        std::string message;
        if (connections_json.empty()) {
            message = "No connections available.";
        } else {
            message = "Connections: " + std::to_string(connections_json.size());
        }
        return {true, message, connections_json};
    }

    Models::CloudResponse CloudAPIClient::selectConnection(const std::string& name) {
        auto it = std::find_if(connections.begin(), connections.end(),
                               [&name](const Models::Connection& conn) { return conn.name == name; });

        if (it == connections.end()) {
            return {false, "Connection with name '" + name + "' not found.", nlohmann::json()};
        }

        // Deactivate old active connection
        for (auto& conn : connections) {
            conn.isActive = false;
        }
        // Set new active connection
        it->isActive = true;
        currentConnectionName = name;
        saveConnections(true); // Save changes after selecting a connection

        return {true, "Connection '" + name + "' selected as default.", nlohmann::json::parse("{\"selected_name\": \"" + name + "\", \"endpoint\": \"" + it->endpoint + "\"}")};
    }

    Models::CloudResponse CloudAPIClient::unsetDefaultConnection() {
        if (currentConnectionName.empty()) {
            return {true, "No default connection is currently set.", nlohmann::json()};
        }

        // Find the current active connection and deactivate it
        auto it = std::find_if(connections.begin(), connections.end(),
                               [this](const Models::Connection& conn) { return conn.name == currentConnectionName; });
        if (it != connections.end()) {
            it->isActive = false;
        }
        std::string unset_name = currentConnectionName;
        currentConnectionName = ""; // Clear the default connection name
        saveConnections(true); // Save changes after unsetting default
        // no default: go back to the original host:port
        cli = std::make_unique<httplib::Client>(host, port);
        cli->set_connection_timeout(5);
        cli->set_read_timeout(180);
        cli->set_write_timeout(180);
        return {true, "Default connection '" + unset_name + "' unset successfully.", nlohmann::json::parse("{\"unset_name\": \"" + unset_name + "\"}")};
    }

    // Method to check if a default connection is set
    bool CloudAPIClient::hasDefaultConnection() const {
        return !currentConnectionName.empty();
    }

    // Method to get the current default connection details
    std::optional<Models::Connection> CloudAPIClient::getDefaultConnection() const {
        if (currentConnectionName.empty()) {
            return std::nullopt;
        }
        auto it = std::find_if(connections.begin(), connections.end(),
                               [this](const Models::Connection& conn) { return conn.name == currentConnectionName; });
        if (it != connections.end()) {
            return *it;
        }
        return std::nullopt; // Should not happen if currentConnectionName is consistent
    }

    // --- Persistence Methods ---

    void CloudAPIClient::loadConnections(bool showMessage) {
        std::ifstream configFile(configFilePath);
        if (!configFile.is_open()) {
            if (showMessage == true) {
                std::cout << "Info: Configuration file not found. Creating a new one at " << configFilePath
                          << std::endl;
            }

            // Ensure the directory exists (e.g., ~/.nmc/)
            try {
                std::filesystem::path path(configFilePath);
                if (path.has_parent_path()) {
                    std::filesystem::create_directories(path.parent_path());
                }
            } catch (const std::filesystem::filesystem_error &e) {
                std::cerr << "Error: Could not create directory for config file: " << e.what() << std::endl;
                // Even if directory creation fails, proceed to create an in-memory config
            }

            // Create a default, empty configuration structure
            connectionsConfig["connections"] = nlohmann::json::array();
            connectionsConfig["default_connection"] = "";

            // Save the new empty config file, but don't show a "saved" message
            saveConnections(false);
            return;
        }

        try {
            configFile >> connectionsConfig;
            std::cout << "Info: Connections loaded from " << configFilePath << std::endl;
        } catch (const nlohmann::json::parse_error &e) {
            std::cerr << "Error: Could not parse config file " << configFilePath << ". It might be corrupted. "
                      << e.what() << std::endl;
            // Initialize with an empty config to prevent crashes
            connectionsConfig["connections"] = nlohmann::json::array();
            connectionsConfig["default_connection"] = "";
        }
        // --- SYNC JSON → VECTOR + DEFAULT NAME ---
        connections.clear();
        for (auto &j: connectionsConfig["connections"]) {
            Models::Connection c;
            c.name = j["name"].get<std::string>();
            c.endpoint = j["endpoint"].get<std::string>();
            c.isActive = j.value("isActive", false);
            connections.push_back(std::move(c));
        }
        currentConnectionName = connectionsConfig.value("default_connection", "");
    }

    void CloudAPIClient::saveConnections(bool showMessage) {
        // --- SYNC VECTOR + DEFAULT NAME → JSON ---
        nlohmann::json arr = nlohmann::json::array();
            for (auto& conn : connections) {
                arr.push_back({
                    {"name",     conn.name},
                    {"endpoint", conn.endpoint},
                    {"isActive", conn.isActive}
                 });
            }
        connectionsConfig["connections"]           = std::move(arr);
        connectionsConfig["default_connection"] = currentConnectionName;

        std::ofstream configFile(configFilePath);
        if (!configFile.is_open()) {
            std::cerr << "Error: Could not open config file for writing at " << configFilePath << std::endl;
            return;
        }
        configFile << connectionsConfig.dump(4); // Pretty-print JSON with 4-space indent
        if (showMessage) {
            std::cout << "Info: Connections saved to " << configFilePath << std::endl;
        }
    }


} // namespace NMC::Core