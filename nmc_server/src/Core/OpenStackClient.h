// nmc_server/src/Core/OpenStackClient.h
#pragma once

#include "OpenShiftClient.h"

namespace NMC::Server {

    /**
     * @brief OpenStack portal client facade.
     *
     * This wrapper intentionally delegates transport and parsing concerns to the
     * shared OpenShiftClient implementation, configured with an "OpenStack" label.
     * It provides a provider-specific type so APIRoutes can remain explicit and
     * readable without duplicating HTTP client logic.
     */
    class OpenStackClient {
    public:
        explicit OpenStackClient(const std::string& baseUrl);

        Models::CloudResponse getResources();
        Models::CloudResponse listClusters();
        Models::CloudResponse requestCluster(const nlohmann::json& requestBody);

    private:
        OpenShiftClient delegate;
    };

} // namespace NMC::Server

