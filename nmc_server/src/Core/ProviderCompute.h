// Provider-aware compute lifecycle adapter for AWS, GCP and Azure.
#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace NMC::Server {

    struct ProviderComputeResult {
        bool success{false};
        int httpStatus{502};
        std::string message;
        nlohmann::json data{nlohmann::json::object()};
    };

    /**
     * Execute scoped cloud compute operations through the vendor CLIs.
     *
     * All commands use fixed executable paths and argv vectors. Provider scope
     * allowlists are mandatory, and mutations remain disabled unless the runtime
     * explicitly enables them.
     */
    class ProviderCompute final {
    public:
        static ProviderComputeResult status();
        static ProviderComputeResult list(const std::string& provider,
                                          const std::string& scope,
                                          const std::string& region);
        static ProviderComputeResult create(const nlohmann::json& request);
        static ProviderComputeResult action(const nlohmann::json& request);
    };

} // namespace NMC::Server
