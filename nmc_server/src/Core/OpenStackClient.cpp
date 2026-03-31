// nmc_server/src/Core/OpenStackClient.cpp
#include "OpenStackClient.h"

namespace NMC::Server {

    OpenStackClient::OpenStackClient(const std::string& baseUrl)
            : delegate(baseUrl, "OpenStack") {}

    Models::CloudResponse OpenStackClient::getResources() {
        return delegate.getResources();
    }

    Models::CloudResponse OpenStackClient::listClusters() {
        return delegate.listClusters();
    }

    Models::CloudResponse OpenStackClient::requestCluster(const nlohmann::json& requestBody) {
        return delegate.requestCluster(requestBody);
    }

} // namespace NMC::Server

