// nmc_client/src/Core/CloudAPIClient.cpp
#include "CloudAPIClient.h"
#include <iostream>
#include <nlohmann/json.hpp> // For JSON parsing and creation

namespace NMC::Core {

// Constructor: Initializes the httplib client with the server's host and port.
    CloudAPIClient::CloudAPIClient(const std::string& host, int port) {
        cli = std::make_unique<httplib::Client>(host, port);
        // Optional: Set client-side timeouts if necessary
        cli->set_connection_timeout(5); // 5 seconds
        cli->set_read_timeout(180);     // 3 minutes, for potentially long operations like model uploads
        cli->set_write_timeout(180);    // 3 minutes
    }

// Destructor: Cleans up the httplib client.
    CloudAPIClient::~CloudAPIClient() {
        // std::unique_ptr automatically handles deletion, so nothing explicit needed here
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
        auto res = cli->Get(path);
        return processHttpResponse(res, "K8s clusters listed.");
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

} // namespace NMC::Core