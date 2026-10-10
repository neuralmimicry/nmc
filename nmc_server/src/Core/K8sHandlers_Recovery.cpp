#include "K8sHandlers.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <set>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>

namespace NMC::Server {
namespace {

bool validDnsLabel(const std::string& value) {
    static const std::regex pattern(R"(^[a-z0-9](?:[-a-z0-9]*[a-z0-9])?$)");
    return value.size() <= 63 && std::regex_match(value, pattern);
}

bool validRequestId(const std::string& value) {
    static const std::regex pattern(R"(^[A-Za-z0-9][A-Za-z0-9._-]{7,127}$)");
    return std::regex_match(value, pattern);
}

bool validGhcrDigest(const std::string& value) {
    static const std::string prefix = "ghcr.io/neuralmimicry/";
    static const std::string marker = "@sha256:";
    static const std::regex repositoryPattern(
            R"(^[a-z0-9]+(?:[._-][a-z0-9]+)*(?:/[a-z0-9]+(?:[._-][a-z0-9]+)*)*$)");
    if (value.size() > 255 || value.rfind(prefix, 0) != 0) return false;
    const size_t digestMarker = value.rfind(marker);
    if (digestMarker == std::string::npos || digestMarker <= prefix.size()
            || value.size() - digestMarker - marker.size() != 64) return false;
    const std::string repository = value.substr(prefix.size(), digestMarker - prefix.size());
    const std::string digest = value.substr(digestMarker + marker.size());
    return std::regex_match(repository, repositoryPattern)
            && std::all_of(digest.begin(), digest.end(), [](unsigned char ch) {
                return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
            });
}

bool printableValue(const std::string& value, size_t maximum) {
    if (value.empty() || value.size() > maximum
            || std::isspace(static_cast<unsigned char>(value.front()))
            || std::isspace(static_cast<unsigned char>(value.back()))) {
        return false;
    }
    return std::none_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0 || std::iscntrl(ch) != 0;
    });
}

bool containsExactKeySet(const nlohmann::json& object,
                         const std::set<std::string>& requiredKeys) {
    if (!object.is_object() || object.size() != requiredKeys.size()) return false;
    for (auto item = object.cbegin(); item != object.cend(); ++item) {
        if (requiredKeys.count(item.key()) == 0) return false;
    }
    return true;
}

nlohmann::json deploymentStatusData(const nlohmann::json& deployment) {
    const auto metadata = deployment.value("metadata", nlohmann::json::object());
    const auto spec = deployment.value("spec", nlohmann::json::object());
    const auto status = deployment.value("status", nlohmann::json::object());
    const auto deploymentAnnotations = metadata.value("annotations", nlohmann::json::object());
    const auto podSpec = spec.value("template", nlohmann::json::object())
            .value("spec", nlohmann::json::object());
    const auto templateMetadata = spec.value("template", nlohmann::json::object())
            .value("metadata", nlohmann::json::object());
    const auto annotations = templateMetadata.value("annotations", nlohmann::json::object());
    const auto containers = podSpec.value("containers", nlohmann::json::array());
    const auto containerStatuses = status.value("containerStatuses", nlohmann::json::array());
    std::unordered_map<std::string, bool> readyByName;
    if (containerStatuses.is_array()) {
        for (const auto& item : containerStatuses) {
            if (!item.is_object() || !item.contains("name") || !item["name"].is_string()) continue;
            readyByName[item["name"].get<std::string>()] = item.value("ready", false);
        }
    }

    nlohmann::json containerImages = nlohmann::json::array();
    if (containers.is_array()) {
        for (const auto& item : containers) {
            if (!item.is_object() || !item.contains("name") || !item["name"].is_string()) continue;
            const std::string name = item["name"].get<std::string>();
            containerImages.push_back({
                    {"name", name},
                    {"image", item.value("image", "")},
                    {"ready", readyByName[name]}
            });
        }
    }

    const int64_t generation = metadata.value("generation", int64_t{0});
    const int64_t observedGeneration = status.value("observedGeneration", int64_t{0});
    const int desired = spec.value("replicas", 1);
    const int updated = status.value("updatedReplicas", 0);
    const int ready = status.value("readyReplicas", 0);
    const int available = status.value("availableReplicas", 0);
    return {
            {"name", metadata.value("name", "")},
            {"namespace", metadata.value("namespace", "")},
            {"uid", metadata.value("uid", "")},
            {"resource_version", metadata.value("resourceVersion", "")},
            {"generation", generation},
            {"observed_generation", observedGeneration},
            {"desired_replicas", desired},
            {"updated_replicas", updated},
            {"ready_replicas", ready},
            {"available_replicas", available},
            {"rollout_request_id", annotations.value("neuralmimicry.ai/rollout-request-id", "")},
            {"rollout_change_id", annotations.value("neuralmimicry.ai/rollout-change-id", "")},
            {"octobot_config_request_id", deploymentAnnotations.value("neuralmimicry.ai/octobot-config-request-id", "")},
            {"octobot_config_change_id", deploymentAnnotations.value("neuralmimicry.ai/octobot-config-change-id", "")},
            {"containers", std::move(containerImages)},
            {"rollout_complete", desired > 0 && observedGeneration >= generation
                    && updated >= desired && ready >= desired && available >= desired}
    };
}

std::string utcTimestamp() {
  const std::time_t now =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm utc{};
  gmtime_r(&now, &utc);
  char buffer[32]{};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return buffer;
}

bool rolloutSettingWithin(const nlohmann::json& value, int maximum, bool allowZero) {
    if (value.is_number_integer()) {
        const int parsed = value.get<int>();
        return (allowZero ? parsed >= 0 : parsed > 0) && parsed <= maximum;
    }
    if (!value.is_string()) {
        return false;
    }
    std::string text = value.get<std::string>();
    if (text.empty() || text.back() != '%') {
        return false;
    }
    text.pop_back();
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        return false;
    }
    try {
        const int parsed = std::stoi(text);
        return (allowZero ? parsed >= 0 : parsed > 0) && parsed <= maximum;
    } catch (const std::exception&) {
        return false;
    }
}

bool ownedBy(const nlohmann::json& object, const std::string& uid, const std::string& kind) {
    const auto references = object.value("metadata", nlohmann::json::object())
            .value("ownerReferences", nlohmann::json::array());
    if (!references.is_array()) {
        return false;
    }
    return std::any_of(references.begin(), references.end(), [&](const nlohmann::json& reference) {
        return reference.is_object()
                && reference.value("uid", "") == uid
                && reference.value("kind", "") == kind
                && reference.value("controller", false);
    });
}

bool labelsMatchSelector(const nlohmann::json& labels, const nlohmann::json& selector) {
    if (!labels.is_object() || !selector.is_object()) {
        return false;
    }
    const auto matchLabels = selector.value("matchLabels", nlohmann::json::object());
    if (!matchLabels.is_object() || matchLabels.empty()) {
        return false;
    }
    for (auto item = matchLabels.begin(); item != matchLabels.end(); ++item) {
        if (!labels.contains(item.key()) || !labels[item.key()].is_string()
                || labels[item.key()].get<std::string>() != item.value().get<std::string>()) {
            return false;
        }
    }
    // Expression selectors are intentionally unsupported here. They are
    // valid Kubernetes, but recovery must not guess how a selector matches.
    const auto expressions = selector.value("matchExpressions", nlohmann::json::array());
    return expressions.is_array() && expressions.empty();
}

void addBlocker(nlohmann::json& blockers, const std::string& code, const std::string& detail) {
    blockers.push_back({{"code", code}, {"detail", detail}});
}

bool freshNodeHeartbeat(const std::string& timestamp) {
    if (timestamp.empty()) return false;
    std::tm heartbeat{};
    std::istringstream input(timestamp);
    input >> std::get_time(&heartbeat, "%Y-%m-%dT%H:%M:%S");
    if (input.fail()) return false;
    const std::time_t heartbeatTime = timegm(&heartbeat);
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const double ageSeconds = std::difftime(now, heartbeatTime);
    return ageSeconds >= -30.0 && ageSeconds <= 120.0;
}

