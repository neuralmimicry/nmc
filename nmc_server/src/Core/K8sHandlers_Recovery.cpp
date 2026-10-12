#include "K8sHandlers.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <set>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace NMC::Server {
namespace {

std::string trimCopy(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (first >= last) return {};
    return std::string(first, last);
}

bool validDnsLabel(const std::string& value) {
    static const std::regex pattern(R"(^[a-z0-9](?:[-a-z0-9]*[a-z0-9])?$)");
    return value.size() <= 63 && std::regex_match(value, pattern);
}

size_t boundedUtf8Prefix(const std::string& value, size_t maximumBytes) {
    if (value.size() <= maximumBytes) return value.size();
    size_t end = maximumBytes;
    if (end < value.size()
            && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80) {
        while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80) --end;
    }
    return end;
}

std::string boundedTextValue(const nlohmann::json& source,
                             const char* key,
                             size_t maximumBytes,
                             bool& truncated) {
    truncated = false;
    if (!source.is_object()) return "";
    const auto value = source.find(key);
    if (value == source.end() || !value->is_string()) return "";
    const std::string text = value->get<std::string>();
    const size_t prefixLength = boundedUtf8Prefix(text, maximumBytes);
    truncated = prefixLength < text.size();
    return text.substr(0, prefixLength);
}

void addBoundedText(nlohmann::json& target,
                    const nlohmann::json& source,
                    const char* sourceKey,
                    size_t maximumBytes,
                    const char* targetKey = nullptr) {
    const std::string outputKey = targetKey ? targetKey : sourceKey;
    bool truncated = false;
    target[outputKey] = boundedTextValue(source, sourceKey, maximumBytes, truncated);
    if (truncated) target[outputKey + "_truncated"] = true;
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

struct BoundedPodEventListDownload {
    curl_off_t maximumBytes;
    bool exceeded{false};
};

int abortOversizedPodEventList(void *limitPointer, curl_off_t downloadTotal, curl_off_t downloadNow, curl_off_t,
                               curl_off_t) {
    auto *limit = static_cast<BoundedPodEventListDownload *>(limitPointer);
    if (!limit || (downloadNow <= limit->maximumBytes && downloadTotal <= limit->maximumBytes)) {
        return 0;
    }
    limit->exceeded = true;
    return 1;
}

std::optional<nlohmann::json> listBoundedPodEvents(apiClient_t *client, const std::string &namespaceName,
                                                   const std::string &podUid, int limit, long &responseStatus,
                                                   bool &truncated) {
    constexpr curl_off_t maximumResponseBytes = 128 * 1024;
    responseStatus = 0;
    truncated = false;
    if (!client || namespaceName.empty() || podUid.empty() || limit < 1 || limit > 10) {
        return std::nullopt;
    }
    if (client->dataReceived) {
        free(client->dataReceived);
        client->dataReceived = nullptr;
        client->dataReceivedLen = 0;
    }
    client->response_code = 0;

    // Namespace and pod UID are read from validated Kubernetes objects. Encode
    // the field-selector equality delimiter so it cannot alter the query.
    const std::string requestPath = "/api/v1/namespaces/" + namespaceName +
                                    "/events?fieldSelector=involvedObject.uid%3D" + podUid +
                                    "&limit=" + std::to_string(limit);
    BoundedPodEventListDownload downloadLimit{maximumResponseBytes};
    const auto previousProgressFunction = client->progress_func;
    void *const previousProgressData = client->progress_data;
    client->progress_func = abortOversizedPodEventList;
    client->progress_data = &downloadLimit;
    apiClient_invoke(client, requestPath.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "GET");
    responseStatus = client->response_code;
    client->progress_func = previousProgressFunction;
    client->progress_data = previousProgressData;

    std::optional<nlohmann::json> result;
    if (!downloadLimit.exceeded && responseStatus >= 200 && responseStatus < 300 && client->dataReceived &&
        client->dataReceivedLen >= 0 &&
        static_cast<size_t>(client->dataReceivedLen) <= static_cast<size_t>(maximumResponseBytes)) {
        try {
            const std::string responseBody(static_cast<const char *>(client->dataReceived),
                                           static_cast<size_t>(client->dataReceivedLen));
            auto parsed = nlohmann::json::parse(responseBody);
            if (parsed.is_object() && parsed.contains("items") && parsed["items"].is_array()) {
                const auto metadata = parsed.value("metadata", nlohmann::json::object());
                if (metadata.is_object() && metadata.value("continue", "") != "") {
                    truncated = true;
                }
                result = std::move(parsed);
            }
        } catch (const std::exception &) {
            result = std::nullopt;
        }
    }
    if (downloadLimit.exceeded) {
        truncated = true;
        responseStatus = 502;
    } else if (client->dataReceivedLen > maximumResponseBytes) {
        truncated = true;
        responseStatus = 502;
    }
    if (client->dataReceived) {
        free(client->dataReceived);
        client->dataReceived = nullptr;
        client->dataReceivedLen = 0;
    }
    return result;
}

bool validPodUid(const std::string &value) {
    return !value.empty() && value.size() <= 128 && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-';
    });
}

const nlohmann::json *objectField(const nlohmann::json &source, const char *key) {
    if (!source.is_object())
        return nullptr;
    const auto value = source.find(key);
    return value != source.end() && value->is_object() ? &*value : nullptr;
}

