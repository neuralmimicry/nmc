// src/Core/K8sHandlers.h
#pragma once

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <vector>
#include <mutex>
#include <functional> // For std::function

#include "../Models/K8sCluster.h"
#include "../Models/CloudResponse.h"
#include "Utils.h" // For Utils::generateUniqueId

namespace NMC {
    namespace Server {

        /**
         * @brief A class to encapsulate Kubernetes (K8s) API handler logic.
         *
         * This class manages the data for K8s clusters and provides
         * methods to handle API requests related to K8s operations. It is designed
         * to be instantiated by APIRoutes and provided with necessary dependencies
         * like shared data and response sending functions.
         */
        class K8sHandlers {
        public:
            /**
             * @brief Constructor for K8sHandlers.
             *
             * Initializes the K8sHandlers with references to shared data,
             * a mutex for thread safety, and callback functions for sending
             * JSON and error responses.
             *
             * @param clusters_ref A reference to the vector of K8sCluster objects.
             * @param mutex_ref A reference to the mutex protecting the shared data.
             * @param send_json_cb A callback function to send a successful JSON response.
             * @param send_error_cb A callback function to send an error JSON response.
             */
            K8sHandlers(
                    std::vector<Models::K8sCluster>& clusters_ref,
                    std::mutex& mutex_ref,
                    std::function<void(httplib::Response&, const Models::CloudResponse&)> send_json_cb,
            std::function<void(httplib::Response&, int, const std::string&)> send_error_cb
            );

            // K8s API Handlers
            void handleCreateK8sCluster(const httplib::Request& req, httplib::Response& res);
            void handleDeleteK8sCluster(const httplib::Request& req, httplib::Response& res);
            void handleGetK8sCluster(const httplib::Request& req, httplib::Response& res);
            void handleGetKubeConfig(const httplib::Request& req, httplib::Response& res);
            void handleListK8sClusters(const httplib::Request& req, httplib::Response& res);
            void handleListK8sLocations(const httplib::Request& req, httplib::Response& res);
            void handleResumeK8sCluster(const httplib::Request& req, httplib::Response& res);
            void handleSuspendK8sCluster(const httplib::Request& req, httplib::Response& res);

        private:
            std::vector<Models::K8sCluster>& mockK8sClusters; /**< Reference to shared K8s cluster data. */
            std::mutex& dataMutex; /**< Reference to the mutex protecting shared data. */

            // Callbacks for sending responses, provided by APIRoutes
            std::function<void(httplib::Response&, const Models::CloudResponse&)> sendJsonResponse;
            std::function<void(httplib::Response&, int, const std::string&)> sendErrorResponse;
        };

    } // namespace Server
} // namespace NMC