bool podReady(const nlohmann::json& pod) {
    const auto status = pod.value("status", nlohmann::json::object());
    const auto conditions = status.value("conditions", nlohmann::json::array());
    if (!conditions.is_array()) {
        return false;
    }
    return std::any_of(conditions.begin(), conditions.end(), [](const nlohmann::json& condition) {
        return condition.is_object()
                && condition.value("type", "") == "Ready"
                && condition.value("status", "") == "True";
    });
}

bool nodeHealthy(const nlohmann::json& node, nlohmann::json& reasons) {
    const auto status = node.value("status", nlohmann::json::object());
    const auto conditions = status.value("conditions", nlohmann::json::array());
    std::set<std::string> seenConditions;
    bool ready = false;
    if (conditions.is_array()) {
        for (const auto& condition : conditions) {
            if (!condition.is_object()) continue;
            const std::string type = condition.value("type", "");
            const std::string value = condition.value("status", "");
            if (type == "Ready" || type == "MemoryPressure" || type == "DiskPressure" || type == "PIDPressure") {
                seenConditions.insert(type);
            }
            if (type == "Ready" && value == "True") ready = true;
            if ((type == "Ready" && value != "True")
                    || ((type == "MemoryPressure" || type == "DiskPressure"
                         || type == "PIDPressure" || type == "NetworkUnavailable")
                        && value != "False")) {
                reasons.push_back(type + "=" + value);
            }
            if (type == "Ready"
                    && !freshNodeHeartbeat(condition.value("lastHeartbeatTime", ""))) {
                reasons.push_back("Ready heartbeat is missing, stale or in the future");
            }
        }
    }
    for (const auto* required : {"Ready", "MemoryPressure", "DiskPressure", "PIDPressure"}) {
        if (seenConditions.count(required) == 0) {
            reasons.push_back(std::string("missing node condition ") + required);
        }
    }
    if (!ready && reasons.empty()) reasons.push_back("Ready condition is not True");
    const auto spec = node.value("spec", nlohmann::json::object());
    if (spec.value("unschedulable", false)) reasons.push_back("node is unschedulable");
    const auto taints = spec.value("taints", nlohmann::json::array());
    if (taints.is_array()) {
        for (const auto& taint : taints) {
            const std::string key = taint.value("key", "");
            const std::string effect = taint.value("effect", "");
            if ((effect == "NoSchedule" || effect == "NoExecute")
                    && (key == "node.kubernetes.io/not-ready"
                        || key == "node.kubernetes.io/unreachable"
                        || key == "node.kubernetes.io/disk-pressure"
                        || key == "node.kubernetes.io/memory-pressure"
                        || key == "node.kubernetes.io/pid-pressure")) {
                reasons.push_back("blocking taint " + key);
            }
        }
    }
    return ready && reasons.empty();
}

} // namespace

