// src/Core/K8sHandlers.cpp
#include "K8sHandlers.h"
#include "Utils.h" // For Utils::generateUniqueId
#include <algorithm> // For std::find_if, std::remove_if
#include <iostream>

namespace NMC {
    namespace Server {

        K8sHandlers::K8sHandlers(
                std::vector<Models::K8sCluster>& clusters_ref,
                std::mutex& mutex_ref,
                std::function<void(httplib::Response&, const Models::CloudResponse&)> send_json_cb,
        std::function<void(httplib::Response&, int, const std::string&)> send_error_cb
        ) :
        mockK8sClusters(clusters_ref),
        dataMutex(mutex_ref),
        sendJsonResponse(send_json_cb),
        sendErrorResponse(send_error_cb)
        {
            // Constructor initializes references and callbacks
        }

        /**
         * @brief Handles the creation of a new Kubernetes cluster.
         *
         * Expects a JSON body with 'name' and 'region'.
         * Checks for missing parameters, duplicate cluster names, and creates a K8s cluster.
         * Sends a success or error JSON response.
         *
         * @param req The httplib::Request object.
         * @param res The httplib::Response object.
         */
        void K8sHandlers::handleCreateK8sCluster(const httplib::Request& req, httplib::Response& res) {
            try {
                auto json_body = nlohmann::json::parse(req.body);
                std::string name = json_body.value("name", "");
                std::string region = json_body.value("region", "");

                if (name.empty() || region.empty()) {
                    return sendErrorResponse(res, 400, "Missing required parameters: name or region.");
                }

                std::lock_guard<std::mutex> lock(dataMutex);
                auto it = std::find_if(mockK8sClusters.begin(), mockK8sClusters.end(),
                                       [&](const Models::K8sCluster& c) { return c.name == name; });
                if (it != mockK8sClusters.end()) {
                    return sendErrorResponse(res, 409, "K8s cluster '" + name + "' already exists.");
                }

                Models::K8sCluster newCluster;
                newCluster.id = Utils::generateUniqueId("k8s");
                newCluster.name = name;
                newCluster.region = region;
                newCluster.status = "Provisioning";
                mockK8sClusters.push_back(newCluster);

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "K8s cluster '" + name + "' created successfully.";
                apiResponse.data = newCluster.toJson();
                sendJsonResponse(res, apiResponse);

            } catch (const nlohmann::json::parse_error& e) {
                sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        /**
         * @brief Handles the deletion of a Kubernetes cluster.
         *
         * Extracts the cluster ID from the URL path.
         * Deletes the corresponding K8s cluster if found.
         * Sends a success or 404 Not Found error response.
         *
         * @param req The httplib::Request object.
         * @param res The httplib::Response object.
         */
        void K8sHandlers::handleDeleteK8sCluster(const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];

            std::lock_guard<std::mutex> lock(dataMutex);
            auto initial_size = mockK8sClusters.size();
            mockK8sClusters.erase(std::remove_if(mockK8sClusters.begin(), mockK8sClusters.end(),
                                                 [&](const Models::K8sCluster& c) { return c.id == id; }),
                                  mockK8sClusters.end());

            if (mockK8sClusters.size() < initial_size) {
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "K8s cluster '" + id + "' deleted successfully.";
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "K8s cluster '" + id + "' not found.");
            }
        }

        /**
         * @brief Handles retrieving details of a specific Kubernetes cluster.
         *
         * Extracts the cluster ID from the URL path.
         * Finds and returns the K8s cluster's details if found.
         * Sends a success or 404 Not Found error response.
         *
         * @param req The httplib::Request object.
         * @param res The httplib::Response object.
         */
        void K8sHandlers::handleGetK8sCluster(const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(mockK8sClusters.begin(), mockK8sClusters.end(),
                                   [&](const Models::K8sCluster& c) { return c.id == id; });

            if (it != mockK8sClusters.end()) {
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "K8s cluster details retrieved.";
                apiResponse.data = it->toJson();
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "K8s cluster '" + id + "' not found.");
            }
        }

