#include "CloudAPIClient.h"
#include "httplib.h"
#include <iostream>
#include <nlohmann/json.hpp> // For JSON parsing and creation
#include <algorithm> // For std::find_if, std::remove_if
#include <fstream>   // For file operations (std::ifstream, std::ofstream)
#include <filesystem> // For creating directories (C++17)
#include <array>
#include <cstdlib>

namespace {
    // Read environment variables without throwing or returning nullptrs.
    std::string getenvOrEmpty(const char* key) {
        const char* value = std::getenv(key);
        return value ? std::string(value) : std::string();
    }

    // Append positive integer query parameters while preserving existing query strings.
    void appendQueryInt(std::string& path, const std::string& key, int value) {
        if (value <= 0) {
            return;
        }
        path += (path.find('?') == std::string::npos) ? "?" : "&";
        path += key + "=" + std::to_string(value);
    }

    // Append string query parameters while preserving existing query strings.
    void appendQueryString(std::string& path, const std::string& key, const std::string& value) {
        if (value.empty()) {
            return;
        }
        path += (path.find('?') == std::string::npos) ? "?" : "&";
        path += key + "=" + value;
    }

    void appendTraceySimulationQuery(std::string& path, const NMC::Core::TraceySimulationQuery& simulation) {
        appendQueryInt(path, "simulation_nodes", simulation.simulationNodes);
        appendQueryInt(path, "simulation_gpus", simulation.simulationGpus);
        appendQueryInt(path, "simulation_cores", simulation.simulationCores);
        appendQueryString(path, "simulation_strategy", simulation.simulationStrategy);
    }

    // Normalize heterogeneous cluster list payload shapes to a plain array.
    nlohmann::json extractClusterArray(const nlohmann::json& payload) {
        if (payload.is_array()) {
            return payload;
        }
        if (!payload.is_object()) {
            return nlohmann::json::array();
        }
        if (payload.contains("data") && payload["data"].is_array()) {
            return payload["data"];
        }
        if (payload.contains("clusters") && payload["clusters"].is_array()) {
            return payload["clusters"];
        }
        if (payload.contains("data") && payload["data"].is_object() &&
            payload["data"].contains("clusters") && payload["data"]["clusters"].is_array()) {
            return payload["data"]["clusters"];
        }
        return nlohmann::json::array();
    }

    // Fallback resolver for older servers that do not expose /<provider>/details routes.
    NMC::Models::CloudResponse resolveClusterDetailsFromList(const std::string& backendLabel,
                                                             const std::string& idOrName,
                                                             const NMC::Models::CloudResponse& listResponse) {
        if (!listResponse.success) {
            return listResponse;
        }

        const nlohmann::json clusters = extractClusterArray(listResponse.data);
        nlohmann::json selected = nullptr;
        for (const auto& cluster : clusters) {
            if (!cluster.is_object()) {
                continue;
            }
            const std::string name = cluster.value("name", "");
            const std::string id = cluster.value("id", cluster.value("cluster_id", cluster.value("uuid", "")));
            if (name == idOrName || id == idOrName) {
                selected = cluster;
                break;
            }
        }

        NMC::Models::CloudResponse fallback;
        if (selected.is_null()) {
            fallback.success = false;
            fallback.message = backendLabel + " cluster '" + idOrName + "' not found.";
            fallback.data = nlohmann::json::object();
            fallback.statusCode = 404;
            return fallback;
        }

        fallback.success = true;
        fallback.message = backendLabel + " cluster details retrieved.";
        fallback.data = selected;
        fallback.statusCode = 200;
        return fallback;
    }

    // Unwrap the server's standard {success,message,data} envelope when present.
    nlohmann::json unwrapEnvelopeData(const nlohmann::json& payload) {
        if (payload.is_object() && payload.contains("data")) {
            return payload["data"];
        }
        return payload;
    }

    void unwrapStandardEnvelopeResponse(NMC::Models::CloudResponse& response) {
        if (!response.data.is_object()) {
            return;
        }
        if (response.data.contains("message") && response.data["message"].is_string()) {
            response.message = response.data["message"].get<std::string>();
        }
        response.data = unwrapEnvelopeData(response.data);
    }
}

namespace NMC::Core {

    // Parse "http(s)://host:port/path" into host/port and rebuild a client instance.
    // Falls back to ctor host/port when endpoint parsing fails or omits a component.
    static bool initCliFromUrl(std::unique_ptr<httplib::Client>& cli,
                               const std::string& url,
                               const std::string& fallbackHost,
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
        int port = fallbackPort;
        if (colon != std::string::npos) {
            try {
                port = std::stoi(s.substr(colon + 1));
            } catch (const std::exception&) {
                port = fallbackPort;
            }
        }
        if (host.empty()) {
            host = fallbackHost;
        }

        cli = std::make_unique<httplib::Client>(host, port);
        cli->set_connection_timeout(5);
        cli->set_read_timeout(180);
        cli->set_write_timeout(180);
        return true;
    }