nlohmann::json K8sHandlers::inspectDeploymentRecoveryState(
        const std::string& namespaceName,
        const std::string& deploymentName,
        std::string& error
) {
    nlohmann::json result = {
            {"namespace", namespaceName},
            {"deployment", deploymentName},
            {"recovery_enabled", recoveryEnabled},
            {"eligible", false},
            {"blockers", nlohmann::json::array()},
            {"replicasets", nlohmann::json::array()},
            {"pods", nlohmann::json::array()},
            {"hosts", nlohmann::json::array()},
    };
    auto& blockers = result["blockers"];
    const auto deployment = readNamespacedResource("apps", "v1", "deployments", namespaceName, deploymentName);
    if (!deployment || !deployment->is_object()) {
        error = "Kubernetes deployment state could not be read; recovery preflight failed closed.";
        addBlocker(blockers, "deployment_unavailable", error);
        return result;
    }

    const auto metadata = deployment->value("metadata", nlohmann::json::object());
    const auto spec = deployment->value("spec", nlohmann::json::object());
    const auto status = deployment->value("status", nlohmann::json::object());
    const std::string deploymentUid = metadata.value("uid", "");
    const std::string resourceVersion = metadata.value("resourceVersion", "");
    const int64_t generation = metadata.value("generation", int64_t{0});
    const int64_t observedGeneration = status.value("observedGeneration", int64_t{0});
    const int desired = spec.value("replicas", 1);
    result["resource_version"] = resourceVersion;
    result["uid"] = deploymentUid;
    result["recovery_request_id"] = metadata.value("annotations", nlohmann::json::object())
            .value("neuralmimicry.ai/recovery-request-id", "");
    result["template_recovery_request_id"] = spec.value("template", nlohmann::json::object())
            .value("metadata", nlohmann::json::object())
            .value("annotations", nlohmann::json::object())
            .value("neuralmimicry.ai/recovery-request-id", "");
    result["generation"] = generation;
    result["observed_generation"] = observedGeneration;
    result["desired_replicas"] = desired;
    result["ready_replicas"] = status.value("readyReplicas", 0);
    result["available_replicas"] = status.value("availableReplicas", 0);
    result["updated_replicas"] = status.value("updatedReplicas", 0);

    const auto deploymentLabels = metadata.value("labels", nlohmann::json::object());
    if (!deploymentLabels.is_object()
            || deploymentLabels.value("neuralmimicry.ai/recovery", "") != "enabled") {
        addBlocker(blockers, "target_not_allowlisted",
                "Deployment must explicitly set neuralmimicry.ai/recovery=enabled before recovery is allowed.");
    }

    if (deploymentUid.empty() || resourceVersion.empty()) {
        addBlocker(blockers, "identity_missing", "Deployment UID or resourceVersion is missing.");
    }
    if (desired < 1) addBlocker(blockers, "suspended", "Deployment has no desired replicas.");
    const auto strategy = spec.value("strategy", nlohmann::json::object());
    if (strategy.value("type", "RollingUpdate") != "RollingUpdate") {
        addBlocker(blockers, "unsafe_strategy", "Only RollingUpdate deployments are eligible for recovery.");
    }
    const auto rollingUpdate = strategy.value("rollingUpdate", nlohmann::json::object());
    if (rollingUpdate.contains("maxUnavailable")
            && !rolloutSettingWithin(rollingUpdate["maxUnavailable"], 25, true)) {
        addBlocker(blockers, "unsafe_max_unavailable", "Deployment maxUnavailable exceeds the recovery safety limit.");
    }
    if (rollingUpdate.contains("maxSurge")
            && !rolloutSettingWithin(rollingUpdate["maxSurge"], 100, false)) {
        addBlocker(blockers, "unsafe_max_surge", "Deployment maxSurge must remain positive for a safe restart.");
    }
    if (generation > 0 && observedGeneration < generation) {
        addBlocker(blockers, "generation_unobserved", "Deployment controller has not observed the current spec.");
    }
    if (status.value("updatedReplicas", 0) < desired
            || status.value("readyReplicas", 0) < desired
            || status.value("availableReplicas", 0) < desired
            || status.value("unavailableReplicas", 0) > 0) {
        addBlocker(blockers, "rollout_incomplete", "The Deployment does not have all desired replicas updated, ready and available.");
    }

    const auto conditions = status.value("conditions", nlohmann::json::array());
    if (!conditions.is_array()) {
        addBlocker(blockers, "conditions_unavailable", "Deployment conditions are missing or malformed.");
    } else {
        bool availableConditionSeen = false;
        bool progressingConditionSeen = false;
        for (const auto& condition : conditions) {
            if (!condition.is_object()) continue;
            const std::string type = condition.value("type", "");
            const std::string conditionStatus = condition.value("status", "");
            if (type == "Available") availableConditionSeen = true;
            if (type == "Progressing") progressingConditionSeen = true;
            if ((type == "Available" && conditionStatus != "True")
                    || (type == "Progressing" && conditionStatus != "True")
                    || condition.value("reason", "") == "ProgressDeadlineExceeded") {
                addBlocker(blockers, "deployment_condition", type + " is " + conditionStatus
                        + " (" + condition.value("reason", "unknown") + ").");
            }
        }
        if (!availableConditionSeen) {
            addBlocker(blockers, "available_condition_missing", "Deployment has no Available condition.");
        }
        if (!progressingConditionSeen) {
            addBlocker(blockers, "progressing_condition_missing", "Deployment has no Progressing condition.");
        }
    }

    const auto selector = spec.value("selector", nlohmann::json::object());
    if (deploymentUid.empty() || !selector.is_object()) {
        addBlocker(blockers, "selector_unavailable", "Deployment identity or pod selector is unavailable.");
    }

    const auto replicaSetList = listNamespacedResources("apps", "v1", "replicasets", namespaceName);
    const auto podList = listNamespacedResources("", "v1", "pods", namespaceName);
    if (!replicaSetList || !replicaSetList->contains("items") || !(*replicaSetList)["items"].is_array()) {
        addBlocker(blockers, "replicasets_unavailable", "Owned ReplicaSets could not be inspected.");
    }
    if (!podList || !podList->contains("items") || !(*podList)["items"].is_array()) {
        addBlocker(blockers, "pods_unavailable", "Deployment Pods could not be inspected.");
    }

    std::set<std::string> activeReplicaSetUids;
    if (replicaSetList && replicaSetList->contains("items") && (*replicaSetList)["items"].is_array()) {
        for (const auto& replicaSet : (*replicaSetList)["items"]) {
            if (!ownedBy(replicaSet, deploymentUid, "Deployment")) continue;
            const auto rsMetadata = replicaSet.value("metadata", nlohmann::json::object());
            const auto rsSpec = replicaSet.value("spec", nlohmann::json::object());
            const std::string uid = rsMetadata.value("uid", "");
            const int rsDesired = rsSpec.value("replicas", 1);
            result["replicasets"].push_back({
                    {"name", rsMetadata.value("name", "")}, {"uid", uid},
                    {"desired_replicas", rsDesired},
                    {"ready_replicas", replicaSet.value("status", nlohmann::json::object()).value("readyReplicas", 0)}
            });
            if (uid.empty()) addBlocker(blockers, "replicaset_identity_missing", "An owned ReplicaSet has no UID.");
            if (!labelsMatchSelector(
                    rsSpec.value("template", nlohmann::json::object())
                            .value("metadata", nlohmann::json::object())
                            .value("labels", nlohmann::json::object()), selector)) {
                addBlocker(blockers, "replicaset_selector_mismatch", "An owned ReplicaSet template does not match the Deployment selector.");
            }
            if (rsDesired > 0 && !uid.empty()) activeReplicaSetUids.insert(uid);
        }
    }
    if (activeReplicaSetUids.empty()) {
        addBlocker(blockers, "active_replicaset_missing", "No active owned ReplicaSet was found for the Deployment.");
    }

    std::set<std::string> hostNames;
    if (podList && podList->contains("items") && (*podList)["items"].is_array()) {
        for (const auto& pod : (*podList)["items"]) {
            const auto podMetadata = pod.value("metadata", nlohmann::json::object());
            const auto podSpec = pod.value("spec", nlohmann::json::object());
            const auto podStatus = pod.value("status", nlohmann::json::object());
            const auto owners = podMetadata.value("ownerReferences", nlohmann::json::array());
            std::string replicaSetUid;
            if (owners.is_array()) {
                for (const auto& owner : owners) {
                    if (owner.is_object() && owner.value("kind", "") == "ReplicaSet"
                            && owner.value("controller", true)) {
                        replicaSetUid = owner.value("uid", "");
                        break;
                    }
                }
            }
            const bool active = activeReplicaSetUids.count(replicaSetUid) > 0;
            if (!active) continue;
            const std::string podName = podMetadata.value("name", "");
            const std::string nodeName = podSpec.value("nodeName", "");
            const bool terminating = podMetadata.contains("deletionTimestamp");
            const std::string phase = podStatus.value("phase", "Unknown");
            const bool ready = podReady(pod);
            result["pods"].push_back({{"name", podName}, {"node", nodeName}, {"phase", phase},
                    {"ready", ready}, {"terminating", terminating}, {"replicaset_uid", replicaSetUid}});
            if (terminating) addBlocker(blockers, "pod_terminating", "Active Deployment Pod " + podName + " is terminating.");
            if (nodeName.empty()) addBlocker(blockers, "pod_unscheduled", "Active Deployment Pod " + podName + " has no assigned host.");
            if (phase != "Running" || !ready) addBlocker(blockers, "pod_not_ready", "Active Deployment Pod " + podName + " is not Running and Ready.");
            if (!nodeName.empty()) hostNames.insert(nodeName);
        }
    }
    if (static_cast<int>(result["pods"].size()) < desired) {
        addBlocker(blockers, "pods_below_desired", "Fewer active Deployment Pods were found than desired replicas.");
    }

    genericClient_t* nodeClient = nullptr;
    char* nodesRaw = nullptr;
    std::optional<nlohmann::json> nodeList;
    try {
        nodeClient = getGenericClient("", "v1", "nodes");
        nodesRaw = Generic_list(nodeClient);
        if (nodesRaw) nodeList = nlohmann::json::parse(nodesRaw);
    } catch (const std::exception&) {
        nodeList = std::nullopt;
    }
    if (nodesRaw) free(nodesRaw);
    if (nodeClient) genericClient_free(nodeClient);
    if (!nodeList || !nodeList->contains("items") || !(*nodeList)["items"].is_array()) {
        addBlocker(blockers, "nodes_unavailable", "Kubernetes host readiness could not be inspected.");
    } else {
        std::unordered_map<std::string, nlohmann::json> nodesByName;
        for (const auto& node : (*nodeList)["items"]) {
            const std::string name = node.value("metadata", nlohmann::json::object()).value("name", "");
            if (!name.empty()) nodesByName.emplace(name, node);
        }
        for (const auto& host : hostNames) {
            const auto found = nodesByName.find(host);
            if (found == nodesByName.end()) {
                addBlocker(blockers, "host_missing", "Pod host " + host + " is absent from the live Node list.");
                result["hosts"].push_back({{"name", host}, {"healthy", false}, {"reasons", {"node object missing"}}});
                continue;
            }
            nlohmann::json reasons = nlohmann::json::array();
            const bool healthy = nodeHealthy(found->second, reasons);
            const auto hostMetadata = found->second.value("metadata", nlohmann::json::object());
            const auto hostLabels = hostMetadata.value("labels", nlohmann::json::object());
            result["hosts"].push_back({
                    {"name", host},
                    {"provider_id", found->second.value("spec", nlohmann::json::object()).value("providerID", "")},
                    {"hostname", hostLabels.value("kubernetes.io/hostname", host)},
                    {"provider", hostLabels.value("neuralmimicry.ai/cloud-provider", "unknown")},
                    {"environment", hostLabels.value("neuralmimicry.ai/environment", "unknown")},
                    {"zone", hostLabels.value("topology.kubernetes.io/zone", "")},
                    {"instance_type", hostLabels.value("node.kubernetes.io/instance-type", "")},
                    {"healthy", healthy},
                    {"reasons", reasons}
            });
            if (!healthy) addBlocker(blockers, "host_unhealthy", "Pod host " + host + " is unsafe for recovery.");
        }
    }

    result["eligible"] = blockers.empty();
    return result;
}

