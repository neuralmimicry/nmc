// nmc_server/src/Core/OpenShiftClient.h
#pragma once

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <mutex>
#include <string>

#include "../Models/CloudResponse.h"

namespace NMC::Server {

    /**
     * @brief Lightweight HTTP client for backend cluster orchestration portals.
     *
     * The class keeps OpenShift naming for backward compatibility in the codebase,
     * but is intentionally backend-agnostic: callers can provide a backend label
     * (for example, "OpenShift", "OpenStack", or "Proxmox") to customize
     * response messages.
     *
     * This keeps request/response transport logic in one place and allows
     * additional providers to be introduced without duplicating low-level HTTP code.
     */
    class OpenShiftClient {
    public:
        /**
         * Construct a backend portal client.
         *
         * @param baseUrl Base URL for the backend API service.
         * @param backendLabel Human-readable label used in response messages.
         */
        explicit OpenShiftClient(const std::string& baseUrl, std::string backendLabel = "OpenShift");

        Models::CloudResponse getResources();
        Models::CloudResponse listClusters();
        Models::CloudResponse requestCluster(const nlohmann::json& requestBody);

    private:
        struct ParsedUrl {
            std::string host;
            int port;
            std::string basePath;
            bool https;
        };

        ParsedUrl parseBaseUrl(const std::string& baseUrl) const;
        std::string buildPath(const std::string& suffix) const;
        Models::CloudResponse processHttpResponse(httplib::Result& res,
                                                  const std::string& successMessage) const;

        ParsedUrl url;
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        std::unique_ptr<httplib::SSLClient> httpsClient;
#endif
        std::unique_ptr<httplib::Client> httpClient;
        std::string backendLabel;
        mutable std::mutex clientMutex;
    };

} // namespace NMC::Server