    // Constructor bootstrap sequence:
    // 1) load persisted local connections
    // 2) create fallback client from ctor host/port
    // 3) switch to persisted default connection endpoint when present
    // 4) apply auth headers from resolved token source
    CloudAPIClient::CloudAPIClient(const std::string& host, int port, const std::string& filePath)
            : host(host), port(port), configFilePath(filePath) {
        loadConnections(false);
        cli = std::make_unique<httplib::Client>(host, port);
        cli->set_connection_timeout(5);
        cli->set_read_timeout(180);
        cli->set_write_timeout(180);
        for (const auto& conn : connectionsConfig["connections"]) {
            if (conn["name"] == currentConnectionName) {
                // Assuming endpoint is a full URL like "http://192.168.1.72:8080"
                initCliFromUrl(cli, conn["endpoint"].get<std::string>(), host, port);
                break;
            }
        }
        applyAuthHeaders();
    }

// Destructor: Cleans up the httplib client.
    CloudAPIClient::~CloudAPIClient() {
        // std::unique_ptr automatically handles deletion, so nothing explicit needed here
        // connections are saved automatically on modification, so no need to save here.
    }


// Convert transport outcomes into the command-facing envelope.
// Non-JSON upstream responses are treated as failures to prevent silent corruption.
    Models::CloudResponse CloudAPIClient::processHttpResponse(httplib::Result& res, const std::string& successMessage) const {
        Models::CloudResponse apiResponse;
        if (res) {
            apiResponse.success = (res->status >= 200 && res->status < 300); // Any 2xx is success
            apiResponse.message = successMessage; // Default success message

            if (res->body.empty()) {
                apiResponse.data = nlohmann::json(); // Empty JSON object if no body
            } else {
                try {
                    apiResponse.data = nlohmann::json::parse(res->body);
                } catch (const nlohmann::json::parse_error& e) {
                // JSON decode failure is treated as a hard error for CLI safety.
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

    // Overload used by local-only operations where payload is generated client-side.
    Models::CloudResponse CloudAPIClient::processHttpResponse(httplib::Result& res, const std::string& successMessage, const nlohmann::json& dataPayload) const {
        Models::CloudResponse apiResponse;
        apiResponse.success = true; // Assume success for local data processing, errors handled by message
        apiResponse.message = successMessage;
        apiResponse.data = dataPayload;
        // If the original 'res' was not successful, propagate that status if applicable,
        // but for locally generated data, 'success' typically comes from the logic, not HTTP status.
        if (res && (res->status < 200 || res->status >= 300)) {
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
        auto res = cli->Get("/k8s/details/" + id);
        auto apiResponse = processHttpResponse(res, "K8s cluster details retrieved.");
        if (apiResponse.success || (res && res->status != 404)) {
            return apiResponse;
        }

        // Backward compatibility for older servers that do not expose /k8s/details.
        auto fallbackRes = cli->Get("/k8s/get/" + id);
        return processHttpResponse(fallbackRes, "K8s cluster details retrieved.");
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
        // Failure: still return a consistent array payload for callers.
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

    Models::CloudResponse CloudAPIClient::getK8sHealth() {
        auto res = cli->Get("/k8s/healthz");
        return processHttpResponse(res, "K8s health status retrieved.");
    }

    Models::CloudResponse CloudAPIClient::getRefinerDeploymentStatus(
            const std::string& namespaceName,
            const std::string& deploymentName
    ) {
        std::string path = "/k8s/refiner/status";
        bool hasQuery = false;
        if (!namespaceName.empty()) {
            path += "?namespace=" + namespaceName;
            hasQuery = true;
        }
        if (!deploymentName.empty()) {
            path += hasQuery ? "&" : "?";
            path += "deployment=" + deploymentName;
        }
        auto res = cli->Get(path);
        return processHttpResponse(res, "Refiner deployment status retrieved.");
    }

    Models::CloudResponse CloudAPIClient::scaleRefinerDeployment(
            int replicas,
            const std::string& namespaceName,
            const std::string& deploymentName
    ) {
        nlohmann::json requestBody;
        requestBody["replicas"] = replicas;
        if (!namespaceName.empty()) {
            requestBody["namespace"] = namespaceName;
        }
        if (!deploymentName.empty()) {
            requestBody["deployment"] = deploymentName;
        }
        auto res = cli->Post("/k8s/refiner/scale", requestBody.dump(), "application/json");
        return processHttpResponse(res, "Refiner deployment scaling requested.");
    }

    Models::CloudResponse CloudAPIClient::resumeK8sCluster(const std::string& id) {
        auto res = cli->Post("/k8s/resume/" + id, "", "application/json"); // Empty body for POST
        return processHttpResponse(res, "K8s cluster '" + id + "' resumed.");
    }

    Models::CloudResponse CloudAPIClient::suspendK8sCluster(const std::string& id) {
        auto res = cli->Post("/k8s/suspend/" + id, "", "application/json"); // Empty body for POST
        return processHttpResponse(res, "K8s cluster '" + id + "' suspended.");
    }

// --- VCluster Operations ---
    Models::CloudResponse CloudAPIClient::createVCluster(const std::string& name, const std::string& vclusterNamespace) {
        nlohmann::json request_body;
        request_body["name"] = name;
        if (!vclusterNamespace.empty()) {
            request_body["namespace"] = vclusterNamespace;
        }
        auto res = cli->Post("/vcluster/create", request_body.dump(), "application/json");
        return processHttpResponse(res, "VCluster '" + name + "' created.");
    }

    Models::CloudResponse CloudAPIClient::deleteVCluster(const std::string& id) {
        auto res = cli->Delete("/vcluster/delete/" + id);
        return processHttpResponse(res, "VCluster '" + id + "' deleted.");
    }

    Models::CloudResponse CloudAPIClient::getVCluster(const std::string& id) {
        auto res = cli->Get("/vcluster/get/" + id);
        return processHttpResponse(res, "VCluster '" + id + "' retrieved.");
    }

    Models::CloudResponse CloudAPIClient::listVClusters(const std::string& filterName) {
        std::string endpoint = "/vcluster/list";
        if (!filterName.empty()) {
            endpoint += "?filter-name=" + filterName;
        }
        auto res = cli->Get(endpoint);
        return processHttpResponse(res, "VClusters listed.");
    }

    Models::CloudResponse CloudAPIClient::getVClusterKubeConfig(const std::string& id) {
        auto res = cli->Get("/vcluster/get-config/" + id);
        return processHttpResponse(res, "VCluster kubeconfig for '" + id + "' retrieved.");
    }

// --- VCluster Lifecycle Operations ---
    Models::CloudResponse CloudAPIClient::pauseVCluster(const std::string& id) {
        auto res = cli->Post("/vcluster/pause/" + id, "", "application/json");
        return processHttpResponse(res, "VCluster '" + id + "' paused.");
    }

    Models::CloudResponse CloudAPIClient::resumeVCluster(const std::string& id) {
        auto res = cli->Post("/vcluster/resume/" + id, "", "application/json");
        return processHttpResponse(res, "VCluster '" + id + "' resumed.");
    }

    Models::CloudResponse CloudAPIClient::backupVCluster(const std::string& id, const std::string& backupName) {
        nlohmann::json request_body;
        if (!backupName.empty()) {
            request_body["backup_name"] = backupName;
        }
        auto res = cli->Post("/vcluster/backup/" + id, request_body.dump(), "application/json");
        return processHttpResponse(res, "VCluster '" + id + "' backed up.");
    }

    Models::CloudResponse CloudAPIClient::restoreVCluster(const std::string& backupName, const std::string& targetName) {
        nlohmann::json request_body;
        request_body["backup_name"] = backupName;
        if (!targetName.empty()) {
            request_body["target_name"] = targetName;
        }
        auto res = cli->Post("/vcluster/restore", request_body.dump(), "application/json");
        return processHttpResponse(res, "VCluster restored from backup '" + backupName + "'.");
    }

    Models::CloudResponse CloudAPIClient::upgradeVCluster(const std::string& id, const std::string& newVersion) {
        nlohmann::json request_body;
        request_body["new_version"] = newVersion;
        auto res = cli->Post("/vcluster/upgrade/" + id, request_body.dump(), "application/json");
        return processHttpResponse(res, "VCluster '" + id + "' upgraded to version '" + newVersion + "'.");
    }

// --- VCluster Configuration Operations ---
    Models::CloudResponse CloudAPIClient::getVClusterConfig(const std::string& id) {
        auto res = cli->Get("/vcluster/config/" + id);
        return processHttpResponse(res, "VCluster configuration for '" + id + "' retrieved.");
    }

    Models::CloudResponse CloudAPIClient::updateVClusterConfig(const std::string& id, const nlohmann::json& config) {
        auto res = cli->Put("/vcluster/config/" + id, config.dump(), "application/json");
        return processHttpResponse(res, "VCluster configuration for '" + id + "' updated.");
    }

// --- VCluster Monitoring Operations ---
    Models::CloudResponse CloudAPIClient::getVClusterMetrics(const std::string& id) {
        auto res = cli->Get("/vcluster/metrics/" + id);
        return processHttpResponse(res, "VCluster metrics for '" + id + "' retrieved.");
    }

    Models::CloudResponse CloudAPIClient::getVClusterHealth(const std::string& id) {
        auto res = cli->Get("/vcluster/health/" + id);
        return processHttpResponse(res, "VCluster health for '" + id + "' retrieved.");
    }

    Models::CloudResponse CloudAPIClient::getVClusterResources(const std::string& id) {
        auto res = cli->Get("/vcluster/resources/" + id);
        return processHttpResponse(res, "VCluster resources for '" + id + "' retrieved.");
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

// --- OpenShift / OpenStack / Proxmox Continuum Operations ---
    Models::CloudResponse CloudAPIClient::getServerHealth() {
        auto res = cli->Get("/health");
        return processHttpResponse(res, "Server health retrieved.");
    }

    Models::CloudResponse CloudAPIClient::getServerVersion() {
        auto res = cli->Get("/server/version");
        return processHttpResponse(res, "Server version metadata retrieved.");
    }

    Models::CloudResponse CloudAPIClient::getGailTradingStatus() {
        auto res = cli->Get("/gail/trading/status");
        auto apiResponse = processHttpResponse(res, "Trading bridge status retrieved.");
        unwrapStandardEnvelopeResponse(apiResponse);
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::getGailTradingPortfolio() {
        auto res = cli->Get("/gail/trading/portfolio");
        auto apiResponse = processHttpResponse(res, "Portfolio retrieved.");
        unwrapStandardEnvelopeResponse(apiResponse);
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::getGailTradingPositions() {
        auto res = cli->Get("/gail/trading/positions");
        auto apiResponse = processHttpResponse(res, "Open positions retrieved.");
        unwrapStandardEnvelopeResponse(apiResponse);
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::getGailTradingHistory(int limit) {
        std::string path = "/gail/trading/history";
        appendQueryInt(path, "limit", limit);
        auto res = cli->Get(path);
        auto apiResponse = processHttpResponse(res, "Trade history retrieved.");
        unwrapStandardEnvelopeResponse(apiResponse);
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::getGailTradingLogs(int limit) {
        std::string path = "/gail/trading/logs";
        appendQueryInt(path, "limit", limit);
        auto res = cli->Get(path);
        auto apiResponse = processHttpResponse(res, "Activity log retrieved.");
        unwrapStandardEnvelopeResponse(apiResponse);
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::getGailTradingExchanges() {
        auto res = cli->Get("/gail/trading/exchanges");
        auto apiResponse = processHttpResponse(res, "Exchanges retrieved.");
        unwrapStandardEnvelopeResponse(apiResponse);
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::getGailTradingCurrencies() {
        auto res = cli->Get("/gail/trading/currencies");
        auto apiResponse = processHttpResponse(res, "Currencies retrieved.");
        unwrapStandardEnvelopeResponse(apiResponse);
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::getGailTradingConfig() {
        auto res = cli->Get("/gail/trading/config");
        auto apiResponse = processHttpResponse(res, "Trading config retrieved.");
        unwrapStandardEnvelopeResponse(apiResponse);
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::setGailTradingConfig(const nlohmann::json& config) {
        auto res = cli->Post("/gail/trading/config", config.dump(), "application/json");
        auto apiResponse = processHttpResponse(res, "Trading config updated.");
        unwrapStandardEnvelopeResponse(apiResponse);
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::pauseGailTrading(const nlohmann::json& body) {
        auto res = cli->Post("/gail/trading/pause", body.dump(), "application/json");
        auto apiResponse = processHttpResponse(res, "Trading bridge paused.");
        unwrapStandardEnvelopeResponse(apiResponse);
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::resumeGailTrading(const nlohmann::json& body) {
        auto res = cli->Post("/gail/trading/resume", body.dump(), "application/json");
        auto apiResponse = processHttpResponse(res, "Trading bridge resumed.");
        unwrapStandardEnvelopeResponse(apiResponse);
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::overrideGailTrading(const nlohmann::json& body) {
        auto res = cli->Post("/gail/trading/override", body.dump(), "application/json");
        auto apiResponse = processHttpResponse(res, "Trade override submitted.");
        unwrapStandardEnvelopeResponse(apiResponse);
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::evaluateGailTrading(const nlohmann::json& body) {
        auto res = cli->Post("/gail/trading/evaluate", body.dump(), "application/json");
        auto apiResponse = processHttpResponse(res, "Evaluation triggered.");
        unwrapStandardEnvelopeResponse(apiResponse);
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::listOpenShiftResources() {
        auto res = cli->Get("/openshift/resources");
        auto apiResponse = processHttpResponse(res, "OpenShift resources retrieved.");
        if (apiResponse.success) {
            apiResponse.data = unwrapEnvelopeData(apiResponse.data);
        }
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::listOpenShiftClusters() {
        auto res = cli->Get("/openshift/clusters");
        auto apiResponse = processHttpResponse(res, "OpenShift clusters listed.");
        if (apiResponse.success) {
            apiResponse.data = extractClusterArray(apiResponse.data);
        }
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::getOpenShiftClusterDetails(const std::string& idOrName) {
        auto res = cli->Get("/openshift/details/" + idOrName);
        auto apiResponse = processHttpResponse(res, "OpenShift cluster details retrieved.");
        if (apiResponse.success) {
            apiResponse.data = unwrapEnvelopeData(apiResponse.data);
        }
        if (apiResponse.success || (res && res->status != 404)) {
            return apiResponse;
        }

        // Backward compatibility for older servers that do not expose /openshift/details.
        return resolveClusterDetailsFromList("OpenShift", idOrName, listOpenShiftClusters());
    }

    Models::CloudResponse CloudAPIClient::requestOpenShiftCluster(const std::string& name,
                                                                  const std::string& organization,
                                                                  int gpuCount,
                                                                  const std::string& architecture,
                                                                  const std::string& region,
                                                                  const std::string& provider,
                                                                  const std::vector<std::string>& burstTargets) {
        nlohmann::json request_body;
        request_body["name"] = name;
        request_body["organization"] = organization;
        request_body["gpu_count"] = gpuCount;
        request_body["architecture"] = architecture;
        request_body["region"] = region;
        request_body["provider"] = provider;
        if (!burstTargets.empty()) {
            request_body["burst_targets"] = burstTargets;
        }

        auto res = cli->Post("/openshift/clusters/request", request_body.dump(), "application/json");
        auto apiResponse = processHttpResponse(res, "OpenShift cluster request submitted.");
        if (apiResponse.success) {
            apiResponse.data = unwrapEnvelopeData(apiResponse.data);
        }
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::listOpenStackResources() {
        auto res = cli->Get("/openstack/resources");
        auto apiResponse = processHttpResponse(res, "OpenStack resources retrieved.");
        if (apiResponse.success) {
            apiResponse.data = unwrapEnvelopeData(apiResponse.data);
        }
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::listOpenStackClusters() {
        auto res = cli->Get("/openstack/clusters");
        auto apiResponse = processHttpResponse(res, "OpenStack clusters listed.");
        if (apiResponse.success) {
            apiResponse.data = extractClusterArray(apiResponse.data);
        }
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::getOpenStackClusterDetails(const std::string& idOrName) {
        auto res = cli->Get("/openstack/details/" + idOrName);
        auto apiResponse = processHttpResponse(res, "OpenStack cluster details retrieved.");
        if (apiResponse.success) {
            apiResponse.data = unwrapEnvelopeData(apiResponse.data);
        }
        if (apiResponse.success || (res && res->status != 404)) {
            return apiResponse;
        }

        // Backward compatibility for older servers that do not expose /openstack/details.
        return resolveClusterDetailsFromList("OpenStack", idOrName, listOpenStackClusters());
    }

    Models::CloudResponse CloudAPIClient::requestOpenStackCluster(const std::string& name,
                                                                  const std::string& organization,
                                                                  int gpuCount,
                                                                  const std::string& architecture,
                                                                  const std::string& region,
                                                                  const std::string& provider,
                                                                  const std::vector<std::string>& burstTargets) {
        nlohmann::json request_body;
        request_body["name"] = name;
        request_body["organization"] = organization;
        request_body["gpu_count"] = gpuCount;
        request_body["architecture"] = architecture;
        request_body["region"] = region;
        request_body["provider"] = provider;
        if (!burstTargets.empty()) {
            request_body["burst_targets"] = burstTargets;
        }

        auto res = cli->Post("/openstack/clusters/request", request_body.dump(), "application/json");
        auto apiResponse = processHttpResponse(res, "OpenStack cluster request submitted.");
        if (apiResponse.success) {
            apiResponse.data = unwrapEnvelopeData(apiResponse.data);
        }
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::listProxmoxResources() {
        auto res = cli->Get("/proxmox/resources");
        auto apiResponse = processHttpResponse(res, "Proxmox resources retrieved.");
        if (apiResponse.success) {
            apiResponse.data = unwrapEnvelopeData(apiResponse.data);
        }
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::listProxmoxClusters() {
        auto res = cli->Get("/proxmox/clusters");
        auto apiResponse = processHttpResponse(res, "Proxmox clusters listed.");
        if (apiResponse.success) {
            apiResponse.data = extractClusterArray(apiResponse.data);
        }
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::getProxmoxClusterDetails(const std::string& idOrName) {
        auto res = cli->Get("/proxmox/details/" + idOrName);
        auto apiResponse = processHttpResponse(res, "Proxmox cluster details retrieved.");
        if (apiResponse.success) {
            apiResponse.data = unwrapEnvelopeData(apiResponse.data);
        }
        if (apiResponse.success || (res && res->status != 404)) {
            return apiResponse;
        }

        // Backward compatibility for older servers that do not expose /proxmox/details.
        return resolveClusterDetailsFromList("Proxmox", idOrName, listProxmoxClusters());
    }

    Models::CloudResponse CloudAPIClient::requestProxmoxCluster(const std::string& name,
                                                                const std::string& organization,
                                                                int gpuCount,
                                                                const std::string& architecture,
                                                                const std::string& region,
                                                                const std::string& provider,
                                                                const std::vector<std::string>& burstTargets) {
        nlohmann::json request_body;
        request_body["name"] = name;
        request_body["organization"] = organization;
        request_body["gpu_count"] = gpuCount;
        request_body["architecture"] = architecture;
        request_body["region"] = region;
        request_body["provider"] = provider;
        if (!burstTargets.empty()) {
            request_body["burst_targets"] = burstTargets;
        }

        auto res = cli->Post("/proxmox/clusters/request", request_body.dump(), "application/json");
        auto apiResponse = processHttpResponse(res, "Proxmox cluster request submitted.");
        if (apiResponse.success) {
            apiResponse.data = unwrapEnvelopeData(apiResponse.data);
        }
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::recruitNode(const nlohmann::json& requestPayload) {
        auto res = cli->Post("/node/recruit", requestPayload.dump(), "application/json");
        return processHttpResponse(res, "Node recruitment request submitted.");
    }

// --- Tracey Operations ---
    Models::CloudResponse CloudAPIClient::traceyHeartbeat(const nlohmann::json& heartbeatPayload) {
        auto res = cli->Post("/tracey/agents/heartbeat", heartbeatPayload.dump(), "application/json");
        return processHttpResponse(res, "Tracey heartbeat submitted.");
    }

    Models::CloudResponse CloudAPIClient::listTraceyAgents() {
        auto res = cli->Get("/tracey/agents");
        return processHttpResponse(res, "Tracey agents listed.");
    }

    Models::CloudResponse CloudAPIClient::getTraceyAnalytics(int windowSeconds, int bucketSeconds, int logLimit) {
        std::string path = "/tracey/analytics";
        appendQueryInt(path, "window_seconds", windowSeconds);
        appendQueryInt(path, "bucket_seconds", bucketSeconds);
        appendQueryInt(path, "log_limit", logLimit);
        auto res = cli->Get(path);
        return processHttpResponse(res, "Tracey analytics retrieved.");
    }

    Models::CloudResponse CloudAPIClient::getTraceyFleet(const TraceySimulationQuery& simulation) {
        std::string path = "/tracey/fleet";
        appendTraceySimulationQuery(path, simulation);
        auto res = cli->Get(path);
        return processHttpResponse(res, "Tracey fleet view retrieved.");
    }

    Models::CloudResponse CloudAPIClient::getTraceyAdaptive(const std::string& policy) {
        std::string path = "/tracey/adaptive";
        appendQueryString(path, "policy", policy);
        auto res = cli->Get(path);
        return processHttpResponse(res, "Tracey adaptive loop retrieved.");
    }

    Models::CloudResponse CloudAPIClient::getTraceyCveStatus() {
        auto res = cli->Get("/tracey/cve/status");
        return processHttpResponse(res, "Tracey CVE mirror status retrieved.");
    }

    Models::CloudResponse CloudAPIClient::getTraceyAssessmentFleet() {
        auto res = cli->Get("/tracey/assessment/fleet");
        return processHttpResponse(res, "Tracey fleet compromise assessment retrieved.");
    }

    Models::CloudResponse CloudAPIClient::getTraceyAssessmentPlan(const std::string& agentId) {
        auto res = cli->Get("/tracey/agents/" + agentId + "/assessment/plan");
        return processHttpResponse(res, "Tracey assessment plan retrieved.");
    }

    Models::CloudResponse CloudAPIClient::submitTraceyAssessmentReport(const std::string& agentId,
                                                                       const nlohmann::json& reportPayload) {
        auto res = cli->Post(
                "/tracey/agents/" + agentId + "/assessment/report",
                reportPayload.dump(),
                "application/json"
        );
        return processHttpResponse(res, "Tracey assessment report submitted.");
    }

    Models::CloudResponse CloudAPIClient::listTraceyRacks() {
        auto res = cli->Get("/tracey/racks");
        return processHttpResponse(res, "Tracey racks listed.");
    }

    Models::CloudResponse CloudAPIClient::getTraceyRackDetails(const std::string& rackId,
                                                               const TraceySimulationQuery& simulation) {
        std::string path = "/tracey/racks/" + rackId;
        appendTraceySimulationQuery(path, simulation);
        auto res = cli->Get(path);
        return processHttpResponse(res, "Tracey rack details retrieved.");
    }

    Models::CloudResponse CloudAPIClient::getTraceyAgentAnalysis(const std::string& agentId,
                                                                 int windowSeconds,
                                                                 int bucketSeconds,
                                                                 int logLimit) {
        std::string path = "/tracey/agents/" + agentId + "/analysis";
        appendQueryInt(path, "window_seconds", windowSeconds);
        appendQueryInt(path, "bucket_seconds", bucketSeconds);
        appendQueryInt(path, "log_limit", logLimit);
        auto res = cli->Get(path);
        return processHttpResponse(res, "Tracey agent analysis retrieved.");
    }

    Models::CloudResponse CloudAPIClient::getTraceyAgentServer(const std::string& agentId,
                                                               const TraceySimulationQuery& simulation) {
        std::string path = "/tracey/agents/" + agentId + "/server";
        appendTraceySimulationQuery(path, simulation);
        auto res = cli->Get(path);
        return processHttpResponse(res, "Tracey server telemetry retrieved.");
    }

    Models::CloudResponse CloudAPIClient::getTraceyAgentGpu(const std::string& agentId,
                                                            const std::string& gpuId,
                                                            const TraceySimulationQuery& simulation) {
        std::string path = "/tracey/agents/" + agentId + "/gpus/" + gpuId + "/telemetry";
        appendTraceySimulationQuery(path, simulation);
        auto res = cli->Get(path);
        return processHttpResponse(res, "Tracey GPU telemetry retrieved.");
    }

    Models::CloudResponse CloudAPIClient::getTraceyAgentCompromise(const std::string& agentId) {
        auto res = cli->Get("/tracey/agents/" + agentId + "/compromise");
        return processHttpResponse(res, "Tracey compromise assessment retrieved.");
    }

    Models::CloudResponse CloudAPIClient::controlTraceyAgent(const std::string& agentId, const nlohmann::json& controlPayload) {
        auto res = cli->Post("/tracey/agents/" + agentId + "/control", controlPayload.dump(), "application/json");
        return processHttpResponse(res, "Tracey control request submitted.");
    }

    Models::CloudResponse CloudAPIClient::getTraceyAgentDeepDive(const std::string& agentId) {
        auto res = cli->Get("/tracey/agents/" + agentId + "/deepdive");
        return processHttpResponse(res, "Tracey deep-dive retrieved.");
    }

// --- AARNN Operations ---
    Models::CloudResponse CloudAPIClient::aarnnEndpoints() {
        auto res = cli->Get("/aarnn/endpoints");
        auto apiResponse = processHttpResponse(res, "AARNN endpoints retrieved.");
        if (apiResponse.data.is_object()) {
            if (apiResponse.data.contains("message") && apiResponse.data["message"].is_string()) {
                apiResponse.message = apiResponse.data["message"].get<std::string>();
            }
            apiResponse.data = unwrapEnvelopeData(apiResponse.data);
        }
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::aarnnInventory(const std::string& clusterId,
                                                         const std::string& orchestratorId) {
        std::string path = "/aarnn/inventory";
        appendQueryString(path, "cluster_id", clusterId);
        appendQueryString(path, "orchestrator_id", orchestratorId);
        auto res = cli->Get(path);
        auto apiResponse = processHttpResponse(res, "AARNN inventory retrieved.");
        if (apiResponse.data.is_object()) {
            if (apiResponse.data.contains("message") && apiResponse.data["message"].is_string()) {
                apiResponse.message = apiResponse.data["message"].get<std::string>();
            }
            apiResponse.data = unwrapEnvelopeData(apiResponse.data);
        }
        return apiResponse;
    }

    Models::CloudResponse CloudAPIClient::aarnnProxy(const std::string& plane,
                                                     const std::string& method,
                                                     const std::string& path,
                                                     const nlohmann::json& jsonPayload,
                                                     const std::string& rawBody,
                                                     const std::string& contentType,
                                                     const std::string& clusterId,
                                                     const std::string& orchestratorId) {
        nlohmann::json requestBody = {
                {"method", method},
                {"path", path}
        };
        if (!jsonPayload.is_null()) {
            requestBody["json"] = jsonPayload;
        }
        if (!rawBody.empty()) {
            requestBody["body"] = rawBody;
        }
        if (!contentType.empty()) {
            requestBody["content_type"] = contentType;
        }
        if (!clusterId.empty()) {
            requestBody["cluster_id"] = clusterId;
        }
        if (!orchestratorId.empty()) {
            requestBody["orchestrator_id"] = orchestratorId;
        }

        std::string proxyPath = "/aarnn/proxy/" + plane;
        auto res = cli->Post(proxyPath, requestBody.dump(), "application/json");
        auto apiResponse = processHttpResponse(res, "AARNN proxy request completed.");
        if (apiResponse.data.is_object()) {
            if (apiResponse.data.contains("message") && apiResponse.data["message"].is_string()) {
                apiResponse.message = apiResponse.data["message"].get<std::string>();
            }
            apiResponse.data = unwrapEnvelopeData(apiResponse.data);
        }
        return apiResponse;
    }

// --- Server-side Connection Management ---
    Models::CloudResponse CloudAPIClient::getServerConnectionStatus() {
        auto res = cli->Get("/connections/status");
        return processHttpResponse(res, "Server connection status retrieved.");
    }

    Models::CloudResponse CloudAPIClient::makeServerConnection(const std::string& name,
                                                               const std::string& endpoint,
                                                               bool setDefault) {
        nlohmann::json requestBody = {
                {"name", name},
                {"endpoint", endpoint},
                {"setDefault", setDefault}
        };
        auto res = cli->Post("/connections/make", requestBody.dump(), "application/json");
        return processHttpResponse(res, "Server connection created.");
    }

    Models::CloudResponse CloudAPIClient::dropServerConnection(const std::string& name) {
        auto res = cli->Delete("/connections/" + name);
        return processHttpResponse(res, "Server connection removed.");
    }

    Models::CloudResponse CloudAPIClient::listServerConnections() {
        auto res = cli->Get("/connections");
        return processHttpResponse(res, "Server connections listed.");
    }

    Models::CloudResponse CloudAPIClient::selectServerConnection(const std::string& name) {
        nlohmann::json requestBody = {{"name", name}};
        auto res = cli->Post("/connections/select", requestBody.dump(), "application/json");
        return processHttpResponse(res, "Server connection selected.");
    }

// --- Connection Management ---
    Models::CloudResponse CloudAPIClient::getConnectionStatus() {
        if (currentConnectionName.empty()) {
            return {false,
                    "No default connection set. Please use 'nmc connection make --default' or 'nmc connection select' to set one.",
                    nlohmann::json{{"isActive", false}, {"status", "unset"}}};
        }

        auto it = std::find_if(connections.begin(), connections.end(),
                               [this](const Models::Connection& conn) { return conn.name == currentConnectionName; });

        if (it == connections.end()) {
            // This indicates an inconsistency where currentConnectionName is set but the connection itself is not in the list.
            currentConnectionName = ""; // Clear inconsistent state
            saveConnections(true); // Save to reflect the cleared state
            return {false,
                    "Current connection details not found, though a name was set. This indicates an inconsistency. Resetting default. Please select a connection.",
                    nlohmann::json{{"isActive", false}, {"status", "inconsistent"}}};
        }

        // Probe the selected endpoint with a tolerant fallback chain so older
        // server versions still report useful reachability information.
        std::unique_ptr<httplib::Client> tempCli;
        initCliFromUrl(tempCli, it->endpoint, host, port);
        tempCli->set_connection_timeout(5);
        tempCli->set_read_timeout(10);
        tempCli->set_write_timeout(10);
        applyAuthHeaders(*tempCli, resolveAuthToken());

        std::string probePath = "/health";
        int probeHttpStatus = 0;
        std::string status = "unhealthy";
        std::string diagnostic = "";
        bool reachable = false;
        bool healthy = false;

        // Probe in priority order and keep going when a route is unavailable or
        // returns a non-fatal status, so older server versions are detected as
        // reachable instead of being marked unhealthy immediately.
        const std::array<std::string, 3> probeChain = {
                "/health",
                "/server/version",
                "/connections/status"
        };

        for (const auto& path : probeChain) {
            const httplib::Result probe = tempCli->Get(path);
            probePath = path;

            if (!probe) {
                probeHttpStatus = 0;
                diagnostic = "Probe to " + path + " failed: " + httplib::to_string(probe.error());
                continue;
            }

            probeHttpStatus = probe->status;
            reachable = true;

            if (probe->status >= 200 && probe->status < 300) {
                healthy = true;
                status = "healthy";
                diagnostic.clear();
                break;
            }

            if (probe->status == 401 || probe->status == 403) {
                status = "reachable_auth_required";
                diagnostic = "Endpoint is reachable but authentication failed.";
                break;
            }

            // Route-missing statuses are not treated as hard failure because
            // older server builds may not expose every probe endpoint.
            if (probe->status == 404 || probe->status == 405) {
                status = "reachable_unknown_health";
                diagnostic = "Endpoint is reachable but health route is unavailable.";
                continue;
            }

            status = "reachable_unknown_health";
            diagnostic = "HTTP status " + std::to_string(probe->status) + " from " + path + ".";
        }

        Models::CloudResponse response;
        if (reachable) {
            response.success = true;
            if (healthy) {
                response.message = "Current connection: " + it->name + " (" + it->endpoint + ") is active and healthy.";
            } else if (status == "reachable_auth_required") {
                response.message = "Current connection: " + it->name + " (" + it->endpoint + ") is reachable, but authentication is required.";
            } else {
                response.message = "Current connection: " + it->name + " (" + it->endpoint + ") is reachable.";
            }
            response.data = nlohmann::json{
                    {"name", it->name},
                    {"endpoint", it->endpoint},
                    {"isActive", it->isActive},
                    {"status", status},
                    {"probe_path", probePath},
                    {"probe_http_status", probeHttpStatus},
                    {"diagnostic", diagnostic}
            };
        } else {
            response.success = false;
            response.message = "Connection to " + it->name + " (" + it->endpoint + ") failed. " + diagnostic;
            response.data = nlohmann::json{
                    {"name", it->name},
                    {"endpoint", it->endpoint},
                    {"isActive", it->isActive},
                    {"status", "unhealthy"},
                    {"probe_path", probePath},
                    {"probe_http_status", probeHttpStatus},
                    {"diagnostic", diagnostic}
            };
        }
        return response;
    }

    Models::CloudResponse CloudAPIClient::makeConnection(const std::string& name, const std::string& endpoint, bool setDefault) {
        return makeConnection(name, endpoint, setDefault, "");
    }

    Models::CloudResponse CloudAPIClient::makeConnection(const std::string& name, const std::string& endpoint, bool setDefault, const std::string& token) {
        auto it = std::find_if(connections.begin(), connections.end(),
                               [&name](const Models::Connection& conn) { return conn.name == name; });

        if (it != connections.end()) {
            return {false, "Connection with name '" + name + "' already exists. Use 'drop' first or choose a different name.", nlohmann::json()};
        }

        connections.emplace_back(name, endpoint, setDefault, token); // New connection
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
        initCliFromUrl(cli, newConn.endpoint, host, port);
        applyAuthHeaders();
        return {true,
                "Connection '" + name + "' created" + (setDefault ? " and set as default." : "."),
                nlohmann::json{{"name", name}, {"endpoint", endpoint}, {"default_set", setDefault}}};
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
                return {true,
                        "Connection '" + name + "' dropped. No default connection now.",
                        nlohmann::json{{"dropped_name", name}, {"default_unset", true}}};
            }
            saveConnections(true); // Save changes after dropping a connection
            return {true,
                    "Connection '" + name + "' dropped.",
                    nlohmann::json{{"dropped_name", name}, {"default_unset", false}}};
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
        initCliFromUrl(cli, it->endpoint, host, port);
        applyAuthHeaders();

        return {true,
                "Connection '" + name + "' selected as default.",
                nlohmann::json{{"selected_name", name}, {"endpoint", it->endpoint}}};
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
        applyAuthHeaders();
        return {true,
                "Default connection '" + unset_name + "' unset successfully.",
                nlohmann::json{{"unset_name", unset_name}}};
    }

    Models::CloudResponse CloudAPIClient::setConnectionToken(const std::string& name, const std::string& token) {
        auto it = std::find_if(connections.begin(), connections.end(),
                               [&name](const Models::Connection& conn) { return conn.name == name; });
        if (it == connections.end()) {
            return {false, "Connection with name '" + name + "' not found.", nlohmann::json()};
        }
        it->token = token;
        saveConnections(true);
        if (currentConnectionName == name) {
            applyAuthHeaders();
        }
        return {true, "Token updated for connection '" + name + "'.", nlohmann::json{{"name", name}}};
    }

    Models::CloudResponse CloudAPIClient::clearConnectionToken(const std::string& name) {
        auto it = std::find_if(connections.begin(), connections.end(),
                               [&name](const Models::Connection& conn) { return conn.name == name; });
        if (it == connections.end()) {
            return {false, "Connection with name '" + name + "' not found.", nlohmann::json()};
        }
        it->token.clear();
        saveConnections(true);
        if (currentConnectionName == name) {
            applyAuthHeaders();
        }
        return {true, "Token cleared for connection '" + name + "'.", nlohmann::json{{"name", name}}};
    }

    bool CloudAPIClient::hasDefaultConnection() const {
        return !currentConnectionName.empty();
    }

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
    // Saved schema:
    // {
    //   "connections": [{"name": "...", "endpoint": "...", "isActive": bool, "token": "..."}],
    //   "default_connection": "name-or-empty"
    // }
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
            connections.clear();
            currentConnectionName.clear();

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
            // Initialises with an empty config to prevent crashes
            connectionsConfig["connections"] = nlohmann::json::array();
            connectionsConfig["default_connection"] = "";
        }
        try {
            auto perms = std::filesystem::status(configFilePath).permissions();
            if ((perms & std::filesystem::perms::group_read) != std::filesystem::perms::none
                || (perms & std::filesystem::perms::others_read) != std::filesystem::perms::none) {
                std::cerr << "Warning: Config file permissions are too open. Recommended: owner read/write only."
                          << std::endl;
            }
        } catch (const std::exception&) {
            // Non-fatal: permission checks are best-effort.
        }
        // Mirror serialized state into in-memory objects.
        connections.clear();
        for (auto &j: connectionsConfig["connections"]) {
            Models::Connection c;
            c.name = j["name"].get<std::string>();
            c.endpoint = j["endpoint"].get<std::string>();
            c.isActive = j.value("isActive", false);
            c.token = j.value("token", "");
            connections.push_back(std::move(c));
        }
        currentConnectionName = connectionsConfig.value("default_connection", "");
    }

    void CloudAPIClient::saveConnections(bool showMessage) {
        // Mirror in-memory connection state back to serialized config.
        nlohmann::json arr = nlohmann::json::array();
            for (auto& conn : connections) {
                arr.push_back({
                    {"name",     conn.name},
                    {"endpoint", conn.endpoint},
                    {"isActive", conn.isActive},
                    {"token", conn.token}
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
        configFile.close();
        try {
            std::filesystem::permissions(
                    configFilePath,
                    std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                    std::filesystem::perm_options::replace);
        } catch (const std::exception&) {
            // Non-fatal: continue if filesystem permissions cannot be set.
        }
    }

    std::string CloudAPIClient::resolveAuthToken() const {
        // Keep this precedence in sync with docs/ARCHITECTURE.md.
        std::string token = getenvOrEmpty("NMC_OIDC_ACCESS_TOKEN");
        if (!token.empty()) return token;
        token = getenvOrEmpty("NM_OIDC_ACCESS_TOKEN");
        if (!token.empty()) return token;
        token = getenvOrEmpty("NMC_BEARER_TOKEN");
        if (!token.empty()) return token;
        token = getenvOrEmpty("NM_BEARER_TOKEN");
        if (!token.empty()) return token;

        if (!currentConnectionName.empty()) {
            auto it = std::find_if(connections.begin(), connections.end(),
                                   [this](const Models::Connection& conn) { return conn.name == currentConnectionName; });
            if (it != connections.end() && !it->token.empty()) {
                return it->token;
            }
        }
        for (const auto& conn : connections) {
            if (conn.isActive && !conn.token.empty()) {
                return conn.token;
            }
        }
        token = getenvOrEmpty("NMC_AUTH_TOKEN");
        if (!token.empty()) return token;
        token = getenvOrEmpty("NM_AUTH_TOKEN");
        return token;
    }

    void CloudAPIClient::applyAuthHeaders() {
        if (!cli) return;
        const std::string token = resolveAuthToken();
        applyAuthHeaders(*cli, token);
    }

    void CloudAPIClient::applyAuthHeaders(httplib::Client& client, const std::string& token) const {
        httplib::Headers headers;
        if (!token.empty()) {
            headers.emplace("Authorization", "Bearer " + token);
        }
        client.set_default_headers(headers);
    }


} // namespace NMC::Core