void K8sHandlers::handleGetDeploymentRecoveryStatus(const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::mutex> recoveryLock(recoveryMutex);
    const std::string clusterId = req.get_param_value("cluster_id");
    const std::string namespaceName = req.get_param_value("namespace");
    const std::string deploymentName = req.get_param_value("deployment");
    if (!validDnsLabel(clusterId) || !validDnsLabel(namespaceName) || !validDnsLabel(deploymentName)) {
        return sendErrorResponse(res, 400, "cluster_id, namespace and deployment must be valid Kubernetes DNS labels.");
    }
    if (namespaceName == "kube-system" || namespaceName == "kube-public" || namespaceName == "kube-node-lease") {
        return sendErrorResponse(res, 403, "System namespaces are outside workload recovery scope.");
    }
    if (activeRecoveryClusterId.empty() || !validDnsLabel(activeRecoveryClusterId)) {
        Models::CloudResponse response;
        response.success = true;
        response.message = "Recovery preflight is blocked because this Continuum process has no valid cluster identity bound to its active kubeconfig.";
        response.data = {
                {"cluster_id", clusterId},
                {"namespace", namespaceName},
                {"deployment", deploymentName},
                {"recovery_enabled", recoveryEnabled},
                {"eligible", false},
                {"blockers", {{{"code", "cluster_identity_unconfigured"},
                                {"detail", "Set NMC_K8S_CLUSTER_ID to the stable identity of this process's active Kubernetes context."}}}}
        };
        return sendJsonResponse(res, response);
    }
    if (!recoveryClientReady) {
        Models::CloudResponse response;
        response.success = true;
        response.message = "Recovery preflight is blocked because the active Kubernetes context was not loaded from an authenticated kubeconfig.";
        response.data = {
                {"cluster_id", clusterId},
                {"namespace", namespaceName},
                {"deployment", deploymentName},
                {"recovery_enabled", recoveryEnabled},
                {"eligible", false},
                {"blockers", {{{"code", "kubeconfig_unavailable"},
                                {"detail", "Recovery requires a successfully loaded kubeconfig; the unauthenticated direct-URL fallback is not eligible."}}}}
        };
        return sendJsonResponse(res, response);
    }
    if (clusterId != activeRecoveryClusterId) {
        Models::CloudResponse response;
        response.success = true;
        response.message = "Recovery preflight is blocked because the requested cluster does not match this Continuum process's active Kubernetes context.";
        response.data = {
                {"cluster_id", clusterId},
                {"namespace", namespaceName},
                {"deployment", deploymentName},
                {"recovery_enabled", recoveryEnabled},
                {"eligible", false},
                {"blockers", {{{"code", "cluster_scope_mismatch"},
                                {"detail", "Requested cluster identity is not bound to this Continuum Kubernetes client."}}}}
        };
        return sendJsonResponse(res, response);
    }
    std::string error;
    nlohmann::json state;
    try {
        state = inspectDeploymentRecoveryState(namespaceName, deploymentName, error);
    } catch (const std::exception&) {
        error = "Kubernetes returned malformed recovery state; preflight failed closed.";
        state = {
                {"namespace", namespaceName},
                {"deployment", deploymentName},
                {"eligible", false},
                {"blockers", {{{"code", "malformed_live_state"}, {"detail", error}}}},
        };
    }
    state["recovery_enabled"] = recoveryEnabled;
    if (!recoveryEnabled) {
        if (!state.contains("blockers") || !state["blockers"].is_array()) {
            state["blockers"] = nlohmann::json::array();
        }
        addBlocker(state["blockers"], "recovery_disabled",
                "NMC_RECOVERY_ENABLED is not explicitly true; the read-only preflight is allowed, but restart requests remain disabled.");
        state["eligible"] = false;
    }
    state["cluster_id"] = clusterId;
    Models::CloudResponse response;
    // Ineligibility is a successful read with explicit blockers. Keep this
    // endpoint useful for diagnosing safe suppression rather than conflating
    // it with an HTTP/API failure.
    response.success = true;
    response.message = error.empty() ? "Deployment and host recovery preflight completed."
                                     : "Recovery preflight could not establish safe live state.";
    response.data = std::move(state);
    sendJsonResponse(res, response);
}

void K8sHandlers::handleRestartDeployment(const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::mutex> recoveryLock(recoveryMutex);
    try {
        const auto body = nlohmann::json::parse(req.body);
        if (!body.is_object()) {
            return sendErrorResponse(res, 400, "Expected a JSON object.");
        }

        const std::string namespaceName = body.value("namespace", "");
        const std::string deploymentName = body.value("deployment", "");
        const std::string clusterId = body.value("cluster_id", "");
        const std::string requestId = body.value("request_id", "");
        if (!validDnsLabel(clusterId) || !validDnsLabel(namespaceName) || !validDnsLabel(deploymentName)) {
            return sendErrorResponse(res, 400, "cluster_id, namespace and deployment must be valid Kubernetes DNS labels.");
        }
        if (!validRequestId(requestId)) {
            return sendErrorResponse(res, 400, "request_id must be 8-128 safe ASCII characters.");
        }
        if (namespaceName == "kube-system" || namespaceName == "kube-public" || namespaceName == "kube-node-lease") {
            return sendErrorResponse(res, 403, "System namespaces cannot be restarted through workload recovery.");
        }
        if (activeRecoveryClusterId.empty() || !validDnsLabel(activeRecoveryClusterId)) {
            return sendErrorResponse(res, 503, "No valid cluster identity is bound to Continuum's active Kubernetes context; no restart was attempted.");
        }
        if (!recoveryClientReady) {
            return sendErrorResponse(res, 503, "The authenticated kubeconfig client is unavailable; the unauthenticated direct-URL fallback cannot perform recovery.");
        }
        if (clusterId != activeRecoveryClusterId) {
            return sendErrorResponse(res, 409, "Requested cluster does not match Continuum's active Kubernetes context; no restart was attempted.");
        }
        if (!recoveryEnabled) {
            return sendErrorResponse(res, 403, "NMC workload recovery is disabled by policy; no restart was attempted.");
        }

        genericClient_t* deploymentClient = getGenericClient("apps", "v1", "deployments");
        if (!deploymentClient) {
            return sendErrorResponse(res, 503, "Kubernetes deployment API is unavailable.");
        }

        char* deploymentRaw = Generic_readNamespacedResource(
                deploymentClient,
                const_cast<char*>(namespaceName.c_str()),
                const_cast<char*>(deploymentName.c_str())
        );
        if (!deploymentRaw) {
            const int statusCode = apiClient && apiClient->response_code == 404
                    ? 404
                    : (apiClient && apiClient->response_code >= 400 ? apiClient->response_code : 503);
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, statusCode, statusCode == 404
                    ? "Deployment not found."
                    : "Unable to read the Kubernetes deployment; no restart was requested.");
        }

        nlohmann::json current;
        try {
            current = nlohmann::json::parse(deploymentRaw);
        } catch (const std::exception&) {
            free(deploymentRaw);
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, 502, "Kubernetes returned an invalid deployment response.");
        }
        free(deploymentRaw);

        const auto metadata = current.value("metadata", nlohmann::json::object());
        const auto spec = current.value("spec", nlohmann::json::object());
        const auto status = current.value("status", nlohmann::json::object());
        const int desired = spec.value("replicas", 1);
        if (desired < 1) {
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, 409, "Suspended deployments are not eligible for workload recovery.");
        }

        const auto strategy = spec.value("strategy", nlohmann::json::object());
        const std::string strategyType = strategy.value("type", "RollingUpdate");
        if (strategyType != "RollingUpdate") {
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, 409, "Only RollingUpdate deployments can be restarted safely.");
        }
        const auto rollingUpdate = strategy.value("rollingUpdate", nlohmann::json::object());
        // Kubernetes defaults are 25% unavailable and 25% surge. Restrict
        // explicit values so a restart cannot intentionally take the whole
        // workload down; malformed settings fail closed.
        if (rollingUpdate.contains("maxUnavailable")
            && !rolloutSettingWithin(rollingUpdate["maxUnavailable"], 25, true)) {
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, 409, "Deployment maxUnavailable exceeds the recovery safety limit.");
        }
        if (rollingUpdate.contains("maxSurge")
            && !rolloutSettingWithin(rollingUpdate["maxSurge"], 100, false)) {
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, 409, "Deployment maxSurge must remain positive for a safe restart.");
        }

        const auto annotations = metadata.value("annotations", nlohmann::json::object());
        const std::string existingRequestId = annotations.value("neuralmimicry.ai/recovery-request-id", "");
        const int available = status.value("availableReplicas", 0);
        const int ready = status.value("readyReplicas", 0);
        if (existingRequestId == requestId) {
            genericClient_free(deploymentClient);
            Models::CloudResponse response;
            response.success = true;
            response.message = "This recovery request was already applied to the deployment.";
            response.data = {
                    {"namespace", namespaceName},
                    {"deployment", deploymentName},
                    {"cluster_id", clusterId},
                    {"request_id", requestId},
                    {"already_applied", true},
                    {"desired_replicas", desired},
                    {"ready_replicas", ready},
                    {"available_replicas", available}
            };
            return sendJsonResponse(res, response);
        }

        std::string preflightError;
        const auto preflight = inspectDeploymentRecoveryState(namespaceName, deploymentName, preflightError);
        if (!preflight.value("eligible", false)
                || preflight.value("resource_version", "") != metadata.value("resourceVersion", "")) {
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, 409, preflightError.empty()
                    ? "Deployment, Pod or host state changed or became ineligible before recovery."
                    : preflightError);
        }

        const auto generation = metadata.value("generation", int64_t{0});
        const auto observedGeneration = status.value("observedGeneration", int64_t{0});
        const int updated = status.value("updatedReplicas", 0);
        if (generation > 0 && observedGeneration < generation) {
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, 409, "Deployment has an unobserved spec change; wait for its current rollout.");
        }
        if (updated < desired) {
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, 409, "Deployment already has an incomplete rollout; automated restart is suppressed.");
        }

        const std::string resourceVersion = metadata.value("resourceVersion", "");
        if (resourceVersion.empty()) {
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, 409, "Deployment has no resourceVersion; refusing a non-conditional restart.");
        }

        const std::string requestedAt = utcTimestamp();
        // Refresh all cluster-wide health and ownership checks immediately
        // before the conditional write. The Deployment resourceVersion below
        // protects against a concurrent spec change after this read.
        std::string finalPreflightError;
        const auto finalPreflight = inspectDeploymentRecoveryState(namespaceName, deploymentName, finalPreflightError);
        if (!finalPreflight.value("eligible", false)
                || finalPreflight.value("resource_version", "") != resourceVersion) {
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, 409, finalPreflightError.empty()
                    ? "Recovery preflight changed before the conditional restart; no action was taken."
                    : finalPreflightError);
        }

        const nlohmann::json patch = {
                {"metadata", {
                        {"resourceVersion", resourceVersion},
                        {"annotations", {
                                {"neuralmimicry.ai/recovery-request-id", requestId},
                                {"neuralmimicry.ai/recovery-requested-at", requestedAt}
                        }}
                }},
                {"spec", {
                        {"template", {
                                {"metadata", {"annotations", {
                                        {"neuralmimicry.ai/recovery-request-id", requestId},
                                        {"neuralmimicry.ai/recovery-requested-at", requestedAt}
                                }}}
                        }}
                }}
        };
        const std::string patchBody = patch.dump();
        char* patchedRaw = Generic_patchNamespacedResource(
                deploymentClient,
                const_cast<char*>(namespaceName.c_str()),
                const_cast<char*>(deploymentName.c_str()),
                const_cast<char*>(patchBody.c_str()),
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr
        );
        const int patchStatus = apiClient ? apiClient->response_code : 0;
        genericClient_free(deploymentClient);
        if (!patchedRaw) {
            return sendErrorResponse(res, patchStatus >= 400 ? patchStatus : 503,
                    "Kubernetes rejected the conditional deployment restart request.");
        }

        nlohmann::json patched;
        try {
            patched = nlohmann::json::parse(patchedRaw);
        } catch (const std::exception&) {
            free(patchedRaw);
            return sendErrorResponse(res, 502, "Kubernetes accepted the restart but returned invalid JSON.");
        }
        free(patchedRaw);

        Models::CloudResponse response;
        response.success = true;
        response.message = "Deployment rollout restart requested.";
        response.data = {
                {"namespace", namespaceName},
                {"deployment", deploymentName},
                {"cluster_id", clusterId},
                {"request_id", requestId},
                {"requested_at", requestedAt},
                {"already_applied", false},
                {"desired_replicas", desired},
                {"ready_replicas", ready},
                {"available_replicas", available},
                {"resource_version", patched.value("metadata", nlohmann::json::object()).value("resourceVersion", "")}
        };
        sendJsonResponse(res, response);
    } catch (const nlohmann::json::parse_error&) {
        sendErrorResponse(res, 400, "Invalid JSON body.");
    } catch (const std::exception&) {
        sendErrorResponse(res, 500, "Deployment recovery request failed safely.");
    }
}

