// nmc_server/src/Core/ProxmoxClient.h
#pragma once

#include "OpenShiftClient.h"

namespace NMC::Server {

    /**
     * @brief Proxmox portal client facade.
     *
     * Like the OpenStack facade, this delegates HTTP transport and response parsing
     * to the shared provider-portal client implementation while keeping route
     * ownership explicit in APIRoutes.
     */
    class ProxmoxClient {
    public:
        explicit ProxmoxClient(const std::string& baseUrl);

        Models::CloudResponse getResources();
        Models::CloudResponse listClusters();
        Models::CloudResponse requestCluster(const nlohmann::json& requestBody);

    private:
        OpenShiftClient delegate;
    };

} // namespace NMC::Server