std::string stringField(const nlohmann::json &source, const char *key) {
    if (!source.is_object())
        return "";
    const auto value = source.find(key);
    return value != source.end() && value->is_string() ? value->get<std::string>() : "";
}

nlohmann::json deploymentStatusData(const nlohmann::json& deployment,
                                   const nlohmann::json* podList = nullptr) {
    const auto metadata = deployment.value("metadata", nlohmann::json::object());
    const auto deploymentLabels = metadata.value("labels", nlohmann::json::object());
    const auto spec = deployment.value("spec", nlohmann::json::object());
    const auto status = deployment.value("status", nlohmann::json::object());
    const auto deploymentAnnotations = metadata.value("annotations", nlohmann::json::object());
    const auto podSpec = spec.value("template", nlohmann::json::object())
            .value("spec", nlohmann::json::object());
    const auto templateMetadata = spec.value("template", nlohmann::json::object())
            .value("metadata", nlohmann::json::object());
    const auto annotations = templateMetadata.value("annotations", nlohmann::json::object());
    const auto containers = podSpec.value("containers", nlohmann::json::array());
    std::unordered_map<std::string, bool> readyByName;
    std::unordered_map<std::string, size_t> statusCountByName;
    size_t nonTerminatingPodCount = 0;
    nlohmann::json podDiagnostics = nlohmann::json::array();
    bool podDiagnosticsTruncated = false;
    bool podListComplete = podList && podList->is_object()
            && podList->contains("items") && podList->at("items").is_array();
    if (podListComplete) {
        const auto listMetadata = podList->value("metadata", nlohmann::json::object());
        if (listMetadata.is_object()) {
            podDiagnosticsTruncated = !listMetadata.value("continue", "").empty();
        }
        podListComplete = listMetadata.is_object()
                && listMetadata.value("continue", "").empty();
    }
    if (podListComplete) {
        for (const auto& pod : podList->at("items")) {
            if (!pod.is_object()) {
                podListComplete = false;
                break;
            }
            const auto podMetadata = pod.value("metadata", nlohmann::json::object());
            if (!podMetadata.is_object()) {
                podListComplete = false;
                break;
            }
            const auto deletionTimestamp = podMetadata.value("deletionTimestamp", nlohmann::json());
            if (deletionTimestamp.is_string() && !deletionTimestamp.get<std::string>().empty()) continue;
            ++nonTerminatingPodCount;
            const auto podStatus = pod.value("status", nlohmann::json::object());
            if (!podStatus.is_object()) {
                for (const auto& item : containers) {
                    if (item.is_object() && item.contains("name") && item["name"].is_string()) {
                        readyByName[item["name"].get<std::string>()] = false;
                    }
                }
                continue;
            }
            const auto containerStatuses = podStatus.value("containerStatuses", nlohmann::json::array());
            if (!containerStatuses.is_array()) {
                for (const auto& item : containers) {
                    if (item.is_object() && item.contains("name") && item["name"].is_string()) {
                        const std::string name = item["name"].get<std::string>();
                        readyByName[name] = false;
                    }
                }
                continue;
            }
            for (const auto& item : containerStatuses) {
                if (!item.is_object() || !item.contains("name") || !item["name"].is_string()) continue;
                const std::string name = item["name"].get<std::string>();
                const bool ready = item.value("ready", false);
                if (statusCountByName[name] == 0) readyByName[name] = ready;
                else readyByName[name] = readyByName[name] && ready;
                ++statusCountByName[name];
            }
        }
    }

    if (podListComplete) {
        constexpr size_t maxDiagnosticPods = 64;
        constexpr size_t maxDiagnosticConditionsPerPod = 16;
        constexpr size_t maxDiagnosticContainerStatuses = 128;
        size_t diagnosticContainerStatusCount = 0;
        for (const auto& pod : podList->at("items")) {
            if (!pod.is_object()) continue;
            const auto podMetadata = pod.value("metadata", nlohmann::json::object());
            if (!podMetadata.is_object()) continue;
            const auto deletionTimestamp = podMetadata.value("deletionTimestamp", nlohmann::json());
            if (deletionTimestamp.is_string() && !deletionTimestamp.get<std::string>().empty()) continue;
            if (podDiagnostics.size() >= maxDiagnosticPods) {
                podDiagnosticsTruncated = true;
                break;
            }

            const auto podStatusValue = pod.value("status", nlohmann::json::object());
            const auto podSpecValue = pod.value("spec", nlohmann::json::object());
            const auto podStatus = podStatusValue.is_object() ? podStatusValue : nlohmann::json::object();
            const auto podSpec = podSpecValue.is_object() ? podSpecValue : nlohmann::json::object();
            nlohmann::json summary = nlohmann::json::object();
            addBoundedText(summary, podMetadata, "name", 63);
            addBoundedText(summary, podSpec, "nodeName", 253, "node");
            addBoundedText(summary, podStatus, "phase", 32);
            if (summary["phase"].get<std::string>().empty()) summary["phase"] = "Unknown";
            addBoundedText(summary, podStatus, "reason", 128);
            addBoundedText(summary, podStatus, "message", 512);

            nlohmann::json conditions = nlohmann::json::array();
            const auto conditionList = podStatus.value("conditions", nlohmann::json::array());
            if (conditionList.is_array()) {
                for (const auto& condition : conditionList) {
                    if (!condition.is_object()) continue;
                    if (conditions.size() >= maxDiagnosticConditionsPerPod) {
                        podDiagnosticsTruncated = true;
                        break;
                    }
                    nlohmann::json conditionSummary = nlohmann::json::object();
                    addBoundedText(conditionSummary, condition, "type", 64);
                    addBoundedText(conditionSummary, condition, "status", 16);
                    addBoundedText(conditionSummary, condition, "reason", 128);
                    addBoundedText(conditionSummary, condition, "message", 512);
                    addBoundedText(conditionSummary, condition, "lastTransitionTime", 64);
                    conditions.push_back(std::move(conditionSummary));
                }
                if (conditionList.size() > maxDiagnosticConditionsPerPod) {
                    podDiagnosticsTruncated = true;
                }
            }
            summary["conditions"] = std::move(conditions);

            const auto summariseContainerStatuses = [&](const char* fieldName) {
                nlohmann::json result = nlohmann::json::array();
                const auto statuses = podStatus.value(fieldName, nlohmann::json::array());
                if (!statuses.is_array()) return result;
                for (const auto& containerStatus : statuses) {
                    if (!containerStatus.is_object()) continue;
                    if (diagnosticContainerStatusCount >= maxDiagnosticContainerStatuses) {
                        podDiagnosticsTruncated = true;
                        break;
                    }
                    ++diagnosticContainerStatusCount;
                    nlohmann::json containerSummary = nlohmann::json::object();
                    addBoundedText(containerSummary, containerStatus, "name", 63);
                    const auto readyValue = containerStatus.find("ready");
                    containerSummary["ready"] = readyValue != containerStatus.end()
                            && readyValue->is_boolean() && readyValue->get<bool>();
                    const auto restartCount = containerStatus.find("restartCount");
                    containerSummary["restart_count"] = restartCount != containerStatus.end()
                            && restartCount->is_number_integer()
                            ? std::max<int64_t>(0, restartCount->get<int64_t>()) : 0;
                    const auto state = containerStatus.value("state", nlohmann::json::object());
                    if (state.is_object()) {
                        for (const char* stateName : {"waiting", "running", "terminated"}) {
                            const auto stateValue = state.find(stateName);
                            if (stateValue == state.end() || !stateValue->is_object()) continue;
                            containerSummary["state"] = stateName;
                            if (std::string(stateName) == "waiting"
                                    || std::string(stateName) == "terminated") {
                                addBoundedText(containerSummary, *stateValue, "reason", 128);
                                addBoundedText(containerSummary, *stateValue, "message", 512);
                            }
                            if (std::string(stateName) == "running") {
                                addBoundedText(containerSummary, *stateValue, "startedAt", 64);
                            }
                            if (std::string(stateName) == "terminated") {
                                if (stateValue->contains("exitCode") && (*stateValue)["exitCode"].is_number_integer()) {
                                    containerSummary["exit_code"] = (*stateValue)["exitCode"];
                                }
                                if (stateValue->contains("signal") && (*stateValue)["signal"].is_number_integer()) {
                                    containerSummary["signal"] = (*stateValue)["signal"];
                                }
                            }
                            break;
                        }
                    }
                    result.push_back(std::move(containerSummary));
                }
                return result;
            };
            summary["containers"] = summariseContainerStatuses("containerStatuses");
            summary["init_containers"] = summariseContainerStatuses("initContainerStatuses");
            podDiagnostics.push_back(std::move(summary));
        }
    }

    nlohmann::json containerImages = nlohmann::json::array();
    if (containers.is_array()) {
        for (const auto& item : containers) {
        if (!item.is_object() || !item.contains("name") || !item["name"].is_string()) continue;
        const std::string name = item["name"].get<std::string>();
        nlohmann::json containerReady = nullptr;
        if (podListComplete && nonTerminatingPodCount == 0 && spec.value("replicas", 1) > 0) {
            containerReady = false;
        } else if (podListComplete && nonTerminatingPodCount > 0) {
            containerReady = statusCountByName[name] == nonTerminatingPodCount
                    && readyByName[name];
        }
        containerImages.push_back({
                {"name", name},
                {"image", item.value("image", "")},
                {"ready", std::move(containerReady)}
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
            {"container_readiness_available", podListComplete},
            {"pod_diagnostics_available", podListComplete},
            {"pod_diagnostics_truncated", podDiagnosticsTruncated},
            {"pods", std::move(podDiagnostics)},
            {"continuum_configuration_enabled",
                    deploymentLabels.is_object()
                    && deploymentLabels.value("neuralmimicry.ai/continuum-configuration", "") == "enabled"},
            {"continuum_rollout_enabled",
                    deploymentLabels.is_object()
                    && deploymentLabels.value("neuralmimicry.ai/continuum-rollout", "") == "enabled"},
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

bool maxUnavailableWithin(const nlohmann::json& value, int desiredReplicas) {
    if (desiredReplicas < 1) return false;
    if (value.is_number_integer()) {
        const int parsed = value.get<int>();
        return parsed >= 0 && parsed < desiredReplicas;
    }
    if (!value.is_string()) return false;
    std::string text = value.get<std::string>();
    if (text.empty() || text.back() != '%') return false;
    text.pop_back();
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        })) return false;
    try {
        const int parsed = std::stoi(text);
        return parsed >= 0 && parsed <= 25;
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

nlohmann::json statefulSetStatusData(const nlohmann::json& statefulSet,
                                    const nlohmann::json* podList) {
    const auto metadataValue = statefulSet.value("metadata", nlohmann::json::object());
    const auto metadata = metadataValue.is_object() ? metadataValue : nlohmann::json::object();
    const auto specValue = statefulSet.value("spec", nlohmann::json::object());
    const auto spec = specValue.is_object() ? specValue : nlohmann::json::object();
    const auto statusValue = statefulSet.value("status", nlohmann::json::object());
    const auto status = statusValue.is_object() ? statusValue : nlohmann::json::object();
    const std::string statefulSetUid = metadata.value("uid", "");
    const int64_t generation = metadata.value("generation", int64_t{0});
    const int64_t observedGeneration = status.value("observedGeneration", int64_t{0});
    const int desiredReplicas = std::max(0, spec.value("replicas", 1));
    bool resourceVersionTruncated = false;
    bool currentRevisionTruncated = false;
    bool updateRevisionTruncated = false;
    const std::string resourceVersion = boundedTextValue(metadata, "resourceVersion", 128, resourceVersionTruncated);
    const std::string currentRevision = boundedTextValue(status, "currentRevision", 128, currentRevisionTruncated);
    const std::string updateRevision = boundedTextValue(status, "updateRevision", 128, updateRevisionTruncated);

    bool podListComplete = podList && podList->is_object()
            && podList->contains("items") && podList->at("items").is_array();
    bool podDiagnosticsTruncated = false;
    if (podListComplete) {
        const auto listMetadata = podList->value("metadata", nlohmann::json::object());
        if (!listMetadata.is_object()) {
            podListComplete = false;
        } else if (!listMetadata.value("continue", "").empty()) {
            podListComplete = false;
            podDiagnosticsTruncated = true;
        }
    }

    nlohmann::json pods = nlohmann::json::array();
    int readyPods = 0;
    if (podListComplete) {
        constexpr size_t maxDiagnosticPods = 64;
        constexpr size_t maxDiagnosticConditionsPerPod = 16;
        constexpr size_t maxDiagnosticContainerStatuses = 128;
        size_t containerStatusCount = 0;
        for (const auto& pod : podList->at("items")) {
            if (!pod.is_object()) {
                podListComplete = false;
                break;
            }
            const auto podMetadataValue = pod.value("metadata", nlohmann::json::object());
            const auto podMetadata = podMetadataValue.is_object()
                    ? podMetadataValue : nlohmann::json::object();
            const auto deletionTimestamp = podMetadata.value("deletionTimestamp", nlohmann::json());
            if (deletionTimestamp.is_string() && !deletionTimestamp.get<std::string>().empty()) continue;

            bool ownedByTarget = false;
            const auto owners = podMetadata.value("ownerReferences", nlohmann::json::array());
            if (owners.is_array() && !statefulSetUid.empty()) {
                for (const auto& owner : owners) {
                    if (owner.is_object() && owner.value("kind", "") == "StatefulSet"
                            && owner.value("uid", "") == statefulSetUid
                            && owner.value("controller", false)) {
                        ownedByTarget = true;
                        break;
                    }
                }
            }
            if (!ownedByTarget) continue;
            if (pods.size() >= maxDiagnosticPods) {
                podDiagnosticsTruncated = true;
                podListComplete = false;
                break;
            }

            const auto podStatusValue = pod.value("status", nlohmann::json::object());
            const auto podSpecValue = pod.value("spec", nlohmann::json::object());
            const auto podStatus = podStatusValue.is_object()
                    ? podStatusValue : nlohmann::json::object();
            const auto podSpec = podSpecValue.is_object()
                    ? podSpecValue : nlohmann::json::object();
            const bool ready = podReady(pod);
            if (ready) ++readyPods;

            nlohmann::json summary = nlohmann::json::object();
            addBoundedText(summary, podMetadata, "name", 63);
            addBoundedText(summary, podSpec, "nodeName", 253, "node");
            addBoundedText(summary, podStatus, "phase", 32);
            if (summary.value("phase", std::string{}).empty()) summary["phase"] = "Unknown";
            addBoundedText(summary, podStatus, "reason", 128);
            addBoundedText(summary, podStatus, "message", 512);
            summary["ready"] = ready;

            nlohmann::json conditions = nlohmann::json::array();
            const auto conditionList = podStatus.value("conditions", nlohmann::json::array());
            if (conditionList.is_array()) {
                for (const auto& condition : conditionList) {
                    if (!condition.is_object()) continue;
                    if (conditions.size() >= maxDiagnosticConditionsPerPod) {
                        podDiagnosticsTruncated = true;
                        break;
                    }
                    nlohmann::json conditionSummary = nlohmann::json::object();
                    addBoundedText(conditionSummary, condition, "type", 64);
                    addBoundedText(conditionSummary, condition, "status", 16);
                    addBoundedText(conditionSummary, condition, "reason", 128);
                    addBoundedText(conditionSummary, condition, "message", 512);
                    addBoundedText(conditionSummary, condition, "lastTransitionTime", 64);
                    conditions.push_back(std::move(conditionSummary));
                }
                if (conditionList.size() > maxDiagnosticConditionsPerPod) {
                    podDiagnosticsTruncated = true;
                }
            }
            summary["conditions"] = std::move(conditions);

            const auto summariseContainerStatuses = [&](const char* fieldName) {
                nlohmann::json summaries = nlohmann::json::array();
                const auto statuses = podStatus.value(fieldName, nlohmann::json::array());
                if (!statuses.is_array()) return summaries;
                for (const auto& containerStatus : statuses) {
                    if (!containerStatus.is_object()) continue;
                    if (containerStatusCount >= maxDiagnosticContainerStatuses) {
                        podDiagnosticsTruncated = true;
                        break;
                    }
                    ++containerStatusCount;
                    nlohmann::json containerSummary = nlohmann::json::object();
                    addBoundedText(containerSummary, containerStatus, "name", 63);
                    containerSummary["ready"] = containerStatus.value("ready", false);
                    const auto restartCount = containerStatus.find("restartCount");
                    containerSummary["restart_count"] = restartCount != containerStatus.end()
                            && restartCount->is_number_integer()
                            ? std::max<int64_t>(0, restartCount->get<int64_t>()) : 0;
                    const auto stateValue = containerStatus.value("state", nlohmann::json::object());
                    if (stateValue.is_object()) {
                        for (const char* stateName : {"waiting", "running", "terminated"}) {
                            const auto stateDetail = stateValue.find(stateName);
                            if (stateDetail == stateValue.end() || !stateDetail->is_object()) continue;
                            containerSummary["state"] = stateName;
                            if (std::string(stateName) == "waiting" || std::string(stateName) == "terminated") {
                                addBoundedText(containerSummary, *stateDetail, "reason", 128);
                                addBoundedText(containerSummary, *stateDetail, "message", 512);
                            }
                            if (std::string(stateName) == "running") {
                                addBoundedText(containerSummary, *stateDetail, "startedAt", 64);
                            }
                            break;
                        }
                    }
                    summaries.push_back(std::move(containerSummary));
                }
                return summaries;
            };
            summary["containers"] = summariseContainerStatuses("containerStatuses");
            summary["init_containers"] = summariseContainerStatuses("initContainerStatuses");
            pods.push_back(std::move(summary));
        }
    }

    const int readyReplicas = std::max(0, status.value("readyReplicas", 0));
    const int currentReplicas = std::max(0, status.value("currentReplicas", 0));
    const int updatedReplicas = std::max(0, status.value("updatedReplicas", 0));
    const bool statusObserved = generation > 0 && observedGeneration >= generation;
    const bool healthy = podListComplete && desiredReplicas > 0 && statusObserved
            && readyReplicas >= desiredReplicas && currentReplicas >= desiredReplicas
            && updatedReplicas >= desiredReplicas && readyPods >= desiredReplicas;

    return {
            {"kind", "StatefulSet"},
            {"namespace", metadata.value("namespace", "")},
            {"statefulset", metadata.value("name", "")},
            {"uid", statefulSetUid},
            {"resource_version", resourceVersion},
            {"resource_version_truncated", resourceVersionTruncated},
            {"generation", generation},
            {"observed_generation", observedGeneration},
            {"status_observed", statusObserved},
            {"desired_replicas", desiredReplicas},
            {"current_replicas", currentReplicas},
            {"updated_replicas", updatedReplicas},
            {"ready_replicas", readyReplicas},
            {"current_revision", currentRevision},
            {"current_revision_truncated", currentRevisionTruncated},
            {"update_revision", updateRevision},
            {"update_revision_truncated", updateRevisionTruncated},
            {"healthy", healthy},
            {"pod_diagnostics_available", podListComplete},
            {"pod_diagnostics_truncated", podDiagnosticsTruncated},
            {"pods", std::move(pods)}
    };
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
            {"degraded_pods", nlohmann::json::array()},
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
    result["readiness_degraded"] = status.value("readyReplicas", 0) < desired
            || status.value("availableReplicas", 0) < desired
            || status.value("unavailableReplicas", 0) > 0;

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
            && !maxUnavailableWithin(rollingUpdate["maxUnavailable"], desired)) {
        addBlocker(blockers, "unsafe_max_unavailable", "Deployment maxUnavailable exceeds the recovery safety limit.");
    }
    if (rollingUpdate.contains("maxSurge")
            && !rolloutSettingWithin(rollingUpdate["maxSurge"], 100, false)) {
        addBlocker(blockers, "unsafe_max_surge", "Deployment maxSurge must remain positive for a safe restart.");
    }
    if (generation > 0 && observedGeneration < generation) {
        addBlocker(blockers, "generation_unobserved", "Deployment controller has not observed the current spec.");
    }
    // A rollout that has not yet placed the current template everywhere is
    // ambiguous and must finish before another restart. Once every desired
    // replica runs the observed template, failed readiness is a recovery
    // signal rather than a blanket blocker: a surge-first restart can replace
    // the unhealthy Pods without deliberately taking healthy replicas down.
    if (status.value("updatedReplicas", 0) < desired) {
        addBlocker(blockers, "rollout_incomplete", "The current Deployment template is not running on all desired replicas.");
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
            const std::string reason = condition.value("reason", "unknown");
            const bool stableDegradedAvailability = type == "Available"
                    && conditionStatus == "False"
                    && reason == "MinimumReplicasUnavailable"
                    && observedGeneration >= generation
                    && status.value("updatedReplicas", 0) >= desired;
            const bool recoverableProgressDeadline = type == "Progressing"
                    && conditionStatus == "False"
                    && reason == "ProgressDeadlineExceeded"
                    && observedGeneration >= generation
                    && status.value("updatedReplicas", 0) >= desired;
            if ((type == "Available" && conditionStatus != "True" && !stableDegradedAvailability)
                    || (type == "Progressing" && conditionStatus != "True" && !recoverableProgressDeadline)) {
                addBlocker(blockers, "deployment_condition", type + " is " + conditionStatus
                        + " (" + reason + ").");
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
            if (phase != "Running") {
                addBlocker(blockers, "pod_not_running", "Active Deployment Pod " + podName + " is not Running.");
            } else if (!ready) {
                // Readiness failure is the condition the explicitly opted-in
                // recovery action is intended to address. Placement,
                // termination and host checks below remain fail-closed.
                result["degraded_pods"].push_back(podName);
            }
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
    std::lock_guard<std::recursive_mutex> apiClientLock(apiClientMutex);
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
    response.message = error.empty() ? "Deployment recovery preflight completed; readiness degradation is reported separately."
                                     : "Recovery preflight could not establish safe live state.";
    response.data = std::move(state);
    sendJsonResponse(res, response);
}

void K8sHandlers::handleRestartDeployment(const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::recursive_mutex> apiClientLock(apiClientMutex);
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
            && !maxUnavailableWithin(rollingUpdate["maxUnavailable"], desired)) {
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
                    {"available_replicas", available},
                    {"readiness_degraded", ready < desired || available < desired
                            || status.value("unavailableReplicas", 0) > 0}
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
                {"readiness_degraded", ready < desired || available < desired
                        || status.value("unavailableReplicas", 0) > 0},
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
    std::lock_guard<std::recursive_mutex> apiClientLock(apiClientMutex);
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

    std::lock_guard<std::recursive_mutex> clientLock(workloadLogsClientMutex);
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

    std::optional<nlohmann::json> podList;
    const auto labelSelector = deploymentLabelSelector(deployment);
    if (labelSelector) {
        constexpr int maxStatusPods = 64;
        long podListStatus = 0;
        podList = listWorkloadPods(namespaceName, *labelSelector, maxStatusPods, podListStatus);
    }

    Models::CloudResponse response;
    response.success = true;
    response.message = "Continuum Deployment status retrieved.";
    response.data = deploymentStatusData(deployment, podList ? &*podList : nullptr);
    response.data["cluster_id"] = clusterId;
    sendJsonResponse(res, response);
}

void K8sHandlers::handleGetDeploymentEvents(const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::recursive_mutex> apiClientLock(apiClientMutex);
    try {
        const std::string clusterId = trimCopy(req.get_param_value("cluster_id"));
        const std::string namespaceName = trimCopy(req.get_param_value("namespace"));
        const std::string deploymentName = trimCopy(req.get_param_value("deployment"));
        if (!validDnsLabel(clusterId) || !validDnsLabel(namespaceName) || !validDnsLabel(deploymentName)) {
            return sendErrorResponse(res, 400,
                                     "cluster_id, namespace and deployment must be valid Kubernetes DNS labels.");
        }
        if (activeRecoveryClusterId.empty() || clusterId != activeRecoveryClusterId) {
            return sendErrorResponse(res, 409,
                                     "Requested cluster does not match Continuum's active Kubernetes context.");
        }
        int maxEvents = 20;
        if (req.has_param("max_events")) {
            const std::string raw = trimCopy(req.get_param_value("max_events"));
            try {
                size_t consumed = 0;
                maxEvents = std::stoi(raw, &consumed);
                if (consumed != raw.size())
                    throw std::invalid_argument("max_events");
            } catch (const std::exception &) {
                return sendErrorResponse(res, 400, "max_events must be an integer between 1 and 100.");
            }
        }
        if (maxEvents < 1 || maxEvents > 100) {
            return sendErrorResponse(res, 400, "max_events must be between 1 and 100.");
        }
        if (!recoveryClientReady || !workloadLogsApiClient) {
            return sendErrorResponse(res, 503, "Deployment Events require the configured authenticated kubeconfig.");
        }

        std::lock_guard<std::recursive_mutex> clientLock(workloadLogsClientMutex);
        char group[] = "apps";
        char version[] = "v1";
        char plural[] = "deployments";
        genericClient_t *deploymentClient = genericClient_create(workloadLogsApiClient, group, version, plural);
        if (!deploymentClient) {
            return sendErrorResponse(res, 503, "Continuum could not initialise its Kubernetes Deployment reader.");
        }
        char *rawDeployment =
                Generic_readNamespacedResource(deploymentClient, const_cast<char *>(namespaceName.c_str()),
                                               const_cast<char *>(deploymentName.c_str()));
        const int readStatus = workloadLogsApiClient->response_code;
        genericClient_free(deploymentClient);
        if (!rawDeployment) {
            return sendErrorResponse(res, readStatus == 404 ? 404 : 502,
                                     readStatus == 404 ? "Deployment was not found in the requested namespace."
                                                       : "Continuum could not read the requested Deployment.");
        }

        nlohmann::json deployment;
        try {
            deployment = nlohmann::json::parse(rawDeployment);
        } catch (const std::exception &) {
            free(rawDeployment);
            return sendErrorResponse(res, 502, "Kubernetes returned invalid Deployment JSON.");
        }
        free(rawDeployment);
        if (!deployment.is_object()) {
            return sendErrorResponse(res, 502, "Kubernetes returned an invalid Deployment object.");
        }
        const auto labelSelector = deploymentLabelSelector(deployment);
        if (!labelSelector) {
            return sendErrorResponse(
                    res, 422,
                    "Deployment requires a non-empty matchLabels selector; selector expressions are not supported.");
        }

        constexpr int maxPods = 20;
        constexpr int maxEventsPerPod = 10;
        constexpr size_t maxSerializedResponseBytes = 512 * 1024;
        long podListStatus = 0;
        auto podList = listWorkloadPods(namespaceName, *labelSelector, maxPods, podListStatus);
        if (!podList) {
            return sendErrorResponse(
                    res, podListStatus >= 400 && podListStatus <= 599 ? static_cast<int>(podListStatus) : 502,
                    "Continuum could not retrieve a bounded Pod list for the Deployment.");
        }

        bool truncated = false;
        if (podList->contains("metadata") && (*podList)["metadata"].is_object() &&
            (*podList)["metadata"].value("continue", "") != "") {
            truncated = true;
        }
        nlohmann::json podStatuses = nlohmann::json::array();
        std::vector<nlohmann::json> events;
        int errorCount = 0;
        int podsConsidered = 0;
        for (const auto &pod : (*podList)["items"]) {
            if (!pod.is_object())
                continue;
            if (podsConsidered >= maxPods) {
                truncated = true;
                break;
            }
            const auto *metadata = objectField(pod, "metadata");
            const std::string podName = metadata ? stringField(*metadata, "name") : "";
            if (!validDnsLabel(podName))
                continue;
            ++podsConsidered;
            const auto *spec = objectField(pod, "spec");
            const auto *status = objectField(pod, "status");
            const std::string podUid = metadata ? stringField(*metadata, "uid") : "";
            const std::string nodeName = spec ? stringField(*spec, "nodeName") : "";
            std::string phase = status ? stringField(*status, "phase") : "";
            if (phase.empty())
                phase = "Unknown";
            nlohmann::json podSummary = {{"name", podName},
                                         {"node", nodeName},
                                         {"phase", phase},
                                         {"event_status", "unavailable"},
                                         {"event_count", 0}};
            if (!validPodUid(podUid)) {
                ++errorCount;
                podSummary["status_code"] = 502;
                podStatuses.push_back(std::move(podSummary));
                continue;
            }

            long eventStatus = 0;
            bool eventListTruncated = false;
            auto eventList = listBoundedPodEvents(workloadLogsApiClient, namespaceName, podUid, maxEventsPerPod,
                                                  eventStatus, eventListTruncated);
            if (!eventList) {
                ++errorCount;
                podSummary["status_code"] = eventStatus >= 400 && eventStatus <= 599 ? eventStatus : 502;
                truncated = truncated || eventListTruncated;
                podStatuses.push_back(std::move(podSummary));
                continue;
            }

            podSummary["event_status"] = "ok";
            const auto eventItems = eventList->value("items", nlohmann::json::array());
            int matchedEvents = 0;
            for (const auto &event : eventItems) {
                if (!event.is_object())
                    continue;
                const auto *involvedObject = objectField(event, "involvedObject");
                if (!involvedObject || stringField(*involvedObject, "uid") != podUid)
                    continue;

                const auto *eventMetadata = objectField(event, "metadata");
                const auto *series = objectField(event, "series");
                const auto *source = objectField(event, "source");
                std::string lastSeen = stringField(event, "eventTime");
                if (lastSeen.empty() && series)
                    lastSeen = stringField(*series, "lastObservedTime");
                if (lastSeen.empty())
                    lastSeen = stringField(event, "lastTimestamp");
                if (lastSeen.empty() && eventMetadata)
                    lastSeen = stringField(*eventMetadata, "creationTimestamp");
                std::string firstSeen = stringField(event, "firstTimestamp");
                if (firstSeen.empty())
                    firstSeen = stringField(event, "eventTime");
                if (lastSeen.size() > 64)
                    lastSeen.resize(64);
                if (firstSeen.size() > 64)
                    firstSeen.resize(64);

                int eventCount = 1;
                const auto count = event.find("count");
                if (count != event.end() && count->is_number_integer()) {
                    const int64_t suppliedCount = count->get<int64_t>();
                    if (suppliedCount > 0 && suppliedCount <= 1000000) {
                        eventCount = static_cast<int>(suppliedCount);
                    }
                }

                nlohmann::json normalized = {{"pod", podName},
                                             {"node", nodeName},
                                             {"last_seen", lastSeen},
                                             {"first_seen", firstSeen},
                                             {"count", eventCount}};
                if (eventMetadata)
                    addBoundedText(normalized, *eventMetadata, "name", 253);
                addBoundedText(normalized, event, "type", 32);
                addBoundedText(normalized, event, "reason", 128);
                if (event.contains("message")) {
                    addBoundedText(normalized, event, "message", 1024);
                } else {
                    addBoundedText(normalized, event, "note", 1024, "message");
                }
                if (event.contains("reportingComponent")) {
                    addBoundedText(normalized, event, "reportingComponent", 128, "reporting_component");
                } else if (source) {
                    addBoundedText(normalized, *source, "component", 128, "reporting_component");
                }
                events.push_back(std::move(normalized));
                ++matchedEvents;
            }
            podSummary["event_count"] = matchedEvents;
            if (matchedEvents == 0)
                podSummary["event_status"] = "empty";
            podStatuses.push_back(std::move(podSummary));
            truncated = truncated || eventListTruncated;
        }

        std::sort(events.begin(), events.end(), [](const nlohmann::json &left, const nlohmann::json &right) {
            return left.value("last_seen", "") > right.value("last_seen", "");
        });
        if (events.size() > static_cast<size_t>(maxEvents)) {
            events.resize(static_cast<size_t>(maxEvents));
            truncated = true;
        }
        const size_t eventCount = events.size();

        Models::CloudResponse response;
        response.success = true;
        response.message = "Bounded Deployment Pod Events retrieved.";
        response.data = {
                {"cluster_id", clusterId},       {"namespace", namespaceName},        {"deployment", deploymentName},
                {"max_events", maxEvents},       {"pods_considered", podsConsidered}, {"events", std::move(events)},
                {"event_count", eventCount},     {"error_count", errorCount},         {"truncated", truncated},
                {"pods", std::move(podStatuses)}};
        if (response.toJsonString().dump(4).size() > maxSerializedResponseBytes) {
            return sendErrorResponse(res, 502, "Bounded Deployment Events response exceeded its size limit.");
        }
        sendJsonResponse(res, response);
    } catch (const std::exception &) {
        sendErrorResponse(res, 500, "Deployment Event diagnostics failed safely.");
    }
}

void K8sHandlers::handleGetStatefulSetStatus(const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::recursive_mutex> apiClientLock(apiClientMutex);
    const std::string clusterId = req.get_param_value("cluster_id");
    const std::string namespaceName = req.get_param_value("namespace");
    const std::string statefulSetName = req.get_param_value("statefulset");
    if (!validDnsLabel(clusterId) || !validDnsLabel(namespaceName) || !validDnsLabel(statefulSetName)) {
        return sendErrorResponse(res, 400, "cluster_id, namespace and StatefulSet must be valid Kubernetes DNS labels.");
    }
    if (activeRecoveryClusterId.empty() || clusterId != activeRecoveryClusterId) {
        return sendErrorResponse(res, 409, "Requested cluster does not match Continuum's active Kubernetes context.");
    }
    if (!recoveryClientReady || !workloadLogsApiClient) {
        return sendErrorResponse(res, 503, "StatefulSet status requires the configured authenticated kubeconfig.");
    }

    std::lock_guard<std::recursive_mutex> clientLock(workloadLogsClientMutex);
    char group[] = "apps";
    char version[] = "v1";
    char plural[] = "statefulsets";
    genericClient_t* statefulSetClient = genericClient_create(
            workloadLogsApiClient,
            group,
            version,
            plural
    );
    if (!statefulSetClient) {
        return sendErrorResponse(res, 503, "Continuum could not initialise its Kubernetes StatefulSet reader.");
    }
    char* rawStatefulSet = Generic_readNamespacedResource(
            statefulSetClient,
            const_cast<char*>(namespaceName.c_str()),
            const_cast<char*>(statefulSetName.c_str())
    );
    const int readStatus = workloadLogsApiClient->response_code;
    genericClient_free(statefulSetClient);
    if (!rawStatefulSet) {
        return sendErrorResponse(res, readStatus == 404 ? 404 : 502,
                readStatus == 404 ? "StatefulSet was not found in the requested namespace."
                                  : "Continuum could not read StatefulSet status from Kubernetes.");
    }

    const std::string responseBody(rawStatefulSet);
    free(rawStatefulSet);
    if (responseBody.size() > 2 * 1024 * 1024) {
        return sendErrorResponse(res, 502, "Kubernetes returned oversized StatefulSet status JSON.");
    }

    nlohmann::json statefulSet;
    try {
        statefulSet = nlohmann::json::parse(responseBody);
    } catch (const std::exception&) {
        return sendErrorResponse(res, 502, "Kubernetes returned invalid StatefulSet status JSON.");
    }
    try {
        if (!statefulSet.is_object() || statefulSet.value("kind", "") != "StatefulSet") {
            return sendErrorResponse(res, 502, "Kubernetes returned an invalid StatefulSet object.");
        }
        const auto metadata = statefulSet.value("metadata", nlohmann::json::object());
        if (!metadata.is_object() || metadata.value("name", "") != statefulSetName
                || metadata.value("namespace", "") != namespaceName) {
            return sendErrorResponse(res, 502, "Kubernetes returned a StatefulSet with mismatched identity.");
        }
        const auto uid = metadata.value("uid", std::string{});
        const auto resourceVersion = metadata.value("resourceVersion", std::string{});
        if (uid.empty() || uid.size() > 128 || resourceVersion.empty() || resourceVersion.size() > 128) {
            return sendErrorResponse(res, 502, "Kubernetes returned a StatefulSet without bounded live identity.");
        }
    } catch (const std::exception&) {
        return sendErrorResponse(res, 502, "Kubernetes returned malformed StatefulSet identity data.");
    }

    Models::CloudResponse response;
    response.success = true;
    response.message = "Continuum StatefulSet status retrieved.";
    try {
        std::optional<nlohmann::json> podList;
        const auto labelSelector = deploymentLabelSelector(statefulSet);
        if (labelSelector) {
            constexpr int maxStatusPods = 64;
            long podListStatus = 0;
            podList = listWorkloadPods(namespaceName, *labelSelector, maxStatusPods, podListStatus);
        }
        response.data = statefulSetStatusData(statefulSet, podList ? &*podList : nullptr);
    } catch (const std::exception&) {
        return sendErrorResponse(res, 502, "Kubernetes returned malformed StatefulSet readiness data.");
    }
    response.data["cluster_id"] = clusterId;
    sendJsonResponse(res, response);
}

void K8sHandlers::handleRolloutDeploymentImage(const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::recursive_mutex> apiClientLock(apiClientMutex);
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
        std::lock_guard<std::recursive_mutex> clientLock(workloadLogsClientMutex);
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
  std::lock_guard<std::recursive_mutex> apiClientLock(apiClientMutex);
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
  std::lock_guard<std::recursive_mutex> clientLock(workloadLogsClientMutex);
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
