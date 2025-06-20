// server/APIRoutes.h
#ifndef NMC_SERVER_API_ROUTES_H
#define NMC_SERVER_API_ROUTES_H

#include <httplib.h>
#include <nlohmann/json.hpp>
#include "Models/CloudResponse.h"
#include "Models/Bucket.h"
#include "Models/K8sCluster.h"
#include "Models/SSHKey.h"
#include "Models/VM.h"
#include <vector>
#include <map>
#include <mutex> // For thread-safe access to mock data

namespace NMC {
    namespace Server {

// This class will register all API endpoints with the httplib server.
        class APIRoutes {
        public:
            APIRoutes(httplib::Server& svr);

        private:
            // Mock data storage
            std::vector<Models::Bucket> mockBuckets;
            std::vector<Models::K8sCluster> mockK8sClusters;
            std::vector<Models::SSHKey> mockSshKeys;
            std::vector<Models::VM> mockVMs;

            // Mutex for thread-safe access to mock data
            std::mutex dataMutex;

            // Helper to send JSON responses
            void sendJsonResponse(httplib::Response& res, const Models::CloudResponse& apiResponse) const;
            void sendErrorResponse(httplib::Response& res, int status, const std::string& message) const;
            void logRequest(const httplib::Request& req) const;

            // --- Bucket Handlers ---
            void handleCreateBucket(const httplib::Request& req, httplib::Response& res);
            void handleDeleteBucket(const httplib::Request& req, httplib::Response& res);
            void handleGetBucket(const httplib::Request& req, httplib::Response& res);
            void handleListBuckets(const httplib::Request& req, httplib::Response& res);
            void handleListBucketLocations(const httplib::Request& req, httplib::Response& res);
            void handleListBucketTypes(const httplib::Request& req, httplib::Response& res);
            void handleResetBucketKey(const httplib::Request& req, httplib::Response& res);

            // --- K8s Handlers ---
            void handleCreateK8sCluster(const httplib::Request& req, httplib::Response& res);
            void handleDeleteK8sCluster(const httplib::Request& req, httplib::Response& res);
            void handleGetK8sCluster(const httplib::Request& req, httplib::Response& res);
            void handleGetKubeConfig(const httplib::Request& req, httplib::Response& res);
            void handleListK8sClusters(const httplib::Request& req, httplib::Response& res);
            void handleListK8sLocations(const httplib::Request& req, httplib::Response& res);
            void handleResumeK8sCluster(const httplib::Request& req, httplib::Response& res);
            void handleSuspendK8sCluster(const httplib::Request& req, httplib::Response& res);

            // --- Model Handlers ---
            void handleUploadModel(const httplib::Request& req, httplib::Response& res);

            // --- SSH Handlers ---
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
        };

    } // namespace Server
} // namespace NMC

#endif // NMC_SERVER_API_ROUTES_H