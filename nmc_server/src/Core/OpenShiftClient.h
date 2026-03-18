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
     * @brief Lightweight client for the NeuralMimicry OpenShift Portal API.
     *
     * This client is used by the NMC server to proxy OpenShift provisioning
     * and resource visibility requests to the oshift backend.
     */
    class OpenShiftClient {
    public:
        explicit OpenShiftClient(const std::string& baseUrl);

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
        std::unique_ptr<httplib::Client> client;
        mutable std::mutex clientMutex;
    };

} // namespace NMC::Server