void K8sHandlers::handleGetDeploymentStatus(const httplib::Request& req, httplib::Response& res) {
    const std::string clusterId = req.get_param_value("cluster_id");
    const std::string namespaceName = req.get_param_value("namespace");
    const std::string deploymentName = req.get_param_value("deployment");
    if (!validDnsLabel(clusterId) || !validDnsLabel(namespaceName) || !validDnsLabel(deploymentName)) {
        return sendErrorResponse(res, 400, "cluster_id, namespace and deployment must be valid Kubernetes DNS labels.");
    }
    if (activeRecoveryClusterId.empty() || clusterId != activeRecoveryClusterId) {
        return sendErrorResponse(res, 409, "Requested cluster does not match Continuum's active Kubernetes context.");
    }
    if (!recoveryClientReady || !workloadLogsApiClient) {
        return sendErrorResponse(res, 503, "Deployment status requires the configured authenticated kubeconfig.");
    }

    std::lock_guard<std::mutex> clientLock(workloadLogsClientMutex);
    char group[] = "apps";
    char version[] = "v1";
    char plural[] = "deployments";
    genericClient_t* deploymentClient = genericClient_create(
            workloadLogsApiClient,
            group,
            version,
            plural
    );
    if (!deploymentClient) {
        return sendErrorResponse(res, 503, "Continuum could not initialise its Kubernetes Deployment reader.");
    }
    char* rawDeployment = Generic_readNamespacedResource(
            deploymentClient,
            const_cast<char*>(namespaceName.c_str()),
            const_cast<char*>(deploymentName.c_str())
    );
    const int readStatus = workloadLogsApiClient->response_code;
    genericClient_free(deploymentClient);
    if (!rawDeployment) {
        return sendErrorResponse(res, readStatus == 404 ? 404 : 502,
                readStatus == 404 ? "Deployment was not found in the requested namespace."
                                  : "Continuum could not read Deployment status from Kubernetes.");
    }

    nlohmann::json deployment;
    try {
        deployment = nlohmann::json::parse(rawDeployment);
    } catch (const std::exception&) {
        free(rawDeployment);
        return sendErrorResponse(res, 502, "Kubernetes returned invalid Deployment status JSON.");
    }
    free(rawDeployment);
    if (!deployment.is_object()) {
        return sendErrorResponse(res, 502, "Kubernetes returned an invalid Deployment object.");
    }

    Models::CloudResponse response;
    response.success = true;
    response.message = "Continuum Deployment status retrieved.";
    response.data = deploymentStatusData(deployment);
    response.data["cluster_id"] = clusterId;
    sendJsonResponse(res, response);
}

