// nmc_server/src/Core/ProxmoxClient.cpp
#include "ProxmoxClient.h"

namespace NMC::Server {

    ProxmoxClient::ProxmoxClient(const std::string& baseUrl)
            : delegate(baseUrl, "Proxmox") {}

    Models::CloudResponse ProxmoxClient::getResources() {
        return delegate.getResources();
    }

    Models::CloudResponse ProxmoxClient::listClusters() {
        return delegate.listClusters();
    }

    Models::CloudResponse ProxmoxClient::requestCluster(const nlohmann::json& requestBody) {
        return delegate.requestCluster(requestBody);
    }

} // namespace NMC::Server