        /**
         * @brief Handles retrieving the KubeConfig for a specific Kubernetes cluster.
         *
         * Extracts the cluster ID from the URL path.
         * Generates a KubeConfig YAML string for the cluster if found.
         * Sends a success or 404 Not Found error response.
         *
         * @param req The httplib::Request object.
         * @param res The httplib::Response object.
         */
        void K8sHandlers::handleGetKubeConfig(const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(mockK8sClusters.begin(), mockK8sClusters.end(),
                                   [&](const Models::K8sCluster& c) { return c.id == id; });

            if (it != mockK8sClusters.end()) {
                // Generate a kubeconfig string. In a real application, this would
                // involve fetching sensitive credentials securely.
                std::string kubeconfig =
                        "apiVersion: v1\n"
                        "clusters:\n"
                        "- cluster:\n"
                        "    server: https://kube." + id + ".example.com\n"
                                                           "    certificate-authority-data: REDACTED\n"
                                                           "  name: " + it->name + "\n"
                                                                                   "contexts:\n"
                                                                                   "- context:\n"
                                                                                   "    cluster: " + it->name + "\n"
                                                                                                                "    user: " + it->name + "-user\n"
                                                                                                                                          "  name: " + it->name + "-context\n"
                                                                                                                                                                  "current-context: " + it->name + "-context\n"
                                                                                                                                                                                                   "kind: Config\n"
                                                                                                                                                                                                   "preferences: {}\n"
                                                                                                                                                                                                   "users:\n"
                                                                                                                                                                                                   "- name: " + it->name + "-user\n"
                                                                                                                                                                                                                           "  user:\n"
                                                                                                                                                                                                                           "    token: REDACTED-TOKEN-FOR-" + it->name + "\n";

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "KubeConfig retrieved successfully.";
                // Kubeconfig is usually a YAML string, we put it directly in data
                apiResponse.data = kubeconfig;
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "K8s cluster '" + id + "' not found.");
            }
        }

        /**
         * @brief Handles listing all Kubernetes clusters, with optional name filtering.
         *
         * Supports a 'filter-name' query parameter to filter clusters by name.
         * Returns a JSON array of K8s cluster objects.
         *
         * @param req The httplib::Request object.
         * @param res The httplib::Response object.
         */
        void K8sHandlers::handleListK8sClusters(const httplib::Request& req, httplib::Response& res) {
            std::string filterName = req.get_param_value("filter-name");

            std::lock_guard<std::mutex> lock(dataMutex);
            nlohmann::json clusterList = nlohmann::json::array();
            for (const auto& cluster : mockK8sClusters) {
                if (filterName.empty() || cluster.name.find(filterName) != std::string::npos) {
                    clusterList.push_back(cluster.toJson());
                }
            }

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "K8s clusters listed successfully.";
            apiResponse.data = clusterList;
            sendJsonResponse(res, apiResponse);
        }

        /**
         * @brief Handles listing available Kubernetes cluster locations.
         *
         * Returns a predefined list of K8s cluster locations.
         *
         * @param req The httplib::Request object.
         * @param res The httplib::Response object.
         */
        void K8sHandlers::handleListK8sLocations(const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(dataMutex);
            nlohmann::json locations = {
                    {"name", "rugby-1"},
                    {"name", "london-1-1"},
                    {"name", "frankfurt-3"}
            };

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "K8s cluster locations listed successfully.";
            apiResponse.data = locations;
            sendJsonResponse(res, apiResponse);
        }

        /**
         * @brief Handles resuming a suspended Kubernetes cluster.
         *
         * Extracts the cluster ID from the URL path.
         * Changes the status of the K8s cluster to "Running" if found.
         * Sends a success or 404 Not Found error response.
         *
         * @param req The httplib::Request object.
         * @param res The httplib::Response object.
         */
        void K8sHandlers::handleResumeK8sCluster(const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(mockK8sClusters.begin(), mockK8sClusters.end(),
                                   [&](const Models::K8sCluster& c) { return c.id == id; });

            if (it != mockK8sClusters.end()) {
                it->status = "Running"; // Change status
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "K8s cluster '" + id + "' resumed successfully.";
                apiResponse.data = it->toJson();
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "K8s cluster '" + id + "' not found.");
            }
        }

        /**
         * @brief Handles suspending a running Kubernetes cluster.
         *
         * Extracts the cluster ID from the URL path.
         * Changes the status of the K8s cluster to "Suspended" if found.
         * Sends a success or 404 Not Found error response.
         *
         * @param req The httplib::Request object.
         * @param res The httplib::Response object.
         */
        void K8sHandlers::handleSuspendK8sCluster(const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(mockK8sClusters.begin(), mockK8sClusters.end(),
                                   [&](const Models::K8sCluster& c) { return c.id == id; });

            if (it != mockK8sClusters.end()) {
                it->status = "Suspended"; // Change status
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "K8s cluster '" + id + "' suspended successfully.";
                apiResponse.data = it->toJson();
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "K8s cluster '" + id + "' not found.");
            }
        }

    } // namespace Server
} // namespace NMC