void K8sHandlers::handleRolloutDeploymentImage(const httplib::Request& req, httplib::Response& res) {
    if (!imageRolloutEnabled) {
        return sendErrorResponse(res, 403,
                "Continuum Deployment image rollouts are disabled by NMC_K8S_IMAGE_ROLLOUT_ENABLED policy.");
    }
    if (!recoveryClientReady || !workloadLogsApiClient) {
        return sendErrorResponse(res, 503,
                "Deployment image rollouts require the configured authenticated kubeconfig.");
    }

    try {
        const auto body = nlohmann::json::parse(req.body);
        static const std::set<std::string> expectedKeys = {
                "cluster_id", "namespace", "deployment", "container", "image",
                "expected_image", "request_id", "change_id"
        };
        if (!containsExactKeySet(body, expectedKeys)) {
            return sendErrorResponse(res, 400,
                    "Expected exactly cluster_id, namespace, deployment, container, image, expected_image, request_id and change_id.");
        }
        for (const auto& key : expectedKeys) {
            if (!body[key].is_string()) {
                return sendErrorResponse(res, 400, key + " must be a string.");
            }
        }

        const std::string clusterId = body["cluster_id"].get<std::string>();
        const std::string namespaceName = body["namespace"].get<std::string>();
        const std::string deploymentName = body["deployment"].get<std::string>();
        const std::string containerName = body["container"].get<std::string>();
        const std::string image = body["image"].get<std::string>();
        const std::string expectedImage = body["expected_image"].get<std::string>();
        const std::string requestId = body["request_id"].get<std::string>();
        const std::string changeId = body["change_id"].get<std::string>();

        if (!validDnsLabel(clusterId) || !validDnsLabel(namespaceName)
                || !validDnsLabel(deploymentName) || !validDnsLabel(containerName)) {
            return sendErrorResponse(res, 400,
                    "cluster_id, namespace, deployment and container must be valid Kubernetes DNS labels.");
        }
        if (!validGhcrDigest(image)) {
            return sendErrorResponse(res, 400,
                    "image must be an immutable ghcr.io/neuralmimicry/<repository>@sha256:<64 lowercase hex> reference.");
        }
        if (!printableValue(expectedImage, 512) || !validRequestId(requestId)
                || !validRequestId(changeId)) {
            return sendErrorResponse(res, 400,
                    "expected_image must be a bounded reference and request_id/change_id must be valid identifiers.");
        }
        if (activeRecoveryClusterId.empty() || clusterId != activeRecoveryClusterId) {
            return sendErrorResponse(res, 409,
                    "Requested cluster does not match Continuum's active Kubernetes context.");
        }

        std::lock_guard<std::mutex> rolloutLock(rolloutMutex);
        std::lock_guard<std::mutex> clientLock(workloadLogsClientMutex);
        char group[] = "apps";
        char version[] = "v1";
        char plural[] = "deployments";
        genericClient_t* deploymentClient = genericClient_create(
                workloadLogsApiClient,
                group,
                version,
                plural
        );
        if (!deploymentClient) {
            return sendErrorResponse(res, 503, "Continuum could not initialise its Kubernetes Deployment client.");
        }

        char* rawDeployment = Generic_readNamespacedResource(
                deploymentClient,
                const_cast<char*>(namespaceName.c_str()),
                const_cast<char*>(deploymentName.c_str())
        );
        const int readStatus = workloadLogsApiClient->response_code;
        if (!rawDeployment) {
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, readStatus == 404 ? 404 : 502,
                    readStatus == 404 ? "Deployment was not found in the requested namespace."
                                      : "Continuum could not read current Deployment state.");
        }

        nlohmann::json deployment;
        try {
            deployment = nlohmann::json::parse(rawDeployment);
        } catch (const std::exception&) {
            free(rawDeployment);
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, 502, "Kubernetes returned invalid Deployment JSON.");
        }
        free(rawDeployment);
        if (!deployment.is_object()) {
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, 502, "Kubernetes returned an invalid Deployment object.");
        }

        const auto metadata = deployment.value("metadata", nlohmann::json::object());
        const auto labels = metadata.value("labels", nlohmann::json::object());
        if (!labels.is_object()
                || labels.value("neuralmimicry.ai/continuum-rollout", "") != "enabled") {
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, 403,
                    "Deployment must explicitly set neuralmimicry.ai/continuum-rollout=enabled.");
        }
        const std::string resourceVersion = metadata.value("resourceVersion", "");
        if (resourceVersion.empty()) {
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, 409, "Deployment resourceVersion is missing; rollout precondition failed.");
        }

        auto podTemplate = deployment.value("spec", nlohmann::json::object())
                .value("template", nlohmann::json::object());
        auto podSpec = podTemplate.value("spec", nlohmann::json::object());
        auto containers = podSpec.value("containers", nlohmann::json::array());
        if (!containers.is_array()) {
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, 502, "Deployment has no valid container list.");
        }
        auto selected = containers.end();
        for (auto it = containers.begin(); it != containers.end(); ++it) {
            if (it->is_object() && it->value("name", "") == containerName) {
                selected = it;
                break;
            }
        }
        if (selected == containers.end()) {
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, 404, "The requested container is not present in the Deployment.");
        }
        const std::string currentImage = selected->value("image", "");
        if (!printableValue(currentImage, 512)) {
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, 409, "Current container image is missing or invalid.");
        }

        auto templateMetadata = podTemplate.value("metadata", nlohmann::json::object());
        auto annotations = templateMetadata.value("annotations", nlohmann::json::object());
        if (!annotations.is_object()) annotations = nlohmann::json::object();
        const std::string previousRequestId = annotations.value("neuralmimicry.ai/rollout-request-id", "");
        if (!previousRequestId.empty() && previousRequestId == requestId) {
            const bool sameIntent = annotations.value("neuralmimicry.ai/rollout-change-id", "") == changeId
                    && annotations.value("neuralmimicry.ai/rollout-container", "") == containerName
                    && annotations.value("neuralmimicry.ai/rollout-image", "") == image
                    && currentImage == image;
            genericClient_free(deploymentClient);
            if (!sameIntent) {
                return sendErrorResponse(res, 409, "request_id was already used for a different image rollout intent.");
            }
            Models::CloudResponse response;
            response.success = true;
            response.message = "Deployment image rollout request was already applied.";
            response.data = deploymentStatusData(deployment);
            response.data["cluster_id"] = clusterId;
            response.data["container"] = containerName;
            response.data["image"] = image;
            response.data["already_applied"] = true;
            sendJsonResponse(res, response);
            return;
        }
        if (currentImage != expectedImage) {
            genericClient_free(deploymentClient);
            return sendErrorResponse(res, 409,
                    "Current container image does not match expected_image; no rollout was attempted.");
        }
        if (currentImage == image) {
            genericClient_free(deploymentClient);
            Models::CloudResponse response;
            response.success = true;
            response.message = "Deployment already uses the requested immutable image.";
            response.data = deploymentStatusData(deployment);
            response.data["cluster_id"] = clusterId;
            response.data["container"] = containerName;
            response.data["image"] = image;
            response.data["already_applied"] = true;
            sendJsonResponse(res, response);
            return;
        }

        selected->operator[]("image") = image;
        const std::string requestedAt = utcTimestamp();
        annotations["neuralmimicry.ai/rollout-request-id"] = requestId;
        annotations["neuralmimicry.ai/rollout-change-id"] = changeId;
        annotations["neuralmimicry.ai/rollout-container"] = containerName;
        annotations["neuralmimicry.ai/rollout-image"] = image;
        annotations["neuralmimicry.ai/rollout-previous-image"] = currentImage;
        annotations["neuralmimicry.ai/rollout-requested-at"] = requestedAt;
        templateMetadata["annotations"] = annotations;

        const nlohmann::json patch = {
                {"metadata", {{"resourceVersion", resourceVersion}}},
                {"spec", {{"template", {
                        {"metadata", templateMetadata},
                        {"spec", {{"containers", containers}}}
                }}}}
        };
        const std::string patchBody = patch.dump();
        char* rawPatched = Generic_patchNamespacedResource(
                deploymentClient,
                const_cast<char*>(namespaceName.c_str()),
                const_cast<char*>(deploymentName.c_str()),
                const_cast<char*>(patchBody.c_str()),
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr
        );
        const int patchStatus = workloadLogsApiClient->response_code;
        genericClient_free(deploymentClient);
        if (!rawPatched) {
            return sendErrorResponse(res, patchStatus >= 400 ? patchStatus : 503,
                    "Kubernetes rejected the conditional Deployment image rollout.");
        }

        nlohmann::json patched;
        try {
            patched = nlohmann::json::parse(rawPatched);
        } catch (const std::exception&) {
            free(rawPatched);
            return sendErrorResponse(res, 502, "Kubernetes accepted the image rollout but returned invalid JSON.");
        }
        free(rawPatched);
        if (!patched.is_object()) {
            return sendErrorResponse(res, 502, "Kubernetes returned an invalid patched Deployment object.");
        }

        Models::CloudResponse response;
        response.success = true;
        response.message = "Deployment image rollout accepted; poll Continuum status for readiness.";
        response.data = deploymentStatusData(patched);
        response.data["cluster_id"] = clusterId;
        response.data["container"] = containerName;
        response.data["previous_image"] = currentImage;
        response.data["image"] = image;
        response.data["request_id"] = requestId;
        response.data["change_id"] = changeId;
        response.data["requested_at"] = requestedAt;
        response.data["changed"] = true;
        response.data["already_applied"] = false;
        sendJsonResponse(res, response);
    } catch (const nlohmann::json::parse_error&) {
      sendErrorResponse(res, 400, "Invalid JSON body.");
    } catch (const std::exception &) {
      sendErrorResponse(res, 500, "Deployment image rollout failed safely.");
    }
}

