// server/APIRoutes.h
#pragma once

#include <httplib.h>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>
#include <memory> // Required for std::unique_ptr

// Include data models
#include "../Models/Bucket.h"
#include "../Models/K8sCluster.h"
#include "../Models/SSHKey.h"
#include "../Models/VM.h"
#include "../Models/Connection.h"
#include "../Models/CloudResponse.h"

// Forward declaration for K8sHandlers to avoid circular includes

namespace NMC::Server {
    class K8sHandlers; // Forward declaration
}



namespace NMC::Server {

    /**
     * @brief APIRoutes class manages and registers all API endpoints for the server.
     *
     * This class is responsible for setting up HTTP route handlers, processing requests,
     * and managing data for various cloud resources (Buckets, K8s, Models, SSH, VMs).
     * It also provides utility functions for logging and sending structured JSON responses.
     */
    class APIRoutes {
    public:
        /**
         * @brief Constructor for APIRoutes.
         * @param svr Reference to the httplib::Server instance to register routes on.
         */
        APIRoutes(httplib::Server& svr);

        /**
         * @brief Destructor for APIRoutes.
         *
         * This destructor is explicitly declared here and defined in APIRoutes.cpp
         * to allow for proper destruction of members like std::unique_ptr<K8sHandlers>
         * where the full definition of K8sHandlers is needed.
         */
        ~APIRoutes();

    private:
        // data storage for various resources
        std::vector<Models::Bucket> buckets;
        std::vector<Models::K8sCluster> k8sClusters;
        std::vector<Models::SSHKey> sshKeys;
        std::vector<Models::VM> vms;
        std::vector<Models::Connection> connections;

        std::mutex dataMutex; // Mutex to protect access to data in a multi-threaded environment

        // Utility methods for common server operations
        void logRequest(const httplib::Request& req) const;

        /**
         * @brief Sends a JSON response to the client.
         * @param res The HTTP response object.
         * @param apiResponse The CloudResponse object containing success status, message, and data.
         */
        void sendJsonResponse(httplib::Response& res, const Models::CloudResponse& apiResponse) const;

        /**
         * @brief Sends an error JSON response to the client with a specific HTTP status code.
         * @param res The HTTP response object.
         * @param status The HTTP status code to send (e.g., 400, 404, 500).
         * @param message The error message.
         */
        void sendErrorResponse(httplib::Response& res, int status, const std::string& message) const;

        // --- Bucket Handlers ---
        void handleCreateBucket(const httplib::Request& req, httplib::Response& res);
        void handleDeleteBucket(const httplib::Request& req, httplib::Response& res);
        void handleGetBucket(const httplib::Request& req, httplib::Response& res);
        void handleListBuckets(const httplib::Request& req, httplib::Response& res);
        void handleListBucketLocations(const httplib::Request& req, httplib::Response& res);
        void handleListBucketTypes(const httplib::Request& req, httplib::Response& res);
        void handleResetBucketKey(const httplib::Request& req, httplib::Response& res);

        // --- Model Handlers ---
        void handleUploadModel(const httplib::Request& req, httplib::Response& res);

        // --- SSH Key Handlers ---
        void handleCreateSSHKey(const httplib::Request& req, httplib::Response& res);
        void handleDeleteSSHKey(const httplib::Request& req, httplib::Response& res);
        void handleListSSHKeys(const httplib::Request& req, httplib::Response& res);

        // --- VM Handlers ---
        void handleCreateVM(const httplib::Request& req, httplib::Response& res);
        void handleDeleteVM(const httplib::Request& req, httplib::Response& res);
        void handleGetVM(const httplib::Request& req, httplib::Response& res);
        void handleListVMs(const httplib::Request& req, httplib::Response& res);
        void handleListVMLocations(const httplib::Request& req, httplib::Response& res);
        void handleListVMOSImages(const httplib::Request& req, httplib::Response& res);
        void handleListVMSKUs(const httplib::Request& req, httplib::Response& res);
        void handleRestartVM(const httplib::Request& req, httplib::Response& res);
        void handleResumeVM(const httplib::Request& req, httplib::Response& res);
        void handleSuspendVM(const httplib::Request& req, httplib::Response& res);

        // Declare an instance of K8sHandlers
        std::unique_ptr<K8sHandlers> k8sHandlers;
    };

} // namespace NMC::Server

