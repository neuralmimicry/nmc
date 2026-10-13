#pragma once
#include <nlohmann/json.hpp>

namespace NMC::Server {
// This declares a supported surge-first transition, not immunity to runtime
// regressions. Single-writer/shared-state applications require a cutover adapter.
inline bool zeroUnavailableStrategy(const nlohmann::json& deployment) {
    try {
        const auto& spec = deployment.at("spec");
        const auto labels = deployment.at("metadata").value("labels", nlohmann::json::object());
        if (labels.value("neuralmimicry.ai/surge-safe", "") != "enabled"
                || spec.value("replicas", 0) < 1 || spec.value("minReadySeconds", 0) < 10) return false;
        const auto& strategy = spec.at("strategy");
        const auto& rolling = strategy.at("rollingUpdate");
        const auto& unavailable = rolling.at("maxUnavailable");
        const auto& surge = rolling.at("maxSurge");
        const bool zero = (unavailable.is_number_integer() && unavailable.get<int>() == 0)
                || (unavailable.is_string() && unavailable == "0%");
        // Require a positive integer surge, avoiding rounding ambiguity.
        if (strategy.at("type") != "RollingUpdate" || !zero
                || !surge.is_number_integer() || surge.get<int>() < 1) return false;
        const auto& pod = spec.at("template").at("spec");
        if (pod.value("hostNetwork", false) || pod.value("hostPID", false)) return false;
        const auto containers = pod.at("containers");
        if (!containers.is_array() || containers.empty()) return false;
        for (const auto& container : containers) {
            if (!container.contains("readinessProbe") || !container["readinessProbe"].is_object()
                    || container["readinessProbe"].empty()) return false;
            for (const auto& port : container.value("ports", nlohmann::json::array())) {
                if (port.contains("hostPort")) return false;
            }
        }
        // Conservatively reject shared persistent/host storage even when claimed
        // read-only. Stateful writers must use separately qualified traffic cutover.
        for (const auto& volume : pod.value("volumes", nlohmann::json::array())) {
            if (volume.contains("persistentVolumeClaim") || volume.contains("hostPath")
                    || volume.contains("nfs") || volume.contains("csi")) return false;
        }
        return true;
    } catch (...) { return false; }
}
}