void K8sHandlers::handleConfigureOctoBot(const httplib::Request &req,
                                         httplib::Response &res) {
  if (!octobotConfigurationEnabled) {
    return sendErrorResponse(res, 403,
                             "Continuum OctoBot configuration is disabled by "
                             "NMC_K8S_OCTOBOT_CONFIGURATION_ENABLED policy.");
  }
  if (!recoveryClientReady || !workloadLogsApiClient) {
    return sendErrorResponse(res, 503,
                             "OctoBot configuration requires the configured "
                             "authenticated Kubernetes kubeconfig.");
  }
  if (req.body.size() > 8192) {
    return sendErrorResponse(
        res, 413, "OctoBot configuration request exceeds the 8 KiB limit.");
  }

  try {
    const auto request = nlohmann::json::parse(req.body);
    if (!containsExactKeySet(request, {"cluster_id", "namespace", "deployment",
                                       "request_id", "expected_request_id",
                                       "change_id", "live_execution",
                                       "service_integrations_enabled"})) {
      return sendErrorResponse(res, 400,
                               "OctoBot configuration requires exactly the "
                               "documented policy fields.");
    }
    for (const std::string &key :
         {"cluster_id", "namespace", "deployment", "request_id",
          "expected_request_id", "change_id"}) {
      if (!request[key].is_string()) {
        return sendErrorResponse(
            res, 400, "OctoBot configuration identifiers must be strings.");
      }
    }
    const std::string clusterId = request.at("cluster_id").get<std::string>();
    const std::string namespaceName =
        request.at("namespace").get<std::string>();
    const std::string deploymentName =
        request.at("deployment").get<std::string>();
    const std::string requestId = request.at("request_id").get<std::string>();
    const std::string expectedRequestId =
        request.at("expected_request_id").get<std::string>();
    const std::string changeId = request.at("change_id").get<std::string>();
    if (clusterId != "rk1" || namespaceName != "octobot" ||
        deploymentName != "octobot") {
      return sendErrorResponse(res, 400,
                               "OctoBot configuration is restricted to cluster "
                               "rk1, namespace octobot, Deployment octobot.");
    }
    if (!validRequestId(requestId) ||
        (!expectedRequestId.empty() && !validRequestId(expectedRequestId)) ||
        !printableValue(changeId, 128)) {
      return sendErrorResponse(
          res, 400,
          "request_id, expected_request_id and change_id must be bounded, "
          "printable identifiers; expected_request_id may be empty only before "
          "the first recorded policy request.");
    }
    if (!request.at("live_execution").is_boolean() ||
        !request.at("service_integrations_enabled").is_boolean()) {
      return sendErrorResponse(
          res, 400,
          "live_execution and service_integrations_enabled must be JSON "
          "booleans.");
    }
    if (activeRecoveryClusterId != clusterId) {
      return sendErrorResponse(res, 409,
                               "Requested cluster does not match Continuum's "
                               "active Kubernetes context.");
    }

    const bool liveExecution = request.at("live_execution").get<bool>();
    const bool serviceIntegrationsEnabled =
        request.at("service_integrations_enabled").get<bool>();
    std::lock_guard<std::mutex> rolloutLock(rolloutMutex);
    std::lock_guard<std::mutex> clientLock(workloadLogsClientMutex);
    char group[] = "apps";
    char version[] = "v1";
    char plural[] = "deployments";
    genericClient_t *deploymentClient =
        genericClient_create(workloadLogsApiClient, group, version, plural);
    if (!deploymentClient) {
      return sendErrorResponse(
          res, 503,
          "Continuum could not initialise its Kubernetes Deployment client.");
    }

    char *rawDeployment = Generic_readNamespacedResource(
        deploymentClient, const_cast<char *>(namespaceName.c_str()),
        const_cast<char *>(deploymentName.c_str()));
    const int readStatus = workloadLogsApiClient->response_code;
    if (!rawDeployment) {
      genericClient_free(deploymentClient);
      return sendErrorResponse(
          res, readStatus == 404 ? 404 : 502,
          readStatus == 404
              ? "OctoBot Deployment was not found."
              : "Continuum could not read the OctoBot Deployment.");
    }

    nlohmann::json deployment;
    try {
      deployment = nlohmann::json::parse(rawDeployment);
    } catch (const std::exception &) {
      free(rawDeployment);
      genericClient_free(deploymentClient);
      return sendErrorResponse(
          res, 502, "Kubernetes returned invalid OctoBot Deployment JSON.");
    }
    free(rawDeployment);
    const auto metadata =
        deployment.value("metadata", nlohmann::json::object());
    const auto labels = metadata.value("labels", nlohmann::json::object());
    if (!labels.is_object() ||
        labels.value("neuralmimicry.ai/continuum-configuration", "") !=
            "enabled" ||
        labels.value("neuralmimicry.ai/continuum-rollout", "") != "enabled") {
      genericClient_free(deploymentClient);
      return sendErrorResponse(res, 403,
                               "OctoBot Deployment must explicitly enable "
                               "Continuum configuration and rollout.");
    }
    const std::string resourceVersion = metadata.value("resourceVersion", "");
    if (resourceVersion.empty()) {
      genericClient_free(deploymentClient);
      return sendErrorResponse(res, 409,
                               "OctoBot Deployment resourceVersion is missing; "
                               "configuration precondition failed.");
    }

    auto policyAnnotations =
        metadata.value("annotations", nlohmann::json::object());
    if (!policyAnnotations.is_object())
      policyAnnotations = nlohmann::json::object();
    const std::string currentRequestId = policyAnnotations.value(
        "neuralmimicry.ai/octobot-config-request-id", "");
    if (policyAnnotations.value("neuralmimicry.ai/octobot-config-request-id",
                                "") == requestId) {
      const auto currentPodSpec =
          deployment.value("spec", nlohmann::json::object())
              .value("template", nlohmann::json::object())
              .value("spec", nlohmann::json::object());
      const auto matchesEnvironmentValue =
          [&](const std::string &collectionName, const std::string &targetName,
              const std::string &variableName,
              const std::string &desiredValue) {
            const auto entries =
                currentPodSpec.value(collectionName, nlohmann::json::array());
            if (!entries.is_array())
              return false;
            const nlohmann::json *target = nullptr;
            for (const auto &entry : entries) {
              if (!entry.is_object() || entry.value("name", "") != targetName)
                continue;
              if (target)
                return false;
              target = &entry;
            }
            if (!target)
              return false;
            const auto env = target->value("env", nlohmann::json::array());
            if (!env.is_array())
              return false;
            const nlohmann::json *variable = nullptr;
            for (const auto &entry : env) {
              if (!entry.is_object() || entry.value("name", "") != variableName)
                continue;
              if (variable)
                return false;
              variable = &entry;
            }
            return variable && !variable->contains("valueFrom") &&
                   variable->value("value", "") == desiredValue;
          };
      const std::string expectedLive = liveExecution ? "true" : "false";
      const std::string expectedServices =
          serviceIntegrationsEnabled ? "true" : "false";
      const bool settingsMatch =
          matchesEnvironmentValue("containers", "octobot",
                                  "OCTOBOT_LIVE_EXECUTION_ENABLED",
                                  expectedLive) &&
          matchesEnvironmentValue("containers", "octobot",
                                  "OCTOBOT_SERVICE_INTEGRATIONS_ENABLED",
                                  expectedServices) &&
          matchesEnvironmentValue("initContainers", "bootstrap-gail-authority",
                                  "OCTOBOT_LIVE_EXECUTION_ENABLED",
                                  expectedLive) &&
          matchesEnvironmentValue("initContainers", "bootstrap-gail-authority",
                                  "OCTOBOT_SERVICE_INTEGRATIONS_ENABLED",
                                  expectedServices);
      const bool sameIntent =
          settingsMatch &&
          policyAnnotations.value(
              "neuralmimicry.ai/octobot-config-expected-request-id", "") ==
              expectedRequestId &&
          policyAnnotations.value("neuralmimicry.ai/octobot-config-change-id",
                                  "") == changeId &&
          policyAnnotations.value("neuralmimicry.ai/octobot-live-execution",
                                  "") == expectedLive &&
          policyAnnotations.value(
              "neuralmimicry.ai/octobot-service-integrations", "") ==
              expectedServices;
      genericClient_free(deploymentClient);
      if (!sameIntent) {
        return sendErrorResponse(res, 409,
                                 "request_id was already used for a different "
                                 "OctoBot configuration intent.");
      }
      Models::CloudResponse response;
      response.success = true;
      response.message = "OctoBot configuration intent was already applied; "
                         "inspect Deployment status for readiness.";
      response.data = deploymentStatusData(deployment);
      response.data["cluster_id"] = clusterId;
      response.data["request_id"] = requestId;
      response.data["change_id"] = changeId;
      response.data["live_execution"] = liveExecution;
      response.data["service_integrations_enabled"] =
          serviceIntegrationsEnabled;
      response.data["changed"] = false;
      response.data["already_applied"] = true;
      sendJsonResponse(res, response);
      return;
    }

    if (expectedRequestId != currentRequestId) {
      genericClient_free(deploymentClient);
      return sendErrorResponse(
          res, 409,
          "OctoBot configuration is stale: expected_request_id does not match "
          "the latest recorded policy request.");
    }

    auto podTemplate = deployment.value("spec", nlohmann::json::object())
                           .value("template", nlohmann::json::object());
    auto podSpec = podTemplate.value("spec", nlohmann::json::object());
    auto containers = podSpec.value("containers", nlohmann::json::array());
    auto initContainers =
        podSpec.value("initContainers", nlohmann::json::array());
    if (!containers.is_array() || !initContainers.is_array()) {
      genericClient_free(deploymentClient);
      return sendErrorResponse(
          res, 409,
          "OctoBot Deployment must define container and initContainer arrays.");
    }

    bool changed = false;
    const auto setPolicy = [&](nlohmann::json &entries,
                               const std::string &targetName,
                               const std::string &variableName,
                               const std::string &desiredValue) -> bool {
      auto target = entries.end();
      for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (!it->is_object() || it->value("name", "") != targetName)
          continue;
        if (target != entries.end())
          return false;
        target = it;
      }
      if (target == entries.end())
        return false;
      auto env = target->value("env", nlohmann::json::array());
      if (!env.is_array())
        return false;
      auto variable = env.end();
      for (auto it = env.begin(); it != env.end(); ++it) {
        if (!it->is_object() || it->value("name", "") != variableName)
          continue;
        if (variable != env.end())
          return false;
        variable = it;
      }
      if (variable == env.end()) {
        env.push_back({{"name", variableName}, {"value", desiredValue}});
        changed = true;
      } else {
        if (variable->contains("valueFrom"))
          return false;
        if (variable->value("value", "") != desiredValue) {
          (*variable)["value"] = desiredValue;
          changed = true;
        }
      }
      (*target)["env"] = std::move(env);
      return true;
    };

    const std::string liveValue = liveExecution ? "true" : "false";
    const std::string servicesValue =
        serviceIntegrationsEnabled ? "true" : "false";
    const bool mainLiveReady = setPolicy(
        containers, "octobot", "OCTOBOT_LIVE_EXECUTION_ENABLED", liveValue);
    const bool mainServicesReady =
        setPolicy(containers, "octobot", "OCTOBOT_SERVICE_INTEGRATIONS_ENABLED",
                  servicesValue);
    const bool initLiveReady =
        setPolicy(initContainers, "bootstrap-gail-authority",
                  "OCTOBOT_LIVE_EXECUTION_ENABLED", liveValue);
    const bool initServicesReady =
        setPolicy(initContainers, "bootstrap-gail-authority",
                  "OCTOBOT_SERVICE_INTEGRATIONS_ENABLED", servicesValue);
    if (!mainLiveReady || !mainServicesReady || !initLiveReady ||
        !initServicesReady) {
      genericClient_free(deploymentClient);
      return sendErrorResponse(
          res, 409,
          "OctoBot must have unique, literal configuration variables in its "
          "main and bootstrap containers.");
    }

    const std::string requestedAt = utcTimestamp();
    policyAnnotations["neuralmimicry.ai/octobot-config-request-id"] = requestId;
    policyAnnotations["neuralmimicry.ai/octobot-config-expected-request-id"] =
        expectedRequestId;
    policyAnnotations["neuralmimicry.ai/octobot-config-change-id"] = changeId;
    policyAnnotations["neuralmimicry.ai/octobot-live-execution"] = liveValue;
    policyAnnotations["neuralmimicry.ai/octobot-service-integrations"] =
        servicesValue;
    policyAnnotations["neuralmimicry.ai/octobot-config-requested-at"] =
        requestedAt;
    nlohmann::json patch = {{"metadata",
                             {{"resourceVersion", resourceVersion},
                              {"annotations", policyAnnotations}}}};
    if (changed) {
      patch["spec"] = {{"template",
                        {{"spec",
                          {{"containers", containers},
                           {"initContainers", initContainers}}}}}};
    }
    const std::string patchBody = patch.dump();
    char *rawPatched = Generic_patchNamespacedResource(
        deploymentClient, const_cast<char *>(namespaceName.c_str()),
        const_cast<char *>(deploymentName.c_str()),
        const_cast<char *>(patchBody.c_str()), nullptr, nullptr, nullptr,
        nullptr, nullptr);
    const int patchStatus = workloadLogsApiClient->response_code;
    genericClient_free(deploymentClient);
    if (!rawPatched) {
      return sendErrorResponse(
          res, patchStatus >= 400 ? patchStatus : 503,
          "Kubernetes rejected the conditional OctoBot configuration request.");
    }
    nlohmann::json patched;
    try {
      patched = nlohmann::json::parse(rawPatched);
    } catch (const std::exception &) {
      free(rawPatched);
      return sendErrorResponse(res, 502,
                               "Kubernetes accepted OctoBot configuration but "
                               "returned invalid JSON.");
    }
    free(rawPatched);
    if (!patched.is_object()) {
      return sendErrorResponse(
          res, 502,
          "Kubernetes returned an invalid patched OctoBot Deployment.");
    }

    Models::CloudResponse response;
    response.success = true;
    response.message = changed ? "OctoBot configuration accepted; poll "
                                 "Continuum status for readiness."
                               : "OctoBot configuration recorded; the "
                                 "requested settings were already present.";
    response.data = deploymentStatusData(patched);
    response.data["cluster_id"] = clusterId;
    response.data["request_id"] = requestId;
    response.data["change_id"] = changeId;
    response.data["live_execution"] = liveExecution;
    response.data["service_integrations_enabled"] = serviceIntegrationsEnabled;
    response.data["requested_at"] = requestedAt;
    response.data["changed"] = changed;
    response.data["already_applied"] = false;
    sendJsonResponse(res, response);
  } catch (const nlohmann::json::parse_error &) {
    sendErrorResponse(res, 400, "Invalid JSON body.");
  } catch (const std::exception &) {
    sendErrorResponse(res, 500, "OctoBot configuration request failed safely.");
  }
}

} // namespace NMC::Server
