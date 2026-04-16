// server/APIRoutes.cpp
#include "APIRoutes.h"
#include "K8sHandlers.h"
#include "ConnectionHandlers.h"
#include "VersionCheck.h"
#include "Utils.h"
#include <cstdlib>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <array>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <algorithm> // For std::remove_if, std::find_if
#include <map>
#include <set>       // For unique locations/types
#include <chrono>
#include <thread>
#include <vector>
#include <poll.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <unistd.h>


namespace NMC::Server {

    namespace {
        std::string trim(const std::string& value) {
            const auto start = value.find_first_not_of(" \t");
            if (start == std::string::npos) {
                return "";
            }
            const auto end = value.find_last_not_of(" \t");
            return value.substr(start, end - start + 1);
        }

        std::string trimLineEnd(std::string value) {
            while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
                value.pop_back();
            }
            return value;
        }

        std::string toLower(std::string value) {
            for (auto& ch : value) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return value;
        }

        std::string normalizeOpenShiftStatus(const std::string& rawStatus) {
            const std::string status = toLower(rawStatus);
            if (status == "ready" || status == "running" || status == "active" || status == "available") {
                return "Ready";
            }
            if (status == "failed" || status == "error" || status == "unhealthy" || status == "deleted" || status == "terminated") {
                return "Failed";
            }
            if (status == "pending" || status == "accepted" || status == "queued" || status == "requested") {
                return "Pending";
            }
            if (status == "provisioning" || status == "gitops-syncing" || status == "gitops_syncing"
                || status == "syncing" || status == "installing" || status == "creating"
                || status == "build" || status == "rebuild" || status == "spawning") {
                return "Provisioning";
            }
            return "Unknown";
        }

        std::string normalizeTraceyStatus(const std::string& rawStatus) {
            const std::string status = toLower(rawStatus);
            if (status == "ok" || status == "ready" || status == "running" || status == "healthy") {
                return "healthy";
            }
            if (status == "degraded" || status == "warning" || status == "warn") {
                return "degraded";
            }
            if (status == "offline" || status == "down" || status == "failed" || status == "error") {
                return "offline";
            }
            if (status.empty()) {
                return "unknown";
            }
            return status;
        }

        int64_t nowEpochMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
            ).count();
        }

        constexpr double SAME_HARDWARE_MAX_LOAD_PER_CPU = 0.85;
        constexpr double SAME_HARDWARE_MAX_MEMORY_USED_RATIO = 0.85;
        constexpr int64_t SAME_HARDWARE_TRACEY_FRESHNESS_FLOOR_MS = 30'000;

        struct LocalHostIdentity {
            std::set<std::string> aliases;
            std::set<std::string> addresses;
        };

        struct HostMatchResult {
            bool sameHardware{false};
            nlohmann::json details;
        };

        struct SystemCapacitySnapshot {
            unsigned int cpuCores{0};
            double load1{0.0};
            double loadPerCpu{0.0};
            uint64_t totalMemoryBytes{0};
            uint64_t availableMemoryBytes{0};
            double memoryUsedRatio{0.0};
            bool loadAvailable{false};
            bool memoryAvailable{false};
        };

        void normalizeClusterStatus(nlohmann::json& cluster) {
            if (cluster.is_object() && cluster.contains("status") && cluster["status"].is_string()) {
                cluster["status"] = normalizeOpenShiftStatus(cluster["status"].get<std::string>());
            }
        }

        void addUniqueString(std::vector<std::string>& values, const std::string& value) {
            const std::string trimmed = trim(value);
            if (trimmed.empty()) {
                return;
            }
            if (std::find(values.begin(), values.end(), trimmed) == values.end()) {
                values.push_back(trimmed);
            }
        }

        void addUniqueInt(std::vector<int>& values, int value) {
            if (value <= 0) {
                return;
            }
            if (std::find(values.begin(), values.end(), value) == values.end()) {
                values.push_back(value);
            }
        }

        std::string firstStringValue(const nlohmann::json& objectNode,
                                     std::initializer_list<const char*> keys,
                                     const std::string& fallback = "") {
            if (!objectNode.is_object()) {
                return fallback;
            }
            for (const char* key : keys) {
                const auto it = objectNode.find(key);
                if (it == objectNode.end() || !it->is_string()) {
                    continue;
                }
                const std::string value = trim(it->get<std::string>());
                if (!value.empty()) {
                    return value;
                }
            }
            return fallback;
        }

        int firstPositiveIntValue(const nlohmann::json& objectNode, std::initializer_list<const char*> keys) {
            if (!objectNode.is_object()) {
                return -1;
            }
            for (const char* key : keys) {
                const auto it = objectNode.find(key);
                if (it == objectNode.end()) {
                    continue;
                }
                if (it->is_number_integer()) {
                    const int value = it->get<int>();
                    if (value > 0) {
                        return value;
                    }
                    continue;
                }
                if (it->is_number_unsigned()) {
                    const int value = static_cast<int>(it->get<unsigned int>());
                    if (value > 0) {
                        return value;
                    }
                }
            }
            return -1;
        }

        int64_t firstInt64Value(const nlohmann::json& objectNode,
                                std::initializer_list<const char*> keys,
                                int64_t fallback) {
            if (!objectNode.is_object()) {
                return fallback;
            }
            for (const char* key : keys) {
                const auto it = objectNode.find(key);
                if (it == objectNode.end() || it->is_null()) {
                    continue;
                }
                if (it->is_number_integer()) {
                    return it->get<int64_t>();
                }
                if (it->is_number_unsigned()) {
                    return static_cast<int64_t>(it->get<uint64_t>());
                }
                if (it->is_string()) {
                    const std::string value = trim(it->get<std::string>());
                    if (value.empty()) {
                        continue;
                    }
                    try {
                        return std::stoll(value);
                    } catch (const std::exception&) {
                        continue;
                    }
                }
            }
            return fallback;
        }

        const nlohmann::json* firstValue(const nlohmann::json& objectNode,
                                         std::initializer_list<const char*> keys) {
            if (!objectNode.is_object()) {
                return nullptr;
            }
            for (const char* key : keys) {
                const auto it = objectNode.find(key);
                if (it != objectNode.end() && !it->is_null()) {
                    return &(*it);
                }
            }
            return nullptr;
        }

        const nlohmann::json* firstObjectValue(const nlohmann::json& objectNode,
                                               std::initializer_list<const char*> keys) {
            const auto* value = firstValue(objectNode, keys);
            return value != nullptr && value->is_object() ? value : nullptr;
        }

        const nlohmann::json* firstArrayValue(const nlohmann::json& objectNode,
                                              std::initializer_list<const char*> keys) {
            const auto* value = firstValue(objectNode, keys);
            return value != nullptr && value->is_array() ? value : nullptr;
        }

        double jsonDoubleValue(const nlohmann::json& node, double fallback) {
            if (node.is_number_float()) {
                return node.get<double>();
            }
            if (node.is_number_integer()) {
                return static_cast<double>(node.get<int64_t>());
            }
            if (node.is_number_unsigned()) {
                return static_cast<double>(node.get<uint64_t>());
            }
            if (node.is_string()) {
                const std::string value = trim(node.get<std::string>());
                if (value.empty()) {
                    return fallback;
                }
                try {
                    return std::stod(value);
                } catch (const std::exception&) {
                    return fallback;
                }
            }
            if (node.is_boolean()) {
                return node.get<bool>() ? 1.0 : 0.0;
            }
            return fallback;
        }

        bool jsonBoolValue(const nlohmann::json& node, bool fallback) {
            if (node.is_boolean()) {
                return node.get<bool>();
            }
            if (node.is_number_integer()) {
                return node.get<int64_t>() != 0;
            }
            if (node.is_number_unsigned()) {
                return node.get<uint64_t>() != 0;
            }
            if (node.is_string()) {
                const std::string value = toLower(trim(node.get<std::string>()));
                if (value == "true" || value == "yes" || value == "1" || value == "on") {
                    return true;
                }
                if (value == "false" || value == "no" || value == "0" || value == "off") {
                    return false;
                }
            }
            return fallback;
        }

        double firstDoubleValue(const nlohmann::json& objectNode,
                                std::initializer_list<const char*> keys,
                                double fallback) {
            const auto* value = firstValue(objectNode, keys);
            return value != nullptr ? jsonDoubleValue(*value, fallback) : fallback;
        }

        nlohmann::json extractTraceyLoaderThreatSummary(const nlohmann::json& statusSnapshot) {
            if (!statusSnapshot.is_object()) {
                return nlohmann::json::object();
            }

            const nlohmann::json* loaderThreatNode = firstObjectValue(
                    statusSnapshot,
                    {"loader_threats", "loaderThreats", "loader_security", "loaderSecurity"}
            );
            if (loaderThreatNode == nullptr) {
                const auto loaderIt = statusSnapshot.find("loader");
                if (loaderIt != statusSnapshot.end() && loaderIt->is_object()) {
                    loaderThreatNode = firstObjectValue(
                            *loaderIt,
                            {"threats", "security", "loader_threats", "loaderThreats"}
                    );
                }
            }
            if (loaderThreatNode == nullptr) {
                return nlohmann::json::object();
            }

            const nlohmann::json* summaryNode = firstObjectValue(
                    *loaderThreatNode,
                    {"summary", "totals", "counts"}
            );
            const nlohmann::json& source = summaryNode != nullptr ? *summaryNode : *loaderThreatNode;

            const auto readInt = [&](std::initializer_list<const char*> keys) -> int64_t {
                return std::max<int64_t>(0, firstInt64Value(source, keys, 0));
            };
            const auto readDouble = [&](std::initializer_list<const char*> keys) -> double {
                const auto* value = firstValue(source, keys);
                return value != nullptr ? std::max(0.0, jsonDoubleValue(*value, 0.0)) : 0.0;
            };

            return {
                    {"local_provider_count", readInt({"local_provider_count", "localProviderCount", "providers", "provider_count"})},
                    {"local_artifact_count", readInt({"local_artifact_count", "localArtifactCount", "artifacts", "artifact_count"})},
                    {"remote_provider_count", readInt({"remote_provider_count", "remoteProviderCount"})},
                    {"remote_artifact_count", readInt({"remote_artifact_count", "remoteArtifactCount"})},
                    {"remote_reporters", readInt({"remote_reporters", "remoteReporters", "reporters"})},
                    {"blocked_provider_count", readInt({"blocked_provider_count", "blockedProviderCount", "blocked_providers", "recommended_provider_blocks"})},
                    {"blocked_artifact_count", readInt({"blocked_artifact_count", "blockedArtifactCount", "blocked_artifacts", "recommended_artifact_blocks"})},
                    {"highest_provider_risk", readDouble({"highest_provider_risk", "highestProviderRisk", "provider_risk"})},
                    {"highest_artifact_risk", readDouble({"highest_artifact_risk", "highestArtifactRisk", "artifact_risk"})}
            };
        }

        const nlohmann::json* extractTraceyStatusSnapshotNode(const nlohmann::json& metricsNode) {
            return firstObjectValue(metricsNode, {"status_snapshot", "statusSnapshot"});
        }

        const nlohmann::json* extractContinuumTelemetryNode(const nlohmann::json& metricsNode) {
            const auto* statusSnapshot = extractTraceyStatusSnapshotNode(metricsNode);
            if (statusSnapshot == nullptr) {
                return nullptr;
            }
            return firstObjectValue(*statusSnapshot, {"continuum_telemetry", "continuumTelemetry", "continuum"});
        }

        const nlohmann::json* extractTraceyGuardNode(const nlohmann::json& metricsNode) {
            const auto* statusSnapshot = extractTraceyStatusSnapshotNode(metricsNode);
            if (statusSnapshot == nullptr) {
                return nullptr;
            }
            return firstObjectValue(*statusSnapshot, {"tracey_guard", "traceyGuard"});
        }

        const nlohmann::json* extractTraceyGuardSummaryNode(const nlohmann::json& metricsNode) {
            const auto* traceyGuard = extractTraceyGuardNode(metricsNode);
            if (traceyGuard == nullptr) {
                return nullptr;
            }
            return firstObjectValue(*traceyGuard, {"summary"});
        }

        const nlohmann::json* extractContinuumAssessmentNode(const nlohmann::json& metricsNode) {
            const auto* statusSnapshot = extractTraceyStatusSnapshotNode(metricsNode);
            if (statusSnapshot == nullptr) {
                return nullptr;
            }
            return firstObjectValue(
                    *statusSnapshot,
                    {"continuum_assessment", "continuumAssessment", "compromise_assessment", "compromiseAssessment"}
            );
        }

        const nlohmann::json* extractContinuumAssessmentSummaryNode(const nlohmann::json& metricsNode) {
            const auto* assessment = extractContinuumAssessmentNode(metricsNode);
            if (assessment == nullptr) {
                return nullptr;
            }
            const auto* summary = firstObjectValue(*assessment, {"summary"});
            return summary != nullptr ? summary : assessment;
        }

        std::string defaultTopologyLabel(const std::string& value, const std::string& fallback) {
            const std::string cleaned = trim(value);
            return cleaned.empty() ? fallback : cleaned;
        }

        int traceyStatusSeverityRank(const std::string& status) {
            const std::string normalized = normalizeTraceyStatus(status);
            if (normalized == "offline") {
                return 3;
            }
            if (normalized == "degraded") {
                return 2;
            }
            if (normalized == "unknown") {
                return 1;
            }
            return 0;
        }

        std::string rollupTraceyStatusCounts(int healthy, int degraded, int offline, int unknown) {
            if (offline > 0) {
                return "offline";
            }
            if (degraded > 0) {
                return "degraded";
            }
            if (healthy > 0 && unknown == 0) {
                return "healthy";
            }
            if (healthy > 0) {
                return "degraded";
            }
            return "unknown";
        }

        void sortGpuTiles(nlohmann::json& gpus) {
            if (!gpus.is_array()) {
                return;
            }
            std::sort(gpus.begin(), gpus.end(), [](const nlohmann::json& left, const nlohmann::json& right) {
                const int64_t leftSlot = firstInt64Value(left, {"slot_index", "slotIndex"}, 1'000'000);
                const int64_t rightSlot = firstInt64Value(right, {"slot_index", "slotIndex"}, 1'000'000);
                if (leftSlot != rightSlot) {
                    return leftSlot < rightSlot;
                }
                const std::string leftId = firstStringValue(left, {"gpu_id", "gpuId"});
                const std::string rightId = firstStringValue(right, {"gpu_id", "gpuId"});
                return leftId < rightId;
            });
        }

        void sortActionsByTime(nlohmann::json& actions) {
            if (!actions.is_array()) {
                return;
            }
            std::sort(actions.begin(), actions.end(), [](const nlohmann::json& left, const nlohmann::json& right) {
                return firstInt64Value(left, {"ts_ms", "tsMs"}, 0) > firstInt64Value(right, {"ts_ms", "tsMs"}, 0);
            });
        }

        nlohmann::json limitedJsonArray(nlohmann::json values, std::size_t limit) {
            if (!values.is_array()) {
                return nlohmann::json::array();
            }
            if (values.size() > limit) {
                values.erase(values.begin() + static_cast<nlohmann::json::difference_type>(limit), values.end());
            }
            return values;
        }

        struct TraceyEffectiveStatus {
            std::string status;
            bool stale{false};
            int64_t lastSignalMs{0};
            int64_t ageSeconds{0};
        };

        TraceyEffectiveStatus buildTraceyEffectiveStatus(const std::string& rawStatus,
                                                        bool staleFlag,
                                                        int64_t lastSeenEpochMs,
                                                        int64_t lastAnnouncementEpochMs,
                                                        int64_t nowMs,
                                                        int64_t staleAfterSeconds) {
            TraceyEffectiveStatus view;
            view.lastSignalMs = std::max(lastSeenEpochMs, lastAnnouncementEpochMs);
            if (view.lastSignalMs > 0 && nowMs > view.lastSignalMs) {
                view.ageSeconds = (nowMs - view.lastSignalMs) / 1000;
            }
            view.stale = staleFlag || view.ageSeconds > staleAfterSeconds;
            view.status = view.stale ? "offline" : normalizeTraceyStatus(rawStatus);
            return view;
        }

        nlohmann::json buildTraceyFleetViewFromAgents(const std::vector<nlohmann::json>& agentViews,
                                                      int64_t nowMs) {
            struct RackAgg {
                std::string rack;
                std::string zone;
                int agentCount{0};
                int healthy{0};
                int degraded{0};
                int offline{0};
                int unknown{0};
                int reachable{0};
                int gpuCount{0};
                int traceyGuardQuarantined{0};
                int blockedLoaderProviders{0};
                int blockedLoaderArtifacts{0};
                int thermalAlerts{0};
                int fanAlerts{0};
                int autonomyActions{0};
                double gpuUtilSum{0.0};
                int gpuUtilCount{0};
                double gpuPowerTotal{0.0};
                double netRxBps{0.0};
                double netTxBps{0.0};
                double autonomyRiskMax{0.0};
                bool hasAutonomyRisk{false};
                double gpuTempMax{0.0};
                bool hasGpuTempMax{false};
                uint64_t eccCorrectedTotal{0};
                uint64_t eccUncorrectedTotal{0};
                nlohmann::json heatCells{nlohmann::json::array()};
            };

            struct ZoneAgg {
                std::string zone;
                int agentCount{0};
                int healthy{0};
                int degraded{0};
                int offline{0};
                int unknown{0};
                int reachable{0};
                int gpuCount{0};
                int rackCount{0};
                int thermalAlerts{0};
                int fanAlerts{0};
                int autonomyActions{0};
                int traceyGuardQuarantined{0};
                double gpuUtilSum{0.0};
                int gpuUtilCount{0};
                double gpuPowerTotal{0.0};
                double gpuTempMax{0.0};
                bool hasGpuTempMax{false};
                std::set<std::string> racks;
            };

            int healthy = 0;
            int degraded = 0;
            int offline = 0;
            int unknown = 0;
            int reachable = 0;
            int gpuCount = 0;
            int thermalAlerts = 0;
            int fanAlerts = 0;
            int traceyGuardQuarantined = 0;
            int blockedLoaderProviders = 0;
            int blockedLoaderArtifacts = 0;
            int autonomyActionCount = 0;
            int agentsWithCves = 0;
            int agentsWithKev = 0;
            int elevatedAgents = 0;
            int compromisedAgents = 0;
            int totalCveMatches = 0;
            int totalKevMatches = 0;
            double gpuUtilSum = 0.0;
            int gpuUtilCount = 0;
            double gpuPowerTotal = 0.0;
            double autonomyRiskMax = 0.0;
            bool hasAutonomyRisk = false;
            double highestCompromiseRisk = 0.0;
            bool hasCompromiseRisk = false;
            double gpuTempMax = 0.0;
            bool hasGpuTempMax = false;
            uint64_t eccCorrectedTotal = 0;
            uint64_t eccUncorrectedTotal = 0;

            std::map<std::string, RackAgg> rackAggs;
            std::map<std::string, ZoneAgg> zoneAggs;
            nlohmann::json recentActions = nlohmann::json::array();
            nlohmann::json agents = nlohmann::json::array();

            for (const auto& agentView : agentViews) {
                const std::string status = normalizeTraceyStatus(firstStringValue(agentView, {"status"}));
                if (status == "healthy") {
                    healthy++;
                } else if (status == "degraded") {
                    degraded++;
                } else if (status == "offline") {
                    offline++;
                } else {
                    unknown++;
                }
                if (agentView.value("status_reachable", false)) {
                    reachable++;
                }

                const nlohmann::json summary = firstObjectValue(agentView, {"summary"}) != nullptr
                                               ? *firstObjectValue(agentView, {"summary"})
                                               : nlohmann::json::object();
                const nlohmann::json traceyGuardSummary = firstObjectValue(agentView, {"tracey_guard_summary"}) != nullptr
                                                          ? *firstObjectValue(agentView, {"tracey_guard_summary"})
                                                          : nlohmann::json::object();
                const nlohmann::json loaderThreats = firstObjectValue(agentView, {"loader_threats"}) != nullptr
                                                     ? *firstObjectValue(agentView, {"loader_threats"})
                                                     : nlohmann::json::object();
                const nlohmann::json assessmentSummary = firstObjectValue(agentView, {"continuum_assessment_summary"}) != nullptr
                                                         ? *firstObjectValue(agentView, {"continuum_assessment_summary"})
                                                         : nlohmann::json::object();
                const auto* gpus = firstArrayValue(agentView, {"gpus"});
                const auto* actions = firstArrayValue(agentView, {"recent_actions"});

                const std::string rack = defaultTopologyLabel(firstStringValue(agentView, {"rack"}), "unassigned");
                const std::string zone = defaultTopologyLabel(firstStringValue(agentView, {"zone"}), "unassigned");
                auto& rackAgg = rackAggs[rack];
                if (rackAgg.rack.empty()) {
                    rackAgg.rack = rack;
                    rackAgg.zone = zone;
                }
                auto& zoneAgg = zoneAggs[zone];
                if (zoneAgg.zone.empty()) {
                    zoneAgg.zone = zone;
                }

                rackAgg.agentCount++;
                zoneAgg.agentCount++;
                zoneAgg.racks.insert(rack);
                if (agentView.value("status_reachable", false)) {
                    rackAgg.reachable++;
                    zoneAgg.reachable++;
                }

                if (status == "healthy") {
                    rackAgg.healthy++;
                    zoneAgg.healthy++;
                } else if (status == "degraded") {
                    rackAgg.degraded++;
                    zoneAgg.degraded++;
                } else if (status == "offline") {
                    rackAgg.offline++;
                    zoneAgg.offline++;
                } else {
                    rackAgg.unknown++;
                    zoneAgg.unknown++;
                }

                const int agentGpuCount = std::max(0, static_cast<int>(firstInt64Value(summary, {"gpu_count"}, 0)));
                rackAgg.gpuCount += agentGpuCount;
                zoneAgg.gpuCount += agentGpuCount;
                gpuCount += agentGpuCount;

                const int agentThermalAlerts = std::max(0, static_cast<int>(firstInt64Value(summary, {"thermal_alerts"}, 0)));
                const int agentFanAlerts = std::max(0, static_cast<int>(firstInt64Value(summary, {"fan_alerts"}, 0)));
                rackAgg.thermalAlerts += agentThermalAlerts;
                rackAgg.fanAlerts += agentFanAlerts;
                zoneAgg.thermalAlerts += agentThermalAlerts;
                zoneAgg.fanAlerts += agentFanAlerts;
                thermalAlerts += agentThermalAlerts;
                fanAlerts += agentFanAlerts;

                const int agentQuarantined = std::max(0, static_cast<int>(firstInt64Value(
                        traceyGuardSummary,
                        {"quarantined_devices", "quarantinedDevices"},
                        0
                )));
                rackAgg.traceyGuardQuarantined += agentQuarantined;
                zoneAgg.traceyGuardQuarantined += agentQuarantined;
                traceyGuardQuarantined += agentQuarantined;

                const int agentBlockedProviders = std::max(0, static_cast<int>(firstInt64Value(
                        loaderThreats,
                        {"blocked_provider_count", "blockedProviderCount"},
                        0
                )));
                const int agentBlockedArtifacts = std::max(0, static_cast<int>(firstInt64Value(
                        loaderThreats,
                        {"blocked_artifact_count", "blockedArtifactCount"},
                        0
                )));
                rackAgg.blockedLoaderProviders += agentBlockedProviders;
                rackAgg.blockedLoaderArtifacts += agentBlockedArtifacts;
                blockedLoaderProviders += agentBlockedProviders;
                blockedLoaderArtifacts += agentBlockedArtifacts;

                const double agentUtil = firstDoubleValue(summary, {"gpu_utilization_avg_pct"}, -1.0);
                if (agentUtil >= 0.0) {
                    rackAgg.gpuUtilSum += agentUtil * std::max(1, agentGpuCount);
                    rackAgg.gpuUtilCount += std::max(1, agentGpuCount);
                    zoneAgg.gpuUtilSum += agentUtil * std::max(1, agentGpuCount);
                    zoneAgg.gpuUtilCount += std::max(1, agentGpuCount);
                    gpuUtilSum += agentUtil * std::max(1, agentGpuCount);
                    gpuUtilCount += std::max(1, agentGpuCount);
                }

                const double agentPower = firstDoubleValue(summary, {"gpu_power_total_w"}, 0.0);
                if (agentPower > 0.0) {
                    rackAgg.gpuPowerTotal += agentPower;
                    zoneAgg.gpuPowerTotal += agentPower;
                    gpuPowerTotal += agentPower;
                }

                const double agentNetRx = firstDoubleValue(summary, {"net_rx_bps"}, 0.0);
                const double agentNetTx = firstDoubleValue(summary, {"net_tx_bps"}, 0.0);
                if (agentNetRx > 0.0) {
                    rackAgg.netRxBps += agentNetRx;
                }
                if (agentNetTx > 0.0) {
                    rackAgg.netTxBps += agentNetTx;
                }

                const double agentTempMax = firstDoubleValue(summary, {"gpu_temperature_max_c"}, -1.0);
                if (agentTempMax >= 0.0) {
                    rackAgg.gpuTempMax = rackAgg.hasGpuTempMax ? std::max(rackAgg.gpuTempMax, agentTempMax) : agentTempMax;
                    rackAgg.hasGpuTempMax = true;
                    zoneAgg.gpuTempMax = zoneAgg.hasGpuTempMax ? std::max(zoneAgg.gpuTempMax, agentTempMax) : agentTempMax;
                    zoneAgg.hasGpuTempMax = true;
                    gpuTempMax = hasGpuTempMax ? std::max(gpuTempMax, agentTempMax) : agentTempMax;
                    hasGpuTempMax = true;
                }

                const double agentAutonomyRisk = firstDoubleValue(summary, {"autonomy_risk"}, -1.0);
                if (agentAutonomyRisk >= 0.0) {
                    rackAgg.autonomyRiskMax = rackAgg.hasAutonomyRisk ? std::max(rackAgg.autonomyRiskMax, agentAutonomyRisk) : agentAutonomyRisk;
                    rackAgg.hasAutonomyRisk = true;
                    autonomyRiskMax = hasAutonomyRisk ? std::max(autonomyRiskMax, agentAutonomyRisk) : agentAutonomyRisk;
                    hasAutonomyRisk = true;
                }

                const int agentCveMatches = std::max(0, static_cast<int>(firstInt64Value(
                        assessmentSummary,
                        {"cve_matches", "cveMatches"},
                        0
                )));
                const int agentKevMatches = std::max(0, static_cast<int>(firstInt64Value(
                        assessmentSummary,
                        {"kev_matches", "kevMatches"},
                        0
                )));
                const double agentCompromiseRisk = firstDoubleValue(
                        assessmentSummary,
                        {"compromise_risk", "compromiseRisk"},
                        -1.0
                );
                if (agentCveMatches > 0) {
                    agentsWithCves++;
                }
                if (agentKevMatches > 0) {
                    agentsWithKev++;
                }
                totalCveMatches += agentCveMatches;
                totalKevMatches += agentKevMatches;
                if (agentCompromiseRisk >= 0.80) {
                    compromisedAgents++;
                } else if (agentCompromiseRisk >= 0.55) {
                    elevatedAgents++;
                }
                if (agentCompromiseRisk >= 0.0) {
                    highestCompromiseRisk = hasCompromiseRisk
                                            ? std::max(highestCompromiseRisk, agentCompromiseRisk)
                                            : agentCompromiseRisk;
                    hasCompromiseRisk = true;
                }

                const auto* serverNode = firstObjectValue(agentView, {"server"});
                const auto* ecc = serverNode != nullptr ? firstObjectValue(*serverNode, {"ecc"}) : nullptr;
                if (ecc != nullptr) {
                    const auto corrected = static_cast<uint64_t>(std::max<int64_t>(0, firstInt64Value(*ecc, {"corrected_total", "correctedTotal"}, 0)));
                    const auto uncorrected = static_cast<uint64_t>(std::max<int64_t>(0, firstInt64Value(*ecc, {"uncorrected_total", "uncorrectedTotal"}, 0)));
                    rackAgg.eccCorrectedTotal += corrected;
                    rackAgg.eccUncorrectedTotal += uncorrected;
                    eccCorrectedTotal += corrected;
                    eccUncorrectedTotal += uncorrected;
                }

                if (actions != nullptr) {
                    for (const auto& action : *actions) {
                        nlohmann::json enriched = action.is_object() ? action : nlohmann::json::object();
                        enriched["agent_id"] = firstStringValue(agentView, {"agent_id"});
                        enriched["host"] = firstStringValue(agentView, {"host"});
                        enriched["rack"] = rack;
                        enriched["zone"] = zone;
                        recentActions.push_back(enriched);
                        const std::string category = toLower(firstStringValue(enriched, {"category"}));
                        if (category == "autonomy" || category == "automation") {
                            rackAgg.autonomyActions++;
                            zoneAgg.autonomyActions++;
                            autonomyActionCount++;
                        }
                    }
                }

                if (gpus != nullptr) {
                    for (const auto& gpu : *gpus) {
                        if (!gpu.is_object()) {
                            continue;
                        }
                        nlohmann::json cell = gpu;
                        cell["agent_id"] = firstStringValue(agentView, {"agent_id"});
                        cell["host"] = firstStringValue(agentView, {"host"});
                        cell["rack"] = rack;
                        cell["zone"] = zone;
                        cell["status"] = status;
                        rackAgg.heatCells.push_back(cell);
                    }
                }

                agents.push_back({
                        {"agent_id", firstStringValue(agentView, {"agent_id"})},
                        {"cluster", firstStringValue(agentView, {"cluster"})},
                        {"status", status},
                        {"host", firstStringValue(agentView, {"host"})},
                        {"rack", rack},
                        {"zone", zone},
                        {"gpu_count", agentGpuCount},
                        {"gpu_utilization_avg_pct", agentUtil >= 0.0 ? agentUtil : 0.0},
                        {"gpu_temperature_max_c", agentTempMax >= 0.0 ? agentTempMax : 0.0},
                        {"autonomy_risk", agentAutonomyRisk >= 0.0 ? agentAutonomyRisk : 0.0},
                        {"compromise_risk", agentCompromiseRisk >= 0.0 ? agentCompromiseRisk : 0.0},
                        {"cve_matches", agentCveMatches},
                        {"kev_matches", agentKevMatches},
                        {"assessment_status", firstStringValue(assessmentSummary, {"status"}, "unknown")},
                        {"assessment_action", firstStringValue(assessmentSummary, {"recommended_action", "recommendedAction"})},
                        {"recent_action_count", std::max(0, static_cast<int>(firstInt64Value(summary, {"recent_action_count"}, 0)))},
                        {"last_seen_seconds_ago", std::max<int64_t>(0, firstInt64Value(agentView, {"last_seen_seconds_ago"}, 0))}
                });
            }

            sortActionsByTime(recentActions);
            recentActions = limitedJsonArray(recentActions, 64);

            nlohmann::json zoneBreakdown = nlohmann::json::array();
            for (const auto& [zoneKey, agg] : zoneAggs) {
                (void)zoneKey;
                zoneBreakdown.push_back({
                        {"zone", agg.zone},
                        {"health", rollupTraceyStatusCounts(agg.healthy, agg.degraded, agg.offline, agg.unknown)},
                        {"agent_count", agg.agentCount},
                        {"rack_count", static_cast<int>(agg.racks.size())},
                        {"gpu_count", agg.gpuCount},
                        {"reachable_agents", agg.reachable},
                        {"gpu_utilization_avg_pct", agg.gpuUtilCount > 0 ? agg.gpuUtilSum / static_cast<double>(agg.gpuUtilCount) : 0.0},
                        {"gpu_temperature_max_c", agg.hasGpuTempMax ? agg.gpuTempMax : 0.0},
                        {"gpu_power_total_w", agg.gpuPowerTotal},
                        {"thermal_alerts", agg.thermalAlerts},
                        {"fan_alerts", agg.fanAlerts},
                        {"tracey_guard_quarantined", agg.traceyGuardQuarantined},
                        {"autonomy_actions", agg.autonomyActions}
                });
            }

            nlohmann::json racks = nlohmann::json::array();
            nlohmann::json gpuHeatmap = nlohmann::json::array();
            for (auto& [rackKey, agg] : rackAggs) {
                (void)rackKey;
                sortGpuTiles(agg.heatCells);
                racks.push_back({
                        {"rack", agg.rack},
                        {"zone", agg.zone},
                        {"health", rollupTraceyStatusCounts(agg.healthy, agg.degraded, agg.offline, agg.unknown)},
                        {"agent_count", agg.agentCount},
                        {"reachable_agents", agg.reachable},
                        {"gpu_count", agg.gpuCount},
                        {"gpu_utilization_avg_pct", agg.gpuUtilCount > 0 ? agg.gpuUtilSum / static_cast<double>(agg.gpuUtilCount) : 0.0},
                        {"gpu_temperature_max_c", agg.hasGpuTempMax ? agg.gpuTempMax : 0.0},
                        {"gpu_power_total_w", agg.gpuPowerTotal},
                        {"net_rx_bps", agg.netRxBps},
                        {"net_tx_bps", agg.netTxBps},
                        {"thermal_alerts", agg.thermalAlerts},
                        {"fan_alerts", agg.fanAlerts},
                        {"ecc_corrected_total", agg.eccCorrectedTotal},
                        {"ecc_uncorrected_total", agg.eccUncorrectedTotal},
                        {"tracey_guard_quarantined", agg.traceyGuardQuarantined},
                        {"blocked_loader_providers", agg.blockedLoaderProviders},
                        {"blocked_loader_artifacts", agg.blockedLoaderArtifacts},
                        {"autonomy_actions", agg.autonomyActions},
                        {"autonomy_risk_max", agg.hasAutonomyRisk ? agg.autonomyRiskMax : 0.0}
                });
                gpuHeatmap.push_back({
                        {"rack", agg.rack},
                        {"zone", agg.zone},
                        {"cells", agg.heatCells}
                });
            }

            return {
                    {"generated_epoch_ms", nowMs},
                    {"summary", {
                            {"agents_total", static_cast<int>(agentViews.size())},
                            {"healthy", healthy},
                            {"degraded", degraded},
                            {"offline", offline},
                            {"unknown", unknown},
                            {"reachable", reachable},
                            {"racks_total", static_cast<int>(rackAggs.size())},
                            {"zones_total", static_cast<int>(zoneAggs.size())},
                            {"gpu_total", gpuCount},
                            {"gpu_utilization_avg_pct", gpuUtilCount > 0 ? gpuUtilSum / static_cast<double>(gpuUtilCount) : 0.0},
                            {"gpu_temperature_max_c", hasGpuTempMax ? gpuTempMax : 0.0},
                            {"gpu_power_total_w", gpuPowerTotal},
                            {"thermal_alerts", thermalAlerts},
                            {"fan_alerts", fanAlerts},
                            {"ecc_corrected_total", eccCorrectedTotal},
                            {"ecc_uncorrected_total", eccUncorrectedTotal},
                            {"tracey_guard_quarantined", traceyGuardQuarantined},
                            {"blocked_loader_providers", blockedLoaderProviders},
                            {"blocked_loader_artifacts", blockedLoaderArtifacts},
                            {"autonomy_actions", autonomyActionCount},
                            {"autonomy_risk_max", hasAutonomyRisk ? autonomyRiskMax : 0.0},
                            {"agents_with_cves", agentsWithCves},
                            {"agents_with_kev", agentsWithKev},
                            {"elevated_agents", elevatedAgents},
                            {"compromised_agents", compromisedAgents},
                            {"cve_matches", totalCveMatches},
                            {"kev_matches", totalKevMatches},
                            {"compromise_risk_max", hasCompromiseRisk ? highestCompromiseRisk : 0.0}
                    }},
                    {"zone_breakdown", zoneBreakdown},
                    {"racks", racks},
                    {"gpu_heatmap", gpuHeatmap},
                    {"recent_actions", recentActions},
                    {"agents", agents}
            };
        }

        double clampUnit(double value) {
            return std::max(0.0, std::min(1.0, value));
        }

        std::string truncateDisplay(std::string value, size_t maxLen = 96) {
            value = trim(value);
            if (value.size() <= maxLen) {
                return value;
            }
            if (maxLen < 2) {
                return value.substr(0, maxLen);
            }
            value.resize(maxLen - 1);
            value.push_back('~');
            return value;
        }

        void pushLimitedUniqueString(std::vector<std::string>& target,
                                     const std::string& raw,
                                     size_t limit = 4,
                                     size_t maxLen = 96) {
            if (target.size() >= limit) {
                return;
            }
            const std::string value = truncateDisplay(raw, maxLen);
            if (value.empty()) {
                return;
            }
            if (std::find(target.begin(), target.end(), value) != target.end()) {
                return;
            }
            target.push_back(value);
        }

        std::string joinLimitedStrings(const std::vector<std::string>& values,
                                       const std::string& fallback = "-",
                                       size_t limit = 3) {
            if (values.empty()) {
                return fallback;
            }
            std::ostringstream joined;
            for (size_t i = 0; i < values.size() && i < limit; ++i) {
                if (i > 0) {
                    joined << " | ";
                }
                joined << values[i];
            }
            return joined.str();
        }

        int adaptivePriorityRank(const std::string& priority) {
            const std::string normalized = toLower(priority);
            if (normalized == "critical") {
                return 4;
            }
            if (normalized == "high") {
                return 3;
            }
            if (normalized == "medium") {
                return 2;
            }
            return 1;
        }

        std::string normalizeAdaptivePlacementPolicy(const std::string& rawPolicy) {
            const std::string policy = toLower(trim(rawPolicy));
            if (policy == "throughput" || policy == "risk" || policy == "energy") {
                return policy;
            }
            return "balanced";
        }

        std::string adaptivePlacementPolicyLabel(const std::string& policy) {
            if (policy == "throughput") {
                return "Throughput";
            }
            if (policy == "risk") {
                return "Risk";
            }
            if (policy == "energy") {
                return "Energy";
            }
            return "Balanced";
        }

        nlohmann::json buildAdaptiveLoopFallback(const nlohmann::json& agentView) {
            const auto* summary = firstObjectValue(agentView, {"summary"});
            const auto* assessment = firstObjectValue(agentView, {"continuum_assessment_summary"});
            const auto* autoscaler = firstObjectValue(agentView, {"continuum_autoscaler"});
            const auto* guard = firstObjectValue(agentView, {"tracey_guard_summary"});
            const auto* loader = firstObjectValue(agentView, {"loader_threats"});

            const double gpuUtilPct = summary != nullptr
                                      ? std::max(0.0, firstDoubleValue(*summary, {"gpu_utilization_avg_pct"}, 0.0))
                                      : 0.0;
            const double headroomRatio = clampUnit((100.0 - gpuUtilPct) / 100.0);
            const double compromiseRisk = assessment != nullptr
                                          ? clampUnit(firstDoubleValue(*assessment, {"compromise_risk", "compromiseRisk"}, 0.0))
                                          : 0.0;
            const double fuzzyConfidence = assessment != nullptr
                                           ? clampUnit(firstDoubleValue(*assessment, {"fuzzy_confidence", "fuzzyConfidence"}, 0.0))
                                           : 0.0;
            const double cycleCompletion = assessment != nullptr
                                           ? clampUnit(firstDoubleValue(*assessment, {"cycle_completion_pct", "cycleCompletionPct"}, 0.0))
                                           : 0.0;
            const double autonomyRisk = summary != nullptr
                                        ? clampUnit(firstDoubleValue(*summary, {"autonomy_risk"}, 0.0))
                                        : 0.0;
            const int guardQuarantined = guard != nullptr
                                         ? std::max<int64_t>(0, firstInt64Value(*guard, {"quarantined_devices", "quarantinedDevices"}, 0))
                                         : 0;
            const int loaderBlocks = loader != nullptr
                                     ? std::max<int64_t>(
                                             0,
                                             firstInt64Value(*loader, {"blocked_provider_count", "blockedProviderCount"}, 0)
                                                     + firstInt64Value(*loader, {"blocked_artifact_count", "blockedArtifactCount"}, 0)
                                     )
                                     : 0;
            std::vector<std::string> pressureSignals;
            if (autoscaler != nullptr) {
                const auto* pressureArray = firstArrayValue(*autoscaler, {"pressure_signals", "pressureSignals"});
                if (pressureArray != nullptr) {
                    for (const auto& item : *pressureArray) {
                        if (item.is_string()) {
                            pushLimitedUniqueString(pressureSignals, item.get<std::string>(), 4, 88);
                        }
                    }
                }
            }
            const size_t requestedRemoteNodes = autoscaler != nullptr
                                                ? static_cast<size_t>(std::max<int64_t>(
                                                        0,
                                                        firstInt64Value(*autoscaler, {"requested_remote_nodes", "requestedRemoteNodes"}, 0)
                                                ))
                                                : 0;
            const size_t activeRemoteNodes = autoscaler != nullptr
                                             ? static_cast<size_t>(std::max<int64_t>(
                                                     0,
                                                     firstInt64Value(*autoscaler, {"active_remote_nodes", "activeRemoteNodes"}, 0)
                                             ))
                                             : 0;
            const size_t pressureSignalCount = pressureSignals.size();
            const int recentActionCount = summary != nullptr
                                          ? std::max<int64_t>(0, firstInt64Value(*summary, {"recent_action_count", "recentActionCount"}, 0))
                                          : 0;
            const int64_t lastSeenSecondsAgo = std::max<int64_t>(0, firstInt64Value(agentView, {"last_seen_seconds_ago"}, 0));

            const double planScore = clampUnit(0.55 * (1.0 - compromiseRisk) + 0.20 * fuzzyConfidence + 0.25 * cycleCompletion);
            const double rampScore = clampUnit(1.0 - std::min(1.0, pressureSignalCount * 0.25 + (requestedRemoteNodes > activeRemoteNodes ? 0.25 : 0.0)));
            const double optimizeScore = clampUnit(
                    0.45 * headroomRatio
                    + 0.25 * (1.0 - compromiseRisk)
                    + 0.15 * (1.0 - autonomyRisk)
                    + 0.15 * ((guardQuarantined == 0 && loaderBlocks == 0) ? 1.0 : 0.25)
            );
            const double repeatScore = clampUnit(
                    0.50 * (lastSeenSecondsAgo <= 60 ? 1.0 : (lastSeenSecondsAgo <= 300 ? 0.7 : 0.35))
                    + 0.25 * cycleCompletion
                    + 0.25 * std::min(1.0, static_cast<double>(recentActionCount) / 6.0)
            );

            const std::string planStatus = compromiseRisk >= 0.80 ? "constrained" : (cycleCompletion >= 0.60 ? "ready" : "learning");
            const std::string rampStatus = (pressureSignalCount > 0 || requestedRemoteNodes > activeRemoteNodes) ? "active" : "steady";
            const bool optimizeConstrained = compromiseRisk >= 0.80 || guardQuarantined >= 2 || loaderBlocks >= 4;
            const bool optimizeDegraded = guardQuarantined > 0 || loaderBlocks > 0;
            const std::string optimizeStatus = optimizeConstrained
                                               ? "avoid"
                                               : (optimizeScore >= 0.72
                                                  ? (optimizeDegraded ? "balanced" : "open")
                                                  : (optimizeScore >= 0.45
                                                     ? (optimizeDegraded ? "tight" : "balanced")
                                                     : "tight"));
            const std::string repeatStatus = lastSeenSecondsAgo > 300 ? "stale" : (recentActionCount > 0 ? "learning" : "watch");

            nlohmann::json recommendations = nlohmann::json::array();
            if (compromiseRisk >= 0.80) {
                recommendations.push_back({
                        {"stage", "plan"},
                        {"priority", "critical"},
                        {"action", "Isolate Host"},
                        {"reason", truncateDisplay("Compromise risk exceeds 80%; isolate before further placement.", 104)}
                });
            }
            if (pressureSignalCount > 0 || requestedRemoteNodes > activeRemoteNodes) {
                recommendations.push_back({
                        {"stage", "ramp"},
                        {"priority", "high"},
                        {"action", requestedRemoteNodes > activeRemoteNodes ? "Complete Pending Recruits" : "Recruit Capacity"},
                        {"reason", truncateDisplay(joinLimitedStrings(pressureSignals, "Scaling pressure detected."), 104)}
                });
            }
            if (optimizeScore >= 0.72 && compromiseRisk < 0.55 && guardQuarantined == 0 && loaderBlocks == 0) {
                recommendations.push_back({
                        {"stage", "optimise"},
                        {"priority", "medium"},
                        {"action", "Prefer For Burst Placement"},
                        {"reason", truncateDisplay("Healthy host with low risk and meaningful GPU headroom.", 104)}
                });
            }
            if (lastSeenSecondsAgo > 300) {
                recommendations.push_back({
                        {"stage", "repeat"},
                        {"priority", "medium"},
                        {"action", "Refresh Loop Inputs"},
                        {"reason", truncateDisplay("Telemetry freshness has degraded; refresh agent reporting.", 104)}
                });
            }
            if (recommendations.empty()) {
                recommendations.push_back({
                        {"stage", "repeat"},
                        {"priority", "low"},
                        {"action", "Hold Steady"},
                        {"reason", "No material pressure or compromise signal detected."}
                });
            }

            const double overallScore = clampUnit(0.35 * planScore + 0.25 * rampScore + 0.30 * optimizeScore + 0.10 * repeatScore);
            const double readinessScore = clampUnit((planScore + repeatScore) / 2.0);
            const std::string mode = compromiseRisk >= 0.80 || optimizeStatus == "avoid"
                                     ? "constrained"
                                     : (rampStatus == "active"
                                        ? "ramping"
                                        : (repeatStatus == "stale" ? "degraded" : (optimizeStatus == "open" ? "ready" : "balanced")));

            return {
                    {"enabled", true},
                    {"mode", mode},
                    {"overall_score", overallScore},
                    {"readiness_score", readinessScore},
                    {"placement_score", optimizeScore},
                    {"gpu_headroom_pct", headroomRatio * 100.0},
                    {"requested_remote_nodes", requestedRemoteNodes},
                    {"active_remote_nodes", activeRemoteNodes},
                    {"pressure_signal_count", pressureSignalCount},
                    {"assessment_completion_pct", cycleCompletion},
                    {"compromise_risk", compromiseRisk},
                    {"fuzzy_confidence", fuzzyConfidence},
                    {"next_action", recommendations.front().value("action", "Hold Steady")},
                    {"plan", {
                            {"status", planStatus},
                            {"score", planScore},
                            {"headline", truncateDisplay(
                                    std::to_string(std::max<int64_t>(0, assessment != nullptr
                                                                        ? firstInt64Value(*assessment, {"cve_matches", "cveMatches"}, 0)
                                                                        : 0))
                                    + " CVE signals",
                                    72
                            )},
                            {"signals", nlohmann::json::array({truncateDisplay(
                                    compromiseRisk >= 0.80 ? "High compromise pressure." : "Assessment cycle learning.",
                                    88
                            )})}
                    }},
                    {"ramp", {
                            {"status", rampStatus},
                            {"score", rampScore},
                            {"headline", truncateDisplay(
                                    "req " + std::to_string(requestedRemoteNodes) + " / active " + std::to_string(activeRemoteNodes),
                                    72
                            )},
                            {"signals", pressureSignals}
                    }},
                    {"optimize", {
                            {"status", optimizeStatus},
                            {"score", optimizeScore},
                            {"headline", truncateDisplay(
                                    "headroom " + std::to_string(static_cast<int>(headroomRatio * 100.0))
                                    + "% util " + std::to_string(static_cast<int>(gpuUtilPct)) + "%",
                                    72
                            )},
                            {"signals", nlohmann::json::array({truncateDisplay(
                                    "guard " + std::to_string(guardQuarantined) + " / loader " + std::to_string(loaderBlocks),
                                    88
                            )})}
                    }},
                    {"repeat", {
                            {"status", repeatStatus},
                            {"score", repeatScore},
                            {"headline", truncateDisplay(
                                    "seen " + std::to_string(lastSeenSecondsAgo) + "s ago / actions " + std::to_string(recentActionCount),
                                    72
                            )},
                            {"signals", nlohmann::json::array({truncateDisplay(
                                    lastSeenSecondsAgo > 300 ? "Agent telemetry is stale." : "Feedback loop is current.",
                                    88
                            )})}
                    }},
                    {"recommendations", recommendations}
            };
        }

        std::string clusterIdentityKey(const nlohmann::json& cluster) {
            const std::string name = toLower(firstStringValue(cluster, {"name"}));
            const std::string id = toLower(firstStringValue(cluster, {"id", "cluster_id", "uuid"}));
            return name + "|" + id;
        }

        void appendClusterArray(const nlohmann::json& clusterArray,
                                std::vector<nlohmann::json>& out,
                                std::set<std::string>& seenKeys) {
            if (!clusterArray.is_array()) {
                return;
            }
            for (const auto& item : clusterArray) {
                if (!item.is_object()) {
                    continue;
                }
                nlohmann::json normalized = item;
                normalizeClusterStatus(normalized);
                const std::string key = clusterIdentityKey(normalized);
                if (!key.empty() && seenKeys.find(key) != seenKeys.end()) {
                    continue;
                }
                if (!key.empty()) {
                    seenKeys.insert(key);
                }
                out.push_back(normalized);
            }
        }

        std::vector<nlohmann::json> collectClusterObjects(const nlohmann::json& payload) {
            std::vector<nlohmann::json> clusters;
            std::set<std::string> seenKeys;

            if (payload.is_array()) {
                appendClusterArray(payload, clusters, seenKeys);
                return clusters;
            }

            if (!payload.is_object()) {
                return clusters;
            }

            if (payload.contains("clusters")) {
                appendClusterArray(payload["clusters"], clusters, seenKeys);
            }
            if (payload.contains("data")) {
                if (payload["data"].is_array()) {
                    appendClusterArray(payload["data"], clusters, seenKeys);
                } else if (payload["data"].is_object() && payload["data"].contains("clusters")) {
                    appendClusterArray(payload["data"]["clusters"], clusters, seenKeys);
                }
            }
            if (payload.contains("items")) {
                appendClusterArray(payload["items"], clusters, seenKeys);
            }
            if (clusters.empty() && payload.contains("cluster") && payload["cluster"].is_object()) {
                nlohmann::json normalized = payload["cluster"];
                normalizeClusterStatus(normalized);
                clusters.push_back(normalized);
            }

            return clusters;
        }

        bool parseBoolValue(const char* raw, bool fallback) {
            if (!raw || !*raw) {
                return fallback;
            }
            const std::string value = toLower(trim(raw));
            if (value == "1" || value == "true" || value == "yes" || value == "on") {
                return true;
            }
            if (value == "0" || value == "false" || value == "no" || value == "off") {
                return false;
            }
            return fallback;
        }

        int64_t parseInt64Value(const char* raw, int64_t fallback, int64_t minValue, int64_t maxValue) {
            if (!raw || !*raw) {
                return fallback;
            }
            try {
                int64_t parsed = std::stoll(raw);
                if (parsed < minValue) {
                    parsed = minValue;
                } else if (parsed > maxValue) {
                    parsed = maxValue;
                }
                return parsed;
            } catch (const std::exception&) {
                return fallback;
            }
        }

        struct TraceyEndpoint {
            std::string host;
            int port{80};
            bool https{false};
            std::string basePath;
            std::string normalized;
        };

        bool isPrivateIpv4(const in_addr& addr) {
            const uint32_t ip = ntohl(addr.s_addr);
            if ((ip & 0xFF000000U) == 0x0A000000U) { // 10.0.0.0/8
                return true;
            }
            if ((ip & 0xFFF00000U) == 0xAC100000U) { // 172.16.0.0/12
                return true;
            }
            if ((ip & 0xFFFF0000U) == 0xC0A80000U) { // 192.168.0.0/16
                return true;
            }
            if ((ip & 0xFF000000U) == 0x7F000000U) { // 127.0.0.0/8
                return true;
            }
            if ((ip & 0xFFFF0000U) == 0xA9FE0000U) { // 169.254.0.0/16
                return true;
            }
            if ((ip & 0xFFC00000U) == 0x64400000U) { // 100.64.0.0/10
                return true;
            }
            return false;
        }

        bool isPrivateIpv6(const in6_addr& addr) {
            static const in6_addr loopback = IN6ADDR_LOOPBACK_INIT;
            if (std::memcmp(&addr, &loopback, sizeof(in6_addr)) == 0) {
                return true;
            }
            if ((addr.s6_addr[0] & 0xFEU) == 0xFCU) { // fc00::/7
                return true;
            }
            if (addr.s6_addr[0] == 0xFEU && (addr.s6_addr[1] & 0xC0U) == 0x80U) { // fe80::/10
                return true;
            }
            return false;
        }

        bool hostResolvesToLocal(const std::string& host) {
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags = AI_ADDRCONFIG;
            addrinfo* result = nullptr;
            const int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &result);
            if (rc != 0 || result == nullptr) {
                return false;
            }

            bool foundPrivate = false;
            for (addrinfo* cursor = result; cursor != nullptr; cursor = cursor->ai_next) {
                if (cursor->ai_family == AF_INET) {
                    const auto* in = reinterpret_cast<sockaddr_in*>(cursor->ai_addr);
                    if (!isPrivateIpv4(in->sin_addr)) {
                        ::freeaddrinfo(result);
                        return false;
                    }
                    foundPrivate = true;
                } else if (cursor->ai_family == AF_INET6) {
                    const auto* in6 = reinterpret_cast<sockaddr_in6*>(cursor->ai_addr);
                    if (!isPrivateIpv6(in6->sin6_addr)) {
                        ::freeaddrinfo(result);
                        return false;
                    }
                    foundPrivate = true;
                }
            }

            ::freeaddrinfo(result);
            return foundPrivate;
        }

        bool isLocalOrPrivateHost(const std::string& host, bool allowPublicAddr) {
            if (allowPublicAddr) {
                return true;
            }
            const std::string lowered = toLower(host);
            if (lowered == "localhost") {
                return true;
            }

            in_addr in4{};
            if (::inet_pton(AF_INET, host.c_str(), &in4) == 1) {
                return isPrivateIpv4(in4);
            }

            in6_addr in6{};
            if (::inet_pton(AF_INET6, host.c_str(), &in6) == 1) {
                return isPrivateIpv6(in6);
            }

            return hostResolvesToLocal(host);
        }

        std::string normalizeHostIdentity(std::string value) {
            value = toLower(trim(value));
            while (!value.empty() && value.back() == '.') {
                value.pop_back();
            }
            return value;
        }

        std::vector<std::string> resolveHostAddresses(const std::string& host) {
            std::vector<std::string> addresses;
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags = AI_ADDRCONFIG;
            addrinfo* result = nullptr;
            const int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &result);
            if (rc != 0 || result == nullptr) {
                return addresses;
            }

            std::set<std::string> unique;
            char buffer[INET6_ADDRSTRLEN]{};
            for (addrinfo* cursor = result; cursor != nullptr; cursor = cursor->ai_next) {
                const void* source = nullptr;
                if (cursor->ai_family == AF_INET) {
                    source = &reinterpret_cast<const sockaddr_in*>(cursor->ai_addr)->sin_addr;
                } else if (cursor->ai_family == AF_INET6) {
                    source = &reinterpret_cast<const sockaddr_in6*>(cursor->ai_addr)->sin6_addr;
                }
                if (!source) {
                    continue;
                }
                if (::inet_ntop(cursor->ai_family, source, buffer, sizeof(buffer)) != nullptr) {
                    const std::string normalized = normalizeHostIdentity(buffer);
                    if (!normalized.empty() && unique.insert(normalized).second) {
                        addresses.push_back(normalized);
                    }
                }
            }

            ::freeaddrinfo(result);
            return addresses;
        }

        LocalHostIdentity collectLocalHostIdentity() {
            LocalHostIdentity identity;
            identity.aliases.insert("localhost");
            identity.addresses.insert("127.0.0.1");
            identity.addresses.insert("::1");

            std::array<char, 256> hostnameBuf{};
            if (::gethostname(hostnameBuf.data(), hostnameBuf.size()) == 0) {
                hostnameBuf.back() = '\0';
                const std::string hostname = normalizeHostIdentity(hostnameBuf.data());
                if (!hostname.empty()) {
                    identity.aliases.insert(hostname);
                    const auto dot = hostname.find('.');
                    if (dot != std::string::npos) {
                        identity.aliases.insert(hostname.substr(0, dot));
                    }
                    for (const auto& address : resolveHostAddresses(hostname)) {
                        identity.addresses.insert(address);
                    }
                }
            }

            if (const char* envHostname = std::getenv("HOSTNAME")) {
                const std::string hostname = normalizeHostIdentity(envHostname);
                if (!hostname.empty()) {
                    identity.aliases.insert(hostname);
                }
            }

            ifaddrs* interfaces = nullptr;
            if (::getifaddrs(&interfaces) == 0 && interfaces != nullptr) {
                char buffer[INET6_ADDRSTRLEN]{};
                for (ifaddrs* cursor = interfaces; cursor != nullptr; cursor = cursor->ifa_next) {
                    if (!cursor->ifa_addr) {
                        continue;
                    }
                    const void* source = nullptr;
                    if (cursor->ifa_addr->sa_family == AF_INET) {
                        source = &reinterpret_cast<const sockaddr_in*>(cursor->ifa_addr)->sin_addr;
                    } else if (cursor->ifa_addr->sa_family == AF_INET6) {
                        source = &reinterpret_cast<const sockaddr_in6*>(cursor->ifa_addr)->sin6_addr;
                    }
                    if (!source) {
                        continue;
                    }
                    if (::inet_ntop(cursor->ifa_addr->sa_family, source, buffer, sizeof(buffer)) != nullptr) {
                        const std::string address = normalizeHostIdentity(buffer);
                        if (!address.empty()) {
                            identity.addresses.insert(address);
                        }
                    }
                }
                ::freeifaddrs(interfaces);
            }

            return identity;
        }

        HostMatchResult classifyRecruitHost(const std::string& host, const LocalHostIdentity& identity) {
            HostMatchResult result;
            result.details = {
                    {"requested_host", host},
                    {"resolved_addresses", nlohmann::json::array()}
            };

            const std::string normalizedHost = normalizeHostIdentity(host);
            if (normalizedHost.empty()) {
                result.details["matched_by"] = "invalid";
                return result;
            }

            if (identity.aliases.find(normalizedHost) != identity.aliases.end()) {
                result.sameHardware = true;
                result.details["matched_by"] = "hostname";
                return result;
            }

            if (identity.addresses.find(normalizedHost) != identity.addresses.end()) {
                result.sameHardware = true;
                result.details["matched_by"] = "address";
                return result;
            }

            const auto resolved = resolveHostAddresses(host);
            for (const auto& address : resolved) {
                result.details["resolved_addresses"].push_back(address);
            }
            if (!resolved.empty()) {
                const bool allLocal = std::all_of(resolved.begin(), resolved.end(), [&](const std::string& address) {
                    return identity.addresses.find(address) != identity.addresses.end();
                });
                if (allLocal) {
                    result.sameHardware = true;
                    result.details["matched_by"] = "dns";
                    return result;
                }
            }

            result.details["matched_by"] = "remote";
            return result;
        }

        bool readProcMemInfo(uint64_t& totalBytes, uint64_t& availableBytes) {
            std::ifstream input("/proc/meminfo");
            if (!input.is_open()) {
                return false;
            }

            uint64_t memTotalKb = 0;
            uint64_t memAvailableKb = 0;
            uint64_t memFreeKb = 0;
            uint64_t buffersKb = 0;
            uint64_t cachedKb = 0;
            std::string key;
            uint64_t value = 0;
            std::string unit;
            while (input >> key >> value >> unit) {
                if (key == "MemTotal:") {
                    memTotalKb = value;
                } else if (key == "MemAvailable:") {
                    memAvailableKb = value;
                } else if (key == "MemFree:") {
                    memFreeKb = value;
                } else if (key == "Buffers:") {
                    buffersKb = value;
                } else if (key == "Cached:") {
                    cachedKb = value;
                }
            }

            if (memTotalKb == 0) {
                return false;
            }
            if (memAvailableKb == 0) {
                memAvailableKb = memFreeKb + buffersKb + cachedKb;
            }
            totalBytes = memTotalKb * 1024ULL;
            availableBytes = memAvailableKb * 1024ULL;
            return availableBytes <= totalBytes;
        }

        SystemCapacitySnapshot collectSystemCapacitySnapshot() {
            SystemCapacitySnapshot snapshot;
            snapshot.cpuCores = std::max(1u, std::thread::hardware_concurrency());

            double load[3]{0.0, 0.0, 0.0};
            if (::getloadavg(load, 1) == 1) {
                snapshot.load1 = load[0];
                snapshot.loadPerCpu = snapshot.load1 / static_cast<double>(snapshot.cpuCores);
                snapshot.loadAvailable = true;
            }

            snapshot.memoryAvailable = readProcMemInfo(
                    snapshot.totalMemoryBytes,
                    snapshot.availableMemoryBytes
            );
            if (snapshot.memoryAvailable && snapshot.totalMemoryBytes > 0) {
                snapshot.memoryUsedRatio =
                        1.0 - (static_cast<double>(snapshot.availableMemoryBytes) /
                               static_cast<double>(snapshot.totalMemoryBytes));
            }

            return snapshot;
        }

        bool parseTraceyEndpoint(const std::string& rawValue, TraceyEndpoint& endpoint) {
            std::string value = trim(rawValue);
            if (value.empty()) {
                return false;
            }

            if (value.rfind("http://", 0) != 0 && value.rfind("https://", 0) != 0) {
                value = "http://" + value;
            }

            std::string rest;
            if (value.rfind("https://", 0) == 0) {
                endpoint.https = true;
                endpoint.port = 443;
                rest = value.substr(8);
            } else if (value.rfind("http://", 0) == 0) {
                endpoint.https = false;
                endpoint.port = 80;
                rest = value.substr(7);
            } else {
                return false;
            }

            const auto slashPos = rest.find('/');
            std::string hostPort = slashPos == std::string::npos ? rest : rest.substr(0, slashPos);
            endpoint.basePath = slashPos == std::string::npos ? "" : rest.substr(slashPos);
            if (hostPort.empty()) {
                return false;
            }
            while (!endpoint.basePath.empty() && endpoint.basePath.back() == '/') {
                endpoint.basePath.pop_back();
            }
            if (endpoint.basePath == "/") {
                endpoint.basePath.clear();
            }

            std::string host;
            std::string portPart;
            if (!hostPort.empty() && hostPort.front() == '[') {
                const auto close = hostPort.find(']');
                if (close == std::string::npos) {
                    return false;
                }
                host = hostPort.substr(1, close - 1);
                if (close + 1 < hostPort.size()) {
                    if (hostPort[close + 1] != ':') {
                        return false;
                    }
                    portPart = hostPort.substr(close + 2);
                }
            } else {
                const auto colonPos = hostPort.rfind(':');
                if (colonPos != std::string::npos && hostPort.find(':') == colonPos) {
                    host = hostPort.substr(0, colonPos);
                    portPart = hostPort.substr(colonPos + 1);
                } else {
                    host = hostPort;
                }
            }

            if (host.empty()) {
                return false;
            }

            if (!portPart.empty()) {
                try {
                    endpoint.port = std::stoi(portPart);
                } catch (const std::exception&) {
                    return false;
                }
            }

            if (endpoint.port <= 0 || endpoint.port > 65535) {
                return false;
            }

            endpoint.host = host;
            const bool hostIsIpv6 = host.find(':') != std::string::npos;
            endpoint.normalized = endpoint.https ? "https://" : "http://";
            endpoint.normalized += hostIsIpv6 ? "[" + host + "]" : host;
            endpoint.normalized += ":" + std::to_string(endpoint.port);
            endpoint.normalized += endpoint.basePath;
            return true;
        }

        nlohmann::json findClusterByIdentifier(const std::vector<nlohmann::json>& clusters,
                                               const std::string& identifier) {
            const std::string needle = toLower(trim(identifier));
            if (needle.empty()) {
                return nullptr;
            }

            for (const auto& cluster : clusters) {
                const std::string name = toLower(firstStringValue(cluster, {"name"}));
                const std::string id = toLower(firstStringValue(cluster, {"id", "cluster_id", "uuid"}));
                if ((!name.empty() && name == needle) || (!id.empty() && id == needle)) {
                    return cluster;
                }
            }
            return nullptr;
        }

        nlohmann::json buildClusterDetailsWithNetworking(const nlohmann::json& selectedCluster) {
            std::vector<std::string> endpointCandidates;
            std::vector<std::string> ipsInUse;
            std::vector<int> portsInUse;

            const auto captureEndpoint = [&](const std::string& value) {
                const std::string endpoint = trim(value);
                if (endpoint.empty()) {
                    return;
                }
                addUniqueString(endpointCandidates, endpoint);
                TraceyEndpoint parsed{};
                if (parseTraceyEndpoint(endpoint, parsed)) {
                    addUniqueString(ipsInUse, parsed.host);
                    addUniqueInt(portsInUse, parsed.port);
                }
            };

            captureEndpoint(firstStringValue(selectedCluster, {"endpoint", "api", "api_url", "url", "apiServer", "api_server"}));
            captureEndpoint(firstStringValue(selectedCluster, {"console", "console_url", "consoleUrl"}));

            if (selectedCluster.contains("networking") && selectedCluster["networking"].is_object()) {
                const auto& networking = selectedCluster["networking"];
                captureEndpoint(firstStringValue(networking, {"endpoint", "api", "api_url", "url", "apiServer", "api_server"}));
                captureEndpoint(firstStringValue(networking, {"console", "console_url", "consoleUrl"}));

                if (networking.contains("ips_in_use") && networking["ips_in_use"].is_array()) {
                    for (const auto& ip : networking["ips_in_use"]) {
                        if (ip.is_string()) {
                            addUniqueString(ipsInUse, ip.get<std::string>());
                        }
                    }
                }
                if (networking.contains("ports_in_use") && networking["ports_in_use"].is_array()) {
                    for (const auto& port : networking["ports_in_use"]) {
                        if (port.is_number_integer()) {
                            addUniqueInt(portsInUse, port.get<int>());
                        }
                    }
                }
            }

            addUniqueInt(portsInUse, firstPositiveIntValue(selectedCluster, {"port", "api_port", "apiPort", "service_port"}));
            if (selectedCluster.contains("networking") && selectedCluster["networking"].is_object()) {
                addUniqueInt(portsInUse, firstPositiveIntValue(selectedCluster["networking"], {"port", "api_port", "apiPort", "service_port"}));
            }

            std::sort(endpointCandidates.begin(), endpointCandidates.end());
            std::sort(ipsInUse.begin(), ipsInUse.end());
            std::sort(portsInUse.begin(), portsInUse.end());

            nlohmann::json details = selectedCluster;
            nlohmann::json networkingPayload = nlohmann::json::object();
            if (details.contains("networking") && details["networking"].is_object()) {
                networkingPayload = details["networking"];
            }
            networkingPayload["endpoint"] = firstStringValue(details, {"endpoint", "api", "api_url", "url", "apiServer", "api_server"});
            networkingPayload["endpoint_candidates"] = endpointCandidates;
            networkingPayload["ips_in_use"] = ipsInUse;
            networkingPayload["ports_in_use"] = portsInUse;
            details["networking"] = networkingPayload;
            return details;
        }

        std::string buildTraceyStatusPath(const TraceyEndpoint& endpoint) {
            if (endpoint.basePath.empty()) {
                return "/status";
            }
            return endpoint.basePath + "/status";
        }

        std::string buildTraceyPath(const TraceyEndpoint& endpoint, const std::string& suffix) {
            const std::string normalizedSuffix = suffix.empty() || suffix.front() == '/'
                                                 ? suffix
                                                 : ("/" + suffix);
            if (endpoint.basePath.empty()) {
                return normalizedSuffix.empty() ? "/" : normalizedSuffix;
            }
            return endpoint.basePath + normalizedSuffix;
        }

        std::string getenvTrimmed(const char* key) {
            if (!key) {
                return "";
            }
            const char* raw = std::getenv(key);
            return raw ? trim(raw) : "";
        }

        std::string getenvTrimmedOr(const char* primary, const char* fallback, const std::string& defaultValue = "") {
            const std::string primaryValue = getenvTrimmed(primary);
            if (!primaryValue.empty()) {
                return primaryValue;
            }
            const std::string fallbackValue = getenvTrimmed(fallback);
            if (!fallbackValue.empty()) {
                return fallbackValue;
            }
            return defaultValue;
        }

        std::string normalizeHttpBaseUrl(std::string value) {
            value = trim(value);
            while (!value.empty() && value.back() == '/') {
                value.pop_back();
            }
            return value;
        }

        bool startsWithHttpScheme(const std::string& value) {
            return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
        }

        bool resolveOptionalRecruitGailEnvironment(std::string& baseUrlOut,
                                                   std::string& apiTokenOut,
                                                   std::string& errorOut) {
            baseUrlOut = normalizeHttpBaseUrl(getenvTrimmedOr("NMC_GAIL_BASE_URL", "GAIL_BASE_URL"));
            if (baseUrlOut.empty()) {
                baseUrlOut = normalizeHttpBaseUrl(getenvTrimmedOr("NMC_GAIL_URL", "GAIL_URL"));
            }

            apiTokenOut.clear();
            static constexpr std::array<const char*, 6> tokenKeys = {
                    "NMC_GAIL_API_TOKEN",
                    "GAIL_API_TOKEN",
                    "NMC_GAIL_BEARER_TOKEN",
                    "GAIL_BEARER_TOKEN",
                    "NMC_GAIL_AUTH_TOKEN",
                    "GAIL_AUTH_TOKEN"
            };
            for (const char* tokenKey : tokenKeys) {
                apiTokenOut = getenvTrimmed(tokenKey);
                if (!apiTokenOut.empty()) {
                    break;
                }
            }

            errorOut.clear();
            if (baseUrlOut.empty()) {
                return true;
            }
            if (!startsWithHttpScheme(baseUrlOut)) {
                errorOut = "NMC_GAIL_BASE_URL must begin with http:// or https://.";
                return false;
            }
            return true;
        }

        std::string toUpper(std::string value) {
            for (auto& ch : value) {
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }
            return value;
        }

        bool looksLikeTextPayload(const std::string& payload) {
            if (payload.empty()) {
                return true;
            }
            size_t controlChars = 0;
            for (const unsigned char ch : payload) {
                if (ch == '\n' || ch == '\r' || ch == '\t') {
                    continue;
                }
                if (ch < 0x20 || ch == 0x7f) {
                    ++controlChars;
                }
            }
            return controlChars == 0;
        }

        std::string base64Encode(const std::string& input) {
            static const char table[] =
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string output;
            output.reserve(((input.size() + 2) / 3) * 4);

            int val = 0;
            int valb = -6;
            for (const unsigned char ch : input) {
                val = (val << 8) + ch;
                valb += 8;
                while (valb >= 0) {
                    output.push_back(table[(val >> valb) & 0x3F]);
                    valb -= 6;
                }
            }
            if (valb > -6) {
                output.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
            }
            while (output.size() % 4 != 0) {
                output.push_back('=');
            }
            return output;
        }

        std::optional<nlohmann::json> findK8sItemByNameOrComponent(
                const nlohmann::json& listPayload,
                const std::string& exactName,
                const std::string& appName,
                const std::string& component
        ) {
            if (!listPayload.is_object() || !listPayload.contains("items") || !listPayload["items"].is_array()) {
                return std::nullopt;
            }

            std::optional<nlohmann::json> componentMatch;
            for (const auto& item : listPayload["items"]) {
                if (!item.is_object()) {
                    continue;
                }
                const auto metadataIt = item.find("metadata");
                if (metadataIt == item.end() || !metadataIt->is_object()) {
                    continue;
                }
                const auto& metadata = *metadataIt;
                const std::string name = metadata.value("name", "");
                if (!exactName.empty() && name == exactName) {
                    return item;
                }
                const auto labelsIt = metadata.find("labels");
                if (labelsIt == metadata.end() || !labelsIt->is_object()) {
                    continue;
                }
                const auto& labels = *labelsIt;
                const std::string labeledApp = labels.value("app.kubernetes.io/name", labels.value("app", ""));
                const std::string labeledComponent = labels.value("app.kubernetes.io/component", "");
                if (!component.empty()
                    && labeledComponent == component
                    && (appName.empty() || labeledApp.empty() || labeledApp == appName)) {
                    componentMatch = item;
                }
            }
            return componentMatch;
        }

        int extractServicePortValue(
                const nlohmann::json& servicePayload,
                const std::string& preferredPortName,
                int fallbackPort
        ) {
            const auto specIt = servicePayload.find("spec");
            if (specIt == servicePayload.end() || !specIt->is_object()) {
                return fallbackPort;
            }
            const auto portsIt = specIt->find("ports");
            if (portsIt == specIt->end() || !portsIt->is_array()) {
                return fallbackPort;
            }

            int firstPort = -1;
            for (const auto& portNode : *portsIt) {
                if (!portNode.is_object()) {
                    continue;
                }
                const int portValue = portNode.value("port", -1);
                if (portValue > 0 && firstPort <= 0) {
                    firstPort = portValue;
                }
                if (!preferredPortName.empty() && portNode.value("name", "") == preferredPortName && portValue > 0) {
                    return portValue;
                }
            }
            return firstPort > 0 ? firstPort : fallbackPort;
        }

        nlohmann::json collectServicePorts(const nlohmann::json& servicePayload) {
            nlohmann::json ports = nlohmann::json::array();
            const auto specIt = servicePayload.find("spec");
            if (specIt == servicePayload.end() || !specIt->is_object()) {
                return ports;
            }
            const auto portsIt = specIt->find("ports");
            if (portsIt == specIt->end() || !portsIt->is_array()) {
                return ports;
            }
            for (const auto& portNode : *portsIt) {
                if (!portNode.is_object()) {
                    continue;
                }
                ports.push_back({
                        {"name", portNode.value("name", "")},
                        {"port", portNode.value("port", 0)},
                        {"target_port", portNode.contains("targetPort") ? portNode["targetPort"] : nlohmann::json(nullptr)},
                        {"protocol", portNode.value("protocol", "TCP")}
                });
            }
            return ports;
        }

        nlohmann::json buildAarnnServiceSummary(
                const nlohmann::json& servicePayload,
                const std::string& component,
                const std::string& scheme,
                const std::string& urlField,
                const std::string& preferredPortName,
                int fallbackPort
        ) {
            nlohmann::json summary = {
                    {"found", false},
                    {"component", component},
                    {"source", "kubernetes"}
            };
            if (!servicePayload.is_object()) {
                return summary;
            }

            const nlohmann::json metadata = servicePayload.value("metadata", nlohmann::json::object());
            const nlohmann::json spec = servicePayload.value("spec", nlohmann::json::object());
            const std::string namespaceName = metadata.value("namespace", "");
            const std::string serviceName = metadata.value("name", "");
            const std::string dnsName = !namespaceName.empty() && !serviceName.empty()
                                        ? serviceName + "." + namespaceName + ".svc.cluster.local"
                                        : "";
            const std::string clusterIp = trim(spec.value("clusterIP", ""));
            const std::string host = (!clusterIp.empty() && clusterIp != "None") ? clusterIp : dnsName;
            const int portValue = extractServicePortValue(servicePayload, preferredPortName, fallbackPort);

            summary["found"] = !host.empty() && portValue > 0;
            summary["namespace"] = namespaceName;
            summary["service_name"] = serviceName;
            summary["cluster_ip"] = clusterIp.empty() ? nlohmann::json(nullptr) : nlohmann::json(clusterIp);
            summary["dns_name"] = dnsName.empty() ? nlohmann::json(nullptr) : nlohmann::json(dnsName);
            summary["host"] = host.empty() ? nlohmann::json(nullptr) : nlohmann::json(host);
            summary["port"] = portValue;
            summary["service"] = {
                    {"observed", true},
                    {"type", spec.value("type", "ClusterIP")},
                    {"ports", collectServicePorts(servicePayload)}
            };
            if (summary["found"].get<bool>()) {
                summary[urlField] = scheme + "://" + host + ":" + std::to_string(portValue);
            }
            return summary;
        }

        nlohmann::json buildAarnnDeploymentSummary(
                const nlohmann::json& deploymentPayload,
                const std::string& fallbackNamespace,
                const std::string& fallbackDeployment
        ) {
            int desiredReplicas = 0;
            int readyReplicas = 0;
            int availableReplicas = 0;
            std::string namespaceName = fallbackNamespace;
            std::string deploymentName = fallbackDeployment;

            if (deploymentPayload.contains("metadata") && deploymentPayload["metadata"].is_object()) {
                const auto& metadata = deploymentPayload["metadata"];
                namespaceName = metadata.value("namespace", namespaceName);
                deploymentName = metadata.value("name", deploymentName);
            }
            if (deploymentPayload.contains("spec") && deploymentPayload["spec"].is_object()) {
                desiredReplicas = deploymentPayload["spec"].value("replicas", 0);
            }
            if (deploymentPayload.contains("status") && deploymentPayload["status"].is_object()) {
                readyReplicas = deploymentPayload["status"].value("readyReplicas", 0);
                availableReplicas = deploymentPayload["status"].value("availableReplicas", 0);
            }

            bool healthy = false;
            std::string status = "Pending";
            if (desiredReplicas <= 0) {
                status = "Suspended";
                healthy = true;
            } else if (readyReplicas >= desiredReplicas && availableReplicas >= desiredReplicas) {
                status = "Running";
                healthy = true;
            } else if (readyReplicas > 0 || availableReplicas > 0) {
                status = "Degraded";
            }

            return {
                    {"observed", true},
                    {"healthy", healthy},
                    {"status", status},
                    {"namespace", namespaceName},
                    {"deployment", deploymentName},
                    {"desired_replicas", desiredReplicas},
                    {"ready_replicas", readyReplicas},
                    {"available_replicas", availableReplicas}
            };
        }

        std::string encodeQueryComponent(const std::string& value) {
            std::ostringstream encoded;
            encoded.fill('0');
            encoded << std::hex << std::uppercase;
            for (const unsigned char ch : value) {
                if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
                    encoded << static_cast<char>(ch);
                } else {
                    encoded << '%' << std::setw(2) << static_cast<int>(ch);
                }
            }
            return encoded.str();
        }

        void appendEncodedQueryParameter(std::string& path, const std::string& key, const std::string& value) {
            if (trim(value).empty()) {
                return;
            }
            path += (path.find('?') == std::string::npos) ? "?" : "&";
            path += encodeQueryComponent(key) + "=" + encodeQueryComponent(value);
        }

        bool pathHasQueryParameter(const std::string& path, const std::string& key) {
            const auto queryPos = path.find('?');
            if (queryPos == std::string::npos || queryPos + 1 >= path.size()) {
                return false;
            }
            const std::string query = path.substr(queryPos + 1);
            std::stringstream stream(query);
            std::string item;
            while (std::getline(stream, item, '&')) {
                const auto equalsPos = item.find('=');
                const std::string candidate = equalsPos == std::string::npos ? item : item.substr(0, equalsPos);
                if (candidate == key) {
                    return true;
                }
            }
            return false;
        }

        std::string sanitizeAarnnIdSegment(const std::string& value) {
            std::string sanitized;
            sanitized.reserve(value.size());
            for (const unsigned char ch : value) {
                if (std::isalnum(ch)) {
                    sanitized.push_back(static_cast<char>(std::tolower(ch)));
                } else if (!sanitized.empty() && sanitized.back() != '-') {
                    sanitized.push_back('-');
                }
            }
            while (!sanitized.empty() && sanitized.back() == '-') {
                sanitized.pop_back();
            }
            return sanitized.empty() ? "unknown" : sanitized;
        }

        std::string buildAarnnSyntheticId(const std::string& prefix, const std::string& host, int port) {
            return prefix + "-" + sanitizeAarnnIdSegment(host) + "-" + std::to_string(std::max(port, 0));
        }

        std::string normalizedUrlWithHost(const TraceyEndpoint& endpoint, const std::string& host) {
            const bool hostIsIpv6 = host.find(':') != std::string::npos;
            std::string normalized = endpoint.https ? "https://" : "http://";
            normalized += hostIsIpv6 ? "[" + host + "]" : host;
            normalized += ":" + std::to_string(endpoint.port);
            normalized += endpoint.basePath;
            return normalized;
        }

        std::string deriveLinkSecurity(bool signaturePresent, const TraceyEndpoint* endpoint, bool tlsVerify) {
            std::string linkSecurity;
            if (endpoint == nullptr) {
                linkSecurity = "announcement-only";
            } else if (endpoint->https) {
                linkSecurity = tlsVerify ? "tls-verified" : "tls-unverified";
            } else {
                linkSecurity = "plaintext";
            }
            if (signaturePresent) {
                linkSecurity += "+signed-announcement";
            } else {
                linkSecurity += "+unsigned-announcement";
            }
            return linkSecurity;
        }

        std::string mergeTraceySource(const std::string& current, const std::string& incoming) {
            std::set<std::string> tokens;
            auto collect = [&tokens](const std::string& value) {
                if (value.empty()) {
                    return;
                }
                std::stringstream ss(value);
                std::string token;
                while (std::getline(ss, token, '+')) {
                    const std::string normalized = trim(token);
                    if (!normalized.empty()) {
                        tokens.insert(normalized);
                    }
                }
            };

            collect(current);
            collect(incoming);
            if (tokens.empty()) {
                return "";
            }

            const std::vector<std::string> orderedKnown = {
                    "heartbeat",
                    "discovery",
                    "continuum-provisioning"
            };

            std::vector<std::string> ordered;
            for (const auto& known : orderedKnown) {
                auto it = tokens.find(known);
                if (it != tokens.end()) {
                    ordered.push_back(*it);
                    tokens.erase(it);
                }
            }
            for (const auto& extra : tokens) {
                ordered.push_back(extra);
            }

            std::ostringstream out;
            for (size_t i = 0; i < ordered.size(); ++i) {
                if (i > 0) {
                    out << "+";
                }
                out << ordered[i];
            }
            return out.str();
        }

        int64_t computeBackoffMs(int failures, int64_t baseMs, int64_t maxMs) {
            int64_t value = std::max<int64_t>(baseMs, 1000);
            for (int i = 1; i < failures; ++i) {
                if (value >= maxMs) {
                    break;
                }
                if (value > maxMs / 2) {
                    value = maxMs;
                    break;
                }
                value *= 2;
            }
            return std::clamp(value, std::max<int64_t>(baseMs, 1000), std::max<int64_t>(maxMs, baseMs));
        }

        int64_t parseQueryInt64(const httplib::Request& req,
                                const char* key,
                                int64_t fallback,
                                int64_t minValue,
                                int64_t maxValue) {
            if (!req.has_param(key)) {
                return fallback;
            }
            const std::string raw = trim(req.get_param_value(key));
            if (raw.empty()) {
                return fallback;
            }
            try {
                int64_t value = std::stoll(raw);
                return std::clamp(value, minValue, maxValue);
            } catch (const std::exception&) {
                return fallback;
            }
        }

        std::string normalizeLogLevel(std::string level) {
            level = toLower(trim(level));
            if (level == "warning") {
                return "warn";
            }
            if (level != "info" && level != "warn" && level != "error") {
                return "info";
            }
            return level;
        }

        struct ExecResult {
            int exitCode{1};
            std::string output;
        };

        std::string shellQuote(const std::string& value) {
            std::string quoted = "'";
            for (const char ch : value) {
                if (ch == '\'') {
                    quoted += "'\\''";
                } else {
                    quoted.push_back(ch);
                }
            }
            quoted.push_back('\'');
            return quoted;
        }

        std::string commandToShellString(const std::vector<std::string>& args) {
            std::ostringstream command;
            bool first = true;
            for (const auto& arg : args) {
                if (!first) {
                    command << ' ';
                }
                command << shellQuote(arg);
                first = false;
            }
            return command.str();
        }

        ExecResult runShellCommand(const std::vector<std::string>& args, size_t outputLimitBytes = 262144) {
            ExecResult result;
            if (args.empty()) {
                result.output = "No command specified.";
                return result;
            }

            std::string command = commandToShellString(args) + " 2>&1";
            FILE* pipe = ::popen(command.c_str(), "r");
            if (!pipe) {
                result.output = "Unable to execute command.";
                return result;
            }

            std::array<char, 4096> buffer{};
            bool outputTruncated = false;
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
                if (result.output.size() < outputLimitBytes) {
                    const size_t remaining = outputLimitBytes - result.output.size();
                    const std::string chunk(buffer.data());
                    result.output.append(chunk, 0, std::min(chunk.size(), remaining));
                    if (chunk.size() > remaining) {
                        outputTruncated = true;
                    }
                }
            }

            const int status = ::pclose(pipe);
            if (status != -1 && WIFEXITED(status)) {
                result.exitCode = WEXITSTATUS(status);
            }

            if (outputTruncated) {
                result.output += "\n...(output truncated)\n";
            }
            return result;
        }

        bool hasControlChars(const std::string& value) {
            return std::any_of(value.begin(), value.end(), [](const unsigned char ch) {
                return std::iscntrl(ch) != 0;
            });
        }

        bool isSafeHost(const std::string& host) {
            if (host.empty() || host.size() > 253 || host.front() == '-' || host.back() == '-') {
                return false;
            }
            return std::all_of(host.begin(), host.end(), [](const unsigned char ch) {
                return std::isalnum(ch) != 0 || ch == '.' || ch == '-';
            });
        }

        bool isSafeUser(const std::string& user) {
            if (user.empty() || user.size() > 64) {
                return false;
            }
            const unsigned char first = static_cast<unsigned char>(user.front());
            if (std::isalpha(first) == 0 && first != '_') {
                return false;
            }
            return std::all_of(user.begin() + 1, user.end(), [](const unsigned char ch) {
                return std::isalnum(ch) != 0 || ch == '_' || ch == '-' || ch == '.';
            });
        }

        bool isSafeLocalPath(const std::string& path) {
            if (path.empty() || path.size() > 4096 || hasControlChars(path)) {
                return false;
            }
            return path.front() == '/';
        }

        bool isSafeRemotePath(const std::string& path) {
            if (path.empty() || path.size() > 4096 || hasControlChars(path) || path.front() != '/') {
                return false;
            }
            return std::all_of(path.begin(), path.end(), [](const unsigned char ch) {
                return std::isalnum(ch) != 0 || ch == '/' || ch == '-' || ch == '_' || ch == '.';
            });
        }

        bool isKnownNodeType(const std::string& nodeType) {
            static const std::set<std::string> allowedTypes = {
                    "bare-metal",
                    "app-install",
                    "vm",
                    "virtual-machine",
                    "podman",
                    "kubernetes",
                    "openstack"
            };
            return allowedTypes.find(nodeType) != allowedTypes.end();
        }

        std::string normalizeNodeType(const std::string& rawNodeType) {
            std::string nodeType = toLower(trim(rawNodeType));
            if (nodeType.empty()) {
                nodeType = "bare-metal";
            }
            if (nodeType == "virtual-machine") {
                nodeType = "vm";
            }
            return nodeType;
        }

        bool readTextFile(const std::string& path, size_t maxBytes, std::string& contentOut, std::string& errorOut) {
            contentOut.clear();
            errorOut.clear();

            try {
                const std::filesystem::path fsPath(path);
                if (!std::filesystem::exists(fsPath) || !std::filesystem::is_regular_file(fsPath)) {
                    errorOut = "File does not exist: " + path;
                    return false;
                }
                const uintmax_t size = std::filesystem::file_size(fsPath);
                if (size > maxBytes) {
                    errorOut = "File exceeds size limit (" + std::to_string(maxBytes) + " bytes): " + path;
                    return false;
                }
            } catch (const std::exception& e) {
                errorOut = "Unable to inspect file '" + path + "': " + std::string(e.what());
                return false;
            }

            std::ifstream input(path, std::ios::binary);
            if (!input.is_open()) {
                errorOut = "Unable to open file: " + path;
                return false;
            }

            std::ostringstream oss;
            oss << input.rdbuf();
            contentOut = oss.str();
            return true;
        }

        bool writeTempFile(const std::string& content, std::string& pathOut, std::string& errorOut) {
            pathOut.clear();
            errorOut.clear();

            char tmpName[] = "/tmp/nmc-recruit-XXXXXX";
            const int fd = ::mkstemp(tmpName);
            if (fd < 0) {
                errorOut = "Failed to create temporary file.";
                return false;
            }
            ::close(fd);

            try {
                std::ofstream output(tmpName, std::ios::binary | std::ios::trunc);
                if (!output.is_open()) {
                    errorOut = "Failed to open temporary file for writing.";
                    ::unlink(tmpName);
                    return false;
                }
                output << content;
                output.close();
                std::filesystem::permissions(
                        tmpName,
                        std::filesystem::perms::owner_read |
                        std::filesystem::perms::owner_write |
                        std::filesystem::perms::owner_exec,
                        std::filesystem::perm_options::replace
                );
                pathOut = tmpName;
                return true;
            } catch (const std::exception& e) {
                errorOut = "Failed to write temporary file: " + std::string(e.what());
                ::unlink(tmpName);
                return false;
            }
        }

        std::string buildDefaultRecruitScript() {
            return R"(#!/usr/bin/env bash
set -euo pipefail
umask 077

export DEBIAN_FRONTEND=noninteractive
if command -v apt-get >/dev/null 2>&1; then
  apt-get update -y
  apt-get install -y curl ca-certificates jq
fi

mkdir -p /opt/continuum/node
cat >/opt/continuum/node/recruitment.env <<EOF
NMC_NODE_TYPE=${NMC_NODE_TYPE:-bare-metal}
NMC_NODE_REGION=${NMC_NODE_REGION:-}
NMC_NODE_NAME=${NMC_NODE_NAME:-}
NMC_CONTINUUM_URL=${NMC_CONTINUUM_URL:-}
NMC_CONTINUUM_AUTH_TOKEN=${NMC_CONTINUUM_AUTH_TOKEN:-}
NMC_GAIL_BASE_URL=${NMC_GAIL_BASE_URL:-}
NMC_GAIL_API_TOKEN=${NMC_GAIL_API_TOKEN:-}
TRACEY_AGENT_ID=${TRACEY_AGENT_ID:-}
TRACEY_STATUS_ADDR=${TRACEY_STATUS_ADDR:-}
RECRUITED_AT_UTC=$(date -u +%FT%TZ)
EOF

case "${NMC_NODE_TYPE:-bare-metal}" in
  podman)
    if command -v apt-get >/dev/null 2>&1; then
      apt-get install -y podman
    fi
    ;;
  kubernetes)
    if command -v apt-get >/dev/null 2>&1; then
      apt-get install -y apt-transport-https containerd
    fi
    ;;
  openstack)
    if command -v apt-get >/dev/null 2>&1; then
      apt-get install -y python3-openstackclient
    fi
    ;;
  vm|app-install|bare-metal)
    true
    ;;
esac

echo "ready" >/opt/continuum/node/state
echo "Continuum recruitment completed for ${NMC_NODE_NAME:-unknown-node} (${NMC_NODE_TYPE:-bare-metal})."
)";
        }

        int parsePortOrDefault(const nlohmann::json& payload, int fallback) {
            if (!payload.contains("port")) {
                return fallback;
            }
            try {
                if (payload["port"].is_number_integer()) {
                    return payload["port"].get<int>();
                }
                if (payload["port"].is_string()) {
                    return std::stoi(payload["port"].get<std::string>());
                }
            } catch (const std::exception&) {
                return -1;
            }
            return -1;
        }

        std::vector<std::string> parseBinaryArgs(const nlohmann::json& payload, std::string& errorOut) {
            std::vector<std::string> args;
            errorOut.clear();

            if (payload.contains("binary_args")) {
                const auto& value = payload["binary_args"];
                if (value.is_array()) {
                    for (const auto& item : value) {
                        if (!item.is_string()) {
                            errorOut = "binary_args entries must be strings.";
                            return {};
                        }
                        args.push_back(item.get<std::string>());
                    }
                } else if (value.is_string()) {
                    args.push_back(value.get<std::string>());
                } else {
                    errorOut = "binary_args must be a string or array of strings.";
                    return {};
                }
            }

            if (payload.contains("binary_arg")) {
                if (!payload["binary_arg"].is_string()) {
                    errorOut = "binary_arg must be a string.";
                    return {};
                }
                args.push_back(payload["binary_arg"].get<std::string>());
            }
            return args;
        }

        std::string normalizeCapability(std::string capability) {
            capability = toLower(trim(capability));
            if (capability == "app" || capability == "app-install") {
                return "apps";
            }
            if (capability == "k8s") {
                return "kubernetes";
            }
            if (capability == "virtual-machine") {
                return "vm";
            }
            if (capability == "os") {
                return "openstack";
            }
            return capability;
        }

        bool isKnownCapability(const std::string& capability) {
            static const std::set<std::string> allowed = {
                    "apps",
                    "vm",
                    "podman",
                    "kubernetes",
                    "openstack"
            };
            return allowed.find(capability) != allowed.end();
        }

        void appendUnique(std::vector<std::string>& values, const std::string& value) {
            if (value.empty()) {
                return;
            }
            if (std::find(values.begin(), values.end(), value) == values.end()) {
                values.push_back(value);
            }
        }

        bool parseCapabilitiesField(const nlohmann::json& payload,
                                    const std::string& fieldName,
                                    std::vector<std::string>& capabilities,
                                    std::string& errorOut) {
            if (!payload.contains(fieldName)) {
                return true;
            }
            const auto& value = payload[fieldName];
            if (value.is_string()) {
                std::stringstream ss(value.get<std::string>());
                std::string token;
                while (std::getline(ss, token, ',')) {
                    const std::string normalized = normalizeCapability(token);
                    if (normalized.empty()) {
                        continue;
                    }
                    if (!isKnownCapability(normalized)) {
                        errorOut = "Unsupported capability '" + token + "'.";
                        return false;
                    }
                    appendUnique(capabilities, normalized);
                }
                return true;
            }
            if (value.is_array()) {
                for (const auto& item : value) {
                    if (!item.is_string()) {
                        errorOut = fieldName + " entries must be strings.";
                        return false;
                    }
                    const std::string normalized = normalizeCapability(item.get<std::string>());
                    if (normalized.empty()) {
                        continue;
                    }
                    if (!isKnownCapability(normalized)) {
                        errorOut = "Unsupported capability '" + item.get<std::string>() + "'.";
                        return false;
                    }
                    appendUnique(capabilities, normalized);
                }
                return true;
            }
            errorOut = fieldName + " must be a string or array of strings.";
            return false;
        }

        std::vector<std::string> defaultCapabilitiesForNodeType(const std::string& nodeType) {
            if (nodeType == "kubernetes") {
                return {"kubernetes"};
            }
            if (nodeType == "podman") {
                return {"podman"};
            }
            if (nodeType == "vm") {
                return {"vm"};
            }
            if (nodeType == "app-install") {
                return {"apps"};
            }
            if (nodeType == "openstack") {
                return {"openstack"};
            }
            return {};
        }

        bool isSafeTenantId(const std::string& tenantId) {
            if (tenantId.empty()) {
                return true;
            }
            if (tenantId.size() > 128) {
                return false;
            }
            return std::all_of(tenantId.begin(), tenantId.end(), [](const unsigned char ch) {
                return std::isalnum(ch) != 0 || ch == '_' || ch == '-' || ch == '.';
            });
        }

        bool isSafeAnsibleVarKey(const std::string& key) {
            if (key.empty() || key.size() > 128) {
                return false;
            }
            const unsigned char first = static_cast<unsigned char>(key.front());
            if (std::isalpha(first) == 0 && first != '_') {
                return false;
            }
            return std::all_of(key.begin() + 1, key.end(), [](const unsigned char ch) {
                return std::isalnum(ch) != 0 || ch == '_';
            });
        }

        std::string resolveRecruitPlaybookPath(const std::string& requestedPath) {
            std::vector<std::filesystem::path> candidates;
            if (!requestedPath.empty()) {
                candidates.emplace_back(requestedPath);
            } else {
                const char* fromEnv = std::getenv("NMC_RECRUIT_ANSIBLE_PLAYBOOK");
                if (fromEnv && *fromEnv) {
                    candidates.emplace_back(std::string(fromEnv));
                }
            }

            if (candidates.empty()) {
                try {
                    std::filesystem::path cursor = std::filesystem::current_path();
                    for (int depth = 0; depth < 7; ++depth) {
                        candidates.push_back(cursor / "ansible" / "recruited-node.yml");
                        if (!cursor.has_parent_path()) {
                            break;
                        }
                        cursor = cursor.parent_path();
                    }
                } catch (const std::exception&) {
                    return "";
                }
            }

            for (const auto& candidate : candidates) {
                try {
                    std::filesystem::path absolute = candidate;
                    if (absolute.is_relative()) {
                        absolute = std::filesystem::current_path() / absolute;
                    }
                    absolute = std::filesystem::weakly_canonical(absolute);
                    if (std::filesystem::exists(absolute) && std::filesystem::is_regular_file(absolute)) {
                        return absolute.string();
                    }
                } catch (const std::exception&) {
                    continue;
                }
            }
            return "";
        }
    }

    APIRoutes::APIRoutes(httplib::Server& svr) {
        auto envOr = [](const char* primary, const char* fallback) -> const char* {
            const char* val = std::getenv(primary);
            if (val && *val) {
                return val;
            }
            val = std::getenv(fallback);
            if (val && *val) {
                return val;
            }
            return nullptr;
        };

        const char* authModeEnv = envOr("NMC_AUTH_MODE", "NM_AUTH_MODE");
        authMode = authModeEnv ? toLower(std::string(authModeEnv)) : "";
        if (authMode.empty()) {
            authMode = "token";
        }

        const char* authTokenEnv = envOr("NMC_AUTH_TOKEN", "NM_AUTH_TOKEN");
        authToken = authTokenEnv ? authTokenEnv : "";

        const char* centralSessionEnv = envOr("NMC_CENTRAL_AUTH_SESSION_URL", "NM_CENTRAL_AUTH_SESSION_URL");
        centralAuthSessionUrl = centralSessionEnv ? trim(centralSessionEnv) : "";
        const char* centralLoginEnv = envOr("NMC_CENTRAL_AUTH_LOGIN_URL", "NM_CENTRAL_AUTH_LOGIN_URL");
        centralAuthLoginUrl = centralLoginEnv ? trim(centralLoginEnv) : "";
        centralAuthCacheTtlMs = parseInt64Value(
                envOr("NMC_CENTRAL_AUTH_CACHE_TTL_MS", "NM_CENTRAL_AUTH_CACHE_TTL_MS"),
                15'000,
                1'000,
                300'000
        );
        centralAuthTimeoutMs = parseInt64Value(
                envOr("NMC_CENTRAL_AUTH_TIMEOUT_MS", "NM_CENTRAL_AUTH_TIMEOUT_MS"),
                3'000,
                500,
                120'000
        );
        centralAuthTlsVerify = parseBoolValue(
                envOr("NMC_CENTRAL_AUTH_TLS_VERIFY", "NM_CENTRAL_AUTH_TLS_VERIFY"),
                true
        );

        if (authMode == "off") {
            authRequired = false;
        } else if (authMode == "token") {
            authRequired = !authToken.empty() || !centralAuthSessionUrl.empty();
        } else {
            authRequired = true;
        }

        const char* maxBodyEnv = std::getenv("NMC_MAX_BODY_BYTES");
        maxBodyBytes = 1024 * 1024; // 1 MiB default
        if (maxBodyEnv) {
            try {
                maxBodyBytes = static_cast<size_t>(std::stoul(maxBodyEnv));
            } catch (const std::exception&) {
                maxBodyBytes = 1024 * 1024;
            }
        }

        const char* maxLogBodyEnv = std::getenv("NMC_LOG_BODY_BYTES");
        maxLogBodyBytes = 2048;
        if (maxLogBodyEnv) {
            try {
                maxLogBodyBytes = static_cast<size_t>(std::stoul(maxLogBodyEnv));
            } catch (const std::exception&) {
                maxLogBodyBytes = 2048;
            }
        }

        const char* docsEnv = std::getenv("NMC_DOCS_ENABLED");
        docsEnabled = true;
        if (docsEnv) {
            const std::string val = toLower(std::string(docsEnv));
            docsEnabled = !(val == "0" || val == "false" || val == "no");
        }

        stopTraceyDiscovery.store(false);
        stopAarnnDiscovery.store(false);
        aarnnDiscoveryEnabled = parseBoolValue(
                envOr("NMC_AARNN_DISCOVERY_ENABLED", "NM_AARNN_DISCOVERY_ENABLED"),
                true
        );
        aarnnAllowPublicAddr = parseBoolValue(
                envOr("NMC_AARNN_ALLOW_PUBLIC_ADDR", "NM_AARNN_ALLOW_PUBLIC_ADDR"),
                false
        );
        const char* aarnnBindAddrEnv = envOr("NMC_AARNN_DISCOVERY_BIND_ADDR", "NM_AARNN_DISCOVERY_BIND_ADDR");
        aarnnDiscoveryBindAddr = aarnnBindAddrEnv ? trim(aarnnBindAddrEnv) : "0.0.0.0";
        if (aarnnDiscoveryBindAddr.empty()) {
            aarnnDiscoveryBindAddr = "0.0.0.0";
        }
        aarnnDiscoveryPort = static_cast<int>(
                parseInt64Value(envOr("NMC_AARNN_DISCOVERY_PORT", "NM_AARNN_DISCOVERY_PORT"), 50050, 1, 65535)
        );
        aarnnStaleAfterSeconds = parseInt64Value(
                envOr("NMC_AARNN_STALE_SECONDS", "NM_AARNN_STALE_SECONDS"),
                15,
                4,
                86400
        );
        aarnnStatusPollMs = parseInt64Value(
                envOr("NMC_AARNN_STATUS_POLL_MS", "NM_AARNN_STATUS_POLL_MS"),
                5000,
                1000,
                600000
        );
        aarnnStatusTimeoutMs = parseInt64Value(
                envOr("NMC_AARNN_STATUS_TIMEOUT_MS", "NM_AARNN_STATUS_TIMEOUT_MS"),
                2500,
                500,
                120000
        );

        traceyStaleAfterSeconds = parseInt64Value(std::getenv("NMC_TRACEY_STALE_SECONDS"), 90, 5, 86400);
        traceyEnforceManagedResources = parseBoolValue(
                envOr("NMC_TRACEY_ENFORCE_MANAGED_RESOURCES", "NM_TRACEY_ENFORCE_MANAGED_RESOURCES"),
                true
        );
        traceyDiscoveryEnabled = parseBoolValue(envOr("NMC_TRACEY_DISCOVERY_ENABLED", "NM_TRACEY_DISCOVERY_ENABLED"), true);
        traceyAllowPublicAddr = parseBoolValue(envOr("NMC_TRACEY_ALLOW_PUBLIC_ADDR", "NM_TRACEY_ALLOW_PUBLIC_ADDR"), false);
        traceyTlsVerify = parseBoolValue(envOr("NMC_TRACEY_TLS_VERIFY", "NM_TRACEY_TLS_VERIFY"), true);
        traceyRequireSignature = parseBoolValue(envOr("NMC_TRACEY_REQUIRE_SIGNATURE", "NM_TRACEY_REQUIRE_SIGNATURE"), false);
        traceyRequirementGraceSeconds = parseInt64Value(
                envOr("NMC_TRACEY_REQUIREMENT_GRACE_SECONDS", "NM_TRACEY_REQUIREMENT_GRACE_SECONDS"),
                300,
                5,
                86400
        );
        const char* traceyBindAddrEnv = envOr("NMC_TRACEY_DISCOVERY_BIND_ADDR", "NM_TRACEY_DISCOVERY_BIND_ADDR");
        traceyDiscoveryBindAddr = traceyBindAddrEnv ? trim(traceyBindAddrEnv) : "0.0.0.0";
        if (traceyDiscoveryBindAddr.empty()) {
            traceyDiscoveryBindAddr = "0.0.0.0";
        }
        traceyDiscoveryPort = static_cast<int>(
                parseInt64Value(envOr("NMC_TRACEY_DISCOVERY_PORT", "NM_TRACEY_DISCOVERY_PORT"), 47990, 1, 65535)
        );
        traceyDiscoveryMaxAgeMs = parseInt64Value(
                envOr("NMC_TRACEY_DISCOVERY_MAX_AGE_MS", "NM_TRACEY_DISCOVERY_MAX_AGE_MS"),
                15000,
                1000,
                3600000
        );
        traceyStatusPollMs = parseInt64Value(
                envOr("NMC_TRACEY_STATUS_POLL_MS", "NM_TRACEY_STATUS_POLL_MS"),
                10000,
                1000,
                600000
        );
        traceyStatusTimeoutMs = parseInt64Value(
                envOr("NMC_TRACEY_STATUS_TIMEOUT_MS", "NM_TRACEY_STATUS_TIMEOUT_MS"),
                2500,
                500,
                120000
        );
        traceyStatusMaxBackoffMs = parseInt64Value(
                envOr("NMC_TRACEY_STATUS_MAX_BACKOFF_MS", "NM_TRACEY_STATUS_MAX_BACKOFF_MS"),
                120000,
                traceyStatusPollMs,
                3600000
        );
        traceyHistoryMaxSamples = static_cast<size_t>(parseInt64Value(
                envOr("NMC_TRACEY_HISTORY_MAX_SAMPLES", "NM_TRACEY_HISTORY_MAX_SAMPLES"),
                1440,
                60,
                200000
        ));
        traceyAgentLogMaxEntries = static_cast<size_t>(parseInt64Value(
                envOr("NMC_TRACEY_AGENT_LOG_MAX_ENTRIES", "NM_TRACEY_AGENT_LOG_MAX_ENTRIES"),
                400,
                50,
                50000
        ));
        const char* traceyStatusTokenEnv = envOr("NMC_TRACEY_STATUS_BEARER_TOKEN", "NM_TRACEY_STATUS_BEARER_TOKEN");
        traceyStatusBearerToken = traceyStatusTokenEnv ? trim(traceyStatusTokenEnv) : "";

        const bool traceyBootstrapLocalAgent = parseBoolValue(
                envOr("NMC_TRACEY_BOOTSTRAP_LOCAL_AGENT", "NM_TRACEY_BOOTSTRAP_LOCAL_AGENT"),
                true
        );
        const char* localAgentIdEnv = envOr("NMC_TRACEY_LOCAL_AGENT_ID", "NM_TRACEY_LOCAL_AGENT_ID");
        const std::string localTraceyAgentId = localAgentIdEnv ? trim(localAgentIdEnv) : "tracey-continuum-local";
        traceyLocalAgentId = localTraceyAgentId;
        const char* localStatusAddrEnv = envOr("NMC_TRACEY_LOCAL_STATUS_ADDR", "NM_TRACEY_LOCAL_STATUS_ADDR");
        std::string localTraceyStatusAddr = localStatusAddrEnv ? trim(localStatusAddrEnv) : "http://127.0.0.1:48000";
        if (!localTraceyStatusAddr.empty()) {
            TraceyEndpoint endpoint;
            if (!parseTraceyEndpoint(localTraceyStatusAddr, endpoint) ||
                !isLocalOrPrivateHost(endpoint.host, traceyAllowPublicAddr)) {
                std::cerr << "[WARN] Ignoring invalid local Tracey status address '" << localTraceyStatusAddr
                          << "' for bootstrap sidecar requirement." << std::endl;
                localTraceyStatusAddr.clear();
            } else {
                localTraceyStatusAddr = endpoint.normalized;
            }
        }
        if (traceyBootstrapLocalAgent && !localTraceyAgentId.empty()) {
            std::lock_guard<std::mutex> lock(dataMutex);
            const int64_t nowMs = nowEpochMs();
            upsertTraceyRequirementLocked(
                    "continuum_server",
                    "nmc_server",
                    "local",
                    localTraceyAgentId,
                    localTraceyStatusAddr,
                    nowMs
            );
            auto& localAgent = traceyAgents[localTraceyAgentId];
            localAgent.cluster = "continuum-local";
            localAgent.host = "localhost";
            localAgent.status = localAgent.status.empty() ? "unknown" : localAgent.status;
            localAgent.linkState = "required";
            if (localAgent.metrics.is_null() || !localAgent.metrics.is_object()) {
                localAgent.metrics = nlohmann::json::object();
            }
            localAgent.metrics["bootstrap_managed"] = true;
            localAgent.metrics["monitors"] = {"nmc_server"};
            localAgent.metrics["bootstrap_epoch_ms"] = nowMs;
            if (localTraceyStatusAddr.empty()) {
                localAgent.lastError = "Bootstrap Tracey sidecar is required but no local status_addr is configured.";
            }
        }

        if (authMode == "oidc") {
            OIDCConfig cfg;
            const char* introspection = envOr("NMC_OIDC_INTROSPECTION_URL", "NM_OIDC_INTROSPECTION_URL");
            cfg.introspectionUrl = introspection ? introspection : "";
            const char* issuer = envOr("NMC_OIDC_ISSUER", "NM_OIDC_ISSUER");
            cfg.issuer = issuer ? issuer : "";
            const char* clientId = envOr("NMC_OIDC_CLIENT_ID", "NM_OIDC_CLIENT_ID");
            cfg.clientId = clientId ? clientId : "";
            const char* clientSecret = envOr("NMC_OIDC_CLIENT_SECRET", "NM_OIDC_CLIENT_SECRET");
            cfg.clientSecret = clientSecret ? clientSecret : "";

            auto appendCsv = [&cfg](const char* raw, const auto& trimFn) {
                if (!raw) return;
                std::stringstream ss(raw);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    const std::string trimmed = trimFn(item);
                    if (!trimmed.empty()) {
                        cfg.audiences.push_back(trimmed);
                    }
                }
            };

            appendCsv(envOr("NMC_OIDC_AUDIENCE", "NM_OIDC_AUDIENCE"), trim);
            appendCsv(envOr("NMC_OIDC_ALLOWED_AUDIENCES", "NM_OIDC_ALLOWED_AUDIENCES"), trim);
            appendCsv(envOr("NMC_OIDC_AUDIENCES", "NM_OIDC_AUDIENCES"), trim);

            const char* requiredScope = envOr("NMC_OIDC_REQUIRED_SCOPE", "NM_OIDC_REQUIRED_SCOPE");
            if (requiredScope) {
                std::stringstream ss(requiredScope);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    const std::string trimmed = trim(item);
                    if (!trimmed.empty()) {
                        cfg.requiredScopes.push_back(trimmed);
                    }
                }
            }

            const char* requiredScopesCsv = envOr("NMC_OIDC_REQUIRED_SCOPES", "NM_OIDC_REQUIRED_SCOPES");
            if (requiredScopesCsv) {
                std::stringstream ss(requiredScopesCsv);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    const std::string trimmed = trim(item);
                    if (!trimmed.empty()) {
                        cfg.requiredScopes.push_back(trimmed);
                    }
                }
            }
            oidcValidator = std::make_unique<OIDCValidator>(cfg);
        }

        // Initialize K8sHandlers, passing necessary references and callbacks
        const char* kubeconfigEnv = envOr("KUBECONFIG", "NMC_KUBECONFIG");
        const std::string kubeconfigPath = kubeconfigEnv ? kubeconfigEnv : "";

        std::string api_server_url_value;
        const char* explicitApiUrl = envOr("NMC_K8S_API_URL", "NM_K8S_API_URL");
        if (explicitApiUrl && *explicitApiUrl) {
            api_server_url_value = explicitApiUrl;
        } else {
            const char* k8sHost = std::getenv("KUBERNETES_SERVICE_HOST");
            const char* k8sPort = std::getenv("KUBERNETES_SERVICE_PORT");
            if (k8sHost && *k8sHost) {
                api_server_url_value = "https://" + std::string(k8sHost) + ":" + (k8sPort && *k8sPort ? std::string(k8sPort) : "443");
            } else {
                api_server_url_value = "https://127.0.0.1:6443";
            }
        }

        k8sHandlers = std::make_unique<K8sHandlers>(
                api_server_url_value,
                kubeconfigPath,
                dataMutex,
                vclusterConfigs,
                [this](httplib::Response& res, const Models::CloudResponse& apiResponse) {
                    sendJsonResponse(res, apiResponse);
                },
                [this](httplib::Response& res, int status, const std::string& message) {
                    sendErrorResponse(res, status, message);
                }
        );

        // OpenShift portal API base URL (FastAPI in oshift). Defaults to localhost:8000.
        const char* osUrlEnv = std::getenv("NMC_OSHIFT_API_URL");
        std::string osUrl = osUrlEnv ? osUrlEnv : "http://127.0.0.1:8000";
        openShiftClient = std::make_unique<OpenShiftClient>(osUrl);
        // OpenStack portal API base URL. Defaults to the OpenShift URL so a single
        // multi-provider backend can satisfy both routes when desired.
        const char* openStackUrlEnv = std::getenv("NMC_OPENSTACK_API_URL");
        std::string openStackUrl = openStackUrlEnv ? openStackUrlEnv : osUrl;
        openStackClient = std::make_unique<OpenStackClient>(openStackUrl);
        // Proxmox portal API base URL. Defaults to the OpenStack URL so the same
        // unified provider backend can satisfy all portal routes when desired.
        const char* proxmoxUrlEnv = std::getenv("NMC_PROXMOX_API_URL");
        std::string proxmoxUrl = proxmoxUrlEnv ? proxmoxUrlEnv : openStackUrl;
        proxmoxClient = std::make_unique<ProxmoxClient>(proxmoxUrl);

        // Enable logging of all requests
        svr.set_logger([this](const httplib::Request& req, const httplib::Response& res) {
            logRequest(req, res);
        });

        if (docsEnabled) {
            // Set up static file serving for documentation
            // Assumes a 'docs' directory exists relative to the executable
            // or where the server is run from.
            // This is a robust way to serve static content like documentation.
            // All requests to /docs/* will look for files in the ./docs/ directory.
            // E.g., /docs/index.html will serve ./docs/index.html
            // /docs/bucket.html will serve ./docs/bucket.html
            svr.set_base_dir("./docs"); // Set the base directory for static files

            // --- Documentation Route ---
            // This route will serve the index.html from the docs directory
            // when /docs is accessed directly.
            // Other files like /docs/bucket.html will be served automatically
            // by set_base_dir.
            svr.Get("/", [](const httplib::Request& req, httplib::Response& res) {
                res.set_content(Utils::readFile("./docs/index.html"), "text/html");
            });
            svr.Get("/index.html", [](const httplib::Request& req, httplib::Response& res) {
                res.set_content(Utils::readFile("./docs/index.html"), "text/html");
            });
            svr.Get("/docs", [](const httplib::Request& req, httplib::Response& res) {
                res.set_content(Utils::readFile("./docs/index.html"), "text/html");
            });
            svr.Get("/login", [](const httplib::Request& req, httplib::Response& res) {
                res.set_content(Utils::readFile("./docs/login.html"), "text/html");
            });
            svr.Post("/auth/login", [this](const httplib::Request& req, httplib::Response& res) {
                handleAuthLogin(req, res);
            });
            svr.Get("/auth/session", [this](const httplib::Request& req, httplib::Response& res) {
                handleAuthSession(req, res);
            });
            // Legacy launch path used by neuralmimicry.ai control-panel links.
            svr.Get(R"(^/services/health/monitoring/?$)", [](const httplib::Request& req, httplib::Response& res) {
                res.set_content(Utils::readFile("./docs/index.html"), "text/html");
            });
            svr.Get(R"(^/services/health/monitoring/index\.html$)", [](const httplib::Request& req, httplib::Response& res) {
                res.set_content(Utils::readFile("./docs/index.html"), "text/html");
            });
            svr.Get(R"(^/services/health/monitoring/login/?$)", [](const httplib::Request& req, httplib::Response& res) {
                res.set_content(Utils::readFile("./docs/login.html"), "text/html");
            });
            svr.Post(R"(^/services/health/monitoring/auth/login/?$)", [this](const httplib::Request& req, httplib::Response& res) {
                handleAuthLogin(req, res);
            });
            svr.Get(R"(^/services/health/monitoring/auth/session/?$)", [this](const httplib::Request& req, httplib::Response& res) {
                handleAuthSession(req, res);
            });
            svr.Get("/logout", [](const httplib::Request& req, httplib::Response& res) {
                res.set_redirect("/login");
            });
        }

        auto guard = [this](const httplib::Request& req, httplib::Response& res) -> bool {
            ensureRequestId(req, res);
            if (!enforceBodyLimit(req, res)) {
                return false;
            }
            if (!authorizeOrReject(req, res)) {
                return false;
            }
            return true;
        };

        // --- Unauthenticated Health ---
        // Lightweight probe for CLI connection checks and external monitors.
        svr.Get("/health", [this](const httplib::Request& req, httplib::Response& res) {
            ensureRequestId(req, res);
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "nmc_server is healthy.";
            apiResponse.data = {
                    {"status", "ok"},
                    {"service", "nmc_server"}
            };
            sendJsonResponse(res, apiResponse);
        });

        // --- Server Metadata ---
        svr.Get("/server/version", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleServerVersion(req, res);
        });

        // --- Bucket Routes ---
        svr.Post("/bucket/create", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleCreateBucket(req, res);
        });
        svr.Delete(R"(/bucket/delete/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleDeleteBucket(req, res);
        });
        svr.Get(R"(/bucket/get/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGetBucket(req, res);
        });
        svr.Get("/bucket/list", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListBuckets(req, res);
        });
        svr.Get("/bucket/list-locations", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListBucketLocations(req, res);
        });
        svr.Get("/bucket/list-types", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListBucketTypes(req, res);
        });
        svr.Post(R"(/bucket/reset-key/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleResetBucketKey(req, res);
        });

        // --- Connection Routes ---
        // GET /connections/status
        svr.Get("/connections/status", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            ConnectionHandlers::handleGetConnectionStatus(req, res);
        });

        // POST /connections/make
        svr.Post("/connections/make", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            ConnectionHandlers::handleMakeConnection(req, res);
        });

        // DELETE /connections/:name
        // svr.Delete(R"(/connections/(?P<name>[^/]+))", ConnectionHandlers::handleDropConnection);
        svr.Delete(R"(^/connections/([^/]+)$)",  [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            ConnectionHandlers::handleDropConnection(req, res);
        });

        // GET /connections
        svr.Get("/connections", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            ConnectionHandlers::handleListConnections(req, res);
        });

        // POST /connections/select
        svr.Post("/connections/select", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            ConnectionHandlers::handleSelectConnection(req, res);
        });

        std::cout << "Connection API routes registered." << std::endl;

        // --- K8s Routes ---
        svr.Post("/k8s/create", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            std::string clusterName = "unknown";
            std::string region;
            std::string traceyAgentId;
            std::string traceyStatusAddr;
            try {
                auto jsonBody = nlohmann::json::parse(req.body);
                clusterName = jsonBody.value("name", clusterName);
                region = jsonBody.value("region", "");
                std::string reason;
                if (!extractTraceyProvisioningRequest(jsonBody, traceyAgentId, traceyStatusAddr, reason)) {
                    return sendErrorResponse(res, 400, reason);
                }
            } catch (const nlohmann::json::parse_error& e) {
                return sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
            }
            k8sHandlers->handleCreateK8sCluster(req, res);
            if (res.status >= 200 && res.status < 300 && !traceyAgentId.empty()) {
                std::lock_guard<std::mutex> lock(dataMutex);
                upsertTraceyRequirementLocked("k8s_cluster", clusterName, region, traceyAgentId, traceyStatusAddr, nowEpochMs());
            }
        });
        svr.Delete(R"(/k8s/delete/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            const std::string clusterId = req.matches.size() > 1 ? req.matches[1].str() : "";
            k8sHandlers->handleDeleteK8sCluster(req, res);
            if (res.status >= 200 && res.status < 300 && !clusterId.empty()) {
                std::lock_guard<std::mutex> lock(dataMutex);
                removeTraceyRequirementLocked("k8s_cluster", clusterId);
            }
        });
        svr.Get(R"(/k8s/get/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetK8sCluster(req, res);
        });
        svr.Get(R"(/k8s/details/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetK8sClusterDetails(req, res);
        });
        svr.Get(R"(/k8s/get-config/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetKubeConfig(req, res);
        });
        svr.Get("/k8s/list", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleListK8sClusters(req, res);
        });
        svr.Get("/k8s/list-locations", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleListK8sLocations(req, res);
        });
        svr.Get("/k8s/healthz", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleK8sHealthCheck(req, res);
        });
        svr.Get("/k8s/refiner/status", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetRefinerStatus(req, res);
        });
        svr.Post("/k8s/refiner/scale", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleScaleRefiner(req, res);
        });
        svr.Post(R"(/k8s/resume/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleResumeK8sCluster(req, res);
        });
        svr.Post(R"(/k8s/suspend/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleSuspendK8sCluster(req, res);
        });

        // --- AARNN Routes ---
        svr.Get("/aarnn/endpoints", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleAarnnEndpoints(req, res);
        });
        svr.Get("/aarnn/inventory", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleAarnnInventory(req, res);
        });
        svr.Post(R"(^/aarnn/proxy/([^/]+)$)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            const std::string plane = req.matches.size() > 1 ? req.matches[1].str() : "";
            handleAarnnProxy(req, res, plane);
        });

        // --- VCluster Routes ---
        svr.Post("/vcluster/create", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleCreateVCluster(req, res);
        });
        svr.Delete(R"(/vcluster/delete/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleDeleteVCluster(req, res);
        });
        svr.Get(R"(/vcluster/get/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetVCluster(req, res);
        });
        svr.Get("/vcluster/list", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleListVClusters(req, res);
        });
        svr.Get(R"(/vcluster/get-config/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetVClusterKubeConfig(req, res);
        });

        // --- VCluster Lifecycle Routes ---
        svr.Post(R"(/vcluster/pause/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handlePauseVCluster(req, res);
        });
        svr.Post(R"(/vcluster/resume/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleResumeVCluster(req, res);
        });
        svr.Post(R"(/vcluster/backup/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleBackupVCluster(req, res);
        });
        svr.Post("/vcluster/restore", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleRestoreVCluster(req, res);
        });
        svr.Post(R"(/vcluster/upgrade/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleUpgradeVCluster(req, res);
        });

        // --- VCluster Configuration Routes ---
        svr.Get(R"(/vcluster/config/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetVClusterConfig(req, res);
        });
        svr.Put(R"(/vcluster/config/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleUpdateVClusterConfig(req, res);
        });

        // --- VCluster Monitoring Routes ---
        svr.Get(R"(/vcluster/metrics/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetVClusterMetrics(req, res);
        });
        svr.Get(R"(/vcluster/health/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetVClusterHealth(req, res);
        });
        svr.Get(R"(/vcluster/resources/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetVClusterResources(req, res);
        });

        // --- Model Routes ---
        svr.Post("/model/upload", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleUploadModel(req, res);
        });

        // --- SSH Routes ---
        svr.Post("/ssh/create", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleCreateSSHKey(req, res);
        });
        svr.Delete(R"(/ssh/delete/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleDeleteSSHKey(req, res);
        });
        svr.Get(R"(/ssh/get/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGetSSHKey(req, res);
        });
        svr.Get("/ssh/list", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListSSHKeys(req, res);
        });

        // --- VM Routes ---
        svr.Post("/vm/create", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleCreateVM(req, res);
        });
        svr.Delete(R"(/vm/delete/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleDeleteVM(req, res);
        });
        svr.Get(R"(/vm/get/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGetVM(req, res);
        });
        svr.Get("/vm/list", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListVMs(req, res);
        });
        svr.Get("/vm/list-locations", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListVMLocations(req, res);
        });
        svr.Get("/vm/list-os", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListVMOSImages(req, res);
        });
        svr.Get("/vm/list-sku", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListVMSKUs(req, res);
        });
        svr.Post(R"(/vm/restart/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleRestartVM(req, res);
        });
        svr.Post(R"(/vm/resume/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleResumeVM(req, res);
        });
        svr.Post(R"(/vm/suspend/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleSuspendVM(req, res);
        });

        // --- OpenShift Routes ---
        svr.Get("/openshift/resources", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenShiftResources(req, res);
        });
        svr.Get("/openshift/clusters", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenShiftClusters(req, res);
        });
        svr.Get(R"(/openshift/details/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenShiftClusterDetails(req, res);
        });
        svr.Post("/openshift/clusters/request", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenShiftRequestCluster(req, res);
        });

        // --- OpenStack Routes ---
        svr.Get("/openstack/resources", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenStackResources(req, res);
        });
        svr.Get("/openstack/clusters", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenStackClusters(req, res);
        });
        svr.Get(R"(/openstack/details/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenStackClusterDetails(req, res);
        });
        svr.Post("/openstack/clusters/request", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenStackRequestCluster(req, res);
        });

        // --- Proxmox Routes ---
        svr.Get("/proxmox/resources", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleProxmoxResources(req, res);
        });
        svr.Get("/proxmox/clusters", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleProxmoxClusters(req, res);
        });
        svr.Get(R"(/proxmox/details/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleProxmoxClusterDetails(req, res);
        });
        svr.Post("/proxmox/clusters/request", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleProxmoxRequestCluster(req, res);
        });

        // --- Tracey Agent Routes ---
        svr.Post("/tracey/agents/heartbeat", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyHeartbeat(req, res);
        });
        svr.Get("/tracey/agents", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListTraceyAgents(req, res);
        });
        svr.Get("/tracey/analytics", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAnalytics(req, res);
        });
        svr.Get("/tracey/fleet", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyFleet(req, res);
        });
        svr.Get("/tracey/adaptive", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAdaptive(req, res);
        });
        svr.Get("/tracey/cve/status", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyCveStatus(req, res);
        });
        svr.Get("/tracey/assessment/fleet", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAssessmentFleet(req, res);
        });
        svr.Get("/tracey/racks", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListTraceyRacks(req, res);
        });
        svr.Get(R"(/tracey/racks/([^/]+))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyRackDetails(req, res);
        });
        svr.Get(R"(/tracey/agents/(.*)/analysis)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAgentAnalysis(req, res);
        });
        svr.Get(R"(/tracey/agents/([^/]+)/server)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAgentServer(req, res);
        });
        svr.Get(R"(/tracey/agents/([^/]+)/gpus/([^/]+)/telemetry)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAgentGpu(req, res);
        });
        svr.Get(R"(/tracey/agents/(.*)/assessment/plan)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAssessmentPlan(req, res);
        });
        svr.Post(R"(/tracey/agents/(.*)/assessment/report)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAssessmentReport(req, res);
        });
        svr.Get(R"(/tracey/agents/(.*)/compromise)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAgentCompromise(req, res);
        });
        svr.Post(R"(/tracey/agents/(.*)/control)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAgentControl(req, res);
        });
        svr.Get(R"(/tracey/agents/(.*)/deepdive)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAgentDeepDive(req, res);
        });

        // --- Node Recruitment Routes ---
        svr.Post("/node/recruit", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleRecruitNode(req, res);
        });

        if (traceyDiscoveryEnabled) {
            try {
                traceyDiscoveryThread = std::thread(&APIRoutes::runTraceyDiscoveryLoop, this);
            } catch (const std::exception& e) {
                traceyDiscoveryEnabled = false;
                std::cerr << "[WARN] Failed to start Tracey discovery thread: " << e.what() << std::endl;
            }
        }
        if (aarnnDiscoveryEnabled) {
            try {
                aarnnDiscoveryThread = std::thread(&APIRoutes::runAarnnDiscoveryLoop, this);
            } catch (const std::exception& e) {
                aarnnDiscoveryEnabled = false;
                std::cerr << "[WARN] Failed to start AARNN discovery thread: " << e.what() << std::endl;
            }
        }

        traceyCveIntel.start();
    }

    // Explicit definition of the destructor for APIRoutes
    // This is crucial because APIRoutes contains a std::unique_ptr to K8sHandlers,
    // and K8sHandlers is only forward-declared in APIRoutes.h.
    // The default destructor for unique_ptr needs the full definition of the type
    // it manages when it's instantiated, which happens when the APIRoutes destructor
    // is defined.
    APIRoutes::~APIRoutes() {
        stopTraceyDiscovery.store(true);
        stopAarnnDiscovery.store(true);
        traceyCveIntel.stop();
        if (traceyDiscoveryThread.joinable()) {
            traceyDiscoveryThread.join();
        }
        if (aarnnDiscoveryThread.joinable()) {
            aarnnDiscoveryThread.join();
        }
    }

    /**
     * @brief Logs incoming HTTP requests to standard output.
     *
     * This method provides basic logging for each request received by the server,
     * including the HTTP method, path, and request body if present.
     *
     * @param req The httplib::Request object representing the incoming request.
     */
    void APIRoutes::logRequest(const httplib::Request& req, const httplib::Response& res) const {
        const std::string requestId = res.get_header_value("X-Request-ID").empty()
                ? req.get_header_value("X-Request-ID")
                : res.get_header_value("X-Request-ID");

        std::cout << "[" << req.method << "] " << req.path;
        if (!requestId.empty()) {
            std::cout << " request_id=" << requestId;
        }
        if (!req.body.empty() && shouldLogBody(req.path)) {
            std::cout << " Body: " << redactBody(req.body);
        } else if (!req.body.empty()) {
            std::cout << " Body: [redacted]";
        }
        std::cout << std::endl;
    }

    /**
     * @brief Sends a JSON response based on a Models::CloudResponse object.
     *
     * Sets the Content-Type header to application/json, determines the HTTP status
     * (200 for success, 400 for client error based on apiResponse.success), and
     * sets the response body to a pretty-printed JSON string.
     *
     * @param res The httplib::Response object to send the response through.
     * @param apiResponse The Models::CloudResponse object containing success status,
     * message, and data to be converted to JSON.
     */
    void APIRoutes::sendJsonResponse(httplib::Response& res, const Models::CloudResponse& apiResponse) const {
        res.set_header("Content-Type", "application/json");
        res.status = apiResponse.success ? 200 : 400; // 200 for success, 400 for client error
        res.body = apiResponse.toJsonString().dump(4); // Pretty print JSON
    }

    /**
     * @brief Sends an error JSON response.
     *
     * Creates a Models::CloudResponse object with success set to false,
     * the provided message, and an empty JSON data object. It then calls
     * sendJsonResponse to format and send this error response, finally
     * setting the HTTP status code explicitly.
     *
     * @param res The httplib::Response object to send the error response through.
     * @param status The HTTP status code for the error (e.g., 400, 404, 500).
     * @param message A string describing the error.
     */
    void APIRoutes::sendErrorResponse(httplib::Response& res, int status, const std::string& message) const {
        Models::CloudResponse apiResponse;
        apiResponse.success = false;
        apiResponse.message = message;
        apiResponse.data = nlohmann::json({});
        sendJsonResponse(res, apiResponse);
        res.status = status; // Set HTTP status code for error
    }

    void APIRoutes::ensureRequestId(const httplib::Request& req, httplib::Response& res) const {
        const std::string existing = req.get_header_value("X-Request-ID");
        if (!existing.empty()) {
            res.set_header("X-Request-ID", existing);
            return;
        }
        res.set_header("X-Request-ID", Utils::generateUniqueId("req"));
    }

    bool APIRoutes::enforceBodyLimit(const httplib::Request& req, httplib::Response& res) const {
        if (req.body.size() > maxBodyBytes) {
            sendErrorResponse(res, 413, "Request body exceeds maximum allowed size.");
            return false;
        }
        return true;
    }

    std::string APIRoutes::extractAuthToken(const httplib::Request& req) const {
        const std::string bearer = trim(req.get_header_value("Authorization"));
        if (!bearer.empty()) {
            const std::string prefix = "Bearer ";
            if (bearer.rfind(prefix, 0) == 0) {
                return trim(bearer.substr(prefix.size()));
            }
        }
        return trim(req.get_header_value("X-NMC-Token"));
    }

    nlohmann::json APIRoutes::centralAuthClaimsJson(const CentralAuthCacheEntry& entry) const {
        return {
                {"authenticated", entry.authenticated},
                {"user", entry.user},
                {"role", entry.role},
                {"groups", entry.groups},
                {"active_team", entry.activeTeam.is_null() ? nlohmann::json(nullptr) : entry.activeTeam},
                {"team_count", entry.teamCount},
                {"pending_invitation_count", entry.pendingInvitationCount}
        };
    }

    bool APIRoutes::validateCentralAuthToken(const std::string& token, nlohmann::json* claimsOut) const {
        const std::string trimmedToken = trim(token);
        if (trimmedToken.empty() || centralAuthSessionUrl.empty()) {
            return false;
        }

        const int64_t nowMs = nowEpochMs();
        {
            std::lock_guard<std::mutex> lock(centralAuthCacheMutex);
            auto it = centralAuthTokenCache.find(trimmedToken);
            if (it != centralAuthTokenCache.end()) {
                if (it->second.expiresAtMs > nowMs) {
                    if (it->second.authenticated && !it->second.user.empty() && claimsOut) {
                        *claimsOut = centralAuthClaimsJson(it->second);
                    }
                    return it->second.authenticated && !it->second.user.empty();
                }
                centralAuthTokenCache.erase(it);
            }
        }

        TraceyEndpoint endpoint;
        if (!parseTraceyEndpoint(centralAuthSessionUrl, endpoint)) {
            return false;
        }
        const std::string sessionPath = endpoint.basePath.empty() ? "/api/session" : endpoint.basePath;
        const int timeoutSec = static_cast<int>(std::max<int64_t>(1, (centralAuthTimeoutMs + 999) / 1000));
        httplib::Headers headers{
                {"Accept", "application/json"},
                {"Authorization", "Bearer " + trimmedToken}
        };

        httplib::Result result;
        if (endpoint.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient client(endpoint.host, endpoint.port);
            client.enable_server_certificate_verification(centralAuthTlsVerify);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Get(sessionPath.c_str(), headers);
#else
            return false;
#endif
        } else {
            httplib::Client client(endpoint.host, endpoint.port);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Get(sessionPath.c_str(), headers);
        }

        nlohmann::json payload = nlohmann::json::object();
        auto parseGroups = [this](const nlohmann::json& value) {
            std::vector<std::string> groups;
            auto pushGroup = [&groups, this](const std::string& candidate) {
                const std::string cleaned = trim(candidate);
                if (cleaned.empty()) {
                    return;
                }
                const std::string lowered = toLower(cleaned);
                if (std::find(groups.begin(), groups.end(), lowered) != groups.end()) {
                    return;
                }
                groups.push_back(lowered);
            };
            if (value.is_string()) {
                std::stringstream stream(value.get<std::string>());
                std::string item;
                while (std::getline(stream, item, ',')) {
                    pushGroup(item);
                }
                return groups;
            }
            if (value.is_array()) {
                for (const auto& item: value) {
                    if (item.is_string()) {
                        pushGroup(item.get<std::string>());
                    }
                }
            }
            return groups;
        };
        auto parseCount = [](const nlohmann::json& value) -> int64_t {
            if (value.is_number_integer()) {
                return value.get<int64_t>();
            }
            if (value.is_number_unsigned()) {
                return static_cast<int64_t>(value.get<uint64_t>());
            }
            if (value.is_string()) {
                try {
                    return std::stoll(value.get<std::string>());
                } catch (...) {
                    return 0;
                }
            }
            return 0;
        };
        auto normalizeActiveTeam = [this](const nlohmann::json& value) {
            if (value.is_null()) {
                return nlohmann::json(nullptr);
            }
            if (value.is_string()) {
                const std::string teamId = trim(value.get<std::string>());
                if (teamId.empty()) {
                    return nlohmann::json(nullptr);
                }
                return nlohmann::json{{"team_id", teamId}};
            }
            if (!value.is_object()) {
                return nlohmann::json(nullptr);
            }
            auto normalized = value;
            std::string teamId;
            if (normalized.contains("team_id") && normalized["team_id"].is_string()) {
                teamId = trim(normalized["team_id"].get<std::string>());
            } else if (normalized.contains("id") && normalized["id"].is_string()) {
                teamId = trim(normalized["id"].get<std::string>());
            }
            if (teamId.empty()) {
                return nlohmann::json(nullptr);
            }
            normalized["team_id"] = teamId;
            return normalized;
        };
        bool authenticated = false;
        std::string user;
        std::string role;
        if (result && result->status < 500 && !result->body.empty()) {
            const auto parsed = nlohmann::json::parse(result->body, nullptr, false);
            if (!parsed.is_discarded() && parsed.is_object()) {
                payload = parsed;
                authenticated = payload.value("authenticated", false);
                user = trim(payload.value("user", ""));
                role = trim(payload.value("role", ""));
            }
        }

        CentralAuthCacheEntry cacheEntry;
        cacheEntry.authenticated = authenticated && !user.empty();
        cacheEntry.user = user;
        cacheEntry.role = role;
        if (payload.contains("groups")) {
            cacheEntry.groups = parseGroups(payload["groups"]);
        }
        if (!cacheEntry.role.empty()
            && std::find(cacheEntry.groups.begin(), cacheEntry.groups.end(), cacheEntry.role) == cacheEntry.groups.end()) {
            cacheEntry.groups.insert(cacheEntry.groups.begin(), cacheEntry.role);
        }
        if (payload.contains("active_team")) {
            cacheEntry.activeTeam = normalizeActiveTeam(payload["active_team"]);
        }
        if (payload.contains("team_count")) {
            cacheEntry.teamCount = std::max<int64_t>(0, parseCount(payload["team_count"]));
        } else if (!cacheEntry.activeTeam.is_null()) {
            cacheEntry.teamCount = 1;
        }
        if (payload.contains("pending_invitation_count")) {
            cacheEntry.pendingInvitationCount = std::max<int64_t>(0, parseCount(payload["pending_invitation_count"]));
        }
        cacheEntry.expiresAtMs = nowMs + (cacheEntry.authenticated
                                          ? centralAuthCacheTtlMs
                                          : std::min<int64_t>(centralAuthCacheTtlMs, 2000));
        {
            std::lock_guard<std::mutex> lock(centralAuthCacheMutex);
            if (centralAuthTokenCache.size() > 1024) {
                centralAuthTokenCache.clear();
            }
            centralAuthTokenCache[trimmedToken] = cacheEntry;
        }

        if (cacheEntry.authenticated && claimsOut) {
            *claimsOut = centralAuthClaimsJson(cacheEntry);
        }
        return cacheEntry.authenticated;
    }

    void APIRoutes::handleAuthLogin(const httplib::Request& req, httplib::Response& res) {
        ensureRequestId(req, res);
        if (!enforceBodyLimit(req, res)) {
            return;
        }
        if (centralAuthLoginUrl.empty()) {
            sendErrorResponse(res, 503, "Central username/password login is not configured.");
            return;
        }

        const auto payload = nlohmann::json::parse(req.body, nullptr, false);
        if (payload.is_discarded() || !payload.is_object()) {
            sendErrorResponse(res, 400, "Invalid JSON payload.");
            return;
        }
        const std::string username = trim(payload.value("username", ""));
        const std::string password = trim(payload.value("password", ""));
        if (username.empty() || password.empty()) {
            sendErrorResponse(res, 400, "Username and password are required.");
            return;
        }

        TraceyEndpoint endpoint;
        if (!parseTraceyEndpoint(centralAuthLoginUrl, endpoint)) {
            sendErrorResponse(res, 503, "Central username/password login endpoint is invalid.");
            return;
        }
        const std::string loginPath = endpoint.basePath.empty() ? "/api/login" : endpoint.basePath;
        const int timeoutSec = static_cast<int>(std::max<int64_t>(1, (centralAuthTimeoutMs + 999) / 1000));
        httplib::Headers headers{
                {"Accept", "application/json"},
                {"Content-Type", "application/json"}
        };

        httplib::Result result;
        if (endpoint.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient client(endpoint.host, endpoint.port);
            client.enable_server_certificate_verification(centralAuthTlsVerify);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Post(loginPath.c_str(), headers, payload.dump(), "application/json");
#else
            sendErrorResponse(res, 503, "HTTPS central auth login is unavailable: httplib lacks OpenSSL support.");
            return;
#endif
        } else {
            httplib::Client client(endpoint.host, endpoint.port);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Post(loginPath.c_str(), headers, payload.dump(), "application/json");
        }

        if (!result) {
            sendErrorResponse(res, 502, "Central auth login request failed.");
            return;
        }

        res.status = result->status;
        res.set_content(result->body.empty() ? "{}" : result->body, "application/json");
    }

    void APIRoutes::handleAuthSession(const httplib::Request& req, httplib::Response& res) {
        ensureRequestId(req, res);
        const std::string token = extractAuthToken(req);
        nlohmann::json claims;
        if (validateCentralAuthToken(token, &claims)) {
            res.status = 200;
            res.set_content(claims.dump(), "application/json");
            return;
        }
        if (!token.empty() && !authToken.empty() && token == authToken) {
            res.status = 200;
            res.set_content(
                    nlohmann::json{
                            {"authenticated", true},
                            {"user", "service-token"},
                            {"role", "admin"},
                            {"groups", nlohmann::json::array({"admin"})},
                            {"active_team", nullptr},
                            {"team_count", 0},
                            {"pending_invitation_count", 0}
                    }.dump(),
                    "application/json");
            return;
        }
        res.set_header("WWW-Authenticate", "Bearer");
        sendErrorResponse(res, 401, "Unauthorized.");
    }

    nlohmann::json APIRoutes::buildAarnnEndpointsPayload(std::string& errorOut) {
        errorOut.clear();

        const std::string namespaceName = getenvTrimmedOr(
                "NMC_AARNN_NAMESPACE",
                "NM_AARNN_NAMESPACE",
                "aarnn"
        );
        const std::string appName = getenvTrimmedOr(
                "NMC_AARNN_APP_NAME",
                "NM_AARNN_APP_NAME",
                "aarnn"
        );

        nlohmann::json payload = {
                {"namespace", namespaceName},
                {"app_name", appName},
                {"runtime", {
                        {"found", false},
                        {"component", "web-ui"}
                }},
                {"control", {
                        {"found", false},
                        {"component", "control-api"}
                }},
                {"orchestrator", {
                        {"found", false},
                        {"component", "orchestrator"}
                }},
                {"discovery", {
                        {"kubernetes_available", k8sHandlers != nullptr},
                        {"errors", nlohmann::json::array()}
                }}
        };

        std::optional<nlohmann::json> services;
        std::optional<nlohmann::json> deployments;
        if (k8sHandlers) {
            services = k8sHandlers->listNamespacedResources("", "v1", "services", namespaceName);
            if (!services) {
                payload["discovery"]["errors"].push_back(
                        "Unable to list Service resources in namespace '" + namespaceName + "'."
                );
            }
            deployments = k8sHandlers->listNamespacedResources("apps", "v1", "deployments", namespaceName);
            if (!deployments) {
                payload["discovery"]["errors"].push_back(
                        "Unable to list Deployment resources in namespace '" + namespaceName + "'."
                );
            }
        }

        auto attachEndpoint = [&](const std::string& fieldName,
                                  const std::string& component,
                                  const std::string& serviceName,
                                  const std::string& workloadName,
                                  const std::string& overrideUrl,
                                  const std::string& urlField,
                                  const std::string& preferredPortName,
                                  int fallbackPort) {
            nlohmann::json summary = {
                    {"found", false},
                    {"component", component}
            };

            if (services) {
                const auto serviceMatch = findK8sItemByNameOrComponent(*services, serviceName, appName, component);
                if (serviceMatch) {
                    summary = buildAarnnServiceSummary(
                            *serviceMatch,
                            component,
                            "http",
                            urlField,
                            preferredPortName,
                            fallbackPort
                    );
                }
            }

            if (deployments) {
                const auto workloadMatch = findK8sItemByNameOrComponent(*deployments, workloadName, appName, component);
                if (workloadMatch) {
                    const auto metadata = workloadMatch->value("metadata", nlohmann::json::object());
                    summary["workload"] = buildAarnnDeploymentSummary(
                            *workloadMatch,
                            metadata.value("namespace", namespaceName),
                            metadata.value("name", workloadName)
                    );
                }
            }

            if (!overrideUrl.empty()) {
                TraceyEndpoint parsed{};
                if (parseTraceyEndpoint(overrideUrl, parsed)) {
                    summary["found"] = true;
                    summary["source"] = summary.value("source", "") == "kubernetes" ? "environment+kubernetes" : "environment";
                    summary["host"] = parsed.host;
                    summary["port"] = parsed.port;
                    summary[urlField] = parsed.normalized;
                } else {
                    payload["discovery"]["errors"].push_back(
                            "Invalid AARNN " + fieldName + " override: " + overrideUrl
                    );
                }
            }

            if (!summary.contains("source")) {
                summary["source"] = summary.value("found", false) ? "kubernetes" : "undiscovered";
            }
            if (!summary.contains("workload")) {
                summary["workload"] = nlohmann::json{
                        {"observed", false},
                        {"healthy", false},
                        {"status", "Unknown"},
                        {"namespace", namespaceName},
                        {"deployment", workloadName}
                };
            }
            payload[fieldName] = summary;
        };

        attachEndpoint(
                "runtime",
                "web-ui",
                getenvTrimmedOr("NMC_AARNN_RUNTIME_SERVICE_NAME", "NM_AARNN_RUNTIME_SERVICE_NAME", "aarnn-web-ui"),
                getenvTrimmedOr("NMC_AARNN_RUNTIME_WORKLOAD_NAME", "NM_AARNN_RUNTIME_WORKLOAD_NAME", "aarnn-web-ui"),
                getenvTrimmedOr("NMC_AARNN_RUNTIME_URL", "NM_AARNN_RUNTIME_URL"),
                "api_base_url",
                "http",
                8080
        );
        attachEndpoint(
                "control",
                "control-api",
                getenvTrimmedOr("NMC_AARNN_CONTROL_SERVICE_NAME", "NM_AARNN_CONTROL_SERVICE_NAME", "aarnn-control-api"),
                getenvTrimmedOr("NMC_AARNN_CONTROL_WORKLOAD_NAME", "NM_AARNN_CONTROL_WORKLOAD_NAME", "aarnn-control-api"),
                getenvTrimmedOr("NMC_AARNN_CONTROL_URL", "NM_AARNN_CONTROL_URL"),
                "api_base_url",
                "http",
                8080
        );
        attachEndpoint(
                "orchestrator",
                "orchestrator",
                getenvTrimmedOr("NMC_AARNN_ORCHESTRATOR_SERVICE_NAME", "NM_AARNN_ORCHESTRATOR_SERVICE_NAME", "aarnn-orchestrator"),
                getenvTrimmedOr("NMC_AARNN_ORCHESTRATOR_WORKLOAD_NAME", "NM_AARNN_ORCHESTRATOR_WORKLOAD_NAME", "aarnn-orchestrator"),
                getenvTrimmedOr("NMC_AARNN_ORCHESTRATOR_URL", "NM_AARNN_ORCHESTRATOR_URL"),
                "grpc_url",
                "grpc",
                50051
        );

        const bool anyFound = payload["runtime"].value("found", false)
                              || payload["control"].value("found", false)
                              || payload["orchestrator"].value("found", false);
        if (!anyFound) {
            errorOut = "AARNN services were not discovered in namespace '" + namespaceName + "'.";
        }

        return payload;
    }

    bool APIRoutes::resolveAarnnEndpoint(const std::string& plane,
                                         std::string& endpointOut,
                                         nlohmann::json& endpointInfoOut,
                                         std::string& errorOut) {
        endpointOut.clear();
        endpointInfoOut = nlohmann::json::object();
        errorOut.clear();

        const std::string normalizedPlane = toLower(trim(plane));
        if (normalizedPlane != "runtime" && normalizedPlane != "control") {
            errorOut = "Unsupported AARNN plane '" + plane + "'. Expected 'runtime' or 'control'.";
            return false;
        }

        std::string discoveryError;
        const nlohmann::json payload = buildAarnnEndpointsPayload(discoveryError);
        endpointInfoOut = payload.value(normalizedPlane, nlohmann::json::object());
        endpointOut = trim(endpointInfoOut.value("api_base_url", ""));
        if (!endpointOut.empty() && endpointInfoOut.value("found", false)) {
            return true;
        }

        errorOut = discoveryError.empty()
                   ? ("AARNN " + normalizedPlane + " endpoint is not available.")
                   : discoveryError;
        return false;
    }

    bool APIRoutes::fetchAarnnStatusViaControlPlane(const std::string& grpcUrl,
                                                    int64_t nowMs,
                                                    nlohmann::json& statusOut,
                                                    std::string& errorOut) {
        statusOut = nlohmann::json::object();
        errorOut.clear();

        const std::string trimmedGrpcUrl = trim(grpcUrl);
        if (trimmedGrpcUrl.empty()) {
            errorOut = "AARNN orchestrator gRPC URL is empty.";
            return false;
        }

        std::string controlEndpointRaw;
        nlohmann::json controlEndpointInfo;
        if (!resolveAarnnEndpoint("control", controlEndpointRaw, controlEndpointInfo, errorOut)) {
            return false;
        }

        TraceyEndpoint controlEndpoint;
        if (!parseTraceyEndpoint(controlEndpointRaw, controlEndpoint)) {
            errorOut = "Resolved AARNN control endpoint is invalid.";
            return false;
        }
        if (!isLocalOrPrivateHost(controlEndpoint.host, aarnnAllowPublicAddr)) {
            errorOut = "AARNN control endpoint is outside allowed network ranges.";
            return false;
        }

        std::string statusPath = buildTraceyPath(controlEndpoint, "/api/status");
        appendEncodedQueryParameter(statusPath, "addr", trimmedGrpcUrl);

        httplib::Headers headers{{"Accept", "application/json"}};
        const int timeoutSec = static_cast<int>(std::max<int64_t>(1, (aarnnStatusTimeoutMs + 999) / 1000));
        httplib::Result result;
        if (controlEndpoint.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            const bool tlsVerify = parseBoolValue(
                    std::getenv("NMC_AARNN_TLS_VERIFY"),
                    parseBoolValue(std::getenv("NM_AARNN_TLS_VERIFY"), true)
            );
            httplib::SSLClient client(controlEndpoint.host, controlEndpoint.port);
            client.enable_server_certificate_verification(tlsVerify);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Get(statusPath.c_str(), headers);
#else
            errorOut = "HTTPS AARNN status polling is unavailable: httplib built without OpenSSL support.";
            return false;
#endif
        } else {
            httplib::Client client(controlEndpoint.host, controlEndpoint.port);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Get(statusPath.c_str(), headers);
        }

        if (!result) {
            errorOut = "AARNN status poll failed: " + httplib::to_string(result.error());
            return false;
        }
        if (result->status < 200 || result->status >= 300) {
            errorOut = "AARNN status endpoint returned HTTP " + std::to_string(result->status) + ".";
            return false;
        }

        statusOut = nlohmann::json::parse(result->body, nullptr, false);
        if (statusOut.is_discarded() || !statusOut.is_object()) {
            errorOut = "AARNN status endpoint returned invalid JSON.";
            return false;
        }
        if (!statusOut.contains("orchestrator")) {
            statusOut["orchestrator"] = trimmedGrpcUrl;
        }
        statusOut["polled_at_epoch_ms"] = nowMs > 0 ? nowMs : nowEpochMs();
        return true;
    }

    nlohmann::json APIRoutes::buildAarnnInventoryPayload(const std::string& clusterId,
                                                         const std::string& orchestratorId,
                                                         std::string& errorOut) {
        errorOut.clear();
        const std::string normalizedClusterId = toLower(trim(clusterId));
        const std::string normalizedOrchestratorId = toLower(trim(orchestratorId));

        std::string endpointsError;
        const nlohmann::json endpointsPayload = buildAarnnEndpointsPayload(endpointsError);

        nlohmann::json payload = {
                {"planes", endpointsPayload},
                {"discovery", {
                        {"enabled", aarnnDiscoveryEnabled},
                        {"bind_addr", aarnnDiscoveryBindAddr},
                        {"port", aarnnDiscoveryPort},
                        {"stale_after_seconds", aarnnStaleAfterSeconds},
                        {"status_poll_ms", aarnnStatusPollMs},
                        {"status_timeout_ms", aarnnStatusTimeoutMs},
                        {"errors", nlohmann::json::array()}
                }},
                {"clusters", nlohmann::json::array()},
                {"orchestrators", nlohmann::json::array()},
                {"nodes", nlohmann::json::array()},
                {"networks", nlohmann::json::array()},
                {"summary", {
                        {"cluster_count", 0},
                        {"orchestrator_count", 0},
                        {"node_count", 0},
                        {"network_count", 0},
                        {"stale_orchestrator_count", 0}
                }}
        };

        if (endpointsPayload.contains("discovery") && endpointsPayload["discovery"].is_object()) {
            const auto endpointErrors = endpointsPayload["discovery"].value("errors", nlohmann::json::array());
            if (endpointErrors.is_array()) {
                for (const auto& entry : endpointErrors) {
                    payload["discovery"]["errors"].push_back(entry);
                }
            }
        }

        const auto matchesTarget = [&](const std::string& candidateClusterId,
                                       const std::string& candidateOrchestratorId,
                                       const std::string& candidateGrpcUrl) {
            const std::string clusterNeedle = toLower(trim(candidateClusterId));
            const std::string orchestratorNeedle = toLower(trim(candidateOrchestratorId));
            const std::string grpcNeedle = toLower(trim(candidateGrpcUrl));
            if (!normalizedClusterId.empty()) {
                return clusterNeedle == normalizedClusterId || grpcNeedle == normalizedClusterId;
            }
            if (!normalizedOrchestratorId.empty()) {
                return orchestratorNeedle == normalizedOrchestratorId || grpcNeedle == normalizedOrchestratorId;
            }
            return true;
        };

        std::vector<AarnnOrchestratorRecord> discoveredSnapshot;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            discoveredSnapshot.reserve(aarnnOrchestrators.size());
            for (const auto& [mapKey, record] : aarnnOrchestrators) {
                (void)mapKey;
                discoveredSnapshot.push_back(record);
            }
        }
        std::sort(discoveredSnapshot.begin(), discoveredSnapshot.end(), [](const AarnnOrchestratorRecord& left,
                                                                           const AarnnOrchestratorRecord& right) {
            if (left.clusterId == right.clusterId) {
                return left.orchestratorId < right.orchestratorId;
            }
            return left.clusterId < right.clusterId;
        });

        std::set<std::string> seenGrpcUrls;
        int staleOrchestratorCount = 0;
        const auto appendInventoryRecord = [&](const AarnnOrchestratorRecord& record,
                                               const nlohmann::json& statusPayload) {
            const auto nodes = statusPayload.contains("nodes") && statusPayload["nodes"].is_array()
                               ? statusPayload["nodes"]
                               : nlohmann::json::array();
            const auto networks = statusPayload.contains("networks") && statusPayload["networks"].is_array()
                                  ? statusPayload["networks"]
                                  : nlohmann::json::array();
            const int64_t statusTsMs = firstInt64Value(statusPayload, {"timestamp_ms"}, 0);

            nlohmann::json orchestratorView = {
                    {"orchestrator_id", record.orchestratorId},
                    {"cluster_id", record.clusterId},
                    {"grpc_url", record.grpcUrl},
                    {"host", record.host},
                    {"port", record.port},
                    {"source", record.source},
                    {"discovery_protocol", record.discoveryProtocol.empty() ? nlohmann::json(nullptr) : nlohmann::json(record.discoveryProtocol)},
                    {"stale", record.stale},
                    {"status_reachable", record.statusReachable},
                    {"first_seen_epoch_ms", record.firstSeenEpochMs > 0 ? nlohmann::json(record.firstSeenEpochMs) : nlohmann::json(nullptr)},
                    {"last_seen_epoch_ms", record.lastSeenEpochMs > 0 ? nlohmann::json(record.lastSeenEpochMs) : nlohmann::json(nullptr)},
                    {"last_polled_epoch_ms", record.lastPolledEpochMs > 0 ? nlohmann::json(record.lastPolledEpochMs) : nlohmann::json(nullptr)},
                    {"status_timestamp_ms", statusTsMs > 0 ? nlohmann::json(statusTsMs) : nlohmann::json(nullptr)},
                    {"last_error", record.lastError.empty() ? nlohmann::json(nullptr) : nlohmann::json(record.lastError)},
                    {"node_count", nodes.size()},
                    {"network_count", networks.size()}
            };
            payload["orchestrators"].push_back(orchestratorView);

            nlohmann::json clusterView = orchestratorView;
            clusterView["name"] = record.clusterId;
            payload["clusters"].push_back(clusterView);

            for (const auto& node : nodes) {
                if (!node.is_object()) {
                    continue;
                }
                nlohmann::json enriched = node;
                enriched["cluster_id"] = record.clusterId;
                enriched["orchestrator_id"] = record.orchestratorId;
                enriched["orchestrator_grpc_url"] = record.grpcUrl;
                enriched["source"] = record.source;
                payload["nodes"].push_back(std::move(enriched));
            }
            for (const auto& network : networks) {
                if (!network.is_object()) {
                    continue;
                }
                nlohmann::json enriched = network;
                enriched["cluster_id"] = record.clusterId;
                enriched["orchestrator_id"] = record.orchestratorId;
                enriched["orchestrator_grpc_url"] = record.grpcUrl;
                enriched["source"] = record.source;
                payload["networks"].push_back(std::move(enriched));
            }
        };

        for (const auto& record : discoveredSnapshot) {
            if (!matchesTarget(record.clusterId, record.orchestratorId, record.grpcUrl)) {
                continue;
            }
            if (!trim(record.grpcUrl).empty()) {
                seenGrpcUrls.insert(trim(record.grpcUrl));
            }
            if (record.stale) {
                staleOrchestratorCount += 1;
            }
            appendInventoryRecord(record, record.statusCache.is_object() ? record.statusCache : nlohmann::json::object());
        }

        const nlohmann::json staticOrchestrator = endpointsPayload.value("orchestrator", nlohmann::json::object());
        const std::string staticGrpcUrl = trim(staticOrchestrator.value("grpc_url", ""));
        if (staticOrchestrator.value("found", false) && !staticGrpcUrl.empty() && seenGrpcUrls.find(staticGrpcUrl) == seenGrpcUrls.end()) {
            TraceyEndpoint staticEndpoint;
            if (parseTraceyEndpoint(staticGrpcUrl, staticEndpoint)) {
                AarnnOrchestratorRecord staticRecord;
                staticRecord.grpcUrl = staticEndpoint.normalized;
                staticRecord.host = staticEndpoint.host;
                staticRecord.port = staticEndpoint.port;
                staticRecord.source = trim(staticOrchestrator.value("source", "static"));
                if (staticRecord.source.empty()) {
                    staticRecord.source = "static";
                }
                staticRecord.discoveryProtocol = staticRecord.source == "environment" ? "configured" : "continuum";
                staticRecord.clusterId = "continuum-default";
                staticRecord.orchestratorId = buildAarnnSyntheticId("aarnn-orchestrator", staticRecord.host, staticRecord.port);
                staticRecord.stale = false;

                if (matchesTarget(staticRecord.clusterId, staticRecord.orchestratorId, staticRecord.grpcUrl)) {
                    nlohmann::json statusPayload = nlohmann::json::object();
                    std::string statusError;
                    staticRecord.statusReachable = fetchAarnnStatusViaControlPlane(staticRecord.grpcUrl, nowEpochMs(), statusPayload, statusError);
                    staticRecord.statusCache = staticRecord.statusReachable ? statusPayload : nlohmann::json::object();
                    staticRecord.lastPolledEpochMs = nowEpochMs();
                    staticRecord.lastError = staticRecord.statusReachable ? "" : statusError;
                    appendInventoryRecord(staticRecord, staticRecord.statusCache);
                }
            }
        }

        payload["summary"]["cluster_count"] = payload["clusters"].size();
        payload["summary"]["orchestrator_count"] = payload["orchestrators"].size();
        payload["summary"]["node_count"] = payload["nodes"].size();
        payload["summary"]["network_count"] = payload["networks"].size();
        payload["summary"]["stale_orchestrator_count"] = staleOrchestratorCount;
        payload["discovery"]["known_orchestrators"] = payload["orchestrators"].size();

        if (payload["orchestrators"].empty()) {
            errorOut = !endpointsError.empty()
                       ? endpointsError
                       : "No AARNN orchestrators are currently known to Continuum.";
        }
        return payload;
    }

    bool APIRoutes::resolveAarnnOrchestratorTarget(const std::string& clusterId,
                                                   const std::string& orchestratorId,
                                                   nlohmann::json& targetOut,
                                                   std::string& errorOut) {
        targetOut = nlohmann::json::object();
        errorOut.clear();

        if (trim(clusterId).empty() && trim(orchestratorId).empty()) {
            errorOut = "Specify either cluster_id or orchestrator_id.";
            return false;
        }

        std::string inventoryError;
        const nlohmann::json inventory = buildAarnnInventoryPayload(clusterId, orchestratorId, inventoryError);
        const auto orchestrators = inventory.value("orchestrators", nlohmann::json::array());
        if (orchestrators.is_array() && !orchestrators.empty() && orchestrators.front().is_object()) {
            targetOut = orchestrators.front();
            return true;
        }

        errorOut = inventoryError.empty()
                   ? "AARNN orchestrator target was not found."
                   : inventoryError;
        return false;
    }

    void APIRoutes::handleAarnnInventory(const httplib::Request& req, httplib::Response& res) {
        const std::string clusterId = req.has_param("cluster_id") ? trim(req.get_param_value("cluster_id")) : "";
        const std::string orchestratorId = req.has_param("orchestrator_id") ? trim(req.get_param_value("orchestrator_id")) : "";
        if (!clusterId.empty() && !orchestratorId.empty()) {
            sendErrorResponse(res, 400, "Use either cluster_id or orchestrator_id, not both.");
            return;
        }

        std::string error;
        const nlohmann::json payload = buildAarnnInventoryPayload(clusterId, orchestratorId, error);
        const bool anyKnown = payload["summary"].value("orchestrator_count", 0) > 0;

        Models::CloudResponse apiResponse;
        apiResponse.success = anyKnown;
        apiResponse.message = anyKnown
                              ? "AARNN inventory generated."
                              : (error.empty() ? "AARNN inventory is empty." : error);
        apiResponse.data = payload;
        sendJsonResponse(res, apiResponse);
        if (!anyKnown) {
            res.status = 404;
        }
    }

    void APIRoutes::handleAarnnEndpoints(const httplib::Request& req, httplib::Response& res) {
        (void)req;
        std::string error;
        const nlohmann::json payload = buildAarnnEndpointsPayload(error);
        const bool anyFound = payload["runtime"].value("found", false)
                              || payload["control"].value("found", false)
                              || payload["orchestrator"].value("found", false);

        Models::CloudResponse apiResponse;
        apiResponse.success = anyFound;
        apiResponse.message = anyFound
                              ? "AARNN endpoints discovered."
                              : (error.empty() ? "AARNN endpoints were not found." : error);
        apiResponse.data = payload;
        sendJsonResponse(res, apiResponse);
        if (!anyFound) {
            res.status = 404;
        }
    }

    void APIRoutes::handleAarnnProxy(const httplib::Request& req, httplib::Response& res, const std::string& plane) {
        const nlohmann::json payload = nlohmann::json::parse(req.body, nullptr, false);
        if (payload.is_discarded() || !payload.is_object()) {
            sendErrorResponse(res, 400, "Invalid JSON payload.");
            return;
        }

        const std::string normalizedPlane = toLower(trim(plane));
        if (normalizedPlane != "runtime" && normalizedPlane != "control") {
            sendErrorResponse(res, 400, "Unsupported AARNN plane. Use 'runtime' or 'control'.");
            return;
        }

        const std::string method = toUpper(trim(payload.value("method", "GET")));
        if (method != "GET" && method != "POST" && method != "PUT" && method != "PATCH" && method != "DELETE") {
            sendErrorResponse(res, 400, "Unsupported HTTP method for AARNN proxy.");
            return;
        }

        const std::string requestPath = trim(payload.value("path", ""));
        if (requestPath.empty() || requestPath.front() != '/') {
            sendErrorResponse(res, 400, "AARNN proxy path must start with '/'.");
            return;
        }

        const std::string clusterId = trim(payload.value("cluster_id", ""));
        const std::string orchestratorId = trim(payload.value("orchestrator_id", ""));
        if (!clusterId.empty() && !orchestratorId.empty()) {
            sendErrorResponse(res, 400, "Use either cluster_id or orchestrator_id, not both.");
            return;
        }

        if (payload.contains("json") && payload.contains("body")) {
            sendErrorResponse(res, 400, "Provide either 'json' or 'body', not both.");
            return;
        }

        std::string endpointRaw;
        nlohmann::json endpointInfo;
        std::string resolutionError;
        if (!resolveAarnnEndpoint(normalizedPlane, endpointRaw, endpointInfo, resolutionError)) {
            sendErrorResponse(res, 503, resolutionError);
            return;
        }

        TraceyEndpoint endpoint;
        if (!parseTraceyEndpoint(endpointRaw, endpoint)) {
            sendErrorResponse(res, 503, "Resolved AARNN endpoint is invalid.");
            return;
        }

        const bool allowPublicAddr = parseBoolValue(
                std::getenv("NMC_AARNN_ALLOW_PUBLIC_ADDR"),
                parseBoolValue(std::getenv("NM_AARNN_ALLOW_PUBLIC_ADDR"), false)
        );
        if (!isLocalOrPrivateHost(endpoint.host, allowPublicAddr)) {
            sendErrorResponse(res, 403, "AARNN endpoint is outside allowed network ranges.");
            return;
        }

        std::string resolvedRequestPath = requestPath;
        if (!clusterId.empty() || !orchestratorId.empty()) {
            if (normalizedPlane != "control") {
                sendErrorResponse(res, 400, "cluster_id/orchestrator_id targeting is only supported on the AARNN control plane.");
                return;
            }
            if (pathHasQueryParameter(resolvedRequestPath, "addr")) {
                sendErrorResponse(res, 400, "Do not combine cluster_id/orchestrator_id with an explicit addr query parameter.");
                return;
            }

            nlohmann::json targetInfo;
            std::string targetError;
            if (!resolveAarnnOrchestratorTarget(clusterId, orchestratorId, targetInfo, targetError)) {
                sendErrorResponse(res, 404, targetError);
                return;
            }

            const std::string targetGrpcUrl = trim(targetInfo.value("grpc_url", ""));
            if (targetGrpcUrl.empty()) {
                sendErrorResponse(res, 404, "Resolved AARNN orchestrator target has no grpc_url.");
                return;
            }
            appendEncodedQueryParameter(resolvedRequestPath, "addr", targetGrpcUrl);
        }

        std::string requestBody;
        std::string contentType = trim(payload.value("content_type", ""));
        if (payload.contains("json")) {
            requestBody = payload["json"].dump();
            if (contentType.empty()) {
                contentType = "application/json";
            }
        } else if (payload.contains("body")) {
            if (!payload["body"].is_string()) {
                sendErrorResponse(res, 400, "AARNN proxy 'body' must be a string.");
                return;
            }
            requestBody = payload["body"].get<std::string>();
        }
        if (contentType.empty() && !requestBody.empty()) {
            contentType = "text/plain";
        }

        httplib::Headers headers{
                {"Accept", "application/json, application/octet-stream;q=0.9, text/plain;q=0.8, */*;q=0.7"}
        };
        const std::string authToken = extractAuthToken(req);
        if (!authToken.empty()) {
            headers.emplace("Authorization", "Bearer " + authToken);
        }

        const auto maybeAuthenticateRuntimeLocally = [&](auto& client) {
            if (normalizedPlane != "runtime" || !authToken.empty()) {
                return;
            }
            if (resolvedRequestPath == "/api/login" || resolvedRequestPath == "/login") {
                return;
            }

            const std::string runtimeUser = getenvTrimmedOr(
                    "NMC_AARNN_RUNTIME_LOCAL_USER",
                    "NM_AARNN_RUNTIME_LOCAL_USER",
                    getenvTrimmedOr("NMC_AARNN_LOCAL_USER", "NM_AARNN_LOCAL_USER")
            );
            const std::string runtimePass = getenvTrimmedOr(
                    "NMC_AARNN_RUNTIME_LOCAL_PASS",
                    "NM_AARNN_RUNTIME_LOCAL_PASS",
                    getenvTrimmedOr("NMC_AARNN_LOCAL_PASS", "NM_AARNN_LOCAL_PASS")
            );
            if (runtimeUser.empty() || runtimePass.empty()) {
                return;
            }

            nlohmann::json loginPayload = {
                    {"username", runtimeUser},
                    {"password", runtimePass}
            };
            httplib::Headers loginHeaders{
                    {"Accept", "application/json"},
                    {"Content-Type", "application/json"}
            };
            const std::string loginPath = buildTraceyPath(endpoint, "/api/login");
            httplib::Result loginResult = client.Post(
                    loginPath.c_str(),
                    loginHeaders,
                    loginPayload.dump(),
                    "application/json"
            );
            if (!loginResult || loginResult->status < 200 || loginResult->status >= 300) {
                return;
            }
            const std::string setCookie = trim(loginResult->get_header_value("Set-Cookie"));
            if (setCookie.empty()) {
                return;
            }
            const auto semiPos = setCookie.find(';');
            headers.emplace("Cookie", semiPos == std::string::npos ? setCookie : setCookie.substr(0, semiPos));
        };

        const auto executeRequest = [&](auto& client) -> httplib::Result {
            client.set_connection_timeout(10);
            client.set_read_timeout(180);
            client.set_write_timeout(180);
            maybeAuthenticateRuntimeLocally(client);

            const std::string upstreamPath = buildTraceyPath(endpoint, resolvedRequestPath);
            if (method == "GET") {
                return client.Get(upstreamPath.c_str(), headers);
            }
            if (method == "DELETE") {
                return client.Delete(upstreamPath.c_str(), headers, requestBody, contentType.c_str());
            }
            if (method == "POST") {
                return client.Post(upstreamPath.c_str(), headers, requestBody, contentType.c_str());
            }
            if (method == "PUT") {
                return client.Put(upstreamPath.c_str(), headers, requestBody, contentType.c_str());
            }
            return client.Patch(upstreamPath.c_str(), headers, requestBody, contentType.c_str());
        };

        httplib::Result result;
        if (endpoint.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient client(endpoint.host, endpoint.port);
            client.enable_server_certificate_verification(true);
            result = executeRequest(client);
#else
            sendErrorResponse(res, 503, "HTTPS AARNN proxy unavailable: httplib built without OpenSSL support.");
            return;
#endif
        } else {
            httplib::Client client(endpoint.host, endpoint.port);
            result = executeRequest(client);
        }

        if (!result) {
            sendErrorResponse(res, 502, "AARNN proxy request failed: " + httplib::to_string(result.error()));
            return;
        }

        const int upstreamStatus = result->status;
        const std::string upstreamContentType = trim(result->get_header_value("Content-Type"));
        nlohmann::json upstreamData = {
                {"plane", normalizedPlane},
                {"endpoint", endpointInfo},
                {"path", requestPath},
                {"resolved_path", resolvedRequestPath},
                {"method", method},
                {"upstream_status", upstreamStatus},
                {"content_type", upstreamContentType.empty() ? nlohmann::json(nullptr) : nlohmann::json(upstreamContentType)}
        };
        if (!clusterId.empty()) {
            upstreamData["cluster_id"] = clusterId;
        }
        if (!orchestratorId.empty()) {
            upstreamData["orchestrator_id"] = orchestratorId;
        }
        const std::string disposition = trim(result->get_header_value("Content-Disposition"));
        if (!disposition.empty()) {
            upstreamData["content_disposition"] = disposition;
        }

        nlohmann::json parsedBody = nlohmann::json::parse(result->body, nullptr, false);
        if (!parsedBody.is_discarded()) {
            upstreamData["payload"] = parsedBody;
        } else if (!result->body.empty()) {
            if (looksLikeTextPayload(result->body)) {
                upstreamData["text"] = result->body;
            } else {
                upstreamData["body_base64"] = base64Encode(result->body);
            }
        } else {
            upstreamData["payload"] = nlohmann::json::object();
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = upstreamStatus >= 200 && upstreamStatus < 300;
        apiResponse.message = apiResponse.success
                              ? "AARNN " + normalizedPlane + " proxy request completed."
                              : ("AARNN " + normalizedPlane + " proxy request failed with HTTP " + std::to_string(upstreamStatus) + ".");
        apiResponse.data = upstreamData;
        sendJsonResponse(res, apiResponse);
        res.status = upstreamStatus;
    }

    bool APIRoutes::authorizeOrReject(const httplib::Request& req, httplib::Response& res) const {
        if (!authRequired) {
            return true;
        }
        if (authMode == "token") {
            const std::string token = extractAuthToken(req);
            if (validateCentralAuthToken(token)) {
                return true;
            }
            if (!token.empty() && !authToken.empty() && token == authToken) {
                return true;
            }
            res.set_header("WWW-Authenticate", "Bearer");
            sendErrorResponse(res, 401, "Unauthorized.");
            return false;
        }

        if (authMode == "oidc") {
            if (!oidcValidator || !oidcValidator->isConfigured()) {
                sendErrorResponse(res, 500, "OIDC authentication is misconfigured.");
                return false;
            }
            const std::string bearer = req.get_header_value("Authorization");
            std::string token;
            if (!bearer.empty()) {
                const std::string prefix = "Bearer ";
                if (bearer.rfind(prefix, 0) == 0) {
                    token = bearer.substr(prefix.size());
                }
            }
            if (token.empty()) {
                res.set_header("WWW-Authenticate", "Bearer");
                sendErrorResponse(res, 401, "Unauthorized.");
                return false;
            }

            std::string errorMessage;
            nlohmann::json claims;
            if (!oidcValidator->validateToken(token, errorMessage, &claims)) {
                res.set_header("WWW-Authenticate", "Bearer");
                sendErrorResponse(res, 401, "Unauthorized.");
                return false;
            }
            return true;
        }

        sendErrorResponse(res, 500, "Unsupported authentication mode.");
        return false;
    }

    bool APIRoutes::shouldLogBody(const std::string& path) const {
        // Redact known sensitive endpoints (keys, scripts, credentials, provisioning).
        const std::vector<std::string> redactedPrefixes = {
                "/ssh/create",
                "/vm/create",
                "/model/upload",
                "/connections/make",
                "/auth/login",
                "/openshift/clusters/request",
                "/openstack/clusters/request",
                "/proxmox/clusters/request",
                "/node/recruit",
                "/services/health/monitoring/auth/login"
        };
        for (const auto& prefix : redactedPrefixes) {
            if (path.rfind(prefix, 0) == 0) {
                return false;
            }
        }
        return true;
    }

    std::string APIRoutes::redactBody(const std::string& body) const {
        if (body.size() <= maxLogBodyBytes) {
            return body;
        }
        return body.substr(0, maxLogBodyBytes) + "...(truncated)";
    }

// --- Bucket Handlers ---

    /**
     * @brief Handles the creation of a new bucket.
     *
     * Expects a JSON body with 'name', 'location', and 'type'.
     * Checks for missing parameters, duplicate bucket names, and creates a bucket.
     * Sends a success or error JSON response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleCreateBucket(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string name = json_body.value("name", "");
            std::string location = json_body.value("location", "");
            std::string type = json_body.value("type", "");

            if (name.empty() || location.empty() || type.empty()) {
                return sendErrorResponse(res, 400, "Missing required parameters: name, location, or type.");
            }

            std::lock_guard<std::mutex> lock(dataMutex);
            // Check if bucket already exists
            auto it = std::find_if(buckets.begin(), buckets.end(),
                                   [&](const Models::Bucket& b) { return b.name == name; });
            if (it != buckets.end()) {
                return sendErrorResponse(res, 409, "Bucket '" + name + "' already exists."); // Conflict
            }

            Models::Bucket newBucket;
            newBucket.name = name;
            newBucket.location = location;
            newBucket.type = type;
            newBucket.status = "Active";
            buckets.push_back(newBucket);

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Bucket '" + name + "' created successfully.";
            apiResponse.data = newBucket.toJsonString();
            sendJsonResponse(res, apiResponse);

        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

    /**
     * @brief Handles the deletion of a bucket.
     *
     * Extracts the bucket name from the URL path.
     * Deletes the corresponding bucket if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleDeleteBucket(const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1]; // Get name from regex capture

        std::lock_guard<std::mutex> lock(dataMutex);
        auto initial_size = buckets.size();
        buckets.erase(std::remove_if(buckets.begin(), buckets.end(),
                                         [&](const Models::Bucket& b) { return b.name == name; }),
                          buckets.end());

        if (buckets.size() < initial_size) {
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Bucket '" + name + "' deleted successfully.";
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "Bucket '" + name + "' not found.");
        }
    }

    /**
     * @brief Handles retrieving details of a specific bucket.
     *
     * Extracts the bucket name from the URL path.
     * Finds and returns the bucket's details if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleGetBucket(const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = std::find_if(buckets.begin(), buckets.end(),
                               [&](const Models::Bucket& b) { return b.name == name; });

        if (it != buckets.end()) {
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Bucket details retrieved.";
            apiResponse.data = it->toJsonString();
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "Bucket '" + name + "' not found.");
        }
    }

    /**
     * @brief Handles listing all buckets, with optional name filtering.
     *
     * Supports a 'filter-name' query parameter to filter buckets by name.
     * Returns a JSON array of bucket objects.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListBuckets(const httplib::Request& req, httplib::Response& res) {
        std::string filterName = req.get_param_value("filter-name");

        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json bucketList = nlohmann::json::array();
        for (const auto& bucket : buckets) {
            if (filterName.empty() || bucket.name.find(filterName) != std::string::npos) {
                bucketList.push_back(bucket.toJsonString());
            }
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Buckets listed successfully.";
        apiResponse.data = bucketList;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles listing available bucket locations.
     *
     * Returns a predefined list of bucket locations.
     * In a real system, these would be fetched from a configuration or cloud provider API.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListBucketLocations(const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        // In a real system, these would come from a configuration or backend service
        nlohmann::json locations = {
                {"name", "eu-central"},
                {"name", "us-east"},
                {"name", "asia-southeast"}
        };

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Bucket locations listed successfully.";
        apiResponse.data = locations;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles listing available bucket types.
     *
     * Returns a predefined list of bucket types.
     * In a real system, these would be fetched from a configuration or cloud provider API.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListBucketTypes(const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json types = {
                {"name", "standard"},
                {"name", "archive"},
                {"name", "coldline"}
        };

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Bucket types listed successfully.";
        apiResponse.data = types;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles resetting access credentials for a bucket.
     *
     * Extracts the bucket name from the URL path.
     * Simulates resetting credentials for the bucket if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleResetBucketKey(const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = std::find_if(buckets.begin(), buckets.end(),
                               [&](const Models::Bucket& b) { return b.name == name; });

        if (it != buckets.end()) {
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Bucket '" + name + "' access credentials reset successfully.";
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "Bucket '" + name + "' not found.");
        }
    }

// --- Model Handlers ---
    /**
     * @brief Handles the upload of a model.
     *
     * Expects a JSON body with 'filePath', 'sku', 'registryName', and 'tag'.
     * This is a implementation that only acknowledges the upload without
     * actual file handling.
     * Sends a success or error JSON response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleUploadModel(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string filePath = json_body.value("filePath", "");
            std::string sku = json_body.value("sku", "");
            std::string registryName = json_body.value("registryName", "");
            std::string tag = json_body.value("tag", "");

            if (filePath.empty() || sku.empty() || registryName.empty() || tag.empty()) {
                return sendErrorResponse(res, 400, "Missing required parameters: filePath, sku, registryName, or tag.");
            }

            // In a real scenario, you'd handle file upload logic here
            // For mock, just acknowledge the upload
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Model '" + sku + "' version '" + tag + "' uploaded to registry '" + registryName + "' successfully.";
            apiResponse.data = {
                    {"filePath", filePath},
                    {"sku", sku},
                    {"registryName", registryName},
                    {"tag", tag}
            };
            sendJsonResponse(res, apiResponse);

        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

// --- SSH Key Handlers ---
    /**
     * @brief Handles the creation of a new SSH key.
     *
     * Expects a JSON body with 'name' and 'publicKey', and optional 'description'.
     * Checks for missing parameters, duplicate key names, and creates a SSH key.
     * Sends a success or error JSON response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleCreateSSHKey(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string name = json_body.value("name", "");
            std::string publicKey = json_body.value("publicKey", "");
            std::string description = json_body.value("description", "");

            if (name.empty() || publicKey.empty()) {
                return sendErrorResponse(res, 400, "Missing required parameters: name or publicKey.");
            }

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(sshKeys.begin(), sshKeys.end(),
                                   [&](const Models::SSHKey& k) { return k.name == name; });
            if (it != sshKeys.end()) {
                return sendErrorResponse(res, 409, "SSH key '" + name + "' already exists.");
            }

            Models::SSHKey newKey;
            newKey.id = Utils::generateUniqueId("sshkey");
            newKey.name = name;
            newKey.publicKey = publicKey;
            newKey.description = description;
            sshKeys.push_back(newKey);

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "SSH key '" + name + "' created successfully.";
            apiResponse.data = newKey.toJsonString();
            sendJsonResponse(res, apiResponse);

        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

    /**
     * @brief Handles the deletion of an SSH key.
     *
     * Extracts the key ID from the URL path.
     * Deletes the corresponding SSH key if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleDeleteSSHKey(const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto initial_size = sshKeys.size();
        sshKeys.erase(std::remove_if(sshKeys.begin(), sshKeys.end(),
                                         [&](const Models::SSHKey& k) { return k.id == id; }),
                          sshKeys.end());

        if (sshKeys.size() < initial_size) {
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "SSH key '" + id + "' deleted successfully.";
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "SSH key '" + id + "' not found.");
        }
    }

    /**
     * @brief Handles retrieval of a single SSH key by ID.
     *
     * Returns redacted key metadata when found.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleGetSSHKey(const httplib::Request& req, httplib::Response& res) {
        const std::string id = req.matches[1];
        if (id.empty()) {
            return sendErrorResponse(res, 400, "SSH key ID is required.");
        }

        std::lock_guard<std::mutex> lock(dataMutex);
        const auto it = std::find_if(sshKeys.begin(), sshKeys.end(),
                                     [&](const Models::SSHKey& k) { return k.id == id; });
        if (it == sshKeys.end()) {
            return sendErrorResponse(res, 404, "SSH key '" + id + "' not found.");
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "SSH key '" + id + "' retrieved successfully.";
        apiResponse.data = it->toJsonString();
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles listing all SSH keys, with optional name filtering.
     *
     * Supports a 'filter-name' query parameter to filter keys by name.
     * Returns a JSON array of SSH key objects.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListSSHKeys(const httplib::Request& req, httplib::Response& res) {
        std::string filterName = req.get_param_value("filter-name");

        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json keyList = nlohmann::json::array();
        for (const auto& key : sshKeys) {
            if (filterName.empty() || key.name.find(filterName) != std::string::npos) {
                keyList.push_back(key.toJsonString());
            }
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "SSH keys listed successfully.";
        apiResponse.data = keyList;
        sendJsonResponse(res, apiResponse);
    }

    bool APIRoutes::extractTraceyProvisioningRequest(const nlohmann::json& payload,
                                                     std::string& agentIdOut,
                                                     std::string& statusAddrOut,
                                                     std::string& reasonOut) const {
        agentIdOut.clear();
        statusAddrOut.clear();
        reasonOut.clear();

        if (!payload.is_object()) {
            reasonOut = "Invalid provisioning payload.";
            return false;
        }

        if (!payload.contains("tracey")) {
            if (traceyEnforceManagedResources) {
                reasonOut = "Tracey configuration is required. Provide tracey.agent_id and optional tracey.status_addr.";
                return false;
            }
            return true;
        }

        const auto& tracey = payload["tracey"];
        if (!tracey.is_object()) {
            reasonOut = "Invalid tracey configuration. Expected object.";
            return false;
        }

        agentIdOut = trim(tracey.value("agent_id", tracey.value("agentId", "")));
        statusAddrOut = trim(tracey.value("status_addr", tracey.value("statusAddr", "")));
        const bool autoDiscovery = tracey.value("auto_discovery", true);

        if (agentIdOut.empty()) {
            reasonOut = "Missing required tracey.agent_id for managed resource provisioning.";
            return false;
        }
        if (statusAddrOut.empty() && !autoDiscovery) {
            reasonOut = "tracey.status_addr is required when tracey.auto_discovery is false.";
            return false;
        }

        if (!statusAddrOut.empty()) {
            TraceyEndpoint endpoint;
            if (!parseTraceyEndpoint(statusAddrOut, endpoint)) {
                reasonOut = "tracey.status_addr is not a valid HTTP(S) endpoint.";
                return false;
            }
            if (!isLocalOrPrivateHost(endpoint.host, traceyAllowPublicAddr)) {
                reasonOut = "tracey.status_addr is outside allowed network ranges.";
                return false;
            }
            statusAddrOut = endpoint.normalized;
        }

        return true;
    }

    void APIRoutes::upsertTraceyRequirementLocked(const std::string& resourceType,
                                                  const std::string& resourceName,
                                                  const std::string& region,
                                                  const std::string& agentId,
                                                  const std::string& statusAddr,
                                                  int64_t nowMs) {
        if (agentId.empty()) {
            return;
        }

        const std::string key = resourceType + ":" + toLower(trim(resourceName));
        TraceyRequirement requirement;
        requirement.key = key;
        requirement.resourceType = resourceType;
        requirement.resourceName = resourceName;
        requirement.region = region;
        requirement.expectedAgentId = agentId;
        requirement.expectedStatusAddr = statusAddr;
        requirement.createdEpochMs = nowMs;
        if (!key.empty() && key.back() != ':') {
            traceyRequirements[key] = requirement;
        }

        auto& agent = traceyAgents[agentId];
        agent.agentId = agentId;
        agent.source = mergeTraceySource(agent.source, "continuum-provisioning");
        if (agent.cluster.empty()) {
            agent.cluster = resourceName;
        }
        if (agent.status.empty()) {
            agent.status = "unknown";
        }
        if (agent.metrics.is_null() || !agent.metrics.is_object()) {
            agent.metrics = nlohmann::json::object();
        }
        agent.metrics["managed_by_continuum"] = true;
        agent.metrics["managed_resource_type"] = resourceType;
        agent.metrics["managed_resource_name"] = resourceName;
        agent.metrics["managed_resource_region"] = region;
        agent.metrics["managed_since_epoch_ms"] = nowMs;
        if (!statusAddr.empty()) {
            agent.statusAddr = statusAddr;
            agent.nextQueryEpochMs = nowMs;
            TraceyEndpoint endpoint;
            if (parseTraceyEndpoint(statusAddr, endpoint)) {
                agent.linkSecurity = deriveLinkSecurity(agent.signaturePresent, &endpoint, traceyTlsVerify);
            }
        }
        if (agent.linkState.empty()) {
            agent.linkState = "required";
        }
    }

    void APIRoutes::removeTraceyRequirementLocked(const std::string& resourceType, const std::string& resourceName) {
        const std::string normalized = toLower(trim(resourceName));
        if (normalized.empty()) {
            return;
        }

        const std::string directKey = resourceType + ":" + normalized;
        auto direct = traceyRequirements.find(directKey);
        if (direct != traceyRequirements.end()) {
            traceyRequirements.erase(direct);
            return;
        }

        for (auto it = traceyRequirements.begin(); it != traceyRequirements.end();) {
            if (it->second.resourceType == resourceType &&
                toLower(trim(it->second.resourceName)) == normalized) {
                it = traceyRequirements.erase(it);
            } else {
                ++it;
            }
        }
    }

// --- VM Handlers ---
    /**
     * @brief Handles the creation of a new Virtual Machine.
     *
     * Expects a JSON body with 'name', 'sku', 'region', 'osImage', and 'publicKeyId',
     * and optional 'initScript'.
     * Checks for missing parameters, duplicate VM names, valid publicKeyId,
     * and creates a VM.
     * Sends a success or error JSON response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleCreateVM(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string name = json_body.value("name", "");
            std::string sku = json_body.value("sku", "");
            std::string region = json_body.value("region", "");
            std::string osImage = json_body.value("osImage", "");
            std::string publicKeyId = json_body.value("publicKeyId", "");
            std::string initScript = json_body.value("initScript", "");
            std::string traceyAgentId;
            std::string traceyStatusAddr;
            std::string traceyReason;

            if (name.empty() || sku.empty() || region.empty() || osImage.empty() || publicKeyId.empty()) {
                return sendErrorResponse(res, 400, "Missing required parameters: name, sku, region, osImage, or publicKeyId.");
            }
            if (!extractTraceyProvisioningRequest(json_body, traceyAgentId, traceyStatusAddr, traceyReason)) {
                return sendErrorResponse(res, 400, traceyReason);
            }

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(vms.begin(), vms.end(),
                                   [&](const Models::VM& vm) { return vm.name == name; });
            if (it != vms.end()) {
                return sendErrorResponse(res, 409, "VM '" + name + "' already exists.");
            }

            // Check if publicKeyId exists in sshKeys
            auto sshKeyIt = std::find_if(sshKeys.begin(), sshKeys.end(),
                                         [&](const Models::SSHKey& k) { return k.id == publicKeyId; });
            if (sshKeyIt == sshKeys.end()) {
                return sendErrorResponse(res, 400, "Invalid publicKeyId: '" + publicKeyId + "' not found.");
            }


            Models::VM newVM;
            newVM.id = Utils::generateUniqueId("vm");
            newVM.name = name;
            newVM.sku = sku;
            newVM.region = region;
            newVM.osImage = osImage;
            newVM.publicKeyId = publicKeyId;
            newVM.initScript = initScript;
            newVM.status = "Creating";
            vms.push_back(newVM);
            upsertTraceyRequirementLocked("vm", name, region, traceyAgentId, traceyStatusAddr, nowEpochMs());

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM '" + name + "' created successfully.";
            apiResponse.data = newVM.toJsonString();
            if (!traceyAgentId.empty()) {
                apiResponse.data["tracey"] = {
                        {"required", true},
                        {"agent_id", traceyAgentId},
                        {"status_addr", traceyStatusAddr},
                        {"compliance_state", "pending"},
                        {"reason", "Waiting for Tracey agent heartbeat/discovery after provisioning."}
                };
            } else {
                apiResponse.data["tracey"] = {
                        {"required", traceyEnforceManagedResources}
                };
            }
            sendJsonResponse(res, apiResponse);

        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

    /**
     * @brief Handles the deletion of a Virtual Machine.
     *
     * Extracts the VM ID from the URL path.
     * Deletes the corresponding VM if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleDeleteVM(const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto initial_size = vms.size();
        std::string removedVmName;
        vms.erase(std::remove_if(vms.begin(), vms.end(),
                                     [&](const Models::VM& vm) {
                                         if (vm.id == id) {
                                             removedVmName = vm.name;
                                             return true;
                                         }
                                         return false;
                                     }),
                      vms.end());

        if (vms.size() < initial_size) {
            removeTraceyRequirementLocked("vm", removedVmName);
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM '" + id + "' deleted successfully.";
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "VM '" + id + "' not found.");
        }
    }

    /**
     * @brief Handles retrieving details of a specific Virtual Machine.
     *
     * Extracts the VM ID from the URL path.
     * Finds and returns the VM's details if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleGetVM(const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = std::find_if(vms.begin(), vms.end(),
                               [&](const Models::VM& vm) { return vm.id == id; });

        if (it != vms.end()) {
            nlohmann::json vmData = it->toJsonString();
            const std::string reqKey = "vm:" + toLower(trim(it->name));
            auto reqIt = traceyRequirements.find(reqKey);
            if (reqIt != traceyRequirements.end()) {
                const auto& requirement = reqIt->second;
                auto agentIt = traceyAgents.find(requirement.expectedAgentId);
                const int64_t nowMs = nowEpochMs();
                const int64_t ageMs = (requirement.createdEpochMs > 0 && nowMs > requirement.createdEpochMs)
                                      ? (nowMs - requirement.createdEpochMs)
                                      : 0;
                const bool withinGrace = ageMs <= (traceyRequirementGraceSeconds * 1000);
                std::string complianceState = "pending";
                std::string reason = "Waiting for Tracey agent heartbeat/discovery.";
                if (agentIt != traceyAgents.end()) {
                    const auto& agent = agentIt->second;
                    const int64_t lastSignalMs = std::max(agent.lastSeenEpochMs, agent.lastAnnouncementEpochMs);
                    const bool stale = (lastSignalMs <= 0) ||
                                       (nowMs > lastSignalMs && (nowMs - lastSignalMs) > (traceyStaleAfterSeconds * 1000));
                    const bool reporting = agent.statusReachable ||
                                           agent.source.find("heartbeat") != std::string::npos ||
                                           agent.source.find("discovery") != std::string::npos;
                    if (!stale && reporting) {
                        complianceState = "compliant";
                        reason = "Tracey agent is actively reporting.";
                    } else if (!withinGrace) {
                        complianceState = "noncompliant";
                        reason = "Tracey agent is stale or not reporting after grace period.";
                    }
                } else if (!withinGrace) {
                    complianceState = "noncompliant";
                    reason = "Required Tracey agent not discovered after grace period.";
                }
                vmData["tracey"] = {
                        {"required", true},
                        {"agent_id", requirement.expectedAgentId},
                        {"status_addr", requirement.expectedStatusAddr},
                        {"compliance_state", complianceState},
                        {"reason", reason}
                };
            } else {
                vmData["tracey"] = {
                        {"required", traceyEnforceManagedResources},
                        {"compliance_state", "untracked"}
                };
            }

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM details retrieved.";
            apiResponse.data = vmData;
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "VM '" + id + "' not found.");
        }
    }

    /**
     * @brief Handles listing all Virtual Machines, with optional name filtering.
     *
     * Supports a 'filter-name' query parameter to filter VMs by name.
     * Returns a JSON array of VM objects.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListVMs(const httplib::Request& req, httplib::Response& res) {
        std::string filterName = req.get_param_value("filter-name");
        const int64_t nowMs = nowEpochMs();

        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json vmList = nlohmann::json::array();
        for (const auto& vm : vms) {
            if (filterName.empty() || vm.name.find(filterName) != std::string::npos) {
                nlohmann::json vmJson = vm.toJsonString();
                const std::string reqKey = "vm:" + toLower(trim(vm.name));
                auto reqIt = traceyRequirements.find(reqKey);
                if (reqIt != traceyRequirements.end()) {
                    const auto& requirement = reqIt->second;
                    auto agentIt = traceyAgents.find(requirement.expectedAgentId);
                    const int64_t ageMs = (requirement.createdEpochMs > 0 && nowMs > requirement.createdEpochMs)
                                          ? (nowMs - requirement.createdEpochMs)
                                          : 0;
                    const bool withinGrace = ageMs <= (traceyRequirementGraceSeconds * 1000);
                    std::string complianceState = "pending";
                    if (agentIt != traceyAgents.end()) {
                        const auto& agent = agentIt->second;
                        const int64_t lastSignalMs = std::max(agent.lastSeenEpochMs, agent.lastAnnouncementEpochMs);
                        const bool stale = (lastSignalMs <= 0) ||
                                           (nowMs > lastSignalMs && (nowMs - lastSignalMs) > (traceyStaleAfterSeconds * 1000));
                        const bool reporting = agent.statusReachable ||
                                               agent.source.find("heartbeat") != std::string::npos ||
                                               agent.source.find("discovery") != std::string::npos;
                        if (!stale && reporting) {
                            complianceState = "compliant";
                        } else if (!withinGrace) {
                            complianceState = "noncompliant";
                        }
                    } else if (!withinGrace) {
                        complianceState = "noncompliant";
                    }
                    vmJson["tracey"] = {
                            {"required", true},
                            {"agent_id", requirement.expectedAgentId},
                            {"status_addr", requirement.expectedStatusAddr},
                            {"compliance_state", complianceState}
                    };
                } else {
                    vmJson["tracey"] = {
                            {"required", traceyEnforceManagedResources},
                            {"compliance_state", "untracked"}
                    };
                }
                vmList.push_back(vmJson);
            }
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "VMs listed successfully.";
        apiResponse.data = vmList;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles listing available VM locations (regions).
     *
     * Returns a predefined list of VM locations.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListVMLocations(const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json locations = {
                {"name", "gb-mids"},
                {"name", "gb-mids"},
                {"name", "us-central"}
        };

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "VM locations listed successfully.";
        apiResponse.data = locations;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles listing available VM OS images.
     *
     * Returns a predefined list of VM OS images (SKUs).
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListVMOSImages(const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json osImages = {
                {"sku", "ubuntu-22.04"},
                {"sku", "windows-server-2022"},
                {"sku", "debian-11"}
        };

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "VM OS images listed successfully.";
        apiResponse.data = osImages;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles listing available VM SKUs (machine types).
     *
     * Returns a predefined list of VM SKUs.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListVMSKUs(const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json skus = {
                {"sku", "nmx-h100-80"},
                {"sku", "standard-a2"},
                {"sku", "premium-gpu-p100"}
        };

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "VM SKUs listed successfully.";
        apiResponse.data = skus;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles restarting a Virtual Machine.
     *
     * Extracts the VM ID from the URL path.
     * Changes the status of the VM to "Restarting" if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleRestartVM(const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = std::find_if(vms.begin(), vms.end(),
                               [&](const Models::VM& vm) { return vm.id == id; });

        if (it != vms.end()) {
            it->status = "Restarting"; // Change status
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM '" + id + "' is restarting.";
            apiResponse.data = it->toJsonString();
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "VM '" + id + "' not found.");
        }
    }

    /**
     * @brief Handles resuming a suspended Virtual Machine.
     *
     * Extracts the VM ID from the URL path.
     * Changes the status of the VM to "Running" if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleResumeVM(const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = std::find_if(vms.begin(), vms.end(),
                               [&](const Models::VM& vm) { return vm.id == id; });

        if (it != vms.end()) {
            it->status = "Running"; // Change status
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM '" + id + "' resumed successfully.";
            apiResponse.data = it->toJsonString();
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "VM '" + id + "' not found.");
        }
    }

    /**
     * @brief Handles suspending a running Virtual Machine.
     *
     * Extracts the VM ID from the URL path.
     * Changes the status of the VM to "Suspended" if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleSuspendVM(const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = std::find_if(vms.begin(), vms.end(),
                               [&](const Models::VM& vm) { return vm.id == id; });

        if (it != vms.end()) {
            it->status = "Suspended"; // Change status
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM '" + id + "' suspended successfully.";
            apiResponse.data = it->toJsonString();
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "VM '" + id + "' not found.");
        }
    }

// --- OpenShift Handlers ---
    void APIRoutes::handleServerVersion(const httplib::Request& req, httplib::Response& res) {
        (void)req;

        const auto releaseInfo = VersionCheck::checkLatestRelease();

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Server version information retrieved successfully.";
        apiResponse.data = {
                {"component", "nmc_server"},
                {"current_version", releaseInfo.currentVersion},
                {"repository", releaseInfo.repository},
                {"release_check", {
                        {"attempted", true},
                        {"succeeded", releaseInfo.checkSucceeded},
                        {"latest_version", releaseInfo.latestVersion},
                        {"update_available", releaseInfo.updateAvailable},
                        {"message", releaseInfo.message}
                }}
        };
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleOpenShiftResources(const httplib::Request& req, httplib::Response& res) {
        Models::CloudResponse apiResponse = openShiftClient->getResources();
        sendJsonResponse(res, apiResponse);
        res.status = apiResponse.statusCode;
    }

    void APIRoutes::handleOpenShiftClusters(const httplib::Request& req, httplib::Response& res) {
        Models::CloudResponse apiResponse = openShiftClient->listClusters();
        if (apiResponse.success) {
            if (apiResponse.data.is_array()) {
                for (auto& cluster : apiResponse.data) {
                    normalizeClusterStatus(cluster);
                }
            } else if (apiResponse.data.is_object()) {
                if (apiResponse.data.contains("data") && apiResponse.data["data"].is_array()) {
                    for (auto& cluster : apiResponse.data["data"]) {
                        normalizeClusterStatus(cluster);
                    }
                }
                if (apiResponse.data.contains("clusters") && apiResponse.data["clusters"].is_array()) {
                    for (auto& cluster : apiResponse.data["clusters"]) {
                        normalizeClusterStatus(cluster);
                    }
                }
            }
        }
        sendJsonResponse(res, apiResponse);
        res.status = apiResponse.statusCode;
    }

    void APIRoutes::handleOpenShiftClusterDetails(const httplib::Request& req, httplib::Response& res) {
        const std::string identifier = req.matches.size() > 1 ? trim(req.matches[1].str()) : "";
        if (identifier.empty()) {
            return sendErrorResponse(res, 400, "Missing OpenShift cluster identifier.");
        }

        Models::CloudResponse listResponse = openShiftClient->listClusters();
        if (!listResponse.success) {
            sendJsonResponse(res, listResponse);
            res.status = listResponse.statusCode;
            return;
        }

        const std::vector<nlohmann::json> clusters = collectClusterObjects(listResponse.data);
        if (clusters.empty()) {
            Models::CloudResponse notFound;
            notFound.success = false;
            notFound.statusCode = 404;
            notFound.message = "OpenShift cluster '" + identifier + "' not found.";
            notFound.data = nlohmann::json::object();
            sendJsonResponse(res, notFound);
            res.status = 404;
            return;
        }

        nlohmann::json selectedCluster = findClusterByIdentifier(clusters, identifier);

        if (selectedCluster.is_null()) {
            Models::CloudResponse notFound;
            notFound.success = false;
            notFound.statusCode = 404;
            notFound.message = "OpenShift cluster '" + identifier + "' not found.";
            notFound.data = nlohmann::json::object();
            sendJsonResponse(res, notFound);
            res.status = 404;
            return;
        }

        normalizeClusterStatus(selectedCluster);
        nlohmann::json details = buildClusterDetailsWithNetworking(selectedCluster);

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.statusCode = 200;
        apiResponse.message = "OpenShift cluster details retrieved.";
        apiResponse.data = details;
        sendJsonResponse(res, apiResponse);
        res.status = 200;
    }

    void APIRoutes::handleOpenShiftRequestCluster(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string traceyAgentId;
            std::string traceyStatusAddr;
            std::string traceyReason;

            // Validate minimum required fields before forwarding to oshift.
            const std::string name = json_body.value("name", "");
            const std::string organization = json_body.value("organization", "");
            const int gpuCount = json_body.value("gpu_count", 0);
            const std::string architecture = json_body.value("architecture", "");
            const std::string region = json_body.value("region", "");
            const std::string provider = json_body.value("provider", "on-prem");
            const std::set<std::string> allowedProviders = {"on-prem", "rosa", "aro", "gcp", "hybrid-burst", "openstack"};

            if (name.empty() || organization.empty() || architecture.empty() || region.empty() || gpuCount <= 0) {
                return sendErrorResponse(res, 400, "Missing required parameters: name, organization, gpu_count, architecture, or region.");
            }
            if (allowedProviders.find(provider) == allowedProviders.end()) {
                return sendErrorResponse(res, 400, "Invalid provider. Supported: on-prem, rosa, aro, gcp, hybrid-burst, openstack.");
            }
            if (provider == "hybrid-burst" && (!json_body.contains("burst_targets") || json_body["burst_targets"].empty())) {
                return sendErrorResponse(res, 400, "burst_targets is required when provider is hybrid-burst.");
            }
            if (!extractTraceyProvisioningRequest(json_body, traceyAgentId, traceyStatusAddr, traceyReason)) {
                return sendErrorResponse(res, 400, traceyReason);
            }

            Models::CloudResponse apiResponse = openShiftClient->requestCluster(json_body);
            if (apiResponse.success && apiResponse.data.is_object()) {
                if (apiResponse.data.contains("cluster")) {
                    normalizeClusterStatus(apiResponse.data["cluster"]);
                }
            }
            if (apiResponse.success && !traceyAgentId.empty()) {
                std::lock_guard<std::mutex> lock(dataMutex);
                upsertTraceyRequirementLocked("openshift_cluster", name, region, traceyAgentId, traceyStatusAddr, nowEpochMs());
                if (!apiResponse.data.is_object()) {
                    apiResponse.data = nlohmann::json::object();
                }
                apiResponse.data["tracey"] = {
                        {"required", true},
                        {"agent_id", traceyAgentId},
                        {"status_addr", traceyStatusAddr},
                        {"compliance_state", "pending"},
                        {"reason", "Waiting for Tracey agent heartbeat/discovery after provisioning."}
                };
            }
            sendJsonResponse(res, apiResponse);
            res.status = apiResponse.statusCode;
        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

    void APIRoutes::handleOpenStackResources(const httplib::Request& req, httplib::Response& res) {
        (void)req;
        Models::CloudResponse apiResponse = openStackClient->getResources();
        sendJsonResponse(res, apiResponse);
        res.status = apiResponse.statusCode;
    }

    void APIRoutes::handleOpenStackClusters(const httplib::Request& req, httplib::Response& res) {
        (void)req;
        Models::CloudResponse apiResponse = openStackClient->listClusters();
        if (apiResponse.success) {
            if (apiResponse.data.is_array()) {
                for (auto& cluster : apiResponse.data) {
                    normalizeClusterStatus(cluster);
                }
            } else if (apiResponse.data.is_object()) {
                if (apiResponse.data.contains("data") && apiResponse.data["data"].is_array()) {
                    for (auto& cluster : apiResponse.data["data"]) {
                        normalizeClusterStatus(cluster);
                    }
                }
                if (apiResponse.data.contains("clusters") && apiResponse.data["clusters"].is_array()) {
                    for (auto& cluster : apiResponse.data["clusters"]) {
                        normalizeClusterStatus(cluster);
                    }
                }
            }
        }
        sendJsonResponse(res, apiResponse);
        res.status = apiResponse.statusCode;
    }

    void APIRoutes::handleOpenStackClusterDetails(const httplib::Request& req, httplib::Response& res) {
        const std::string identifier = req.matches.size() > 1 ? trim(req.matches[1].str()) : "";
        if (identifier.empty()) {
            return sendErrorResponse(res, 400, "Missing OpenStack cluster identifier.");
        }

        Models::CloudResponse listResponse = openStackClient->listClusters();
        if (!listResponse.success) {
            sendJsonResponse(res, listResponse);
            res.status = listResponse.statusCode;
            return;
        }

        const std::vector<nlohmann::json> clusters = collectClusterObjects(listResponse.data);
        if (clusters.empty()) {
            Models::CloudResponse notFound;
            notFound.success = false;
            notFound.statusCode = 404;
            notFound.message = "OpenStack cluster '" + identifier + "' not found.";
            notFound.data = nlohmann::json::object();
            sendJsonResponse(res, notFound);
            res.status = 404;
            return;
        }

        nlohmann::json selectedCluster = findClusterByIdentifier(clusters, identifier);
        if (selectedCluster.is_null()) {
            Models::CloudResponse notFound;
            notFound.success = false;
            notFound.statusCode = 404;
            notFound.message = "OpenStack cluster '" + identifier + "' not found.";
            notFound.data = nlohmann::json::object();
            sendJsonResponse(res, notFound);
            res.status = 404;
            return;
        }

        normalizeClusterStatus(selectedCluster);
        const nlohmann::json details = buildClusterDetailsWithNetworking(selectedCluster);

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.statusCode = 200;
        apiResponse.message = "OpenStack cluster details retrieved.";
        apiResponse.data = details;
        sendJsonResponse(res, apiResponse);
        res.status = 200;
    }

    void APIRoutes::handleOpenStackRequestCluster(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string traceyAgentId;
            std::string traceyStatusAddr;
            std::string traceyReason;

            const std::string name = json_body.value("name", "");
            const std::string organization = json_body.value("organization", "");
            const int gpuCount = json_body.value("gpu_count", 0);
            const std::string architecture = json_body.value("architecture", "");
            const std::string region = json_body.value("region", "");
            const std::string provider = json_body.value("provider", "openstack");
            const std::set<std::string> allowedProviders = {"openstack", "openstack-heat", "openstack-magnum", "hybrid-burst"};

            if (name.empty() || organization.empty() || architecture.empty() || region.empty() || gpuCount <= 0) {
                return sendErrorResponse(res, 400, "Missing required parameters: name, organization, gpu_count, architecture, or region.");
            }
            if (allowedProviders.find(provider) == allowedProviders.end()) {
                return sendErrorResponse(res, 400, "Invalid provider. Supported: openstack, openstack-heat, openstack-magnum, hybrid-burst.");
            }
            if (provider == "hybrid-burst" && (!json_body.contains("burst_targets") || json_body["burst_targets"].empty())) {
                return sendErrorResponse(res, 400, "burst_targets is required when provider is hybrid-burst.");
            }
            if (!extractTraceyProvisioningRequest(json_body, traceyAgentId, traceyStatusAddr, traceyReason)) {
                return sendErrorResponse(res, 400, traceyReason);
            }

            Models::CloudResponse apiResponse = openStackClient->requestCluster(json_body);
            if (apiResponse.success && apiResponse.data.is_object() && apiResponse.data.contains("cluster")) {
                normalizeClusterStatus(apiResponse.data["cluster"]);
            }
            if (apiResponse.success && !traceyAgentId.empty()) {
                std::lock_guard<std::mutex> lock(dataMutex);
                upsertTraceyRequirementLocked("openstack_cluster", name, region, traceyAgentId, traceyStatusAddr, nowEpochMs());
                if (!apiResponse.data.is_object()) {
                    apiResponse.data = nlohmann::json::object();
                }
                apiResponse.data["tracey"] = {
                        {"required", true},
                        {"agent_id", traceyAgentId},
                        {"status_addr", traceyStatusAddr},
                        {"compliance_state", "pending"},
                        {"reason", "Waiting for Tracey agent heartbeat/discovery after provisioning."}
                };
            }
            sendJsonResponse(res, apiResponse);
            res.status = apiResponse.statusCode;
        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

    void APIRoutes::handleProxmoxResources(const httplib::Request& req, httplib::Response& res) {
        (void)req;
        Models::CloudResponse apiResponse = proxmoxClient->getResources();
        sendJsonResponse(res, apiResponse);
        res.status = apiResponse.statusCode;
    }

    void APIRoutes::handleProxmoxClusters(const httplib::Request& req, httplib::Response& res) {
        (void)req;
        Models::CloudResponse apiResponse = proxmoxClient->listClusters();
        if (apiResponse.success) {
            if (apiResponse.data.is_array()) {
                for (auto& cluster : apiResponse.data) {
                    normalizeClusterStatus(cluster);
                }
            } else if (apiResponse.data.is_object()) {
                if (apiResponse.data.contains("data") && apiResponse.data["data"].is_array()) {
                    for (auto& cluster : apiResponse.data["data"]) {
                        normalizeClusterStatus(cluster);
                    }
                }
                if (apiResponse.data.contains("clusters") && apiResponse.data["clusters"].is_array()) {
                    for (auto& cluster : apiResponse.data["clusters"]) {
                        normalizeClusterStatus(cluster);
                    }
                }
            }
        }
        sendJsonResponse(res, apiResponse);
        res.status = apiResponse.statusCode;
    }

    void APIRoutes::handleProxmoxClusterDetails(const httplib::Request& req, httplib::Response& res) {
        const std::string identifier = req.matches.size() > 1 ? trim(req.matches[1].str()) : "";
        if (identifier.empty()) {
            return sendErrorResponse(res, 400, "Missing Proxmox cluster identifier.");
        }

        Models::CloudResponse listResponse = proxmoxClient->listClusters();
        if (!listResponse.success) {
            sendJsonResponse(res, listResponse);
            res.status = listResponse.statusCode;
            return;
        }

        const std::vector<nlohmann::json> clusters = collectClusterObjects(listResponse.data);
        if (clusters.empty()) {
            Models::CloudResponse notFound;
            notFound.success = false;
            notFound.statusCode = 404;
            notFound.message = "Proxmox cluster '" + identifier + "' not found.";
            notFound.data = nlohmann::json::object();
            sendJsonResponse(res, notFound);
            res.status = 404;
            return;
        }

        nlohmann::json selectedCluster = findClusterByIdentifier(clusters, identifier);
        if (selectedCluster.is_null()) {
            Models::CloudResponse notFound;
            notFound.success = false;
            notFound.statusCode = 404;
            notFound.message = "Proxmox cluster '" + identifier + "' not found.";
            notFound.data = nlohmann::json::object();
            sendJsonResponse(res, notFound);
            res.status = 404;
            return;
        }

        normalizeClusterStatus(selectedCluster);
        const nlohmann::json details = buildClusterDetailsWithNetworking(selectedCluster);

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.statusCode = 200;
        apiResponse.message = "Proxmox cluster details retrieved.";
        apiResponse.data = details;
        sendJsonResponse(res, apiResponse);
        res.status = 200;
    }

    void APIRoutes::handleProxmoxRequestCluster(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string traceyAgentId;
            std::string traceyStatusAddr;
            std::string traceyReason;

            const std::string name = json_body.value("name", "");
            const std::string organization = json_body.value("organization", "");
            const int gpuCount = json_body.value("gpu_count", 0);
            const std::string architecture = json_body.value("architecture", "");
            const std::string region = json_body.value("region", "");
            const std::string provider = json_body.value("provider", "proxmox");
            const std::set<std::string> allowedProviders = {"proxmox", "proxmox-ve", "hybrid-burst"};

            if (name.empty() || organization.empty() || architecture.empty() || region.empty() || gpuCount <= 0) {
                return sendErrorResponse(res, 400, "Missing required parameters: name, organization, gpu_count, architecture, or region.");
            }
            if (allowedProviders.find(provider) == allowedProviders.end()) {
                return sendErrorResponse(res, 400, "Invalid provider. Supported: proxmox, proxmox-ve, hybrid-burst.");
            }
            if (provider == "hybrid-burst" && (!json_body.contains("burst_targets") || json_body["burst_targets"].empty())) {
                return sendErrorResponse(res, 400, "burst_targets is required when provider is hybrid-burst.");
            }
            if (!extractTraceyProvisioningRequest(json_body, traceyAgentId, traceyStatusAddr, traceyReason)) {
                return sendErrorResponse(res, 400, traceyReason);
            }

            Models::CloudResponse apiResponse = proxmoxClient->requestCluster(json_body);
            if (apiResponse.success && apiResponse.data.is_object() && apiResponse.data.contains("cluster")) {
                normalizeClusterStatus(apiResponse.data["cluster"]);
            }
            if (apiResponse.success && !traceyAgentId.empty()) {
                std::lock_guard<std::mutex> lock(dataMutex);
                upsertTraceyRequirementLocked("proxmox_cluster", name, region, traceyAgentId, traceyStatusAddr, nowEpochMs());
                if (!apiResponse.data.is_object()) {
                    apiResponse.data = nlohmann::json::object();
                }
                apiResponse.data["tracey"] = {
                        {"required", true},
                        {"agent_id", traceyAgentId},
                        {"status_addr", traceyStatusAddr},
                        {"compliance_state", "pending"},
                        {"reason", "Waiting for Tracey agent heartbeat/discovery after provisioning."}
                };
            }
            sendJsonResponse(res, apiResponse);
            res.status = apiResponse.statusCode;
        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

// --- Tracey Agent Handlers ---
    void APIRoutes::runAarnnDiscoveryLoop() {
        constexpr size_t maxDatagramBytes = 2048;
        std::vector<char> buffer(maxDatagramBytes);
        int sockFd = -1;
        int64_t nextBindAttemptMs = 0;
        int64_t lastMaintenanceMs = 0;

        while (!stopAarnnDiscovery.load()) {
            const int64_t nowMs = nowEpochMs();

            if (sockFd < 0 && nowMs >= nextBindAttemptMs) {
                sockFd = ::socket(AF_INET, SOCK_DGRAM, 0);
                if (sockFd < 0) {
                    nextBindAttemptMs = nowMs + 3000;
                    std::cerr << "[WARN] AARNN discovery socket() failed: " << std::strerror(errno) << std::endl;
                } else {
                    int enable = 1;
                    (void)::setsockopt(sockFd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
#ifdef SO_REUSEPORT
                    (void)::setsockopt(sockFd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
#endif
                    sockaddr_in bindAddr{};
                    bindAddr.sin_family = AF_INET;
                    bindAddr.sin_port = htons(static_cast<uint16_t>(aarnnDiscoveryPort));
                    if (aarnnDiscoveryBindAddr == "*" || aarnnDiscoveryBindAddr.empty() || aarnnDiscoveryBindAddr == "0.0.0.0") {
                        bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
                    } else if (::inet_pton(AF_INET, aarnnDiscoveryBindAddr.c_str(), &bindAddr.sin_addr) != 1) {
                        std::cerr << "[WARN] Invalid NMC_AARNN_DISCOVERY_BIND_ADDR '" << aarnnDiscoveryBindAddr
                                  << "': expected IPv4 address" << std::endl;
                        ::close(sockFd);
                        sockFd = -1;
                        nextBindAttemptMs = nowMs + 10000;
                    }

                    if (sockFd >= 0 && ::bind(sockFd, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) != 0) {
                        std::cerr << "[WARN] AARNN discovery bind(" << aarnnDiscoveryBindAddr << ":" << aarnnDiscoveryPort
                                  << ") failed: " << std::strerror(errno) << std::endl;
                        ::close(sockFd);
                        sockFd = -1;
                        nextBindAttemptMs = nowMs + 5000;
                    }
                }
            }

            if (sockFd >= 0) {
                pollfd pfd{};
                pfd.fd = sockFd;
                pfd.events = POLLIN;
                const int pollRc = ::poll(&pfd, 1, 200);
                if (pollRc > 0 && (pfd.revents & POLLIN)) {
                    sockaddr_storage sender{};
                    socklen_t senderLen = sizeof(sender);
                    const ssize_t bytes = ::recvfrom(sockFd, buffer.data(), buffer.size(), 0,
                                                     reinterpret_cast<sockaddr*>(&sender), &senderLen);
                    if (bytes > 0) {
                        std::string senderAddress = "unknown";
                        char addrBuf[INET6_ADDRSTRLEN] = {0};
                        if (sender.ss_family == AF_INET) {
                            const auto* sin = reinterpret_cast<sockaddr_in*>(&sender);
                            if (::inet_ntop(AF_INET, &sin->sin_addr, addrBuf, sizeof(addrBuf)) != nullptr) {
                                senderAddress = addrBuf;
                            }
                        } else if (sender.ss_family == AF_INET6) {
                            const auto* sin6 = reinterpret_cast<sockaddr_in6*>(&sender);
                            if (::inet_ntop(AF_INET6, &sin6->sin6_addr, addrBuf, sizeof(addrBuf)) != nullptr) {
                                senderAddress = addrBuf;
                            }
                        }

                        const std::string payload(buffer.data(), static_cast<size_t>(bytes));
                        ingestAarnnDiscoveryAnnouncement(trimLineEnd(payload), senderAddress, nowEpochMs());
                    } else if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                        std::cerr << "[WARN] AARNN discovery recvfrom() failed: " << std::strerror(errno) << std::endl;
                        ::close(sockFd);
                        sockFd = -1;
                        nextBindAttemptMs = nowEpochMs() + 3000;
                    }
                } else if (pollRc < 0 && errno != EINTR) {
                    std::cerr << "[WARN] AARNN discovery poll() failed: " << std::strerror(errno) << std::endl;
                    ::close(sockFd);
                    sockFd = -1;
                    nextBindAttemptMs = nowEpochMs() + 3000;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }

            const int64_t maintenanceNowMs = nowEpochMs();
            if (maintenanceNowMs - lastMaintenanceMs >= 1000) {
                markAarnnStaleOrchestrators(maintenanceNowMs);

                std::vector<std::pair<std::string, std::string>> duePolls;
                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    duePolls.reserve(aarnnOrchestrators.size());
                    for (auto& [mapKey, record] : aarnnOrchestrators) {
                        if (record.grpcUrl.empty() || record.stale) {
                            continue;
                        }
                        if (record.nextPollEpochMs <= 0 || maintenanceNowMs >= record.nextPollEpochMs) {
                            duePolls.emplace_back(mapKey, record.grpcUrl);
                            record.nextPollEpochMs = maintenanceNowMs + aarnnStatusPollMs;
                        }
                    }
                }

                for (const auto& pendingPoll : duePolls) {
                    pollAarnnOrchestratorStatus(pendingPoll.first, pendingPoll.second, maintenanceNowMs);
                }

                lastMaintenanceMs = maintenanceNowMs;
            }
        }

        if (sockFd >= 0) {
            ::close(sockFd);
        }
    }

    void APIRoutes::ingestAarnnDiscoveryAnnouncement(const std::string& payload,
                                                     const std::string& senderAddress,
                                                     int64_t receivedAtMs) {
        constexpr const char* kBeaconPrefix = "NEUROMORPHIC_ORCHESTRATOR:";
        const std::string trimmedPayload = trim(payload);
        if (trimmedPayload.rfind(kBeaconPrefix, 0) != 0) {
            return;
        }

        std::string rawEndpoint = trim(trimmedPayload.substr(std::strlen(kBeaconPrefix)));
        if (rawEndpoint.empty()) {
            return;
        }
        if (senderAddress != "unknown") {
            if (rawEndpoint.rfind("http://0.0.0.0", 0) == 0) {
                rawEndpoint.replace(7, 7, senderAddress);
            } else if (rawEndpoint.rfind("https://0.0.0.0", 0) == 0) {
                rawEndpoint.replace(8, 7, senderAddress);
            } else if (rawEndpoint.rfind("0.0.0.0", 0) == 0) {
                rawEndpoint.replace(0, 7, senderAddress);
            }
        }

        TraceyEndpoint endpoint;
        if (!parseTraceyEndpoint(rawEndpoint, endpoint)) {
            return;
        }
        if ((endpoint.host == "0.0.0.0" || endpoint.host == "localhost") && senderAddress != "unknown") {
            endpoint.host = senderAddress;
            endpoint.normalized = normalizedUrlWithHost(endpoint, endpoint.host);
        }
        if (!isLocalOrPrivateHost(endpoint.host, aarnnAllowPublicAddr)) {
            return;
        }

        std::lock_guard<std::mutex> lock(dataMutex);
        auto& record = aarnnOrchestrators[endpoint.normalized];
        if (record.firstSeenEpochMs <= 0) {
            record.firstSeenEpochMs = receivedAtMs;
            record.nextPollEpochMs = receivedAtMs;
        }
        record.orchestratorId = buildAarnnSyntheticId("aarnn-orchestrator", endpoint.host, endpoint.port);
        record.clusterId = buildAarnnSyntheticId("aarnn-cluster", endpoint.host, endpoint.port);
        record.grpcUrl = endpoint.normalized;
        record.host = endpoint.host;
        record.port = endpoint.port;
        record.source = "discovery";
        record.discoveryProtocol = "udp-beacon";
        record.lastSeenEpochMs = std::max(record.lastSeenEpochMs, receivedAtMs);
        record.stale = false;
        if (record.lastError == "AARNN orchestrator beacon is stale.") {
            record.lastError.clear();
        }
    }

    void APIRoutes::pollAarnnOrchestratorStatus(const std::string& mapKey, const std::string& grpcUrl, int64_t nowMs) {
        nlohmann::json statusPayload;
        std::string error;
        const bool ok = fetchAarnnStatusViaControlPlane(grpcUrl, nowMs, statusPayload, error);

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = aarnnOrchestrators.find(mapKey);
        if (it == aarnnOrchestrators.end()) {
            return;
        }

        auto& record = it->second;
        record.lastPolledEpochMs = nowMs;
        record.nextPollEpochMs = nowMs + aarnnStatusPollMs;
        if (ok) {
            record.statusReachable = true;
            record.statusCache = statusPayload;
            record.lastError.clear();
            return;
        }
        record.statusReachable = false;
        record.lastError = error;
    }

    void APIRoutes::markAarnnStaleOrchestrators(int64_t nowMs) {
        std::lock_guard<std::mutex> lock(dataMutex);
        const int64_t staleAfterMs = std::max<int64_t>(4, aarnnStaleAfterSeconds) * 1000;
        for (auto& [mapKey, record] : aarnnOrchestrators) {
            (void)mapKey;
            if (record.lastSeenEpochMs <= 0) {
                continue;
            }
            const bool staleNow = nowMs > record.lastSeenEpochMs && (nowMs - record.lastSeenEpochMs) > staleAfterMs;
            record.stale = staleNow;
            if (staleNow) {
                record.statusReachable = false;
                if (record.lastError.empty()) {
                    record.lastError = "AARNN orchestrator beacon is stale.";
                }
            }
        }
    }

    void APIRoutes::runTraceyDiscoveryLoop() {
        constexpr size_t maxDatagramBytes = 8192;
        std::vector<char> buffer(maxDatagramBytes);
        int sockFd = -1;
        int64_t nextBindAttemptMs = 0;
        int64_t lastMaintenanceMs = 0;

        while (!stopTraceyDiscovery.load()) {
            const int64_t nowMs = nowEpochMs();

            if (sockFd < 0 && nowMs >= nextBindAttemptMs) {
                sockFd = ::socket(AF_INET, SOCK_DGRAM, 0);
                if (sockFd < 0) {
                    nextBindAttemptMs = nowMs + 3000;
                    std::cerr << "[WARN] Tracey discovery socket() failed: " << std::strerror(errno) << std::endl;
                } else {
                    int enable = 1;
                    (void)::setsockopt(sockFd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
#ifdef SO_REUSEPORT
                    (void)::setsockopt(sockFd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
#endif
                    sockaddr_in bindAddr{};
                    bindAddr.sin_family = AF_INET;
                    bindAddr.sin_port = htons(static_cast<uint16_t>(traceyDiscoveryPort));
                    if (traceyDiscoveryBindAddr == "*" || traceyDiscoveryBindAddr.empty() || traceyDiscoveryBindAddr == "0.0.0.0") {
                        bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
                    } else if (::inet_pton(AF_INET, traceyDiscoveryBindAddr.c_str(), &bindAddr.sin_addr) != 1) {
                        std::cerr << "[WARN] Invalid NMC_TRACEY_DISCOVERY_BIND_ADDR '" << traceyDiscoveryBindAddr
                                  << "': expected IPv4 address" << std::endl;
                        ::close(sockFd);
                        sockFd = -1;
                        nextBindAttemptMs = nowMs + 10000;
                    }

                    if (sockFd >= 0 && ::bind(sockFd, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) != 0) {
                        std::cerr << "[WARN] Tracey discovery bind(" << traceyDiscoveryBindAddr << ":" << traceyDiscoveryPort
                                  << ") failed: " << std::strerror(errno) << std::endl;
                        ::close(sockFd);
                        sockFd = -1;
                        nextBindAttemptMs = nowMs + 5000;
                    }
                }
            }

            if (sockFd >= 0) {
                pollfd pfd{};
                pfd.fd = sockFd;
                pfd.events = POLLIN;
                const int pollRc = ::poll(&pfd, 1, 200);
                if (pollRc > 0 && (pfd.revents & POLLIN)) {
                    sockaddr_storage sender{};
                    socklen_t senderLen = sizeof(sender);
                    const ssize_t bytes = ::recvfrom(sockFd, buffer.data(), buffer.size(), 0,
                                                     reinterpret_cast<sockaddr*>(&sender), &senderLen);
                    if (bytes > 0) {
                        std::string senderAddress = "unknown";
                        char addrBuf[INET6_ADDRSTRLEN] = {0};
                        if (sender.ss_family == AF_INET) {
                            const auto* sin = reinterpret_cast<sockaddr_in*>(&sender);
                            if (::inet_ntop(AF_INET, &sin->sin_addr, addrBuf, sizeof(addrBuf)) != nullptr) {
                                senderAddress = addrBuf;
                            }
                        } else if (sender.ss_family == AF_INET6) {
                            const auto* sin6 = reinterpret_cast<sockaddr_in6*>(&sender);
                            if (::inet_ntop(AF_INET6, &sin6->sin6_addr, addrBuf, sizeof(addrBuf)) != nullptr) {
                                senderAddress = addrBuf;
                            }
                        }

                        auto payload = nlohmann::json::parse(buffer.data(), buffer.data() + bytes, nullptr, false);
                        if (payload.is_object()) {
                            try {
                                ingestTraceyDiscoveryAnnouncement(payload, senderAddress, nowEpochMs());
                            } catch (const std::exception& e) {
                                std::cerr << "[WARN] Ignoring invalid Tracey discovery payload from "
                                          << senderAddress << ": " << e.what() << std::endl;
                            }
                        }
                    } else if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                        std::cerr << "[WARN] Tracey discovery recvfrom() failed: " << std::strerror(errno) << std::endl;
                        ::close(sockFd);
                        sockFd = -1;
                        nextBindAttemptMs = nowEpochMs() + 3000;
                    }
                } else if (pollRc < 0 && errno != EINTR) {
                    std::cerr << "[WARN] Tracey discovery poll() failed: " << std::strerror(errno) << std::endl;
                    ::close(sockFd);
                    sockFd = -1;
                    nextBindAttemptMs = nowEpochMs() + 3000;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }

            const int64_t maintenanceNowMs = nowEpochMs();
            if (maintenanceNowMs - lastMaintenanceMs >= 1000) {
                markTraceyStaleAgents(maintenanceNowMs);

                std::vector<std::pair<std::string, std::string>> dueStatusPolls;
                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    dueStatusPolls.reserve(traceyAgents.size());
                    for (auto& [id, agent] : traceyAgents) {
                        if (agent.statusAddr.empty()) {
                            continue;
                        }
                        if (agent.nextQueryEpochMs <= 0 || maintenanceNowMs >= agent.nextQueryEpochMs) {
                            dueStatusPolls.emplace_back(id, agent.statusAddr);
                            agent.nextQueryEpochMs = maintenanceNowMs + traceyStatusPollMs;
                        }
                    }
                }

                for (const auto& pendingPoll : dueStatusPolls) {
                    pollTraceyStatus(pendingPoll.first, pendingPoll.second, maintenanceNowMs);
                }

                lastMaintenanceMs = maintenanceNowMs;
            }
        }

        if (sockFd >= 0) {
            ::close(sockFd);
        }
    }

    void APIRoutes::appendTraceyStatusSampleLocked(const TraceyAgent& agent, int64_t tsMs, const std::string& source) {
        if (agent.agentId.empty()) {
            return;
        }
        TraceyStatusSample sample;
        sample.tsMs = tsMs > 0 ? tsMs : nowEpochMs();
        sample.status = normalizeTraceyStatus(agent.status);
        sample.stale = agent.stale;
        sample.statusReachable = agent.statusReachable;
        sample.queryFailures = std::max(0, agent.queryFailures);
        sample.coordinator = agent.coordinator;
        sample.score = agent.score;
        sample.tracey_guardProbeFailures = 0;
        sample.tracey_guardProbeErrors = 0;
        sample.tracey_guardQuarantined = 0;
        sample.tracey_guardRemoteFaults = 0;
        sample.loaderThreatLocalProviders = 0;
        sample.loaderThreatLocalArtifacts = 0;
        sample.loaderThreatBlockedProviders = 0;
        sample.loaderThreatBlockedArtifacts = 0;
        sample.loaderThreatRemoteReporters = 0;
        if (agent.metrics.is_object()) {
            const auto statusIt = agent.metrics.find("status_snapshot");
            if (statusIt != agent.metrics.end() && statusIt->is_object()) {
                const auto tracey_guardIt = statusIt->find("tracey_guard");
                if (tracey_guardIt != statusIt->end() && tracey_guardIt->is_object()) {
                    const auto summaryIt = tracey_guardIt->find("summary");
                    if (summaryIt != tracey_guardIt->end() && summaryIt->is_object()) {
                        sample.tracey_guardProbeFailures = std::max(0, summaryIt->value("total_failures", 0));
                        sample.tracey_guardProbeErrors = std::max(0, summaryIt->value("total_errors", 0) + summaryIt->value("total_timeouts", 0));
                        sample.tracey_guardQuarantined = std::max(0, summaryIt->value("quarantined_devices", 0));
                        sample.tracey_guardRemoteFaults = std::max(0, summaryIt->value("remote_fault_support", 0));
                    }
                }
                const auto loaderThreatSummary = extractTraceyLoaderThreatSummary(*statusIt);
                if (loaderThreatSummary.is_object()) {
                    sample.loaderThreatLocalProviders = std::max(0, loaderThreatSummary.value("local_provider_count", 0));
                    sample.loaderThreatLocalArtifacts = std::max(0, loaderThreatSummary.value("local_artifact_count", 0));
                    sample.loaderThreatBlockedProviders = std::max(0, loaderThreatSummary.value("blocked_provider_count", 0));
                    sample.loaderThreatBlockedArtifacts = std::max(0, loaderThreatSummary.value("blocked_artifact_count", 0));
                    sample.loaderThreatRemoteReporters = std::max(0, loaderThreatSummary.value("remote_reporters", 0));
                }
            }
        }
        sample.source = trim(source);

        auto& history = traceyAgentHistory[agent.agentId];
        if (!history.empty()) {
            const auto& last = history.back();
            if (last.tsMs == sample.tsMs &&
                last.status == sample.status &&
                last.stale == sample.stale &&
                last.statusReachable == sample.statusReachable &&
                last.queryFailures == sample.queryFailures &&
                last.coordinator == sample.coordinator &&
                last.score == sample.score &&
                last.tracey_guardProbeFailures == sample.tracey_guardProbeFailures &&
                last.tracey_guardProbeErrors == sample.tracey_guardProbeErrors &&
                last.tracey_guardQuarantined == sample.tracey_guardQuarantined &&
                last.tracey_guardRemoteFaults == sample.tracey_guardRemoteFaults &&
                last.loaderThreatLocalProviders == sample.loaderThreatLocalProviders &&
                last.loaderThreatLocalArtifacts == sample.loaderThreatLocalArtifacts &&
                last.loaderThreatBlockedProviders == sample.loaderThreatBlockedProviders &&
                last.loaderThreatBlockedArtifacts == sample.loaderThreatBlockedArtifacts &&
                last.loaderThreatRemoteReporters == sample.loaderThreatRemoteReporters &&
                last.source == sample.source) {
                return;
            }
        }

        history.push_back(std::move(sample));
        while (history.size() > traceyHistoryMaxSamples) {
            history.pop_front();
        }
    }

    void APIRoutes::appendTraceyAgentLogLocked(const std::string& agentId,
                                               int64_t tsMs,
                                               const std::string& level,
                                               const std::string& category,
                                               const std::string& message,
                                               nlohmann::json context) {
        if (agentId.empty()) {
            return;
        }
        TraceyAgentLogEntry entry;
        entry.tsMs = tsMs > 0 ? tsMs : nowEpochMs();
        entry.level = normalizeLogLevel(level);
        entry.category = trim(category).empty() ? "general" : trim(category);
        entry.message = trim(message);
        if (entry.message.empty()) {
            entry.message = "Tracey event";
        }
        if (!context.is_object()) {
            context = nlohmann::json::object();
        }
        entry.context = std::move(context);

        auto& logs = traceyAgentLogs[agentId];
        logs.push_back(std::move(entry));
        while (logs.size() > traceyAgentLogMaxEntries) {
            logs.pop_front();
        }
    }

    void APIRoutes::ingestTraceyDiscoveryAnnouncement(const nlohmann::json& payload,
                                                      const std::string& senderAddress,
                                                      int64_t receivedAtMs) {
        if (!payload.is_object()) {
            return;
        }

        const std::string agentId = firstStringValue(payload, {"agent_id", "agentId", "id"});
        if (agentId.empty()) {
            return;
        }

        const int64_t announcedTsMs = firstInt64Value(payload, {"ts_ms", "timestamp_ms"}, receivedAtMs);
        if (announcedTsMs > 0 && receivedAtMs >= announcedTsMs && (receivedAtMs - announcedTsMs) > traceyDiscoveryMaxAgeMs) {
            return;
        }

        const bool signaturePresent =
                payload.contains("signature") && payload["signature"].is_string() && !trim(payload["signature"].get<std::string>()).empty();
        if (traceyRequireSignature && !signaturePresent) {
            return;
        }

        const std::string announceAddr = firstStringValue(payload, {"addr", "announce_addr", "announceAddr"});
        const std::string rawStatusAddr = firstStringValue(payload, {"status_addr", "statusAddr"});
        TraceyEndpoint endpoint;
        const bool hasEndpoint = !rawStatusAddr.empty() && parseTraceyEndpoint(rawStatusAddr, endpoint);
        const bool endpointAllowed = hasEndpoint && isLocalOrPrivateHost(endpoint.host, traceyAllowPublicAddr);

        std::lock_guard<std::mutex> lock(dataMutex);
        auto& agent = traceyAgents[agentId];
        const std::string previousStatus = agent.status;
        const std::string previousLinkState = agent.linkState;
        agent.agentId = agentId;
        agent.source = mergeTraceySource(agent.source, "discovery");
        agent.signaturePresent = signaturePresent;
        agent.lastAnnouncementEpochMs = receivedAtMs;
        agent.lastSeenEpochMs = std::max(agent.lastSeenEpochMs, receivedAtMs);
        agent.stale = false;
        if (agent.status.empty()) {
            agent.status = "unknown";
        }

        if (!announceAddr.empty()) {
            agent.announceAddr = announceAddr;
        } else if (agent.announceAddr.empty()) {
            agent.announceAddr = senderAddress;
        }

        if (payload.contains("capabilities")) {
            agent.capabilities = payload["capabilities"];
        } else if (agent.capabilities.is_null()) {
            agent.capabilities = nlohmann::json::object();
        }

        if (payload.contains("is_coordinator") && payload["is_coordinator"].is_boolean()) {
            agent.coordinator = payload["is_coordinator"].get<bool>();
        }
        if (payload.contains("coordinator_epoch") && payload["coordinator_epoch"].is_number_integer()) {
            agent.coordinatorEpoch = payload["coordinator_epoch"].get<int64_t>();
        }
        if (payload.contains("score") && payload["score"].is_number_integer()) {
            agent.score = payload["score"].get<int64_t>();
        }

        if (hasEndpoint && endpointAllowed) {
            agent.statusAddr = endpoint.normalized;
            agent.nextQueryEpochMs = receivedAtMs;
            if (agent.linkState.empty() || agent.linkState == "offline") {
                agent.linkState = "discovered";
            }
            agent.linkSecurity = deriveLinkSecurity(signaturePresent, &endpoint, traceyTlsVerify);
            if (agent.lastError == "Status endpoint is invalid or disallowed by policy.") {
                agent.lastError.clear();
            }
        } else if (!rawStatusAddr.empty()) {
            agent.lastError = "Status endpoint is invalid or disallowed by policy.";
            agent.linkState = "discovered";
            agent.linkSecurity = deriveLinkSecurity(signaturePresent, nullptr, traceyTlsVerify);
            if (agent.statusAddr.empty()) {
                agent.statusAddr = rawStatusAddr;
            }
        } else {
            agent.linkState = "announcement-only";
            agent.linkSecurity = deriveLinkSecurity(signaturePresent, nullptr, traceyTlsVerify);
        }

        if (normalizeTraceyStatus(previousStatus) != normalizeTraceyStatus(agent.status)) {
            appendTraceyAgentLogLocked(
                    agent.agentId,
                    receivedAtMs,
                    "info",
                    "discovery",
                    "Discovery announcement updated agent status.",
                    {
                            {"previous_status", normalizeTraceyStatus(previousStatus)},
                            {"current_status", normalizeTraceyStatus(agent.status)},
                            {"sender", senderAddress}
                    }
            );
        }
        if (previousLinkState != agent.linkState) {
            appendTraceyAgentLogLocked(
                    agent.agentId,
                    receivedAtMs,
                    "info",
                    "link",
                    "Discovery announcement updated link state.",
                    {
                            {"previous_link_state", previousLinkState},
                            {"current_link_state", agent.linkState},
                            {"sender", senderAddress}
                    }
            );
        }
        if (!rawStatusAddr.empty() && !(hasEndpoint && endpointAllowed)) {
            appendTraceyAgentLogLocked(
                    agent.agentId,
                    receivedAtMs,
                    "warn",
                    "discovery",
                    "Discovery payload contained an invalid or disallowed status endpoint.",
                    {
                            {"raw_status_addr", rawStatusAddr},
                            {"sender", senderAddress}
                    }
            );
        }
        appendTraceyStatusSampleLocked(agent, receivedAtMs, "discovery");
    }

    void APIRoutes::pollTraceyStatus(const std::string& agentId, const std::string& statusAddr, int64_t nowMs) {
        auto markFailure = [&](const std::string& message, const TraceyEndpoint* endpoint = nullptr) {
            std::lock_guard<std::mutex> lock(dataMutex);
            auto& agent = traceyAgents[agentId];
            const std::string previousStatus = normalizeTraceyStatus(agent.status);
            if (agent.agentId.empty()) {
                agent.agentId = agentId;
            }
            if (!statusAddr.empty()) {
                agent.statusAddr = statusAddr;
            }
            agent.statusReachable = false;
            agent.queryFailures += 1;
            agent.lastQueryEpochMs = nowMs;
            agent.nextQueryEpochMs = nowMs + computeBackoffMs(agent.queryFailures, traceyStatusPollMs, traceyStatusMaxBackoffMs);
            agent.linkState = agent.queryFailures >= 3 ? "offline" : "degraded";
            agent.status = agent.queryFailures >= 3 ? "offline" : "degraded";
            agent.lastError = message;
            agent.linkSecurity = deriveLinkSecurity(agent.signaturePresent, endpoint, traceyTlsVerify);
            const std::string currentStatus = normalizeTraceyStatus(agent.status);
            appendTraceyAgentLogLocked(
                    agent.agentId,
                    nowMs,
                    agent.queryFailures >= 3 ? "error" : "warn",
                    "status_poll",
                    "Failed to poll Tracey status endpoint.",
                    {
                            {"status_addr", agent.statusAddr},
                            {"reason", message},
                            {"query_failures", agent.queryFailures},
                            {"previous_status", previousStatus},
                            {"current_status", currentStatus}
                    }
            );
            appendTraceyStatusSampleLocked(agent, nowMs, "status_poll_failure");
        };

        TraceyEndpoint endpoint;
        if (!parseTraceyEndpoint(statusAddr, endpoint)) {
            markFailure("Invalid status endpoint.");
            return;
        }
        if (!isLocalOrPrivateHost(endpoint.host, traceyAllowPublicAddr)) {
            markFailure("Status endpoint is outside allowed network ranges.", &endpoint);
            return;
        }

        const std::string statusPath = buildTraceyStatusPath(endpoint);
        const int timeoutSec = static_cast<int>(std::max<int64_t>(1, (traceyStatusTimeoutMs + 999) / 1000));
        httplib::Headers headers{{"Accept", "application/json"}};
        if (!traceyStatusBearerToken.empty()) {
            headers.emplace("Authorization", "Bearer " + traceyStatusBearerToken);
        }

        httplib::Result result;
        if (endpoint.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient client(endpoint.host, endpoint.port);
            client.enable_server_certificate_verification(traceyTlsVerify);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Get(statusPath.c_str(), headers);
#else
            markFailure("HTTPS status polling is unavailable: httplib lacks OpenSSL support.", &endpoint);
            return;
#endif
        } else {
            httplib::Client client(endpoint.host, endpoint.port);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Get(statusPath.c_str(), headers);
        }

        if (!result) {
            markFailure("Status poll failed: " + httplib::to_string(result.error()), &endpoint);
            return;
        }
        if (result->status < 200 || result->status >= 300) {
            markFailure("Status endpoint returned HTTP " + std::to_string(result->status) + ".", &endpoint);
            return;
        }

        auto statusPayload = nlohmann::json::parse(result->body, nullptr, false);
        if (statusPayload.is_discarded()) {
            markFailure("Status endpoint returned non-JSON payload.", &endpoint);
            return;
        }

        std::string resolvedStatus = "healthy";
        if (statusPayload.is_object()) {
            if (statusPayload.contains("status") && statusPayload["status"].is_string()) {
                resolvedStatus = normalizeTraceyStatus(statusPayload["status"].get<std::string>());
            } else if (statusPayload.contains("posture") && statusPayload["posture"].is_string()) {
                const std::string posture = toLower(statusPayload["posture"].get<std::string>());
                if (posture.find("incident") != std::string::npos || posture.find("critical") != std::string::npos) {
                    resolvedStatus = "offline";
                } else if (posture.find("elevated") != std::string::npos || posture.find("warning") != std::string::npos) {
                    resolvedStatus = "degraded";
                } else {
                    resolvedStatus = "healthy";
                }
            }
        }

        std::lock_guard<std::mutex> lock(dataMutex);
        auto& agent = traceyAgents[agentId];
        const std::string previousStatus = normalizeTraceyStatus(agent.status);
        const int previousFailures = std::max(0, agent.queryFailures);
        const bool previousReachable = agent.statusReachable;
        agent.agentId = agentId;
        agent.status = normalizeTraceyStatus(resolvedStatus);
        agent.statusReachable = true;
        agent.queryFailures = 0;
        agent.lastQueryEpochMs = nowMs;
        agent.nextQueryEpochMs = nowMs + traceyStatusPollMs;
        agent.lastSeenEpochMs = std::max(agent.lastSeenEpochMs, nowMs);
        agent.stale = false;
        agent.linkState = agent.status == "healthy" ? "healthy" : "degraded";
        agent.statusAddr = endpoint.normalized;
        agent.linkSecurity = deriveLinkSecurity(agent.signaturePresent, &endpoint, traceyTlsVerify);
        agent.lastError.clear();

        if (agent.metrics.is_null() || !agent.metrics.is_object()) {
            agent.metrics = nlohmann::json::object();
        }
        agent.metrics["status_snapshot"] = statusPayload;

        if (statusPayload.is_object()) {
            if (statusPayload.contains("is_coordinator") && statusPayload["is_coordinator"].is_boolean()) {
                agent.coordinator = statusPayload["is_coordinator"].get<bool>();
            }
        }

        const std::string currentStatus = normalizeTraceyStatus(agent.status);
        if (currentStatus != previousStatus || previousFailures > 0 || !previousReachable) {
            appendTraceyAgentLogLocked(
                    agent.agentId,
                    nowMs,
                    "info",
                    "status_poll",
                    "Status endpoint poll succeeded.",
                    {
                            {"status_addr", agent.statusAddr},
                            {"previous_status", previousStatus},
                            {"current_status", currentStatus},
                            {"previous_query_failures", previousFailures}
                    }
            );
        }
        appendTraceyStatusSampleLocked(agent, nowMs, "status_poll");
    }

    void APIRoutes::markTraceyStaleAgents(int64_t nowMs) {
        std::lock_guard<std::mutex> lock(dataMutex);
        const int64_t staleAfterMs = std::max<int64_t>(5, traceyStaleAfterSeconds) * 1000;
        for (auto& [id, agent] : traceyAgents) {
            (void)id;
            const int64_t lastSignalMs = std::max(agent.lastSeenEpochMs, agent.lastAnnouncementEpochMs);
            if (lastSignalMs <= 0) {
                continue;
            }
            const bool wasStale = agent.stale;
            const bool staleNow = nowMs > lastSignalMs && (nowMs - lastSignalMs) > staleAfterMs;
            agent.stale = staleNow;
            if (staleNow) {
                agent.status = "offline";
                agent.linkState = "offline";
                if (agent.lastError.empty()) {
                    agent.lastError = "Agent is stale (missed heartbeat/discovery window).";
                }
            }
            if (staleNow && !wasStale) {
                appendTraceyAgentLogLocked(
                        agent.agentId,
                        nowMs,
                        "warn",
                        "liveness",
                        "Tracey agent became stale.",
                        {
                                {"stale_after_seconds", traceyStaleAfterSeconds},
                                {"last_signal_epoch_ms", lastSignalMs}
                        }
                );
                appendTraceyStatusSampleLocked(agent, nowMs, "stale_transition");
            }
        }
    }

    void APIRoutes::handleTraceyHeartbeat(const httplib::Request& req, httplib::Response& res) {
        try {
            const auto body = nlohmann::json::parse(req.body);
            std::string agentId = firstStringValue(body, {"agent_id", "id"});
            if (agentId.empty()) {
                return sendErrorResponse(res, 400, "Missing required parameter: agent_id.");
            }

            const int64_t nowMs = nowEpochMs();
            const std::string rawStatusAddr = firstStringValue(body, {"status_addr", "statusAddr"});
            TraceyEndpoint endpoint;
            const bool hasEndpoint = !rawStatusAddr.empty() && parseTraceyEndpoint(rawStatusAddr, endpoint);
            const bool endpointAllowed = hasEndpoint && isLocalOrPrivateHost(endpoint.host, traceyAllowPublicAddr);

            std::lock_guard<std::mutex> lock(dataMutex);
            auto& agent = traceyAgents[agentId];
            const std::string previousStatus = normalizeTraceyStatus(agent.status);
            const std::string previousLinkState = agent.linkState;
            agent.agentId = agentId;
            agent.source = mergeTraceySource(agent.source, "heartbeat");
            const std::string cluster = firstStringValue(body, {"cluster", "cluster_id"});
            if (!cluster.empty()) {
                agent.cluster = cluster;
            }
            const std::string reportedStatus = firstStringValue(body, {"status"});
            if (!reportedStatus.empty()) {
                agent.status = normalizeTraceyStatus(reportedStatus);
            } else if (agent.status.empty()) {
                agent.status = "healthy";
            }
            const std::string version = firstStringValue(body, {"version"});
            if (!version.empty()) {
                agent.version = version;
            }
            const std::string host = firstStringValue(body, {"host", "hostname"});
            if (!host.empty()) {
                agent.host = host;
            }
            if (body.contains("metrics") && !body["metrics"].is_null()) {
                agent.metrics = body["metrics"];
            } else if (agent.metrics.is_null()) {
                agent.metrics = nlohmann::json::object();
            }
            if (body.contains("capabilities") && !body["capabilities"].is_null()) {
                agent.capabilities = body["capabilities"];
            } else if (agent.capabilities.is_null()) {
                agent.capabilities = nlohmann::json::object();
            }
            if (body.contains("is_coordinator") && body["is_coordinator"].is_boolean()) {
                agent.coordinator = body["is_coordinator"].get<bool>();
            }
            if (body.contains("coordinator_epoch") && body["coordinator_epoch"].is_number_integer()) {
                agent.coordinatorEpoch = body["coordinator_epoch"].get<int64_t>();
            }
            if (body.contains("score") && body["score"].is_number_integer()) {
                agent.score = body["score"].get<int64_t>();
            }

            agent.lastSeenEpochMs = nowMs;
            agent.stale = false;
            agent.statusReachable = true;
            agent.lastError.clear();

            if (hasEndpoint && endpointAllowed) {
                agent.statusAddr = endpoint.normalized;
                agent.nextQueryEpochMs = nowMs;
                agent.linkState = "healthy";
                agent.linkSecurity = deriveLinkSecurity(agent.signaturePresent, &endpoint, traceyTlsVerify);
            } else if (!rawStatusAddr.empty()) {
                agent.lastError = "Heartbeat provided invalid or disallowed status_addr.";
                agent.linkState = "degraded";
                agent.linkSecurity = deriveLinkSecurity(agent.signaturePresent, nullptr, traceyTlsVerify);
            } else if (agent.linkState.empty()) {
                agent.linkState = "heartbeat-only";
                agent.linkSecurity = deriveLinkSecurity(agent.signaturePresent, nullptr, traceyTlsVerify);
            }

            const std::string currentStatus = normalizeTraceyStatus(agent.status);
            if (previousStatus != currentStatus) {
                appendTraceyAgentLogLocked(
                        agent.agentId,
                        nowMs,
                        "info",
                        "heartbeat",
                        "Heartbeat updated Tracey agent status.",
                        {
                                {"previous_status", previousStatus},
                                {"current_status", currentStatus}
                        }
                );
            }
            if (previousLinkState != agent.linkState) {
                appendTraceyAgentLogLocked(
                        agent.agentId,
                        nowMs,
                        "info",
                        "heartbeat",
                        "Heartbeat updated Tracey link state.",
                        {
                                {"previous_link_state", previousLinkState},
                                {"current_link_state", agent.linkState}
                        }
                );
            }
            if (!rawStatusAddr.empty() && !(hasEndpoint && endpointAllowed)) {
                appendTraceyAgentLogLocked(
                        agent.agentId,
                        nowMs,
                        "warn",
                        "heartbeat",
                        "Heartbeat provided an invalid or disallowed status endpoint.",
                        {
                                {"raw_status_addr", rawStatusAddr}
                        }
                );
            }
            appendTraceyStatusSampleLocked(agent, nowMs, "heartbeat");

            nlohmann::json payload = {
                    {"agent_id", agent.agentId},
                    {"cluster", agent.cluster},
                    {"status", agent.status},
                    {"version", agent.version},
                    {"host", agent.host},
                    {"source", agent.source},
                    {"status_addr", agent.statusAddr},
                    {"link_state", agent.linkState},
                    {"link_security", agent.linkSecurity},
                    {"query_failures", agent.queryFailures},
                    {"metrics", agent.metrics.is_null() ? nlohmann::json::object() : agent.metrics},
                    {"last_seen_epoch_ms", agent.lastSeenEpochMs},
                    {"last_seen_seconds_ago", 0},
                    {"stale", false}
            };

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Tracey heartbeat accepted.";
            apiResponse.data = payload;
            sendJsonResponse(res, apiResponse);
        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

    nlohmann::json APIRoutes::buildTraceyContinuumAgentView(const TraceyAgent& agent, int64_t nowMs) const {
        const TraceyEffectiveStatus effective = buildTraceyEffectiveStatus(
                agent.status,
                agent.stale,
                agent.lastSeenEpochMs,
                agent.lastAnnouncementEpochMs,
                nowMs,
                traceyStaleAfterSeconds
        );

        const nlohmann::json emptyObject = nlohmann::json::object();
        const auto* continuum = extractContinuumTelemetryNode(agent.metrics);
        const auto* identity = continuum != nullptr ? firstObjectValue(*continuum, {"identity"}) : nullptr;
        const auto* serverNode = continuum != nullptr ? firstObjectValue(*continuum, {"server"}) : nullptr;
        const auto* gpuArray = continuum != nullptr ? firstArrayValue(*continuum, {"gpus"}) : nullptr;
        const auto* recentActionsArray = continuum != nullptr ? firstArrayValue(*continuum, {"recent_actions", "recentActions"}) : nullptr;
        const auto* traceyGuard = extractTraceyGuardNode(agent.metrics);
        const auto* traceyGuardSummary = extractTraceyGuardSummaryNode(agent.metrics);
        const auto* traceyGuardGpuHealth = traceyGuard != nullptr ? firstArrayValue(*traceyGuard, {"gpu_health", "gpuHealth"}) : nullptr;
        const auto* traceyGuardRecentExecutions = traceyGuard != nullptr ? firstArrayValue(*traceyGuard, {"recent_executions", "recentExecutions"}) : nullptr;
        const auto* traceyGuardRecentFaults = traceyGuard != nullptr ? firstArrayValue(*traceyGuard, {"recent_faults", "recentFaults"}) : nullptr;
        const auto* traceyGuardRemoteFaults = traceyGuard != nullptr ? firstArrayValue(*traceyGuard, {"remote_faults", "remoteFaults"}) : nullptr;
        const auto* statusSnapshot = extractTraceyStatusSnapshotNode(agent.metrics);
        const auto* continuumAutoscaler = statusSnapshot != nullptr
                                          ? firstObjectValue(
                                                  *statusSnapshot,
                                                  {"continuum_autoscaler", "continuumAutoscaler", "autoscaler"}
                                          )
                                          : nullptr;
        const auto* continuumAssessment = extractContinuumAssessmentNode(agent.metrics);
        const auto* continuumAssessmentSummary = extractContinuumAssessmentSummaryNode(agent.metrics);
        const auto* continuumLoop = statusSnapshot != nullptr
                                    ? firstObjectValue(
                                            *statusSnapshot,
                                            {"continuum_loop", "continuumLoop", "adaptive_loop", "adaptiveLoop"}
                                    )
                                    : nullptr;
        const nlohmann::json loaderThreats = statusSnapshot != nullptr
                                             ? extractTraceyLoaderThreatSummary(*statusSnapshot)
                                             : nlohmann::json::object();

        const nlohmann::json& identityNode = identity != nullptr ? *identity : emptyObject;
        const nlohmann::json& server = serverNode != nullptr ? *serverNode : emptyObject;

        nlohmann::json gpus = gpuArray != nullptr ? *gpuArray : nlohmann::json::array();
        if (traceyGuardGpuHealth != nullptr && gpus.is_array()) {
            std::unordered_map<std::string, nlohmann::json> guardByGpu;
            for (const auto& entry : *traceyGuardGpuHealth) {
                if (!entry.is_object()) {
                    continue;
                }
                const std::string gpuId = firstStringValue(entry, {"gpu_id", "gpuId"});
                if (!gpuId.empty()) {
                    guardByGpu[gpuId] = entry;
                }
            }
            for (auto& gpu : gpus) {
                if (!gpu.is_object()) {
                    continue;
                }
                const std::string gpuId = firstStringValue(gpu, {"gpu_id", "gpuId"});
                const auto guardIt = guardByGpu.find(gpuId);
                if (guardIt == guardByGpu.end()) {
                    continue;
                }
                const auto& guardGpu = guardIt->second;
                gpu["probe_pass_count"] = std::max<int64_t>(0, firstInt64Value(
                        gpu,
                        {"probe_pass_count", "probePassCount"},
                        std::max<int64_t>(0, firstInt64Value(guardGpu, {"probe_pass_count", "probePassCount"}, 0))
                ));
                gpu["probe_fail_count"] = std::max<int64_t>(0, firstInt64Value(
                        gpu,
                        {"probe_fail_count", "probeFailCount"},
                        std::max<int64_t>(0, firstInt64Value(guardGpu, {"probe_fail_count", "probeFailCount"}, 0))
                ));
                gpu["probe_error_count"] = std::max<int64_t>(0, firstInt64Value(
                        gpu,
                        {"probe_error_count", "probeErrorCount"},
                        std::max<int64_t>(0, firstInt64Value(guardGpu, {"probe_error_count", "probeErrorCount"}, 0))
                ));
                gpu["sm_count"] = std::max<int64_t>(0, firstInt64Value(
                        gpu,
                        {"sm_count", "smCount"},
                        std::max<int64_t>(0, firstInt64Value(guardGpu, {"sm_count", "smCount"}, 0))
                ));
                gpu["last_probe_ms"] = std::max<int64_t>(0, firstInt64Value(
                        gpu,
                        {"last_probe_ms", "lastProbeMs"},
                        std::max<int64_t>(0, firstInt64Value(guardGpu, {"last_probe_ms", "lastProbeMs"}, 0))
                ));
                gpu["last_guard_risk"] = std::max(0.0, firstDoubleValue(
                        gpu,
                        {"last_guard_risk", "lastGuardRisk"},
                        std::max(0.0, firstDoubleValue(guardGpu, {"last_risk", "lastRisk"}, 0.0))
                ));
                gpu["last_guard_confidence"] = std::max(0.0, firstDoubleValue(
                        gpu,
                        {"last_guard_confidence", "lastGuardConfidence"},
                        std::max(0.0, firstDoubleValue(guardGpu, {"last_confidence", "lastConfidence"}, 0.0))
                ));
            }
        }
        sortGpuTiles(gpus);

        nlohmann::json recentActions = recentActionsArray != nullptr ? *recentActionsArray : nlohmann::json::array();
        sortActionsByTime(recentActions);

        const auto* eccNode = firstObjectValue(server, {"ecc"});
        const std::string derivedHost = firstStringValue(identityNode, {"host"});
        const std::string rack = defaultTopologyLabel(firstStringValue(identityNode, {"rack"}), "unassigned");
        const std::string zone = defaultTopologyLabel(firstStringValue(identityNode, {"zone"}), "unassigned");
        const int gpuCount = gpus.is_array() ? static_cast<int>(gpus.size()) : 0;
        const double autonomyRisk = firstDoubleValue(server, {"autonomy_risk", "autonomyRisk"}, -1.0);

        return {
                {"agent_id", agent.agentId},
                {"cluster", agent.cluster},
                {"status", effective.status},
                {"stale", effective.stale},
                {"status_reachable", agent.statusReachable},
                {"version", agent.version},
                {"host", !derivedHost.empty() ? derivedHost : agent.host},
                {"source", agent.source},
                {"announce_addr", agent.announceAddr},
                {"status_addr", agent.statusAddr},
                {"link_state", agent.linkState},
                {"link_security", agent.linkSecurity},
                {"query_failures", std::max(0, agent.queryFailures)},
                {"score", agent.score},
                {"last_error", agent.lastError},
                {"last_seen_epoch_ms", agent.lastSeenEpochMs},
                {"last_seen_seconds_ago", effective.ageSeconds},
                {"last_announcement_epoch_ms", agent.lastAnnouncementEpochMs},
                {"last_query_epoch_ms", agent.lastQueryEpochMs},
                {"next_query_epoch_ms", agent.nextQueryEpochMs},
                {"rack", rack},
                {"zone", zone},
                {"row", firstStringValue(identityNode, {"row"})},
                {"site", firstStringValue(identityNode, {"site"})},
                {"building", firstStringValue(identityNode, {"building"})},
                {"room", firstStringValue(identityNode, {"room"})},
                {"network", firstStringValue(identityNode, {"network"})},
                {"physical", firstStringValue(identityNode, {"physical"})},
                {"telemetry_ts_ms", continuum != nullptr ? std::max<int64_t>(0, firstInt64Value(*continuum, {"ts_ms", "tsMs"}, 0)) : 0},
                {"tracey_guard_ts_ms", traceyGuardSummary != nullptr ? std::max<int64_t>(0, firstInt64Value(*traceyGuardSummary, {"ts_ms", "tsMs"}, 0)) : 0},
                {"identity", identity != nullptr ? *identity : nlohmann::json::object()},
                {"server", serverNode != nullptr ? *serverNode : nlohmann::json::object()},
                {"gpus", gpus},
                {"recent_actions", recentActions},
                {"tracey_guard", traceyGuard != nullptr ? *traceyGuard : nlohmann::json::object()},
                {"tracey_guard_summary", traceyGuardSummary != nullptr ? *traceyGuardSummary : nlohmann::json::object()},
                {"recent_executions", traceyGuardRecentExecutions != nullptr ? *traceyGuardRecentExecutions : nlohmann::json::array()},
                {"recent_faults", traceyGuardRecentFaults != nullptr ? *traceyGuardRecentFaults : nlohmann::json::array()},
                {"remote_faults", traceyGuardRemoteFaults != nullptr ? *traceyGuardRemoteFaults : nlohmann::json::array()},
                {"continuum_assessment", continuumAssessment != nullptr ? *continuumAssessment : nlohmann::json::object()},
                {"continuum_assessment_summary", continuumAssessmentSummary != nullptr ? *continuumAssessmentSummary : nlohmann::json::object()},
                {"continuum_autoscaler", continuumAutoscaler != nullptr ? *continuumAutoscaler : nlohmann::json::object()},
                {"continuum_loop", continuumLoop != nullptr ? *continuumLoop : nlohmann::json::object()},
                {"loader_threats", loaderThreats},
                {"summary", {
                        {"cpu_usage_pct", firstDoubleValue(server, {"cpu_usage_pct", "cpuUsagePct"}, 0.0)},
                        {"mem_used_pct", firstDoubleValue(server, {"mem_used_pct", "memUsedPct"}, 0.0)},
                        {"mem_app_used_pct", firstDoubleValue(server, {"mem_app_used_pct", "memAppUsedPct"}, 0.0)},
                        {"swap_used_pct", firstDoubleValue(server, {"swap_used_pct", "swapUsedPct"}, 0.0)},
                        {"net_rx_bps", firstDoubleValue(server, {"net_rx_bps", "netRxBps"}, 0.0)},
                        {"net_tx_bps", firstDoubleValue(server, {"net_tx_bps", "netTxBps"}, 0.0)},
                        {"gpu_count", gpuCount},
                        {"gpu_utilization_avg_pct", firstDoubleValue(server, {"gpu_utilization_avg_pct", "gpuUtilizationAvgPct"}, 0.0)},
                        {"gpu_temperature_max_c", firstDoubleValue(server, {"gpu_temperature_max_c", "gpuTemperatureMaxC"}, 0.0)},
                        {"gpu_power_total_w", firstDoubleValue(server, {"gpu_power_total_w", "gpuPowerTotalW"}, 0.0)},
                        {"thermal_alerts", std::max<int64_t>(0, firstInt64Value(server, {"thermal_alerts", "thermalAlerts"}, 0))},
                        {"fan_alerts", std::max<int64_t>(0, firstInt64Value(server, {"fan_alerts", "fanAlerts"}, 0))},
                        {"recent_action_count", std::max<int64_t>(0, firstInt64Value(server, {"recent_action_count", "recentActionCount"}, static_cast<int64_t>(recentActions.size())))},
                        {"autonomy_risk", autonomyRisk >= 0.0 ? autonomyRisk : 0.0},
                        {"autonomy_action", firstStringValue(server, {"autonomy_action", "autonomyAction"})},
                        {"ecc_corrected_total", eccNode != nullptr ? std::max<int64_t>(0, firstInt64Value(*eccNode, {"corrected_total", "correctedTotal"}, 0)) : 0},
                        {"ecc_uncorrected_total", eccNode != nullptr ? std::max<int64_t>(0, firstInt64Value(*eccNode, {"uncorrected_total", "uncorrectedTotal"}, 0)) : 0},
                        {"compromise_risk", continuumAssessmentSummary != nullptr ? std::max(0.0, firstDoubleValue(*continuumAssessmentSummary, {"compromise_risk", "compromiseRisk"}, 0.0)) : 0.0},
                        {"compromise_confidence", continuumAssessmentSummary != nullptr ? std::max(0.0, firstDoubleValue(*continuumAssessmentSummary, {"compromise_confidence", "compromiseConfidence"}, 0.0)) : 0.0},
                        {"cve_matches", continuumAssessmentSummary != nullptr ? std::max<int64_t>(0, firstInt64Value(*continuumAssessmentSummary, {"cve_matches", "cveMatches"}, 0)) : 0},
                        {"kev_matches", continuumAssessmentSummary != nullptr ? std::max<int64_t>(0, firstInt64Value(*continuumAssessmentSummary, {"kev_matches", "kevMatches"}, 0)) : 0},
                        {"assessment_status", continuumAssessmentSummary != nullptr ? firstStringValue(*continuumAssessmentSummary, {"status"}, "unknown") : "unknown"},
                        {"assessment_action", continuumAssessmentSummary != nullptr ? firstStringValue(*continuumAssessmentSummary, {"recommended_action", "recommendedAction"}) : ""},
                        {"adaptive_mode", continuumLoop != nullptr ? firstStringValue(*continuumLoop, {"mode"}, "disabled") : "disabled"},
                        {"adaptive_overall_score", continuumLoop != nullptr ? std::max(0.0, firstDoubleValue(*continuumLoop, {"overall_score", "overallScore"}, 0.0)) : 0.0},
                        {"adaptive_placement_score", continuumLoop != nullptr ? std::max(0.0, firstDoubleValue(*continuumLoop, {"placement_score", "placementScore"}, 0.0)) : 0.0}
                }}
        };
    }

    void APIRoutes::handleListTraceyAgents(const httplib::Request& req, httplib::Response& res) {
        (void)req;
        const int64_t nowMs = nowEpochMs();
        markTraceyStaleAgents(nowMs);

        std::vector<TraceyAgent> snapshot;
        std::vector<TraceyRequirement> requirementSnapshot;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            snapshot.reserve(traceyAgents.size());
            for (const auto& pair : traceyAgents) {
                snapshot.push_back(pair.second);
            }
            requirementSnapshot.reserve(traceyRequirements.size());
            for (const auto& pair : traceyRequirements) {
                requirementSnapshot.push_back(pair.second);
            }
        }

        std::sort(snapshot.begin(), snapshot.end(), [](const TraceyAgent& a, const TraceyAgent& b) {
            return a.agentId < b.agentId;
        });
        std::sort(requirementSnapshot.begin(), requirementSnapshot.end(), [](const TraceyRequirement& a, const TraceyRequirement& b) {
            if (a.resourceType == b.resourceType) {
                return a.resourceName < b.resourceName;
            }
            return a.resourceType < b.resourceType;
        });

        int healthy = 0;
        int degraded = 0;
        int offline = 0;
        int stale = 0;
        int unknown = 0;
        int discovered = 0;
        int heartbeatOnly = 0;
        int reachable = 0;
        int secureLinks = 0;
        int signedAnnouncements = 0;
        int tracey_guardEnabledAgents = 0;
        int tracey_guardQuarantinedTotal = 0;
        int tracey_guardFailuresTotal = 0;
        int tracey_guardErrorsTotal = 0;
        int tracey_guardRemoteFaultsTotal = 0;
        int loaderThreatProvidersTotal = 0;
        int loaderThreatArtifactsTotal = 0;
        int loaderThreatBlockedProvidersTotal = 0;
        int loaderThreatBlockedArtifactsTotal = 0;
        int loaderThreatRemoteReportersTotal = 0;
        int assessmentAgentsWithMatches = 0;
        int assessmentAgentsWithKev = 0;
        int assessmentCompromisedAgents = 0;
        int assessmentElevatedAgents = 0;
        int assessmentCveMatchesTotal = 0;
        int assessmentKevMatchesTotal = 0;
        nlohmann::json agents = nlohmann::json::array();

        for (const auto& agent : snapshot) {
            const int64_t lastSignalMs = std::max(agent.lastSeenEpochMs, agent.lastAnnouncementEpochMs);
            int64_t ageSeconds = 0;
            if (lastSignalMs > 0 && nowMs > lastSignalMs) {
                ageSeconds = (nowMs - lastSignalMs) / 1000;
            }
            int64_t lastAnnouncementSecondsAgo = 0;
            if (agent.lastAnnouncementEpochMs > 0 && nowMs > agent.lastAnnouncementEpochMs) {
                lastAnnouncementSecondsAgo = (nowMs - agent.lastAnnouncementEpochMs) / 1000;
            }
            int64_t lastQuerySecondsAgo = 0;
            if (agent.lastQueryEpochMs > 0 && nowMs > agent.lastQueryEpochMs) {
                lastQuerySecondsAgo = (nowMs - agent.lastQueryEpochMs) / 1000;
            }

            const bool isStale = agent.stale || ageSeconds > traceyStaleAfterSeconds;
            const std::string status = isStale ? "offline" : normalizeTraceyStatus(agent.status);

            if (isStale) {
                stale++;
            }
            if (status == "healthy") {
                healthy++;
            } else if (status == "degraded") {
                degraded++;
            } else if (status == "offline") {
                offline++;
            } else {
                unknown++;
            }

            const bool hasDiscoverySource = agent.source.find("discovery") != std::string::npos;
            const bool hasHeartbeatSource = agent.source.find("heartbeat") != std::string::npos;
            if (hasDiscoverySource) {
                discovered++;
            }
            if (hasHeartbeatSource && !hasDiscoverySource) {
                heartbeatOnly++;
            }
            if (agent.statusReachable) {
                reachable++;
            }
            if (agent.linkSecurity.find("tls-verified") != std::string::npos) {
                secureLinks++;
            }
            if (agent.signaturePresent) {
                signedAnnouncements++;
            }

            nlohmann::json tracey_guardSummary = nlohmann::json::object();
            nlohmann::json loaderThreatSummary = nlohmann::json::object();
            nlohmann::json continuumAssessmentSummary = nlohmann::json::object();
            if (agent.metrics.is_object()) {
                auto statusIt = agent.metrics.find("status_snapshot");
                if (statusIt != agent.metrics.end() && statusIt->is_object()) {
                    auto tracey_guardIt = statusIt->find("tracey_guard");
                    if (tracey_guardIt != statusIt->end() && tracey_guardIt->is_object()) {
                        auto summaryIt = tracey_guardIt->find("summary");
                        if (summaryIt != tracey_guardIt->end() && summaryIt->is_object()) {
                            tracey_guardSummary = *summaryIt;
                            if (summaryIt->value("enabled", false)) {
                                tracey_guardEnabledAgents++;
                            }
                            tracey_guardQuarantinedTotal += std::max(0, summaryIt->value("quarantined_devices", 0));
                            tracey_guardFailuresTotal += std::max(0, summaryIt->value("total_failures", 0));
                            tracey_guardErrorsTotal += std::max(0, summaryIt->value("total_errors", 0) + summaryIt->value("total_timeouts", 0));
                            tracey_guardRemoteFaultsTotal += std::max(0, summaryIt->value("remote_fault_support", 0));
                        }
                    }
                    loaderThreatSummary = extractTraceyLoaderThreatSummary(*statusIt);
                    if (loaderThreatSummary.is_object()) {
                        loaderThreatProvidersTotal += std::max(0, loaderThreatSummary.value("local_provider_count", 0));
                        loaderThreatArtifactsTotal += std::max(0, loaderThreatSummary.value("local_artifact_count", 0));
                        loaderThreatBlockedProvidersTotal += std::max(0, loaderThreatSummary.value("blocked_provider_count", 0));
                        loaderThreatBlockedArtifactsTotal += std::max(0, loaderThreatSummary.value("blocked_artifact_count", 0));
                        loaderThreatRemoteReportersTotal += std::max(0, loaderThreatSummary.value("remote_reporters", 0));
                    }
                    auto assessmentIt = statusIt->find("continuum_assessment");
                    if (assessmentIt == statusIt->end()) {
                        assessmentIt = statusIt->find("continuumAssessment");
                    }
                    if (assessmentIt != statusIt->end() && assessmentIt->is_object()) {
                        auto summaryIt = assessmentIt->find("summary");
                        continuumAssessmentSummary = summaryIt != assessmentIt->end() && summaryIt->is_object()
                                                     ? *summaryIt
                                                     : *assessmentIt;
                        const int cveMatches = std::max(0, continuumAssessmentSummary.value("cve_matches", continuumAssessmentSummary.value("cveMatches", 0)));
                        const int kevMatches = std::max(0, continuumAssessmentSummary.value("kev_matches", continuumAssessmentSummary.value("kevMatches", 0)));
                        const double compromiseRisk = std::max(0.0, continuumAssessmentSummary.value("compromise_risk", continuumAssessmentSummary.value("compromiseRisk", 0.0)));
                        if (cveMatches > 0) {
                            assessmentAgentsWithMatches++;
                        }
                        if (kevMatches > 0) {
                            assessmentAgentsWithKev++;
                        }
                        if (compromiseRisk >= 0.80) {
                            assessmentCompromisedAgents++;
                        } else if (compromiseRisk >= 0.55) {
                            assessmentElevatedAgents++;
                        }
                        assessmentCveMatchesTotal += cveMatches;
                        assessmentKevMatchesTotal += kevMatches;
                    }
                }
            }

            agents.push_back({
                    {"agent_id", agent.agentId},
                    {"cluster", agent.cluster},
                    {"status", status},
                    {"version", agent.version},
                    {"host", agent.host},
                    {"source", agent.source},
                    {"announce_addr", agent.announceAddr},
                    {"status_addr", agent.statusAddr},
                    {"link_state", agent.linkState},
                    {"link_security", agent.linkSecurity},
                    {"last_error", agent.lastError},
                    {"metrics", agent.metrics.is_null() ? nlohmann::json::object() : agent.metrics},
                    {"capabilities", agent.capabilities.is_null() ? nlohmann::json::object() : agent.capabilities},
                    {"last_seen_epoch_ms", agent.lastSeenEpochMs},
                    {"last_seen_seconds_ago", ageSeconds},
                    {"last_announcement_epoch_ms", agent.lastAnnouncementEpochMs},
                    {"last_announcement_seconds_ago", lastAnnouncementSecondsAgo},
                    {"last_query_epoch_ms", agent.lastQueryEpochMs},
                    {"last_query_seconds_ago", lastQuerySecondsAgo},
                    {"next_query_epoch_ms", agent.nextQueryEpochMs},
                    {"query_failures", agent.queryFailures},
                    {"status_reachable", agent.statusReachable},
                    {"signature_present", agent.signaturePresent},
                    {"is_coordinator", agent.coordinator},
                    {"coordinator_epoch", agent.coordinatorEpoch},
                    {"score", agent.score},
                    {"continuum_assessment", continuumAssessmentSummary},
                    {"tracey_guard", tracey_guardSummary},
                    {"loader_threats", loaderThreatSummary},
                    {"stale", isStale}
            });
        }

        int managedCompliant = 0;
        int managedPending = 0;
        int managedNonCompliant = 0;
        nlohmann::json requirements = nlohmann::json::array();
        const int64_t graceMs = std::max<int64_t>(5, traceyRequirementGraceSeconds) * 1000;

        for (const auto& requirement : requirementSnapshot) {
            const TraceyAgent* matchedAgent = nullptr;
            if (!requirement.expectedAgentId.empty()) {
                auto agentIt = std::find_if(snapshot.begin(), snapshot.end(), [&](const TraceyAgent& agent) {
                    return agent.agentId == requirement.expectedAgentId;
                });
                if (agentIt != snapshot.end()) {
                    matchedAgent = &(*agentIt);
                }
            }

            const int64_t resourceAgeMs = (requirement.createdEpochMs > 0 && nowMs > requirement.createdEpochMs)
                                          ? (nowMs - requirement.createdEpochMs)
                                          : 0;
            const bool withinGrace = resourceAgeMs <= graceMs;
            std::string complianceState = "pending";
            std::string reason = "Waiting for required Tracey agent signal.";
            int64_t agentLastSeenSecondsAgo = -1;
            std::string agentStatus = "missing";

            if (matchedAgent != nullptr) {
                const int64_t lastSignalMs = std::max(matchedAgent->lastSeenEpochMs, matchedAgent->lastAnnouncementEpochMs);
                const bool stale = (lastSignalMs <= 0) ||
                                   (nowMs > lastSignalMs && (nowMs - lastSignalMs) > (traceyStaleAfterSeconds * 1000));
                if (lastSignalMs > 0 && nowMs >= lastSignalMs) {
                    agentLastSeenSecondsAgo = (nowMs - lastSignalMs) / 1000;
                }
                agentStatus = matchedAgent->status.empty() ? "unknown" : matchedAgent->status;
                const bool hasSignal = matchedAgent->statusReachable ||
                                       matchedAgent->source.find("heartbeat") != std::string::npos ||
                                       matchedAgent->source.find("discovery") != std::string::npos;
                if (!stale && hasSignal) {
                    complianceState = "compliant";
                    reason = "Tracey agent is reporting for this managed resource.";
                } else if (withinGrace) {
                    complianceState = "pending";
                    reason = stale
                             ? "Tracey agent discovered but currently stale; grace window still active."
                             : "Tracey agent registered but has not reported yet; grace window still active.";
                } else {
                    complianceState = "noncompliant";
                    reason = stale
                             ? "Tracey agent is stale or offline after grace window."
                             : "Tracey agent is not reporting after grace window.";
                }
            } else if (!withinGrace) {
                complianceState = "noncompliant";
                reason = "Required Tracey agent has not been discovered after grace window.";
            }

            if (complianceState == "compliant") {
                managedCompliant++;
            } else if (complianceState == "noncompliant") {
                managedNonCompliant++;
            } else {
                managedPending++;
            }

            requirements.push_back({
                    {"key", requirement.key},
                    {"resource_type", requirement.resourceType},
                    {"resource_name", requirement.resourceName},
                    {"region", requirement.region},
                    {"created_epoch_ms", requirement.createdEpochMs},
                    {"resource_age_seconds", resourceAgeMs / 1000},
                    {"expected_agent_id", requirement.expectedAgentId},
                    {"expected_status_addr", requirement.expectedStatusAddr},
                    {"agent_status", agentStatus},
                    {"agent_last_seen_seconds_ago", agentLastSeenSecondsAgo},
                    {"compliance_state", complianceState},
                    {"reason", reason}
            });
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Tracey agents listed successfully.";
        apiResponse.data = {
                {"summary", {
                        {"total", static_cast<int>(snapshot.size())},
                        {"healthy", healthy},
                        {"degraded", degraded},
                        {"offline", offline},
                        {"stale", stale},
                        {"unknown", unknown},
                        {"discovered", discovered},
                        {"heartbeat_only", heartbeatOnly},
                        {"reachable", reachable},
                        {"secure_links", secureLinks},
                        {"signed_announcements", signedAnnouncements}
                }},
                {"tracey_guard_summary", {
                        {"enabled_agents", tracey_guardEnabledAgents},
                        {"quarantined_devices", tracey_guardQuarantinedTotal},
                        {"total_failures", tracey_guardFailuresTotal},
                        {"total_errors", tracey_guardErrorsTotal},
                        {"remote_fault_support", tracey_guardRemoteFaultsTotal}
                }},
                {"loader_threat_summary", {
                        {"local_provider_count", loaderThreatProvidersTotal},
                        {"local_artifact_count", loaderThreatArtifactsTotal},
                        {"blocked_provider_count", loaderThreatBlockedProvidersTotal},
                        {"blocked_artifact_count", loaderThreatBlockedArtifactsTotal},
                        {"remote_reporters", loaderThreatRemoteReportersTotal}
                }},
                {"continuum_assessment_summary", {
                        {"agents_with_matches", assessmentAgentsWithMatches},
                        {"agents_with_kev", assessmentAgentsWithKev},
                        {"elevated_agents", assessmentElevatedAgents},
                        {"compromised_agents", assessmentCompromisedAgents},
                        {"cve_matches", assessmentCveMatchesTotal},
                        {"kev_matches", assessmentKevMatchesTotal}
                }},
                {"tracey_policy", {
                        {"enforce_managed_resources", traceyEnforceManagedResources},
                        {"requirement_grace_seconds", traceyRequirementGraceSeconds}
                }},
                {"requirement_summary", {
                        {"total", static_cast<int>(requirementSnapshot.size())},
                        {"compliant", managedCompliant},
                        {"pending", managedPending},
                        {"noncompliant", managedNonCompliant}
                }},
                {"requirements", requirements},
                {"discovery", {
                        {"enabled", traceyDiscoveryEnabled},
                        {"bind_addr", traceyDiscoveryBindAddr},
                        {"port", traceyDiscoveryPort},
                        {"max_age_ms", traceyDiscoveryMaxAgeMs},
                        {"status_poll_ms", traceyStatusPollMs},
                        {"status_timeout_ms", traceyStatusTimeoutMs},
                        {"status_max_backoff_ms", traceyStatusMaxBackoffMs},
                        {"allow_public_addr", traceyAllowPublicAddr},
                        {"tls_verify", traceyTlsVerify},
                        {"require_signature", traceyRequireSignature}
                }},
                {"stale_after_seconds", traceyStaleAfterSeconds},
                {"agents", agents}
        };
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleTraceyAnalytics(const httplib::Request& req, httplib::Response& res) {
        const int64_t nowMs = nowEpochMs();
        const int64_t windowSeconds = parseQueryInt64(req, "window_seconds", 3600, 300, 604800);
        const int64_t bucketSeconds = parseQueryInt64(req, "bucket_seconds", 60, 10, 3600);
        const int64_t logLimit = parseQueryInt64(req, "log_limit", 200, 20, 4000);
        const int64_t windowMs = windowSeconds * 1000;
        const int64_t bucketMs = bucketSeconds * 1000;
        const int64_t cutoffMs = nowMs - windowMs;
        const int64_t alignedStartMs = (cutoffMs / bucketMs) * bucketMs;
        const size_t bucketCount = static_cast<size_t>(((nowMs - alignedStartMs) / bucketMs) + 1);

        struct BucketAgg {
            int64_t bucketStartMs{0};
            int sampledAgents{0};
            int healthy{0};
            int degraded{0};
            int offline{0};
            int unknown{0};
            int stale{0};
            int reachable{0};
            int coordinator{0};
            int logInfo{0};
            int logWarn{0};
            int logError{0};
            int64_t queryFailureSum{0};
            int queryFailureSamples{0};
            int64_t scoreSum{0};
            int scoreSamples{0};
            int64_t tracey_guardFailureSum{0};
            int64_t tracey_guardErrorSum{0};
            int64_t tracey_guardQuarantineSum{0};
            int64_t tracey_guardRemoteFaultSum{0};
            int tracey_guardSamples{0};
            int64_t loaderThreatProviderSum{0};
            int64_t loaderThreatArtifactSum{0};
            int64_t loaderThreatBlockedProviderSum{0};
            int64_t loaderThreatBlockedArtifactSum{0};
            int64_t loaderThreatRemoteReporterSum{0};
            int loaderThreatSamples{0};
        };
        std::vector<BucketAgg> buckets(bucketCount);
        for (size_t i = 0; i < bucketCount; ++i) {
            buckets[i].bucketStartMs = alignedStartMs + static_cast<int64_t>(i) * bucketMs;
        }

        std::vector<TraceyAgent> snapshot;
        std::unordered_map<std::string, std::vector<TraceyStatusSample>> histories;
        std::unordered_map<std::string, std::vector<TraceyAgentLogEntry>> logsByAgent;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            snapshot.reserve(traceyAgents.size());
            for (const auto& [id, agent] : traceyAgents) {
                (void)id;
                snapshot.push_back(agent);
            }
            for (const auto& [agentId, history] : traceyAgentHistory) {
                std::vector<TraceyStatusSample> filtered;
                filtered.reserve(history.size());
                for (const auto& sample : history) {
                    if (sample.tsMs >= cutoffMs) {
                        filtered.push_back(sample);
                    }
                }
                if (!filtered.empty()) {
                    histories.emplace(agentId, std::move(filtered));
                }
            }
            for (const auto& [agentId, logs] : traceyAgentLogs) {
                std::vector<TraceyAgentLogEntry> filtered;
                filtered.reserve(logs.size());
                for (const auto& entry : logs) {
                    if (entry.tsMs >= cutoffMs) {
                        filtered.push_back(entry);
                    }
                }
                if (!filtered.empty()) {
                    logsByAgent.emplace(agentId, std::move(filtered));
                }
            }
        }

        std::sort(snapshot.begin(), snapshot.end(), [](const TraceyAgent& a, const TraceyAgent& b) {
            return a.agentId < b.agentId;
        });

        auto bucketIndexForTs = [&](int64_t tsMs) -> int64_t {
            if (tsMs < alignedStartMs || tsMs > nowMs) {
                return -1;
            }
            const int64_t offset = tsMs - alignedStartMs;
            const int64_t idx = offset / bucketMs;
            if (idx < 0 || idx >= static_cast<int64_t>(bucketCount)) {
                return -1;
            }
            return idx;
        };

        int currentHealthy = 0;
        int currentDegraded = 0;
        int currentOffline = 0;
        int currentUnknown = 0;
        int currentTraceyGuardQuarantined = 0;
        int currentTraceyGuardFailures = 0;
        int currentTraceyGuardErrors = 0;
        int currentTraceyGuardRemoteFaults = 0;
        int currentLoaderThreatProviders = 0;
        int currentLoaderThreatArtifacts = 0;
        int currentLoaderThreatBlockedProviders = 0;
        int currentLoaderThreatBlockedArtifacts = 0;
        int currentLoaderThreatRemoteReporters = 0;
        for (const auto& agent : snapshot) {
            const int64_t lastSignalMs = std::max(agent.lastSeenEpochMs, agent.lastAnnouncementEpochMs);
            const bool stale = agent.stale || (lastSignalMs > 0 && nowMs > lastSignalMs &&
                                               (nowMs - lastSignalMs) > (traceyStaleAfterSeconds * 1000));
            const std::string status = stale ? "offline" : normalizeTraceyStatus(agent.status);
            if (status == "healthy") {
                currentHealthy++;
            } else if (status == "degraded") {
                currentDegraded++;
            } else if (status == "offline") {
                currentOffline++;
            } else {
                currentUnknown++;
            }

            if (agent.metrics.is_object()) {
                auto statusIt = agent.metrics.find("status_snapshot");
                if (statusIt != agent.metrics.end() && statusIt->is_object()) {
                    auto tracey_guardIt = statusIt->find("tracey_guard");
                    if (tracey_guardIt != statusIt->end() && tracey_guardIt->is_object()) {
                        auto summaryIt = tracey_guardIt->find("summary");
                        if (summaryIt != tracey_guardIt->end() && summaryIt->is_object()) {
                            currentTraceyGuardQuarantined += std::max(0, summaryIt->value("quarantined_devices", 0));
                            currentTraceyGuardFailures += std::max(0, summaryIt->value("total_failures", 0));
                            currentTraceyGuardErrors += std::max(0, summaryIt->value("total_errors", 0) + summaryIt->value("total_timeouts", 0));
                            currentTraceyGuardRemoteFaults += std::max(0, summaryIt->value("remote_fault_support", 0));
                        }
                    }
                    const auto loaderThreatSummary = extractTraceyLoaderThreatSummary(*statusIt);
                    if (loaderThreatSummary.is_object()) {
                        currentLoaderThreatProviders += std::max(0, loaderThreatSummary.value("local_provider_count", 0));
                        currentLoaderThreatArtifacts += std::max(0, loaderThreatSummary.value("local_artifact_count", 0));
                        currentLoaderThreatBlockedProviders += std::max(0, loaderThreatSummary.value("blocked_provider_count", 0));
                        currentLoaderThreatBlockedArtifacts += std::max(0, loaderThreatSummary.value("blocked_artifact_count", 0));
                        currentLoaderThreatRemoteReporters += std::max(0, loaderThreatSummary.value("remote_reporters", 0));
                    }
                }
            }
        }

        struct GlobalLogView {
            std::string agentId;
            TraceyAgentLogEntry entry;
        };
        std::vector<GlobalLogView> globalLogs;
        std::unordered_map<std::string, int> categoryCounts;
        std::unordered_map<std::string, int> levelCounts;

        for (const auto& [agentId, history] : histories) {
            std::unordered_map<int64_t, const TraceyStatusSample*> lastByBucket;
            for (const auto& sample : history) {
                const int64_t bucketIdx = bucketIndexForTs(sample.tsMs);
                if (bucketIdx < 0) {
                    continue;
                }
                auto it = lastByBucket.find(bucketIdx);
                if (it == lastByBucket.end() || it->second->tsMs <= sample.tsMs) {
                    lastByBucket[bucketIdx] = &sample;
                }
            }
            for (const auto& [bucketIdx, samplePtr] : lastByBucket) {
                auto& bucket = buckets[static_cast<size_t>(bucketIdx)];
                const auto& sample = *samplePtr;
                const std::string status = normalizeTraceyStatus(sample.status);
                bucket.sampledAgents++;
                if (status == "healthy") {
                    bucket.healthy++;
                } else if (status == "degraded") {
                    bucket.degraded++;
                } else if (status == "offline") {
                    bucket.offline++;
                } else {
                    bucket.unknown++;
                }
                if (sample.stale) {
                    bucket.stale++;
                }
                if (sample.statusReachable) {
                    bucket.reachable++;
                }
                if (sample.coordinator) {
                    bucket.coordinator++;
                }
                bucket.queryFailureSum += std::max(0, sample.queryFailures);
                bucket.queryFailureSamples++;
                bucket.scoreSum += sample.score;
                bucket.scoreSamples++;
                bucket.tracey_guardFailureSum += std::max(0, sample.tracey_guardProbeFailures);
                bucket.tracey_guardErrorSum += std::max(0, sample.tracey_guardProbeErrors);
                bucket.tracey_guardQuarantineSum += std::max(0, sample.tracey_guardQuarantined);
                bucket.tracey_guardRemoteFaultSum += std::max(0, sample.tracey_guardRemoteFaults);
                bucket.tracey_guardSamples++;
                bucket.loaderThreatProviderSum += std::max(0, sample.loaderThreatLocalProviders);
                bucket.loaderThreatArtifactSum += std::max(0, sample.loaderThreatLocalArtifacts);
                bucket.loaderThreatBlockedProviderSum += std::max(0, sample.loaderThreatBlockedProviders);
                bucket.loaderThreatBlockedArtifactSum += std::max(0, sample.loaderThreatBlockedArtifacts);
                bucket.loaderThreatRemoteReporterSum += std::max(0, sample.loaderThreatRemoteReporters);
                bucket.loaderThreatSamples++;
            }
            (void)agentId;
        }

        for (const auto& [agentId, logs] : logsByAgent) {
            for (const auto& entry : logs) {
                const int64_t bucketIdx = bucketIndexForTs(entry.tsMs);
                if (bucketIdx >= 0) {
                    auto& bucket = buckets[static_cast<size_t>(bucketIdx)];
                    const std::string level = normalizeLogLevel(entry.level);
                    if (level == "error") {
                        bucket.logError++;
                    } else if (level == "warn") {
                        bucket.logWarn++;
                    } else {
                        bucket.logInfo++;
                    }
                }

                const std::string level = normalizeLogLevel(entry.level);
                levelCounts[level] += 1;
                categoryCounts[entry.category.empty() ? "general" : entry.category] += 1;
                globalLogs.push_back({agentId, entry});
            }
        }

        std::sort(globalLogs.begin(), globalLogs.end(), [](const GlobalLogView& a, const GlobalLogView& b) {
            return a.entry.tsMs > b.entry.tsMs;
        });
        if (globalLogs.size() > static_cast<size_t>(logLimit)) {
            globalLogs.resize(static_cast<size_t>(logLimit));
        }

        nlohmann::json timeline = nlohmann::json::array();
        for (const auto& bucket : buckets) {
            const double avgQueryFailures = bucket.queryFailureSamples > 0
                                            ? static_cast<double>(bucket.queryFailureSum) / static_cast<double>(bucket.queryFailureSamples)
                                            : 0.0;
            const double avgScore = bucket.scoreSamples > 0
                                    ? static_cast<double>(bucket.scoreSum) / static_cast<double>(bucket.scoreSamples)
                                    : 0.0;
            const double avgTraceyGuardFailures = bucket.tracey_guardSamples > 0
                                               ? static_cast<double>(bucket.tracey_guardFailureSum) / static_cast<double>(bucket.tracey_guardSamples)
                                               : 0.0;
            const double avgTraceyGuardErrors = bucket.tracey_guardSamples > 0
                                             ? static_cast<double>(bucket.tracey_guardErrorSum) / static_cast<double>(bucket.tracey_guardSamples)
                                             : 0.0;
            const double avgTraceyGuardQuarantine = bucket.tracey_guardSamples > 0
                                                 ? static_cast<double>(bucket.tracey_guardQuarantineSum) / static_cast<double>(bucket.tracey_guardSamples)
                                                 : 0.0;
            const double avgTraceyGuardRemoteFaults = bucket.tracey_guardSamples > 0
                                                   ? static_cast<double>(bucket.tracey_guardRemoteFaultSum) / static_cast<double>(bucket.tracey_guardSamples)
                                                   : 0.0;
            const double avgLoaderThreatProviders = bucket.loaderThreatSamples > 0
                                                   ? static_cast<double>(bucket.loaderThreatProviderSum) / static_cast<double>(bucket.loaderThreatSamples)
                                                   : 0.0;
            const double avgLoaderThreatArtifacts = bucket.loaderThreatSamples > 0
                                                   ? static_cast<double>(bucket.loaderThreatArtifactSum) / static_cast<double>(bucket.loaderThreatSamples)
                                                   : 0.0;
            const double avgLoaderThreatBlockedProviders = bucket.loaderThreatSamples > 0
                                                           ? static_cast<double>(bucket.loaderThreatBlockedProviderSum) / static_cast<double>(bucket.loaderThreatSamples)
                                                           : 0.0;
            const double avgLoaderThreatBlockedArtifacts = bucket.loaderThreatSamples > 0
                                                           ? static_cast<double>(bucket.loaderThreatBlockedArtifactSum) / static_cast<double>(bucket.loaderThreatSamples)
                                                           : 0.0;
            const double avgLoaderThreatRemoteReporters = bucket.loaderThreatSamples > 0
                                                          ? static_cast<double>(bucket.loaderThreatRemoteReporterSum) / static_cast<double>(bucket.loaderThreatSamples)
                                                          : 0.0;
            timeline.push_back({
                    {"bucket_start_epoch_ms", bucket.bucketStartMs},
                    {"sampled_agents", bucket.sampledAgents},
                    {"healthy", bucket.healthy},
                    {"degraded", bucket.degraded},
                    {"offline", bucket.offline},
                    {"unknown", bucket.unknown},
                    {"stale", bucket.stale},
                    {"reachable", bucket.reachable},
                    {"coordinator", bucket.coordinator},
                    {"log_info", bucket.logInfo},
                    {"log_warn", bucket.logWarn},
                    {"log_error", bucket.logError},
                    {"avg_query_failures", avgQueryFailures},
                    {"avg_score", avgScore},
                    {"avg_tracey_guard_failures", avgTraceyGuardFailures},
                    {"avg_tracey_guard_errors", avgTraceyGuardErrors},
                    {"avg_tracey_guard_quarantined", avgTraceyGuardQuarantine},
                    {"avg_tracey_guard_remote_faults", avgTraceyGuardRemoteFaults},
                    {"avg_loader_threat_providers", avgLoaderThreatProviders},
                    {"avg_loader_threat_artifacts", avgLoaderThreatArtifacts},
                    {"avg_loader_threat_blocked_providers", avgLoaderThreatBlockedProviders},
                    {"avg_loader_threat_blocked_artifacts", avgLoaderThreatBlockedArtifacts},
                    {"avg_loader_threat_remote_reporters", avgLoaderThreatRemoteReporters}
            });
        }

        nlohmann::json topCategories = nlohmann::json::array();
        std::vector<std::pair<std::string, int>> categoryVector(categoryCounts.begin(), categoryCounts.end());
        std::sort(categoryVector.begin(), categoryVector.end(), [](const auto& a, const auto& b) {
            if (a.second == b.second) {
                return a.first < b.first;
            }
            return a.second > b.second;
        });
        if (categoryVector.size() > 15) {
            categoryVector.resize(15);
        }
        for (const auto& [category, count] : categoryVector) {
            topCategories.push_back({
                    {"category", category},
                    {"count", count}
            });
        }

        nlohmann::json agents = nlohmann::json::array();
        for (const auto& agent : snapshot) {
            const auto historyIt = histories.find(agent.agentId);
            const auto logsIt = logsByAgent.find(agent.agentId);

            int sampleCount = 0;
            int healthySamples = 0;
            int degradedSamples = 0;
            int offlineSamples = 0;
            int staleSamples = 0;
            int64_t queryFailureSum = 0;
            int queryFailureSamples = 0;
            int64_t scoreSum = 0;
            int scoreSamples = 0;
            int64_t tracey_guardFailureSum = 0;
            int64_t tracey_guardErrorSum = 0;
            int64_t tracey_guardQuarantineSum = 0;
            int64_t tracey_guardRemoteFaultSum = 0;
            int64_t loaderThreatProviderSum = 0;
            int64_t loaderThreatArtifactSum = 0;
            int64_t loaderThreatBlockedProviderSum = 0;
            int64_t loaderThreatBlockedArtifactSum = 0;
            int64_t loaderThreatRemoteReporterSum = 0;

            if (historyIt != histories.end()) {
                for (const auto& sample : historyIt->second) {
                    sampleCount++;
                    const std::string status = normalizeTraceyStatus(sample.status);
                    if (status == "healthy") {
                        healthySamples++;
                    } else if (status == "degraded") {
                        degradedSamples++;
                    } else if (status == "offline") {
                        offlineSamples++;
                    }
                    if (sample.stale) {
                        staleSamples++;
                    }
                    queryFailureSum += std::max(0, sample.queryFailures);
                    queryFailureSamples++;
                    scoreSum += sample.score;
                    scoreSamples++;
                    tracey_guardFailureSum += std::max(0, sample.tracey_guardProbeFailures);
                    tracey_guardErrorSum += std::max(0, sample.tracey_guardProbeErrors);
                    tracey_guardQuarantineSum += std::max(0, sample.tracey_guardQuarantined);
                    tracey_guardRemoteFaultSum += std::max(0, sample.tracey_guardRemoteFaults);
                    loaderThreatProviderSum += std::max(0, sample.loaderThreatLocalProviders);
                    loaderThreatArtifactSum += std::max(0, sample.loaderThreatLocalArtifacts);
                    loaderThreatBlockedProviderSum += std::max(0, sample.loaderThreatBlockedProviders);
                    loaderThreatBlockedArtifactSum += std::max(0, sample.loaderThreatBlockedArtifacts);
                    loaderThreatRemoteReporterSum += std::max(0, sample.loaderThreatRemoteReporters);
                }
            }

            int infoLogs = 0;
            int warnLogs = 0;
            int errorLogs = 0;
            int64_t lastLogEpochMs = 0;
            if (logsIt != logsByAgent.end()) {
                for (const auto& entry : logsIt->second) {
                    const std::string level = normalizeLogLevel(entry.level);
                    if (level == "error") {
                        errorLogs++;
                    } else if (level == "warn") {
                        warnLogs++;
                    } else {
                        infoLogs++;
                    }
                    lastLogEpochMs = std::max(lastLogEpochMs, entry.tsMs);
                }
            }

            const int64_t lastSignalMs = std::max(agent.lastSeenEpochMs, agent.lastAnnouncementEpochMs);
            int64_t lastSeenSecondsAgo = 0;
            if (lastSignalMs > 0 && nowMs > lastSignalMs) {
                lastSeenSecondsAgo = (nowMs - lastSignalMs) / 1000;
            }
            const bool stale = agent.stale || (lastSignalMs > 0 && nowMs > lastSignalMs &&
                                               (nowMs - lastSignalMs) > (traceyStaleAfterSeconds * 1000));
            const std::string currentStatus = stale ? "offline" : normalizeTraceyStatus(agent.status);

            agents.push_back({
                    {"agent_id", agent.agentId},
                    {"status", currentStatus},
                    {"stale", stale},
                    {"cluster", agent.cluster},
                    {"last_seen_seconds_ago", lastSeenSecondsAgo},
                    {"sample_count", sampleCount},
                    {"healthy_ratio", sampleCount > 0 ? static_cast<double>(healthySamples) / static_cast<double>(sampleCount) : 0.0},
                    {"degraded_ratio", sampleCount > 0 ? static_cast<double>(degradedSamples) / static_cast<double>(sampleCount) : 0.0},
                    {"offline_ratio", sampleCount > 0 ? static_cast<double>(offlineSamples) / static_cast<double>(sampleCount) : 0.0},
                    {"stale_ratio", sampleCount > 0 ? static_cast<double>(staleSamples) / static_cast<double>(sampleCount) : 0.0},
                    {"avg_query_failures", queryFailureSamples > 0 ? static_cast<double>(queryFailureSum) / static_cast<double>(queryFailureSamples) : 0.0},
                    {"avg_score", scoreSamples > 0 ? static_cast<double>(scoreSum) / static_cast<double>(scoreSamples) : 0.0},
                    {"avg_tracey_guard_failures", sampleCount > 0 ? static_cast<double>(tracey_guardFailureSum) / static_cast<double>(sampleCount) : 0.0},
                    {"avg_tracey_guard_errors", sampleCount > 0 ? static_cast<double>(tracey_guardErrorSum) / static_cast<double>(sampleCount) : 0.0},
                    {"avg_tracey_guard_quarantined", sampleCount > 0 ? static_cast<double>(tracey_guardQuarantineSum) / static_cast<double>(sampleCount) : 0.0},
                    {"avg_tracey_guard_remote_faults", sampleCount > 0 ? static_cast<double>(tracey_guardRemoteFaultSum) / static_cast<double>(sampleCount) : 0.0},
                    {"avg_loader_threat_providers", sampleCount > 0 ? static_cast<double>(loaderThreatProviderSum) / static_cast<double>(sampleCount) : 0.0},
                    {"avg_loader_threat_artifacts", sampleCount > 0 ? static_cast<double>(loaderThreatArtifactSum) / static_cast<double>(sampleCount) : 0.0},
                    {"avg_loader_threat_blocked_providers", sampleCount > 0 ? static_cast<double>(loaderThreatBlockedProviderSum) / static_cast<double>(sampleCount) : 0.0},
                    {"avg_loader_threat_blocked_artifacts", sampleCount > 0 ? static_cast<double>(loaderThreatBlockedArtifactSum) / static_cast<double>(sampleCount) : 0.0},
                    {"avg_loader_threat_remote_reporters", sampleCount > 0 ? static_cast<double>(loaderThreatRemoteReporterSum) / static_cast<double>(sampleCount) : 0.0},
                    {"log_info", infoLogs},
                    {"log_warn", warnLogs},
                    {"log_error", errorLogs},
                    {"last_log_epoch_ms", lastLogEpochMs}
            });
        }

        nlohmann::json recentLogs = nlohmann::json::array();
        for (const auto& item : globalLogs) {
            recentLogs.push_back({
                    {"agent_id", item.agentId},
                    {"ts_ms", item.entry.tsMs},
                    {"level", normalizeLogLevel(item.entry.level)},
                    {"category", item.entry.category},
                    {"message", item.entry.message},
                    {"context", item.entry.context.is_object() ? item.entry.context : nlohmann::json::object()}
            });
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Tracey analytics generated successfully.";
        apiResponse.data = {
                {"generated_epoch_ms", nowMs},
                {"window_seconds", windowSeconds},
                {"bucket_seconds", bucketSeconds},
                {"bucket_count", static_cast<int>(bucketCount)},
                {"summary", {
                        {"agents_total", static_cast<int>(snapshot.size())},
                        {"current_healthy", currentHealthy},
                        {"current_degraded", currentDegraded},
                        {"current_offline", currentOffline},
                        {"current_unknown", currentUnknown},
                        {"current_tracey_guard_quarantined", currentTraceyGuardQuarantined},
                        {"current_tracey_guard_failures", currentTraceyGuardFailures},
                        {"current_tracey_guard_errors", currentTraceyGuardErrors},
                        {"current_tracey_guard_remote_faults", currentTraceyGuardRemoteFaults},
                        {"current_loader_threat_providers", currentLoaderThreatProviders},
                        {"current_loader_threat_artifacts", currentLoaderThreatArtifacts},
                        {"current_loader_threat_blocked_providers", currentLoaderThreatBlockedProviders},
                        {"current_loader_threat_blocked_artifacts", currentLoaderThreatBlockedArtifacts},
                        {"current_loader_threat_remote_reporters", currentLoaderThreatRemoteReporters},
                        {"log_info", levelCounts["info"]},
                        {"log_warn", levelCounts["warn"]},
                        {"log_error", levelCounts["error"]}
                }},
                {"timeline", timeline},
                {"top_log_categories", topCategories},
                {"agents", agents},
                {"recent_logs", recentLogs}
        };
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleTraceyFleet(const httplib::Request& req, httplib::Response& res) {
        (void)req;
        const int64_t nowMs = nowEpochMs();
        markTraceyStaleAgents(nowMs);

        std::vector<TraceyAgent> snapshot;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            snapshot.reserve(traceyAgents.size());
            for (const auto& [id, agent] : traceyAgents) {
                (void)id;
                snapshot.push_back(agent);
            }
        }

        std::sort(snapshot.begin(), snapshot.end(), [](const TraceyAgent& left, const TraceyAgent& right) {
            return left.agentId < right.agentId;
        });

        std::vector<nlohmann::json> agentViews;
        agentViews.reserve(snapshot.size());
        for (const auto& agent : snapshot) {
            agentViews.push_back(buildTraceyContinuumAgentView(agent, nowMs));
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Tracey fleet view generated successfully.";
        apiResponse.data = buildTraceyFleetViewFromAgents(agentViews, nowMs);
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleTraceyAdaptive(const httplib::Request& req, httplib::Response& res) {
        const int64_t nowMs = nowEpochMs();
        markTraceyStaleAgents(nowMs);
        const std::string placementPolicy = normalizeAdaptivePlacementPolicy(
                req.has_param("policy") ? req.get_param_value("policy") : ""
        );
        const std::string placementPolicyLabel = adaptivePlacementPolicyLabel(placementPolicy);

        std::vector<TraceyAgent> snapshot;
        size_t k8sCount = 0;
        size_t vclusterCount = 0;
        size_t vmCount = 0;
        size_t requirementCount = 0;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            snapshot.reserve(traceyAgents.size());
            for (const auto& [id, agent] : traceyAgents) {
                (void)id;
                snapshot.push_back(agent);
            }
            k8sCount = k8sClusters.size();
            vclusterCount = vclusterConfigs.size();
            vmCount = vms.size();
            requirementCount = traceyRequirements.size();
        }

        std::sort(snapshot.begin(), snapshot.end(), [](const TraceyAgent& left, const TraceyAgent& right) {
            return left.agentId < right.agentId;
        });

        std::vector<nlohmann::json> agentViews;
        agentViews.reserve(snapshot.size());
        for (const auto& agent : snapshot) {
            agentViews.push_back(buildTraceyContinuumAgentView(agent, nowMs));
        }

        const nlohmann::json fleet = buildTraceyFleetViewFromAgents(agentViews, nowMs);

        nlohmann::json agentRows = nlohmann::json::array();
        nlohmann::json placementCandidates = nlohmann::json::array();
        nlohmann::json gpuCandidates = nlohmann::json::array();
        nlohmann::json recommendations = nlohmann::json::array();

        int pressuredAgents = 0;
        int constrainedAgents = 0;
        int staleAgents = 0;
        int preferredHosts = 0;
        int preferredGpus = 0;
        int recentActionTotal = 0;
        int cveMatchesTotal = 0;
        int kevMatchesTotal = 0;
        int compromisedAgents = 0;
        int elevatedAgents = 0;
        int64_t requestedRemoteNodesTotal = 0;
        int64_t activeRemoteNodesTotal = 0;

        double overallScoreSum = 0.0;
        double readinessScoreSum = 0.0;
        double placementScoreSum = 0.0;
        double headroomPctSum = 0.0;
        double planScoreSum = 0.0;
        double rampScoreSum = 0.0;
        double optimizeScoreSum = 0.0;
        double repeatScoreSum = 0.0;
        int scoreCount = 0;
        int headroomCount = 0;
        int phaseCount = 0;

        auto temperatureSafetyScore = [](double tempC) {
            if (tempC <= 0.0) {
                return 0.5;
            }
            if (tempC >= 92.0) {
                return 0.0;
            }
            return clampUnit((92.0 - tempC) / 40.0);
        };

        auto moderateUtilizationScore = [](double utilPct) {
            const double delta = utilPct >= 55.0 ? (utilPct - 55.0) : (55.0 - utilPct);
            return clampUnit(1.0 - (delta / 55.0));
        };

        auto powerEfficiencyScore = [](double powerW, double referenceW) {
            if (powerW <= 0.0 || referenceW <= 0.0) {
                return 0.5;
            }
            return clampUnit((referenceW - powerW) / referenceW);
        };

        auto pushRecommendation = [&](const nlohmann::json& base,
                                      const std::string& agentId,
                                      const std::string& host,
                                      const std::string& rack,
                                      const std::string& zone) {
            if (!base.is_object()) {
                return;
            }
            nlohmann::json item = base;
            item["stage"] = firstStringValue(item, {"stage"}, "repeat");
            item["priority"] = firstStringValue(item, {"priority"}, "low");
            item["action"] = truncateDisplay(firstStringValue(item, {"action"}, "Hold Steady"), 64);
            item["reason"] = truncateDisplay(firstStringValue(item, {"reason"}, "No further detail."), 120);
            item["agent_id"] = agentId;
            item["host"] = host;
            item["rack"] = rack;
            item["zone"] = zone;
            item["priority_rank"] = adaptivePriorityRank(item.value("priority", "low"));
            recommendations.push_back(item);
        };

        auto buildHostCandidate = [&](const nlohmann::json& agentView, const nlohmann::json& adaptive) {
            const nlohmann::json summary = agentView.value("summary", nlohmann::json::object());
            const nlohmann::json assessment = agentView.value("continuum_assessment_summary", nlohmann::json::object());
            const nlohmann::json guard = agentView.value("tracey_guard_summary", nlohmann::json::object());
            const nlohmann::json loader = agentView.value("loader_threats", nlohmann::json::object());
            const auto* optimizeNode = firstObjectValue(adaptive, {"optimize"});
            const auto* hostGpuArray = firstArrayValue(agentView, {"gpus"});

            const std::string agentId = firstStringValue(agentView, {"agent_id"});
            const std::string host = firstStringValue(agentView, {"host"}, agentId);
            const std::string rack = firstStringValue(agentView, {"rack"}, "unassigned");
            const std::string zone = firstStringValue(agentView, {"zone"}, "unassigned");
            const std::string status = normalizeTraceyStatus(firstStringValue(agentView, {"status"}, "unknown"));
            const std::string adaptiveMode = firstStringValue(adaptive, {"mode"}, "balanced");
            const std::string optimizeStatus = optimizeNode != nullptr
                                               ? firstStringValue(*optimizeNode, {"status"}, "unknown")
                                               : "unknown";

            const double gpuUtilPct = std::max(0.0, firstDoubleValue(summary, {"gpu_utilization_avg_pct"}, 0.0));
            const double placementScore = clampUnit(firstDoubleValue(
                    adaptive,
                    {"placement_score", "placementScore"},
                    firstDoubleValue(summary, {"adaptive_placement_score"}, 0.0)
            ));
            const double headroomPct = std::max(
                    0.0,
                    firstDoubleValue(adaptive, {"gpu_headroom_pct", "gpuHeadroomPct"}, std::max(0.0, 100.0 - gpuUtilPct))
            );
            const double compromiseRisk = clampUnit(firstDoubleValue(assessment, {"compromise_risk", "compromiseRisk"}, 0.0));
            const double autonomyRisk = clampUnit(firstDoubleValue(summary, {"autonomy_risk"}, 0.0));
            const double tempC = std::max(0.0, firstDoubleValue(summary, {"gpu_temperature_max_c"}, 0.0));
            const int reportedGpuCount = std::max<int64_t>(
                    0,
                    firstInt64Value(
                            summary,
                            {"gpu_count"},
                            static_cast<int64_t>(hostGpuArray != nullptr ? hostGpuArray->size() : 0)
                    )
            );
            const double gpuPowerTotalW = std::max(0.0, firstDoubleValue(summary, {"gpu_power_total_w"}, 0.0));
            const double gpuPowerPerGpuW = gpuPowerTotalW > 0.0
                                           ? gpuPowerTotalW / static_cast<double>(std::max(1, reportedGpuCount))
                                           : 0.0;
            const int quarantined = std::max<int64_t>(0, firstInt64Value(guard, {"quarantined_devices", "quarantinedDevices"}, 0));
            const int loaderBlocks = std::max<int64_t>(
                    0,
                    firstInt64Value(loader, {"blocked_provider_count", "blockedProviderCount"}, 0)
                    + firstInt64Value(loader, {"blocked_artifact_count", "blockedArtifactCount"}, 0)
            );
            const double headroomScore = clampUnit(headroomPct / 100.0);
            const double idleScore = clampUnit((100.0 - gpuUtilPct) / 100.0);
            const double riskSafety = clampUnit(1.0 - compromiseRisk);
            const double autonomySafety = clampUnit(1.0 - autonomyRisk);
            const double thermalSafety = temperatureSafetyScore(tempC);
            const double energyScore = powerEfficiencyScore(gpuPowerPerGpuW, 320.0);
            const double moderateUtilScore = moderateUtilizationScore(gpuUtilPct);
            const double hygienePenalty = std::min(
                    0.45,
                    static_cast<double>(quarantined) * 0.12 + static_cast<double>(loaderBlocks) * 0.08
            );

            double candidateScore = placementScore;
            if (placementPolicy == "throughput") {
                candidateScore = clampUnit(
                        0.32 * placementScore
                        + 0.33 * headroomScore
                        + 0.20 * idleScore
                        + 0.10 * thermalSafety
                        + 0.05 * riskSafety
                        - 0.55 * hygienePenalty
                );
            } else if (placementPolicy == "risk") {
                candidateScore = clampUnit(
                        0.18 * placementScore
                        + 0.12 * headroomScore
                        + 0.35 * riskSafety
                        + 0.20 * autonomySafety
                        + 0.15 * thermalSafety
                        - 1.25 * hygienePenalty
                );
            } else if (placementPolicy == "energy") {
                candidateScore = clampUnit(
                        0.20 * placementScore
                        + 0.15 * headroomScore
                        + 0.25 * moderateUtilScore
                        + 0.20 * thermalSafety
                        + 0.20 * energyScore
                        - 0.80 * hygienePenalty
                );
            } else {
                candidateScore = clampUnit(
                        0.35 * placementScore
                        + 0.25 * headroomScore
                        + 0.20 * riskSafety
                        + 0.10 * autonomySafety
                        + 0.10 * thermalSafety
                        - 0.90 * hygienePenalty
                );
            }

            std::vector<std::string> reasons;
            if (headroomPct >= 35.0) {
                pushLimitedUniqueString(reasons, "gpu headroom " + std::to_string(static_cast<int>(headroomPct)) + "%", 4, 56);
            }
            if (gpuUtilPct > 0.0) {
                pushLimitedUniqueString(reasons, "gpu util " + std::to_string(static_cast<int>(gpuUtilPct)) + "%", 4, 56);
            }
            if (compromiseRisk > 0.0) {
                pushLimitedUniqueString(reasons, "compromise " + std::to_string(static_cast<int>(compromiseRisk * 100.0)) + "%", 4, 56);
            }
            if (autonomyRisk > 0.0) {
                pushLimitedUniqueString(reasons, "autonomy " + std::to_string(static_cast<int>(autonomyRisk * 100.0)) + "%", 4, 56);
            }
            if (tempC > 0.0) {
                pushLimitedUniqueString(reasons, "temp " + std::to_string(static_cast<int>(tempC)) + "C", 4, 56);
            }
            if (gpuPowerPerGpuW > 0.0) {
                pushLimitedUniqueString(reasons, "power " + std::to_string(static_cast<int>(gpuPowerPerGpuW)) + "W/gpu", 4, 56);
            }
            if (quarantined > 0) {
                pushLimitedUniqueString(reasons, std::to_string(quarantined) + " quarantined GPUs", 4, 56);
            }
            if (loaderBlocks > 0) {
                pushLimitedUniqueString(reasons, std::to_string(loaderBlocks) + " loader blocks", 4, 56);
            }

            std::string lane = "watch";
            if (status == "offline"
                || adaptiveMode == "constrained"
                || optimizeStatus == "avoid"
                || compromiseRisk >= 0.80
                || (placementPolicy == "risk" && (quarantined > 0 || loaderBlocks > 0))) {
                lane = "avoid";
            } else if (candidateScore >= 0.80 && headroomPct >= 45.0 && compromiseRisk < 0.35 && tempC < 82.0) {
                lane = "burst";
            } else if (candidateScore >= 0.60 && compromiseRisk < 0.55 && optimizeStatus != "tight") {
                lane = "balanced";
            }

            return nlohmann::json{
                    {"agent_id", agentId},
                    {"host", host},
                    {"rack", rack},
                    {"zone", zone},
                    {"status", status},
                    {"lane", lane},
                    {"policy", placementPolicy},
                    {"policy_label", placementPolicyLabel},
                    {"loop_placement_score", placementScore},
                    {"placement_score", candidateScore},
                    {"headroom_pct", headroomPct},
                    {"gpu_utilization_avg_pct", gpuUtilPct},
                    {"gpu_power_total_w", gpuPowerTotalW},
                    {"power_per_gpu_w", gpuPowerPerGpuW},
                    {"compromise_risk", compromiseRisk},
                    {"autonomy_risk", autonomyRisk},
                    {"gpu_temperature_max_c", tempC},
                    {"reason", joinLimitedStrings(reasons, "No placement evidence.")}
            };
        };

        for (const auto& view : agentViews) {
            nlohmann::json adaptive = view.value("continuum_loop", nlohmann::json::object());
            if (!adaptive.is_object() || adaptive.empty()) {
                adaptive = buildAdaptiveLoopFallback(view);
            }

            const nlohmann::json summary = view.value("summary", nlohmann::json::object());
            const nlohmann::json assessment = view.value("continuum_assessment_summary", nlohmann::json::object());
            const nlohmann::json autoscaler = view.value("continuum_autoscaler", nlohmann::json::object());

            const std::string agentId = firstStringValue(view, {"agent_id"});
            const std::string host = firstStringValue(view, {"host"}, agentId);
            const std::string rack = firstStringValue(view, {"rack"}, "unassigned");
            const std::string zone = firstStringValue(view, {"zone"}, "unassigned");
            const std::string mode = firstStringValue(adaptive, {"mode"}, "balanced");
            const std::string nextAction = truncateDisplay(firstStringValue(adaptive, {"next_action", "nextAction"}, "Hold Steady"), 64);
            const std::string planStatus = firstObjectValue(adaptive, {"plan"}) != nullptr
                                           ? firstStringValue(*firstObjectValue(adaptive, {"plan"}), {"status"}, "unknown")
                                           : "unknown";
            const std::string rampStatus = firstObjectValue(adaptive, {"ramp"}) != nullptr
                                           ? firstStringValue(*firstObjectValue(adaptive, {"ramp"}), {"status"}, "unknown")
                                           : "unknown";
            const std::string optimizeStatus = firstObjectValue(adaptive, {"optimize"}) != nullptr
                                               ? firstStringValue(*firstObjectValue(adaptive, {"optimize"}), {"status"}, "unknown")
                                               : "unknown";
            const std::string repeatStatus = firstObjectValue(adaptive, {"repeat"}) != nullptr
                                             ? firstStringValue(*firstObjectValue(adaptive, {"repeat"}), {"status"}, "unknown")
                                             : "unknown";

            const double overallScore = clampUnit(firstDoubleValue(adaptive, {"overall_score", "overallScore"}, 0.0));
            const double readinessScore = clampUnit(firstDoubleValue(adaptive, {"readiness_score", "readinessScore"}, 0.0));
            const double placementScore = clampUnit(firstDoubleValue(adaptive, {"placement_score", "placementScore"}, 0.0));
            const double headroomPct = std::max(0.0, firstDoubleValue(adaptive, {"gpu_headroom_pct", "gpuHeadroomPct"}, std::max(0.0, 100.0 - firstDoubleValue(summary, {"gpu_utilization_avg_pct"}, 0.0))));
            const double compromiseRisk = clampUnit(firstDoubleValue(assessment, {"compromise_risk", "compromiseRisk"}, clampUnit(firstDoubleValue(adaptive, {"compromise_risk", "compromiseRisk"}, 0.0))));

            const int cveMatches = std::max<int64_t>(0, firstInt64Value(assessment, {"cve_matches", "cveMatches"}, 0));
            const int kevMatches = std::max<int64_t>(0, firstInt64Value(assessment, {"kev_matches", "kevMatches"}, 0));
            const int recentActionCount = std::max<int64_t>(0, firstInt64Value(summary, {"recent_action_count", "recentActionCount"}, 0));
            const int64_t requestedRemoteNodes = std::max<int64_t>(0, firstInt64Value(adaptive, {"requested_remote_nodes", "requestedRemoteNodes"}, std::max<int64_t>(0, firstInt64Value(autoscaler, {"requested_remote_nodes", "requestedRemoteNodes"}, 0))));
            const int64_t activeRemoteNodes = std::max<int64_t>(0, firstInt64Value(adaptive, {"active_remote_nodes", "activeRemoteNodes"}, std::max<int64_t>(0, firstInt64Value(autoscaler, {"active_remote_nodes", "activeRemoteNodes"}, 0))));
            const int pressureSignalCount = std::max<int64_t>(
                    0,
                    firstInt64Value(adaptive, {"pressure_signal_count", "pressureSignalCount"}, 0)
            );

            const auto* planNode = firstObjectValue(adaptive, {"plan"});
            const auto* rampNode = firstObjectValue(adaptive, {"ramp"});
            const auto* optimizeNode = firstObjectValue(adaptive, {"optimize"});
            const auto* repeatNode = firstObjectValue(adaptive, {"repeat"});
            const double planScore = planNode != nullptr ? clampUnit(firstDoubleValue(*planNode, {"score"}, 0.0)) : 0.0;
            const double rampScore = rampNode != nullptr ? clampUnit(firstDoubleValue(*rampNode, {"score"}, 0.0)) : 0.0;
            const double optimizeScore = optimizeNode != nullptr ? clampUnit(firstDoubleValue(*optimizeNode, {"score"}, 0.0)) : 0.0;
            const double repeatScore = repeatNode != nullptr ? clampUnit(firstDoubleValue(*repeatNode, {"score"}, 0.0)) : 0.0;

            overallScoreSum += overallScore;
            readinessScoreSum += readinessScore;
            scoreCount++;
            headroomPctSum += headroomPct;
            headroomCount++;
            planScoreSum += planScore;
            rampScoreSum += rampScore;
            optimizeScoreSum += optimizeScore;
            repeatScoreSum += repeatScore;
            phaseCount++;

            recentActionTotal += recentActionCount;
            cveMatchesTotal += cveMatches;
            kevMatchesTotal += kevMatches;
            requestedRemoteNodesTotal += requestedRemoteNodes;
            activeRemoteNodesTotal += activeRemoteNodes;

            if (compromiseRisk >= 0.80 || mode == "constrained" || optimizeStatus == "avoid") {
                constrainedAgents++;
            }
            if (compromiseRisk >= 0.80) {
                compromisedAgents++;
            } else if (compromiseRisk >= 0.55) {
                elevatedAgents++;
            }
            if (rampStatus == "active" || pressureSignalCount > 0 || requestedRemoteNodes > activeRemoteNodes) {
                pressuredAgents++;
            }
            if (repeatStatus == "stale" || std::max<int64_t>(0, firstInt64Value(view, {"last_seen_seconds_ago"}, 0)) > 300) {
                staleAgents++;
            }

            const nlohmann::json hostCandidate = buildHostCandidate(view, adaptive);
            placementScoreSum += hostCandidate.value("placement_score", placementScore);
            const std::string lane = hostCandidate.value("lane", "watch");
            if (lane == "burst" || lane == "balanced") {
                preferredHosts++;
            }
            if (hostCandidate.value("placement_score", 0.0) >= 0.35 && lane != "avoid") {
                placementCandidates.push_back(hostCandidate);
            }

            const auto* recommendationsNode = firstArrayValue(adaptive, {"recommendations"});
            if (recommendationsNode != nullptr) {
                for (const auto& item : *recommendationsNode) {
                    pushRecommendation(item, agentId, host, rack, zone);
                }
            }

            const auto* gpus = firstArrayValue(view, {"gpus"});
            if (gpus != nullptr) {
                for (const auto& gpu : *gpus) {
                    if (!gpu.is_object()) {
                        continue;
                    }
                    const std::string gpuId = firstStringValue(gpu, {"gpu_id", "gpuId"});
                    if (gpuId.empty()) {
                        continue;
                    }
                    const double gpuUtilPct = std::max(0.0, firstDoubleValue(gpu, {"util_pct", "utilPct"}, firstDoubleValue(summary, {"gpu_utilization_avg_pct"}, 0.0)));
                    const double gpuTempC = std::max(0.0, firstDoubleValue(gpu, {"temp_c", "tempC"}, 0.0));
                    const double gpuPowerW = std::max(0.0, firstDoubleValue(gpu, {"power_w", "powerW"}, 0.0));
                    double memUsedPct = firstDoubleValue(gpu, {"mem_used_pct", "memUsedPct"}, -1.0);
                    if (memUsedPct < 0.0) {
                        const double memUsedBytes = firstDoubleValue(gpu, {"mem_used_bytes", "memUsedBytes"}, 0.0);
                        const double memTotalBytes = firstDoubleValue(gpu, {"mem_total_bytes", "memTotalBytes"}, 0.0);
                        if (memTotalBytes > 0.0) {
                            memUsedPct = (memUsedBytes / memTotalBytes) * 100.0;
                        }
                    }
                    const double reliability = clampUnit(firstDoubleValue(gpu, {"reliability_score", "reliabilityScore"}, 0.75));
                    const std::string guardState = toLower(firstStringValue(gpu, {"guard_state", "guardState"}, "healthy"));
                    double guardPenalty = 0.0;
                    if (guardState == "condemned" || guardState == "quarantined") {
                        guardPenalty = 0.60;
                    } else if (guardState == "suspect" || guardState == "deep_test" || guardState == "deep-test") {
                        guardPenalty = 0.25;
                    }
                    const double thermalSafety = temperatureSafetyScore(gpuTempC);
                    const double memHeadroom = memUsedPct >= 0.0 ? clampUnit((100.0 - memUsedPct) / 100.0) : 0.5;
                    const double idleScore = clampUnit((100.0 - gpuUtilPct) / 100.0);
                    const double moderateUtilScore = moderateUtilizationScore(gpuUtilPct);
                    const double riskSafety = clampUnit(1.0 - compromiseRisk);
                    const double energyScore = powerEfficiencyScore(gpuPowerW, 350.0);
                    double gpuScore = placementScore;
                    if (placementPolicy == "throughput") {
                        gpuScore = clampUnit(
                                0.34 * idleScore
                                + 0.24 * placementScore
                                + 0.18 * memHeadroom
                                + 0.14 * reliability
                                + 0.07 * thermalSafety
                                + 0.03 * riskSafety
                                - 0.75 * guardPenalty
                        );
                    } else if (placementPolicy == "risk") {
                        gpuScore = clampUnit(
                                0.18 * placementScore
                                + 0.26 * reliability
                                + 0.18 * memHeadroom
                                + 0.16 * thermalSafety
                                + 0.15 * riskSafety
                                + 0.07 * clampUnit(1.0 - guardPenalty)
                                - 1.20 * guardPenalty
                        );
                    } else if (placementPolicy == "energy") {
                        gpuScore = clampUnit(
                                0.18 * placementScore
                                + 0.16 * reliability
                                + 0.16 * memHeadroom
                                + 0.18 * thermalSafety
                                + 0.17 * moderateUtilScore
                                + 0.15 * energyScore
                                + 0.10 * riskSafety
                                - 0.90 * guardPenalty
                        );
                    } else {
                        gpuScore = clampUnit(
                                0.28 * idleScore
                                + 0.22 * placementScore
                                + 0.20 * reliability
                                + 0.15 * memHeadroom
                                + 0.10 * thermalSafety
                                + 0.05 * riskSafety
                                - 0.90 * guardPenalty
                        );
                    }

                    std::vector<std::string> reasons;
                    pushLimitedUniqueString(reasons, "util " + std::to_string(static_cast<int>(gpuUtilPct)) + "%", 4, 48);
                    if (gpuTempC > 0.0) {
                        pushLimitedUniqueString(reasons, "temp " + std::to_string(static_cast<int>(gpuTempC)) + "C", 4, 48);
                    }
                    if (gpuPowerW > 0.0) {
                        pushLimitedUniqueString(reasons, "power " + std::to_string(static_cast<int>(gpuPowerW)) + "W", 4, 48);
                    }
                    if (memUsedPct >= 0.0) {
                        pushLimitedUniqueString(reasons, "mem " + std::to_string(static_cast<int>(memUsedPct)) + "%", 4, 48);
                    }
                    pushLimitedUniqueString(reasons, "reliability " + std::to_string(static_cast<int>(reliability * 100.0)) + "%", 4, 48);
                    if (guardState != "healthy") {
                        pushLimitedUniqueString(reasons, "guard " + guardState, 4, 48);
                    }

                    if (gpuScore >= (placementPolicy == "risk" ? 0.72 : 0.70)
                        && guardPenalty < (placementPolicy == "risk" ? 0.10 : 0.25)) {
                        preferredGpus++;
                    }
                    if (gpuScore >= 0.35 && guardPenalty < (placementPolicy == "risk" ? 0.25 : 0.60)) {
                        gpuCandidates.push_back({
                                {"agent_id", agentId},
                                {"host", host},
                                {"rack", rack},
                                {"zone", zone},
                                {"gpu_id", gpuId},
                                {"policy", placementPolicy},
                                {"policy_label", placementPolicyLabel},
                                {"loop_placement_score", placementScore},
                                {"placement_score", gpuScore},
                                {"util_pct", gpuUtilPct},
                                {"temp_c", gpuTempC},
                                {"power_w", gpuPowerW},
                                {"mem_used_pct", memUsedPct >= 0.0 ? memUsedPct : 0.0},
                                {"reliability_score", reliability},
                                {"guard_state", guardState.empty() ? "healthy" : guardState},
                                {"reason", joinLimitedStrings(reasons, "No GPU signal detail.")}
                        });
                    }
                }
            }

            agentRows.push_back({
                    {"agent_id", agentId},
                    {"host", host},
                    {"rack", rack},
                    {"zone", zone},
                    {"status", firstStringValue(view, {"status"}, "unknown")},
                    {"mode", mode},
                    {"overall_score", overallScore},
                    {"readiness_score", readinessScore},
                    {"placement_score", placementScore},
                    {"headroom_pct", headroomPct},
                    {"compromise_risk", compromiseRisk},
                    {"cve_matches", cveMatches},
                    {"kev_matches", kevMatches},
                    {"requested_remote_nodes", requestedRemoteNodes},
                    {"active_remote_nodes", activeRemoteNodes},
                    {"recent_action_count", recentActionCount},
                    {"plan_status", planStatus},
                    {"ramp_status", rampStatus},
                    {"optimize_status", optimizeStatus},
                    {"repeat_status", repeatStatus},
                    {"next_action", nextAction}
            });
        }

        std::sort(agentRows.begin(), agentRows.end(), [](const nlohmann::json& left, const nlohmann::json& right) {
            const double leftScore = left.value("overall_score", 0.0);
            const double rightScore = right.value("overall_score", 0.0);
            if (leftScore == rightScore) {
                return left.value("host", "") < right.value("host", "");
            }
            return leftScore > rightScore;
        });

        std::sort(placementCandidates.begin(), placementCandidates.end(), [](const nlohmann::json& left, const nlohmann::json& right) {
            const double leftScore = left.value("placement_score", 0.0);
            const double rightScore = right.value("placement_score", 0.0);
            if (leftScore == rightScore) {
                return left.value("host", "") < right.value("host", "");
            }
            return leftScore > rightScore;
        });
        placementCandidates = limitedJsonArray(placementCandidates, 8);

        std::sort(gpuCandidates.begin(), gpuCandidates.end(), [](const nlohmann::json& left, const nlohmann::json& right) {
            const double leftScore = left.value("placement_score", 0.0);
            const double rightScore = right.value("placement_score", 0.0);
            if (leftScore == rightScore) {
                const std::string leftHost = left.value("host", "");
                const std::string rightHost = right.value("host", "");
                if (leftHost == rightHost) {
                    return left.value("gpu_id", "") < right.value("gpu_id", "");
                }
                return leftHost < rightHost;
            }
            return leftScore > rightScore;
        });
        gpuCandidates = limitedJsonArray(gpuCandidates, 12);

        std::sort(recommendations.begin(), recommendations.end(), [](const nlohmann::json& left, const nlohmann::json& right) {
            const int leftRank = left.value("priority_rank", 1);
            const int rightRank = right.value("priority_rank", 1);
            if (leftRank == rightRank) {
                const std::string leftStage = left.value("stage", "");
                const std::string rightStage = right.value("stage", "");
                if (leftStage == rightStage) {
                    return left.value("host", "") < right.value("host", "");
                }
                return leftStage < rightStage;
            }
            return leftRank > rightRank;
        });
        recommendations = limitedJsonArray(recommendations, 10);
        for (auto& item : recommendations) {
            if (item.is_object()) {
                item.erase("priority_rank");
            }
        }

        const double avgOverallScore = scoreCount > 0 ? overallScoreSum / static_cast<double>(scoreCount) : 0.0;
        const double avgReadinessScore = scoreCount > 0 ? readinessScoreSum / static_cast<double>(scoreCount) : 0.0;
        const double avgPlacementScore = scoreCount > 0 ? placementScoreSum / static_cast<double>(scoreCount) : 0.0;
        const double avgHeadroomPct = headroomCount > 0 ? headroomPctSum / static_cast<double>(headroomCount) : 0.0;
        const double avgPlanScore = phaseCount > 0 ? planScoreSum / static_cast<double>(phaseCount) : 0.0;
        const double avgRampScore = phaseCount > 0 ? rampScoreSum / static_cast<double>(phaseCount) : 0.0;
        const double avgOptimizeScore = phaseCount > 0 ? optimizeScoreSum / static_cast<double>(phaseCount) : 0.0;
        const double avgRepeatScore = phaseCount > 0 ? repeatScoreSum / static_cast<double>(phaseCount) : 0.0;

        const std::string overallMode = constrainedAgents > 0
                                        ? "constrained"
                                        : (pressuredAgents > 0
                                           ? "ramping"
                                           : (staleAgents > 0 ? "degraded" : "ready"));
        const std::string planStatus = constrainedAgents > 0
                                       ? "constrained"
                                       : ((cveMatchesTotal > 0 || kevMatchesTotal > 0) ? "learning" : "ready");
        const std::string rampStatus = (pressuredAgents > 0 || requestedRemoteNodesTotal > activeRemoteNodesTotal)
                                       ? "active"
                                       : ((requestedRemoteNodesTotal > 0 || activeRemoteNodesTotal > 0) ? "ramping" : "steady");
        const std::string optimizeStatus = preferredHosts > 0
                                           ? (avgPlacementScore >= 0.72 ? "open" : "balanced")
                                           : (placementCandidates.empty() ? "tight" : "balanced");
        const std::string repeatStatus = staleAgents > 0 ? "stale" : (recentActionTotal > 0 ? "learning" : "watch");

        const nlohmann::json infrastructure = {
                {"k8s_clusters", static_cast<int>(k8sCount)},
                {"vclusters", static_cast<int>(vclusterCount)},
                {"vms", static_cast<int>(vmCount)},
                {"tracey_agents", static_cast<int>(agentViews.size())},
                {"tracey_requirements", static_cast<int>(requirementCount)}
        };
        const std::string nextAction = !recommendations.empty() && recommendations.front().is_object()
                                       ? firstStringValue(recommendations.front(), {"action"}, "Hold Steady")
                                       : "Hold Steady";

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Tracey adaptive loop generated successfully.";
        apiResponse.data = {
                {"generated_epoch_ms", nowMs},
                {"summary", {
                        {"mode", overallMode},
                        {"policy", placementPolicy},
                        {"policy_label", placementPolicyLabel},
                        {"agents_total", static_cast<int>(agentViews.size())},
                        {"gpu_total", std::max<int64_t>(0, firstInt64Value(fleet.value("summary", nlohmann::json::object()), {"gpu_total"}, 0))},
                        {"overall_score", avgOverallScore},
                        {"readiness_score", avgReadinessScore},
                        {"placement_score", avgPlacementScore},
                        {"gpu_headroom_pct", avgHeadroomPct},
                        {"pressured_agents", pressuredAgents},
                        {"constrained_agents", constrainedAgents},
                        {"stale_agents", staleAgents},
                        {"preferred_hosts", preferredHosts},
                        {"preferred_gpus", preferredGpus},
                        {"placement_candidate_count", static_cast<int>(placementCandidates.size())},
                        {"gpu_candidate_count", static_cast<int>(gpuCandidates.size())},
                        {"recommendation_count", static_cast<int>(recommendations.size())},
                        {"requested_remote_nodes", requestedRemoteNodesTotal},
                        {"active_remote_nodes", activeRemoteNodesTotal},
                        {"recent_action_count", recentActionTotal},
                        {"cve_matches", cveMatchesTotal},
                        {"kev_matches", kevMatchesTotal},
                        {"compromised_agents", compromisedAgents},
                        {"elevated_agents", elevatedAgents},
                        {"next_action", nextAction}
                }},
                {"infrastructure", infrastructure},
                {"plan", {
                        {"status", planStatus},
                        {"score", avgPlanScore},
                        {"headline", truncateDisplay(
                                std::to_string(cveMatchesTotal) + " CVE / " + std::to_string(kevMatchesTotal)
                                + " KEV matches across " + std::to_string(static_cast<int>(agentViews.size())) + " agents",
                                120
                        )},
                        {"signals", nlohmann::json::array({
                                truncateDisplay(std::to_string(compromisedAgents) + " compromised / " + std::to_string(elevatedAgents) + " elevated agents", 96),
                                truncateDisplay(std::to_string(requirementCount) + " managed Tracey requirements", 96)
                        })}
                }},
                {"ramp", {
                        {"status", rampStatus},
                        {"score", avgRampScore},
                        {"headline", truncateDisplay(
                                std::to_string(pressuredAgents) + " pressured agents | req " + std::to_string(requestedRemoteNodesTotal)
                                + " / active " + std::to_string(activeRemoteNodesTotal),
                                120
                        )},
                        {"signals", nlohmann::json::array({
                                truncateDisplay(std::to_string(pressuredAgents) + " agents signalling scale pressure", 96),
                                truncateDisplay(std::to_string(requestedRemoteNodesTotal) + " requested remote nodes", 96)
                        })}
                }},
                {"optimize", {
                        {"status", optimizeStatus},
                        {"score", avgOptimizeScore},
                        {"headline", truncateDisplay(
                                placementPolicyLabel + " policy | " + std::to_string(preferredHosts) + " host candidates | " + std::to_string(preferredGpus)
                                + " GPU targets | headroom " + std::to_string(static_cast<int>(avgHeadroomPct)) + "%",
                                120
                        )},
                        {"signals", nlohmann::json::array({
                                truncateDisplay(std::to_string(placementCandidates.size()) + " hosts shortlisted under " + toLower(placementPolicyLabel) + " policy", 96),
                                truncateDisplay(std::to_string(gpuCandidates.size()) + " GPUs remain placement-eligible", 96)
                        })}
                }},
                {"repeat", {
                        {"status", repeatStatus},
                        {"score", avgRepeatScore},
                        {"headline", truncateDisplay(
                                std::to_string(recentActionTotal) + " recent actions | " + std::to_string(staleAgents)
                                + " stale agents | readiness " + std::to_string(static_cast<int>(avgReadinessScore * 100.0)) + "%",
                                120
                        )},
                        {"signals", nlohmann::json::array({
                                truncateDisplay(std::to_string(recentActionTotal) + " loop actions fed back into placement state", 96),
                                truncateDisplay(std::to_string(staleAgents) + " agents need fresher telemetry", 96)
                        })}
                }},
                {"recommendations", recommendations},
                {"placement_candidates", placementCandidates},
                {"gpu_candidates", gpuCandidates},
                {"recent_actions", fleet.value("recent_actions", nlohmann::json::array())},
                {"agents", agentRows}
        };
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleTraceyCveStatus(const httplib::Request& req, httplib::Response& res) {
        (void)req;

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Tracey CVE mirror status generated successfully.";
        apiResponse.data = traceyCveIntel.mirrorStatus();
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleTraceyAssessmentFleet(const httplib::Request& req, httplib::Response& res) {
        (void)req;
        const int64_t nowMs = nowEpochMs();
        markTraceyStaleAgents(nowMs);

        std::vector<TraceyAgent> snapshot;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            snapshot.reserve(traceyAgents.size());
            for (const auto& [id, agent] : traceyAgents) {
                (void)id;
                snapshot.push_back(agent);
            }
        }

        std::sort(snapshot.begin(), snapshot.end(), [](const TraceyAgent& left, const TraceyAgent& right) {
            return left.agentId < right.agentId;
        });

        std::vector<nlohmann::json> agentViews;
        agentViews.reserve(snapshot.size());
        for (const auto& agent : snapshot) {
            agentViews.push_back(buildTraceyContinuumAgentView(agent, nowMs));
        }

        const nlohmann::json fleet = buildTraceyFleetViewFromAgents(agentViews, nowMs);
        nlohmann::json agentRows = nlohmann::json::array();
        int64_t expectedSlices = 0;
        int64_t completedSlices = 0;
        int agentsWithProgress = 0;
        int agentsCompletedCycle = 0;
        int64_t rejectedReports = 0;
        int64_t duplicateReports = 0;
        int64_t stalePlanReports = 0;
        int64_t semanticFaults = 0;

        for (const auto& view : agentViews) {
            const std::string agentId = firstStringValue(view, {"agent_id"});
            const nlohmann::json summary = view.value("continuum_assessment_summary", nlohmann::json::object());
            const nlohmann::json progress = traceyCveIntel.agentProgress(agentId);
            const int64_t sliceCount = std::max<int64_t>(0, firstInt64Value(progress, {"slice_count"}, 0));
            const int64_t completed = std::max<int64_t>(0, firstInt64Value(progress, {"completed_slice_count"}, 0));
            if (sliceCount > 0) {
                expectedSlices += sliceCount;
                completedSlices += std::min(sliceCount, completed);
                agentsWithProgress++;
                if (completed >= sliceCount) {
                    agentsCompletedCycle++;
                }
            }
            rejectedReports += std::max<int64_t>(0, firstInt64Value(progress, {"reports_rejected"}, 0));
            duplicateReports += std::max<int64_t>(0, firstInt64Value(progress, {"duplicate_reports"}, 0));
            stalePlanReports += std::max<int64_t>(0, firstInt64Value(progress, {"stale_plan_reports"}, 0));
            semanticFaults += std::max<int64_t>(0, firstInt64Value(progress, {"semantic_faults"}, 0));

            agentRows.push_back({
                    {"agent_id", agentId},
                    {"status", firstStringValue(view, {"status"})},
                    {"host", firstStringValue(view, {"host"})},
                    {"rack", firstStringValue(view, {"rack"})},
                    {"zone", firstStringValue(view, {"zone"})},
                    {"compromise_risk", std::max(0.0, firstDoubleValue(summary, {"compromise_risk", "compromiseRisk"}, 0.0))},
                    {"compromise_confidence", std::max(0.0, firstDoubleValue(summary, {"compromise_confidence", "compromiseConfidence"}, 0.0))},
                    {"cve_matches", std::max<int64_t>(0, firstInt64Value(summary, {"cve_matches", "cveMatches"}, 0))},
                    {"kev_matches", std::max<int64_t>(0, firstInt64Value(summary, {"kev_matches", "kevMatches"}, 0))},
                    {"recommended_action", firstStringValue(summary, {"recommended_action", "recommendedAction"})},
                    {"assessment_status", firstStringValue(summary, {"status"}, "unknown")},
                    {"progress", progress}
            });
        }

        std::sort(agentRows.begin(), agentRows.end(), [](const nlohmann::json& left, const nlohmann::json& right) {
            const double leftRisk = left.value("compromise_risk", 0.0);
            const double rightRisk = right.value("compromise_risk", 0.0);
            if (leftRisk == rightRisk) {
                return left.value("agent_id", "") < right.value("agent_id", "");
            }
            return leftRisk > rightRisk;
        });

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Tracey fleet compromise assessment generated successfully.";
        apiResponse.data = {
                {"generated_epoch_ms", nowMs},
                {"mirror", traceyCveIntel.mirrorStatus()},
                {"fleet", fleet.value("summary", nlohmann::json::object())},
                {"progress", {
                        {"expected_slices", expectedSlices},
                        {"completed_slices", completedSlices},
                        {"completion_pct", expectedSlices > 0 ? static_cast<double>(completedSlices) / static_cast<double>(expectedSlices) : 0.0},
                        {"agents_with_progress", agentsWithProgress},
                        {"agents_completed_cycle", agentsCompletedCycle}
                }},
                {"communication", {
                        {"reports_rejected", rejectedReports},
                        {"duplicate_reports", duplicateReports},
                        {"stale_plan_reports", stalePlanReports},
                        {"semantic_faults", semanticFaults}
                }},
                {"agents", agentRows}
        };
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleTraceyAssessmentPlan(const httplib::Request& req, httplib::Response& res) {
        const std::string agentId = req.matches.size() > 1 ? trim(req.matches[1].str()) : "";
        if (agentId.empty()) {
            return sendErrorResponse(res, 400, "Missing required parameter: agent_id.");
        }

        const int64_t nowMs = nowEpochMs();
        markTraceyStaleAgents(nowMs);
        std::vector<std::string> orderedAgentIds;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            orderedAgentIds.reserve(traceyAgents.size() + 1);
            for (const auto& [id, agent] : traceyAgents) {
                (void)agent;
                orderedAgentIds.push_back(id);
            }
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Tracey assessment plan generated successfully.";
        apiResponse.data = traceyCveIntel.buildAgentPlan(agentId, orderedAgentIds, nowMs);
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleTraceyAssessmentReport(const httplib::Request& req, httplib::Response& res) {
        const std::string agentId = req.matches.size() > 1 ? trim(req.matches[1].str()) : "";
        if (agentId.empty()) {
            return sendErrorResponse(res, 400, "Missing required parameter: agent_id.");
        }

        nlohmann::json payload = nlohmann::json::object();
        if (!req.body.empty()) {
            payload = nlohmann::json::parse(req.body, nullptr, false);
            if (payload.is_discarded() || !payload.is_object()) {
                return sendErrorResponse(res, 400, "Invalid assessment report payload.");
            }
        }

        const int64_t nowMs = nowEpochMs();
        markTraceyStaleAgents(nowMs);
        std::vector<std::string> orderedAgentIds;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            orderedAgentIds.reserve(traceyAgents.size() + 1);
            for (const auto& [id, agent] : traceyAgents) {
                (void)agent;
                orderedAgentIds.push_back(id);
            }
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Tracey assessment report processed successfully.";
        apiResponse.data = traceyCveIntel.ingestAssessmentReport(agentId, payload, orderedAgentIds, nowMs);
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleTraceyAgentCompromise(const httplib::Request& req, httplib::Response& res) {
        const std::string agentId = req.matches.size() > 1 ? trim(req.matches[1].str()) : "";
        if (agentId.empty()) {
            return sendErrorResponse(res, 400, "Missing required parameter: agent_id.");
        }

        TraceyAgent agent;
        bool found = false;
        std::vector<std::string> orderedAgentIds;
        const int64_t nowMs = nowEpochMs();
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            orderedAgentIds.reserve(traceyAgents.size() + 1);
            for (const auto& [id, current] : traceyAgents) {
                orderedAgentIds.push_back(id);
                if (id == agentId) {
                    agent = current;
                    found = true;
                }
            }
        }

        if (!found) {
            return sendErrorResponse(res, 404, "Tracey agent '" + agentId + "' not found.");
        }

        const nlohmann::json view = buildTraceyContinuumAgentView(agent, nowMs);
        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Tracey agent compromise view generated successfully.";
        apiResponse.data = {
                {"generated_epoch_ms", nowMs},
                {"agent_id", agentId},
                {"plan", traceyCveIntel.buildAgentPlan(agentId, orderedAgentIds, nowMs)},
                {"progress", traceyCveIntel.agentProgress(agentId)},
                {"mirror", traceyCveIntel.mirrorStatus()},
                {"current", {
                        {"agent_id", firstStringValue(view, {"agent_id"})},
                        {"status", firstStringValue(view, {"status"})},
                        {"host", firstStringValue(view, {"host"})},
                        {"rack", firstStringValue(view, {"rack"})},
                        {"zone", firstStringValue(view, {"zone"})},
                        {"last_seen_seconds_ago", std::max<int64_t>(0, firstInt64Value(view, {"last_seen_seconds_ago"}, 0))}
                }},
                {"continuum_assessment", view.value("continuum_assessment", nlohmann::json::object())},
                {"continuum_assessment_summary", view.value("continuum_assessment_summary", nlohmann::json::object())},
                {"tracey_guard_summary", view.value("tracey_guard_summary", nlohmann::json::object())},
                {"loader_threats", view.value("loader_threats", nlohmann::json::object())}
        };
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleListTraceyRacks(const httplib::Request& req, httplib::Response& res) {
        (void)req;
        const int64_t nowMs = nowEpochMs();
        markTraceyStaleAgents(nowMs);

        std::vector<TraceyAgent> snapshot;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            snapshot.reserve(traceyAgents.size());
            for (const auto& [id, agent] : traceyAgents) {
                (void)id;
                snapshot.push_back(agent);
            }
        }

        std::sort(snapshot.begin(), snapshot.end(), [](const TraceyAgent& left, const TraceyAgent& right) {
            return left.agentId < right.agentId;
        });

        std::vector<nlohmann::json> agentViews;
        agentViews.reserve(snapshot.size());
        for (const auto& agent : snapshot) {
            agentViews.push_back(buildTraceyContinuumAgentView(agent, nowMs));
        }

        const nlohmann::json fleet = buildTraceyFleetViewFromAgents(agentViews, nowMs);
        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Tracey rack summaries generated successfully.";
        apiResponse.data = {
                {"generated_epoch_ms", nowMs},
                {"summary", fleet.value("summary", nlohmann::json::object())},
                {"zone_breakdown", fleet.value("zone_breakdown", nlohmann::json::array())},
                {"racks", fleet.value("racks", nlohmann::json::array())}
        };
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleTraceyRackDetails(const httplib::Request& req, httplib::Response& res) {
        const std::string rackId = req.matches.size() > 1 ? trim(req.matches[1].str()) : "";
        if (rackId.empty()) {
            return sendErrorResponse(res, 400, "Missing required parameter: rack.");
        }

        const int64_t nowMs = nowEpochMs();
        markTraceyStaleAgents(nowMs);

        std::vector<TraceyAgent> snapshot;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            snapshot.reserve(traceyAgents.size());
            for (const auto& [id, agent] : traceyAgents) {
                (void)id;
                snapshot.push_back(agent);
            }
        }

        std::vector<nlohmann::json> filtered;
        filtered.reserve(snapshot.size());
        const std::string wantedRack = toLower(rackId);
        for (const auto& agent : snapshot) {
            const nlohmann::json view = buildTraceyContinuumAgentView(agent, nowMs);
            if (toLower(firstStringValue(view, {"rack"})) == wantedRack) {
                filtered.push_back(view);
            }
        }

        if (filtered.empty()) {
            return sendErrorResponse(res, 404, "Tracey rack '" + rackId + "' not found.");
        }

        const nlohmann::json fleet = buildTraceyFleetViewFromAgents(filtered, nowMs);
        const nlohmann::json rackSummary = fleet.contains("racks") && fleet["racks"].is_array() && !fleet["racks"].empty()
                                           ? fleet["racks"].front()
                                           : nlohmann::json::object();
        const nlohmann::json rackHeatmap = fleet.contains("gpu_heatmap") && fleet["gpu_heatmap"].is_array() && !fleet["gpu_heatmap"].empty()
                                           ? fleet["gpu_heatmap"].front()
                                           : nlohmann::json::object();

        std::sort(filtered.begin(), filtered.end(), [](const nlohmann::json& left, const nlohmann::json& right) {
            const std::string leftHost = firstStringValue(left, {"host"});
            const std::string rightHost = firstStringValue(right, {"host"});
            if (leftHost == rightHost) {
                return firstStringValue(left, {"agent_id"}) < firstStringValue(right, {"agent_id"});
            }
            return leftHost < rightHost;
        });

        nlohmann::json servers = nlohmann::json::array();
        for (const auto& view : filtered) {
            servers.push_back({
                    {"agent_id", firstStringValue(view, {"agent_id"})},
                    {"cluster", firstStringValue(view, {"cluster"})},
                    {"status", firstStringValue(view, {"status"})},
                    {"host", firstStringValue(view, {"host"})},
                    {"version", firstStringValue(view, {"version"})},
                    {"last_seen_seconds_ago", std::max<int64_t>(0, firstInt64Value(view, {"last_seen_seconds_ago"}, 0))},
                    {"query_failures", std::max<int64_t>(0, firstInt64Value(view, {"query_failures"}, 0))},
                    {"summary", view.value("summary", nlohmann::json::object())},
                    {"gpus", view.value("gpus", nlohmann::json::array())},
                    {"recent_actions", limitedJsonArray(view.value("recent_actions", nlohmann::json::array()), 12)},
                    {"continuum_assessment", view.value("continuum_assessment", nlohmann::json::object())},
                    {"continuum_assessment_summary", view.value("continuum_assessment_summary", nlohmann::json::object())},
                    {"tracey_guard_summary", view.value("tracey_guard_summary", nlohmann::json::object())},
                    {"loader_threats", view.value("loader_threats", nlohmann::json::object())}
            });
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Tracey rack detail generated successfully.";
        apiResponse.data = {
                {"generated_epoch_ms", nowMs},
                {"rack", firstStringValue(rackSummary, {"rack"})},
                {"zone", firstStringValue(rackSummary, {"zone"})},
                {"summary", rackSummary},
                {"servers", servers},
                {"gpu_heatmap", rackHeatmap.value("cells", nlohmann::json::array())},
                {"recent_actions", fleet.value("recent_actions", nlohmann::json::array())}
        };
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleTraceyAgentServer(const httplib::Request& req, httplib::Response& res) {
        const std::string agentId = req.matches.size() > 1 ? trim(req.matches[1].str()) : "";
        if (agentId.empty()) {
            return sendErrorResponse(res, 400, "Missing required parameter: agent_id.");
        }

        TraceyAgent agent;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            const auto it = traceyAgents.find(agentId);
            if (it != traceyAgents.end()) {
                agent = it->second;
                found = true;
            }
        }

        if (!found) {
            return sendErrorResponse(res, 404, "Tracey agent '" + agentId + "' not found.");
        }

        const int64_t nowMs = nowEpochMs();
        const nlohmann::json view = buildTraceyContinuumAgentView(agent, nowMs);

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Tracey server telemetry generated successfully.";
        apiResponse.data = {
                {"generated_epoch_ms", nowMs},
                {"agent_id", firstStringValue(view, {"agent_id"})},
                {"status", firstStringValue(view, {"status"})},
                {"host", firstStringValue(view, {"host"})},
                {"rack", firstStringValue(view, {"rack"})},
                {"zone", firstStringValue(view, {"zone"})},
                {"identity", view.value("identity", nlohmann::json::object())},
                {"summary", view.value("summary", nlohmann::json::object())},
                {"server", view.value("server", nlohmann::json::object())},
                {"gpus", view.value("gpus", nlohmann::json::array())},
                {"recent_actions", view.value("recent_actions", nlohmann::json::array())},
                {"recent_executions", limitedJsonArray(view.value("recent_executions", nlohmann::json::array()), 32)},
                {"recent_faults", view.value("recent_faults", nlohmann::json::array())},
                {"remote_faults", view.value("remote_faults", nlohmann::json::array())},
                {"continuum_assessment", view.value("continuum_assessment", nlohmann::json::object())},
                {"continuum_assessment_summary", view.value("continuum_assessment_summary", nlohmann::json::object())},
                {"tracey_guard_summary", view.value("tracey_guard_summary", nlohmann::json::object())},
                {"loader_threats", view.value("loader_threats", nlohmann::json::object())}
        };
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleTraceyAgentGpu(const httplib::Request& req, httplib::Response& res) {
        const std::string agentId = req.matches.size() > 1 ? trim(req.matches[1].str()) : "";
        const std::string gpuId = req.matches.size() > 2 ? trim(req.matches[2].str()) : "";
        if (agentId.empty() || gpuId.empty()) {
            return sendErrorResponse(res, 400, "Missing required parameter: agent_id or gpu_id.");
        }

        TraceyAgent agent;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            const auto it = traceyAgents.find(agentId);
            if (it != traceyAgents.end()) {
                agent = it->second;
                found = true;
            }
        }

        if (!found) {
            return sendErrorResponse(res, 404, "Tracey agent '" + agentId + "' not found.");
        }

        const int64_t nowMs = nowEpochMs();
        const nlohmann::json view = buildTraceyContinuumAgentView(agent, nowMs);
        const nlohmann::json gpus = view.value("gpus", nlohmann::json::array());
        nlohmann::json gpuDetail = nlohmann::json::object();
        for (const auto& gpu : gpus) {
            const std::string candidate = firstStringValue(gpu, {"gpu_id", "gpuId"});
            if (candidate == gpuId || toLower(candidate) == toLower(gpuId)) {
                gpuDetail = gpu;
                break;
            }
        }

        if (!gpuDetail.is_object() || gpuDetail.empty()) {
            return sendErrorResponse(res, 404, "GPU '" + gpuId + "' not found for Tracey agent '" + agentId + "'.");
        }

        struct SmAgg {
            int executions{0};
            int failCount{0};
            int errorCount{0};
            int timeoutCount{0};
            double riskMax{0.0};
            double confidenceSum{0.0};
            int confidenceCount{0};
            int64_t lastTsMs{0};
        };

        std::map<int64_t, SmAgg> smAggs;
        std::map<std::string, int> probeMixCounts;
        nlohmann::json recentExecutions = nlohmann::json::array();
        for (const auto& execution : view.value("recent_executions", nlohmann::json::array())) {
            if (!execution.is_object()) {
                continue;
            }
            const std::string executionGpuId = firstStringValue(execution, {"gpu_id", "gpuId"});
            if (executionGpuId != gpuId && toLower(executionGpuId) != toLower(gpuId)) {
                continue;
            }
            recentExecutions.push_back(execution);
            const int64_t smId = firstInt64Value(execution, {"sm_id", "smId"}, -1);
            if (smId >= 0) {
                auto& agg = smAggs[smId];
                agg.executions++;
                const std::string probeState = toLower(firstStringValue(execution, {"probe_state", "probeState"}));
                if (probeState == "fail") {
                    agg.failCount++;
                } else if (probeState == "error") {
                    agg.errorCount++;
                } else if (probeState == "timeout") {
                    agg.timeoutCount++;
                }
                agg.riskMax = std::max(agg.riskMax, std::max(0.0, firstDoubleValue(execution, {"risk"}, 0.0)));
                agg.confidenceSum += std::max(0.0, firstDoubleValue(execution, {"confidence"}, 0.0));
                agg.confidenceCount++;
                agg.lastTsMs = std::max<int64_t>(agg.lastTsMs, firstInt64Value(execution, {"ts_ms", "tsMs"}, 0));
            }
            const std::string probeType = firstStringValue(execution, {"probe_type", "probeType"});
            if (!probeType.empty()) {
                probeMixCounts[probeType] += 1;
            }
        }
        sortActionsByTime(recentExecutions);
        recentExecutions = limitedJsonArray(recentExecutions, 48);

        int64_t smCount = std::max<int64_t>(0, firstInt64Value(gpuDetail, {"sm_count", "smCount"}, 0));
        if (smCount <= 0 && !smAggs.empty()) {
            smCount = smAggs.rbegin()->first + 1;
        }
        nlohmann::json smHeatmap = nlohmann::json::array();
        for (int64_t smId = 0; smId < smCount; ++smId) {
            const auto it = smAggs.find(smId);
            const SmAgg agg = it != smAggs.end() ? it->second : SmAgg{};
            smHeatmap.push_back({
                    {"sm_id", smId},
                    {"executions", agg.executions},
                    {"fail_count", agg.failCount},
                    {"error_count", agg.errorCount},
                    {"timeout_count", agg.timeoutCount},
                    {"risk_max", agg.riskMax},
                    {"confidence_avg", agg.confidenceCount > 0 ? agg.confidenceSum / static_cast<double>(agg.confidenceCount) : 0.0},
                    {"last_ts_ms", agg.lastTsMs}
            });
        }

        nlohmann::json probeMix = nlohmann::json::array();
        std::vector<std::pair<std::string, int>> probeMixVector(probeMixCounts.begin(), probeMixCounts.end());
        std::sort(probeMixVector.begin(), probeMixVector.end(), [](const auto& left, const auto& right) {
            if (left.second == right.second) {
                return left.first < right.first;
            }
            return left.second > right.second;
        });
        for (const auto& [probeType, count] : probeMixVector) {
            probeMix.push_back({{"probe_type", probeType}, {"count", count}});
        }

        nlohmann::json recentFaults = nlohmann::json::array();
        for (const auto& fault : view.value("recent_faults", nlohmann::json::array())) {
            const std::string faultGpuId = firstStringValue(fault, {"gpu_id", "gpuId"});
            if (faultGpuId == gpuId || toLower(faultGpuId) == toLower(gpuId)) {
                recentFaults.push_back(fault);
            }
        }

        nlohmann::json remoteFaults = nlohmann::json::array();
        for (const auto& fault : view.value("remote_faults", nlohmann::json::array())) {
            const std::string faultGpuId = firstStringValue(fault, {"gpu_id", "gpuId"});
            if (faultGpuId == gpuId || toLower(faultGpuId) == toLower(gpuId)) {
                remoteFaults.push_back(fault);
            }
        }

        nlohmann::json recentActions = nlohmann::json::array();
        for (const auto& action : view.value("recent_actions", nlohmann::json::array())) {
            const std::string actionGpuId = firstStringValue(action, {"gpu_id", "gpuId"});
            if (actionGpuId == gpuId || toLower(actionGpuId) == toLower(gpuId)) {
                recentActions.push_back(action);
            }
        }
        sortActionsByTime(recentActions);

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Tracey GPU telemetry generated successfully.";
        apiResponse.data = {
                {"generated_epoch_ms", nowMs},
                {"agent_id", firstStringValue(view, {"agent_id"})},
                {"gpu_id", firstStringValue(gpuDetail, {"gpu_id", "gpuId"})},
                {"host", firstStringValue(view, {"host"})},
                {"rack", firstStringValue(view, {"rack"})},
                {"zone", firstStringValue(view, {"zone"})},
                {"identity", view.value("identity", nlohmann::json::object())},
                {"server_summary", view.value("summary", nlohmann::json::object())},
                {"summary", gpuDetail},
                {"sm_heatmap", smHeatmap},
                {"probe_mix", probeMix},
                {"recent_executions", recentExecutions},
                {"recent_faults", recentFaults},
                {"remote_faults", remoteFaults},
                {"recent_actions", recentActions}
        };
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleTraceyAgentAnalysis(const httplib::Request& req, httplib::Response& res) {
        const std::string agentId = req.matches.size() > 1 ? trim(req.matches[1].str()) : "";
        if (agentId.empty()) {
            return sendErrorResponse(res, 400, "Missing required parameter: agent_id.");
        }

        const int64_t nowMs = nowEpochMs();
        const int64_t windowSeconds = parseQueryInt64(req, "window_seconds", 3600, 300, 604800);
        const int64_t bucketSeconds = parseQueryInt64(req, "bucket_seconds", 60, 10, 3600);
        const int64_t logLimit = parseQueryInt64(req, "log_limit", 300, 20, 4000);
        const int64_t cutoffMs = nowMs - (windowSeconds * 1000);
        const int64_t bucketMs = bucketSeconds * 1000;
        const int64_t alignedStartMs = (cutoffMs / bucketMs) * bucketMs;
        const size_t bucketCount = static_cast<size_t>(((nowMs - alignedStartMs) / bucketMs) + 1);

        TraceyAgent agent;
        std::vector<TraceyStatusSample> history;
        std::vector<TraceyAgentLogEntry> logs;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            auto agentIt = traceyAgents.find(agentId);
            if (agentIt != traceyAgents.end()) {
                found = true;
                agent = agentIt->second;
            }
            auto historyIt = traceyAgentHistory.find(agentId);
            if (historyIt != traceyAgentHistory.end()) {
                history.reserve(historyIt->second.size());
                for (const auto& sample : historyIt->second) {
                    if (sample.tsMs >= cutoffMs) {
                        history.push_back(sample);
                    }
                }
            }
            auto logsIt = traceyAgentLogs.find(agentId);
            if (logsIt != traceyAgentLogs.end()) {
                logs.reserve(logsIt->second.size());
                for (const auto& entry : logsIt->second) {
                    if (entry.tsMs >= cutoffMs) {
                        logs.push_back(entry);
                    }
                }
            }
        }

        if (!found) {
            return sendErrorResponse(res, 404, "Tracey agent '" + agentId + "' not found.");
        }

        std::sort(history.begin(), history.end(), [](const TraceyStatusSample& a, const TraceyStatusSample& b) {
            return a.tsMs < b.tsMs;
        });
        std::sort(logs.begin(), logs.end(), [](const TraceyAgentLogEntry& a, const TraceyAgentLogEntry& b) {
            return a.tsMs > b.tsMs;
        });
        if (logs.size() > static_cast<size_t>(logLimit)) {
            logs.resize(static_cast<size_t>(logLimit));
        }

        if (history.empty()) {
            TraceyStatusSample fallback;
            fallback.tsMs = nowMs;
            fallback.status = normalizeTraceyStatus(agent.status);
            fallback.stale = agent.stale;
            fallback.statusReachable = agent.statusReachable;
            fallback.queryFailures = std::max(0, agent.queryFailures);
            fallback.coordinator = agent.coordinator;
            fallback.score = agent.score;
            fallback.source = "snapshot";
            history.push_back(fallback);
        }

        struct AgentBucketAgg {
            int64_t bucketStartMs{0};
            int sampleCount{0};
            int healthy{0};
            int degraded{0};
            int offline{0};
            int unknown{0};
            int stale{0};
            int reachable{0};
            int coordinator{0};
            int64_t queryFailureSum{0};
            int64_t scoreSum{0};
            int64_t tracey_guardFailureSum{0};
            int64_t tracey_guardErrorSum{0};
            int64_t tracey_guardQuarantineSum{0};
            int64_t tracey_guardRemoteFaultSum{0};
            int64_t loaderThreatProviderSum{0};
            int64_t loaderThreatArtifactSum{0};
            int64_t loaderThreatBlockedProviderSum{0};
            int64_t loaderThreatBlockedArtifactSum{0};
            int64_t loaderThreatRemoteReporterSum{0};
        };
        std::vector<AgentBucketAgg> buckets(bucketCount);
        for (size_t i = 0; i < bucketCount; ++i) {
            buckets[i].bucketStartMs = alignedStartMs + static_cast<int64_t>(i) * bucketMs;
        }

        auto bucketIndexForTs = [&](int64_t tsMs) -> int64_t {
            if (tsMs < alignedStartMs || tsMs > nowMs) {
                return -1;
            }
            const int64_t offset = tsMs - alignedStartMs;
            const int64_t idx = offset / bucketMs;
            if (idx < 0 || idx >= static_cast<int64_t>(bucketCount)) {
                return -1;
            }
            return idx;
        };

        for (const auto& sample : history) {
            const int64_t idx = bucketIndexForTs(sample.tsMs);
            if (idx < 0) {
                continue;
            }
            auto& bucket = buckets[static_cast<size_t>(idx)];
            bucket.sampleCount++;
            const std::string status = normalizeTraceyStatus(sample.status);
            if (status == "healthy") {
                bucket.healthy++;
            } else if (status == "degraded") {
                bucket.degraded++;
            } else if (status == "offline") {
                bucket.offline++;
            } else {
                bucket.unknown++;
            }
            if (sample.stale) {
                bucket.stale++;
            }
            if (sample.statusReachable) {
                bucket.reachable++;
            }
            if (sample.coordinator) {
                bucket.coordinator++;
            }
            bucket.queryFailureSum += std::max(0, sample.queryFailures);
            bucket.scoreSum += sample.score;
            bucket.tracey_guardFailureSum += std::max(0, sample.tracey_guardProbeFailures);
            bucket.tracey_guardErrorSum += std::max(0, sample.tracey_guardProbeErrors);
            bucket.tracey_guardQuarantineSum += std::max(0, sample.tracey_guardQuarantined);
            bucket.tracey_guardRemoteFaultSum += std::max(0, sample.tracey_guardRemoteFaults);
            bucket.loaderThreatProviderSum += std::max(0, sample.loaderThreatLocalProviders);
            bucket.loaderThreatArtifactSum += std::max(0, sample.loaderThreatLocalArtifacts);
            bucket.loaderThreatBlockedProviderSum += std::max(0, sample.loaderThreatBlockedProviders);
            bucket.loaderThreatBlockedArtifactSum += std::max(0, sample.loaderThreatBlockedArtifacts);
            bucket.loaderThreatRemoteReporterSum += std::max(0, sample.loaderThreatRemoteReporters);
        }

        nlohmann::json transitions = nlohmann::json::array();
        std::string previousStatus;
        for (const auto& sample : history) {
            const std::string status = normalizeTraceyStatus(sample.status);
            if (previousStatus.empty()) {
                previousStatus = status;
                continue;
            }
            if (status != previousStatus) {
                transitions.push_back({
                        {"ts_ms", sample.tsMs},
                        {"from_status", previousStatus},
                        {"to_status", status},
                        {"source", sample.source}
                });
                previousStatus = status;
            }
        }

        nlohmann::json series = nlohmann::json::array();
        for (const auto& bucket : buckets) {
            const double avgQueryFailures = bucket.sampleCount > 0
                                            ? static_cast<double>(bucket.queryFailureSum) / static_cast<double>(bucket.sampleCount)
                                            : 0.0;
            const double avgScore = bucket.sampleCount > 0
                                    ? static_cast<double>(bucket.scoreSum) / static_cast<double>(bucket.sampleCount)
                                    : 0.0;
            const double avgTraceyGuardFailures = bucket.sampleCount > 0
                                               ? static_cast<double>(bucket.tracey_guardFailureSum) / static_cast<double>(bucket.sampleCount)
                                               : 0.0;
            const double avgTraceyGuardErrors = bucket.sampleCount > 0
                                             ? static_cast<double>(bucket.tracey_guardErrorSum) / static_cast<double>(bucket.sampleCount)
                                             : 0.0;
            const double avgTraceyGuardQuarantine = bucket.sampleCount > 0
                                                 ? static_cast<double>(bucket.tracey_guardQuarantineSum) / static_cast<double>(bucket.sampleCount)
                                                 : 0.0;
            const double avgTraceyGuardRemoteFaults = bucket.sampleCount > 0
                                                   ? static_cast<double>(bucket.tracey_guardRemoteFaultSum) / static_cast<double>(bucket.sampleCount)
                                                   : 0.0;
            const double avgLoaderThreatProviders = bucket.sampleCount > 0
                                                   ? static_cast<double>(bucket.loaderThreatProviderSum) / static_cast<double>(bucket.sampleCount)
                                                   : 0.0;
            const double avgLoaderThreatArtifacts = bucket.sampleCount > 0
                                                   ? static_cast<double>(bucket.loaderThreatArtifactSum) / static_cast<double>(bucket.sampleCount)
                                                   : 0.0;
            const double avgLoaderThreatBlockedProviders = bucket.sampleCount > 0
                                                           ? static_cast<double>(bucket.loaderThreatBlockedProviderSum) / static_cast<double>(bucket.sampleCount)
                                                           : 0.0;
            const double avgLoaderThreatBlockedArtifacts = bucket.sampleCount > 0
                                                           ? static_cast<double>(bucket.loaderThreatBlockedArtifactSum) / static_cast<double>(bucket.sampleCount)
                                                           : 0.0;
            const double avgLoaderThreatRemoteReporters = bucket.sampleCount > 0
                                                          ? static_cast<double>(bucket.loaderThreatRemoteReporterSum) / static_cast<double>(bucket.sampleCount)
                                                          : 0.0;
            series.push_back({
                    {"bucket_start_epoch_ms", bucket.bucketStartMs},
                    {"sample_count", bucket.sampleCount},
                    {"healthy", bucket.healthy},
                    {"degraded", bucket.degraded},
                    {"offline", bucket.offline},
                    {"unknown", bucket.unknown},
                    {"stale", bucket.stale},
                    {"reachable", bucket.reachable},
                    {"coordinator", bucket.coordinator},
                    {"avg_query_failures", avgQueryFailures},
                    {"avg_score", avgScore},
                    {"avg_tracey_guard_failures", avgTraceyGuardFailures},
                    {"avg_tracey_guard_errors", avgTraceyGuardErrors},
                    {"avg_tracey_guard_quarantined", avgTraceyGuardQuarantine},
                    {"avg_tracey_guard_remote_faults", avgTraceyGuardRemoteFaults},
                    {"avg_loader_threat_providers", avgLoaderThreatProviders},
                    {"avg_loader_threat_artifacts", avgLoaderThreatArtifacts},
                    {"avg_loader_threat_blocked_providers", avgLoaderThreatBlockedProviders},
                    {"avg_loader_threat_blocked_artifacts", avgLoaderThreatBlockedArtifacts},
                    {"avg_loader_threat_remote_reporters", avgLoaderThreatRemoteReporters}
            });
        }

        nlohmann::json logRows = nlohmann::json::array();
        for (const auto& entry : logs) {
            logRows.push_back({
                    {"ts_ms", entry.tsMs},
                    {"level", normalizeLogLevel(entry.level)},
                    {"category", entry.category},
                    {"message", entry.message},
                    {"context", entry.context.is_object() ? entry.context : nlohmann::json::object()}
            });
        }

        const int64_t lastSignalMs = std::max(agent.lastSeenEpochMs, agent.lastAnnouncementEpochMs);
        int64_t lastSeenSecondsAgo = 0;
        if (lastSignalMs > 0 && nowMs > lastSignalMs) {
            lastSeenSecondsAgo = (nowMs - lastSignalMs) / 1000;
        }
        const bool stale = agent.stale || (lastSignalMs > 0 && nowMs > lastSignalMs &&
                                           (nowMs - lastSignalMs) > (traceyStaleAfterSeconds * 1000));
        const std::string currentStatus = stale ? "offline" : normalizeTraceyStatus(agent.status);
        nlohmann::json tracey_guardSummary = nlohmann::json::object();
        nlohmann::json loaderThreatSummary = nlohmann::json::object();
        if (agent.metrics.is_object()) {
            auto statusIt = agent.metrics.find("status_snapshot");
            if (statusIt != agent.metrics.end() && statusIt->is_object()) {
                auto tracey_guardIt = statusIt->find("tracey_guard");
                if (tracey_guardIt != statusIt->end() && tracey_guardIt->is_object()) {
                    auto summaryIt = tracey_guardIt->find("summary");
                    if (summaryIt != tracey_guardIt->end() && summaryIt->is_object()) {
                        tracey_guardSummary = *summaryIt;
                    }
                }
                loaderThreatSummary = extractTraceyLoaderThreatSummary(*statusIt);
            }
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Tracey agent analysis generated successfully.";
        apiResponse.data = {
                {"generated_epoch_ms", nowMs},
                {"agent_id", agent.agentId},
                {"window_seconds", windowSeconds},
                {"bucket_seconds", bucketSeconds},
                {"current", {
                        {"agent_id", agent.agentId},
                        {"cluster", agent.cluster},
                        {"status", currentStatus},
                        {"stale", stale},
                        {"version", agent.version},
                        {"host", agent.host},
                        {"source", agent.source},
                        {"announce_addr", agent.announceAddr},
                        {"status_addr", agent.statusAddr},
                        {"link_state", agent.linkState},
                        {"link_security", agent.linkSecurity},
                        {"query_failures", agent.queryFailures},
                        {"status_reachable", agent.statusReachable},
                        {"is_coordinator", agent.coordinator},
                        {"coordinator_epoch", agent.coordinatorEpoch},
                        {"score", agent.score},
                        {"last_error", agent.lastError},
                        {"last_seen_epoch_ms", agent.lastSeenEpochMs},
                        {"last_seen_seconds_ago", lastSeenSecondsAgo},
                        {"last_announcement_epoch_ms", agent.lastAnnouncementEpochMs},
                        {"last_query_epoch_ms", agent.lastQueryEpochMs},
                        {"next_query_epoch_ms", agent.nextQueryEpochMs},
                        {"metrics", agent.metrics.is_null() ? nlohmann::json::object() : agent.metrics},
                        {"capabilities", agent.capabilities.is_null() ? nlohmann::json::object() : agent.capabilities},
                        {"tracey_guard", tracey_guardSummary},
                        {"loader_threats", loaderThreatSummary}
                }},
                {"series", series},
                {"status_transitions", transitions},
                {"logs", logRows}
        };
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleTraceyAgentControl(const httplib::Request& req, httplib::Response& res) {
        const std::string agentId = req.matches.size() > 1 ? trim(req.matches[1].str()) : "";
        if (agentId.empty()) {
            return sendErrorResponse(res, 400, "Missing required parameter: agent_id.");
        }

        nlohmann::json requestPayload = nlohmann::json::object();
        if (!req.body.empty()) {
            requestPayload = nlohmann::json::parse(req.body, nullptr, false);
            if (requestPayload.is_discarded() || !requestPayload.is_object()) {
                return sendErrorResponse(res, 400, "Invalid JSON control payload.");
            }
        }

        std::string statusAddr;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = traceyAgents.find(agentId);
            if (it == traceyAgents.end()) {
                return sendErrorResponse(res, 404, "Tracey agent '" + agentId + "' not found.");
            }
            statusAddr = trim(it->second.statusAddr);
        }

        if (statusAddr.empty()) {
            return sendErrorResponse(res, 409, "Tracey agent has no status endpoint to control.");
        }

        TraceyEndpoint endpoint;
        if (!parseTraceyEndpoint(statusAddr, endpoint)) {
            return sendErrorResponse(res, 409, "Tracey status endpoint is invalid.");
        }
        if (!isLocalOrPrivateHost(endpoint.host, traceyAllowPublicAddr)) {
            return sendErrorResponse(res, 403, "Tracey status endpoint is outside allowed network ranges.");
        }

        const std::string controlPath = buildTraceyPath(endpoint, "/control/tracey_guard");
        const int timeoutSec = static_cast<int>(std::max<int64_t>(1, (traceyStatusTimeoutMs + 999) / 1000));
        httplib::Headers headers{
                {"Accept", "application/json"},
                {"Content-Type", "application/json"}
        };
        if (!traceyStatusBearerToken.empty()) {
            headers.emplace("Authorization", "Bearer " + traceyStatusBearerToken);
        }

        httplib::Result result;
        if (endpoint.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient client(endpoint.host, endpoint.port);
            client.enable_server_certificate_verification(traceyTlsVerify);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Post(controlPath.c_str(), headers, requestPayload.dump(), "application/json");
#else
            return sendErrorResponse(res, 503, "HTTPS control unavailable: httplib built without OpenSSL support.");
#endif
        } else {
            httplib::Client client(endpoint.host, endpoint.port);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Post(controlPath.c_str(), headers, requestPayload.dump(), "application/json");
        }

        if (!result) {
            return sendErrorResponse(res, 502, "Control request failed: " + httplib::to_string(result.error()));
        }

        nlohmann::json upstreamPayload = nlohmann::json::parse(result->body, nullptr, false);
        if (upstreamPayload.is_discarded()) {
            upstreamPayload = nlohmann::json::object({
                    {"raw_body", result->body}
            });
        }

        {
            std::lock_guard<std::mutex> lock(dataMutex);
            auto& agent = traceyAgents[agentId];
            if (!agent.metrics.is_object()) {
                agent.metrics = nlohmann::json::object();
            }
            agent.metrics["tracey_guard_control_last"] = {
                    {"requested_at_ms", nowEpochMs()},
                    {"request", requestPayload},
                    {"response", upstreamPayload},
                    {"http_status", result->status}
            };
            appendTraceyAgentLogLocked(
                    agentId,
                    nowEpochMs(),
                    (result->status >= 200 && result->status < 300) ? "info" : "warn",
                    "tracey_guard_control",
                    "TraceyGuard control request completed.",
                    {
                            {"status_addr", endpoint.normalized},
                            {"http_status", result->status},
                            {"control_path", controlPath}
                    }
            );
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = result->status >= 200 && result->status < 300;
        apiResponse.message = apiResponse.success
                              ? "Tracey control applied."
                              : "Tracey control request failed upstream.";
        apiResponse.data = {
                {"agent_id", agentId},
                {"status_addr", endpoint.normalized},
                {"upstream_status", result->status},
                {"request", requestPayload},
                {"upstream", upstreamPayload}
        };
        sendJsonResponse(res, apiResponse);
        if (!apiResponse.success) {
            res.status = 502;
        }
    }

    void APIRoutes::handleTraceyAgentDeepDive(const httplib::Request& req, httplib::Response& res) {
        const std::string agentId = req.matches.size() > 1 ? trim(req.matches[1].str()) : "";
        if (agentId.empty()) {
            return sendErrorResponse(res, 400, "Missing required parameter: agent_id.");
        }

        std::string statusAddr;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = traceyAgents.find(agentId);
            if (it == traceyAgents.end()) {
                return sendErrorResponse(res, 404, "Tracey agent '" + agentId + "' not found.");
            }
            statusAddr = trim(it->second.statusAddr);
        }

        if (statusAddr.empty()) {
            return sendErrorResponse(res, 409, "Tracey agent has no status endpoint for deep-dive.");
        }

        TraceyEndpoint endpoint;
        if (!parseTraceyEndpoint(statusAddr, endpoint)) {
            return sendErrorResponse(res, 409, "Tracey status endpoint is invalid.");
        }
        if (!isLocalOrPrivateHost(endpoint.host, traceyAllowPublicAddr)) {
            return sendErrorResponse(res, 403, "Tracey status endpoint is outside allowed network ranges.");
        }

        const std::string deepDivePath = buildTraceyPath(endpoint, "/tracey_guard/deepdive");
        const int timeoutSec = static_cast<int>(std::max<int64_t>(1, (traceyStatusTimeoutMs + 999) / 1000));
        httplib::Headers headers{{"Accept", "application/json"}};
        if (!traceyStatusBearerToken.empty()) {
            headers.emplace("Authorization", "Bearer " + traceyStatusBearerToken);
        }

        httplib::Result result;
        if (endpoint.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient client(endpoint.host, endpoint.port);
            client.enable_server_certificate_verification(traceyTlsVerify);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Get(deepDivePath.c_str(), headers);
#else
            return sendErrorResponse(res, 503, "HTTPS deep-dive unavailable: httplib built without OpenSSL support.");
#endif
        } else {
            httplib::Client client(endpoint.host, endpoint.port);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Get(deepDivePath.c_str(), headers);
        }

        if (!result) {
            return sendErrorResponse(res, 502, "Deep-dive request failed: " + httplib::to_string(result.error()));
        }
        if (result->status < 200 || result->status >= 300) {
            return sendErrorResponse(res, 502, "Deep-dive endpoint returned HTTP " + std::to_string(result->status) + ".");
        }

        nlohmann::json upstreamPayload = nlohmann::json::parse(result->body, nullptr, false);
        if (upstreamPayload.is_discarded()) {
            return sendErrorResponse(res, 502, "Deep-dive endpoint returned non-JSON payload.");
        }

        {
            std::lock_guard<std::mutex> lock(dataMutex);
            auto& agent = traceyAgents[agentId];
            if (!agent.metrics.is_object()) {
                agent.metrics = nlohmann::json::object();
            }
            agent.metrics["tracey_guard_deepdive_snapshot"] = upstreamPayload;
            appendTraceyAgentLogLocked(
                    agentId,
                    nowEpochMs(),
                    "info",
                    "tracey_guard_deepdive",
                    "Fetched Tracey deep-dive snapshot.",
                    {
                            {"status_addr", endpoint.normalized},
                            {"deep_dive_path", deepDivePath}
                    }
            );
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Tracey deep-dive fetched successfully.";
        apiResponse.data = {
                {"agent_id", agentId},
                {"status_addr", endpoint.normalized},
                {"deepdive", upstreamPayload}
        };
        sendJsonResponse(res, apiResponse);
    }

    APIRoutes::RecruitCapacityAssessment APIRoutes::assessRecruitCapacity(const std::string& host) {
        RecruitCapacityAssessment assessment;
        assessment.details = nlohmann::json::object();

        const LocalHostIdentity localIdentity = collectLocalHostIdentity();
        const HostMatchResult hostMatch = classifyRecruitHost(host, localIdentity);
        assessment.sameHardware = hostMatch.sameHardware;
        assessment.details["host_match"] = hostMatch.details;

        if (!assessment.sameHardware) {
            assessment.source = "remote";
            assessment.reason = "Recruitment targets a different host.";
            return assessment;
        }

        assessment.source = "same_hardware";
        assessment.reason = "Same-hardware recruitment has spare capacity.";
        const int64_t nowMs = nowEpochMs();

        TraceyAgent localAgentSnapshot;
        bool haveLocalAgentSnapshot = false;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = traceyLocalAgentId.empty() ? traceyAgents.end() : traceyAgents.find(traceyLocalAgentId);
            if (it == traceyAgents.end()) {
                it = std::find_if(traceyAgents.begin(), traceyAgents.end(), [](const auto& entry) {
                    const auto& metrics = entry.second.metrics;
                    if (!metrics.is_object()) {
                        return false;
                    }
                    const auto bootstrapIt = metrics.find("bootstrap_managed");
                    return bootstrapIt != metrics.end() && bootstrapIt->is_boolean() &&
                           bootstrapIt->template get<bool>();
                });
            }
            if (it != traceyAgents.end()) {
                localAgentSnapshot = it->second;
                haveLocalAgentSnapshot = true;
            }
        }

        if (haveLocalAgentSnapshot) {
            nlohmann::json traceyDetails = {
                    {"agent_id", localAgentSnapshot.agentId},
                    {"status", normalizeTraceyStatus(localAgentSnapshot.status)},
                    {"stale", localAgentSnapshot.stale},
                    {"status_reachable", localAgentSnapshot.statusReachable},
                    {"last_seen_epoch_ms", localAgentSnapshot.lastSeenEpochMs},
                    {"last_query_epoch_ms", localAgentSnapshot.lastQueryEpochMs}
            };

            if (localAgentSnapshot.metrics.is_object()) {
                const auto statusIt = localAgentSnapshot.metrics.find("status_snapshot");
                if (statusIt != localAgentSnapshot.metrics.end() && statusIt->is_object()) {
                    const int64_t lastSignalMs = std::max(
                            localAgentSnapshot.lastQueryEpochMs,
                            localAgentSnapshot.lastSeenEpochMs
                    );
                    const int64_t freshnessWindowMs = std::max<int64_t>(
                            traceyStatusPollMs * 3,
                            SAME_HARDWARE_TRACEY_FRESHNESS_FLOOR_MS
                    );
                    const bool snapshotFresh = !localAgentSnapshot.stale &&
                                               lastSignalMs > 0 &&
                                               nowMs >= lastSignalMs &&
                                               (nowMs - lastSignalMs) <= freshnessWindowMs;
                    traceyDetails["status_snapshot_fresh"] = snapshotFresh;
                    traceyDetails["status_snapshot_age_ms"] = (lastSignalMs > 0 && nowMs >= lastSignalMs)
                                                              ? (nowMs - lastSignalMs)
                                                              : -1;

                    if (snapshotFresh) {
                        const auto& statusSnapshot = *statusIt;
                        const auto autoscalerIt = statusSnapshot.find("continuum_autoscaler");
                        if (autoscalerIt != statusSnapshot.end() && autoscalerIt->is_object()) {
                            const auto& autoscaler = *autoscalerIt;
                            traceyDetails["continuum_autoscaler"] = autoscaler;

                            std::vector<std::string> pressureSignals;
                            const auto pressureIt = autoscaler.find("pressure_signals");
                            if (pressureIt != autoscaler.end() && pressureIt->is_array()) {
                                for (const auto& item : *pressureIt) {
                                    if (item.is_string()) {
                                        const std::string signal = trim(item.get<std::string>());
                                        if (!signal.empty()) {
                                            pressureSignals.push_back(signal);
                                        }
                                    }
                                }
                            }

                            if (!pressureSignals.empty()) {
                                assessment.allowed = false;
                                assessment.source = "tracey.continuum_autoscaler";
                                std::ostringstream reason;
                                reason << "Local Tracey autoscaler reports pressure on this hardware";
                                reason << " (" << pressureSignals.front();
                                for (size_t i = 1; i < pressureSignals.size(); ++i) {
                                    reason << ", " << pressureSignals[i];
                                }
                                reason << ").";
                                assessment.reason = reason.str();
                                assessment.details["tracey"] = traceyDetails;
                                return assessment;
                            }
                        }

                        const auto slurmIt = statusSnapshot.find("slurm");
                        if (slurmIt != statusSnapshot.end() && slurmIt->is_object()) {
                            const auto& slurm = *slurmIt;
                            traceyDetails["slurm"] = slurm;
                            const int nodesTotal = std::max(0, slurm.value("nodes_total", 0));
                            const int nodesIdle = std::max(0, slurm.value("nodes_idle", 0));
                            const int nodesAllocated = std::max(0, slurm.value("nodes_allocated", 0));
                            const int jobsPending = std::max(0, slurm.value("jobs_pending", 0));
                            if (nodesTotal > 0 && nodesIdle == 0 &&
                                (jobsPending > 0 || nodesAllocated >= nodesTotal)) {
                                assessment.allowed = false;
                                assessment.source = "tracey.slurm";
                                assessment.reason =
                                        "Local Slurm snapshot reports no idle nodes for same-hardware scale-out.";
                                assessment.details["tracey"] = traceyDetails;
                                return assessment;
                            }
                        }
                    } else {
                        traceyDetails["note"] = "Local Tracey status snapshot is stale; using host metrics fallback.";
                    }
                } else {
                    traceyDetails["note"] = "Local Tracey agent has no cached status snapshot; using host metrics fallback.";
                }
            } else {
                traceyDetails["note"] = "Local Tracey agent has no metrics object; using host metrics fallback.";
            }

            assessment.details["tracey"] = traceyDetails;
        }

        const SystemCapacitySnapshot system = collectSystemCapacitySnapshot();
        nlohmann::json systemDetails = {
                {"cpu_cores", system.cpuCores},
                {"load_available", system.loadAvailable},
                {"memory_available", system.memoryAvailable}
        };
        if (system.loadAvailable) {
            systemDetails["load1"] = system.load1;
            systemDetails["load_per_cpu"] = system.loadPerCpu;
        }
        if (system.memoryAvailable) {
            systemDetails["total_memory_bytes"] = system.totalMemoryBytes;
            systemDetails["available_memory_bytes"] = system.availableMemoryBytes;
            systemDetails["memory_used_pct"] = system.memoryUsedRatio * 100.0;
        }
        assessment.details["system"] = systemDetails;

        if (!system.loadAvailable && !system.memoryAvailable) {
            assessment.allowed = false;
            assessment.source = "system";
            assessment.reason =
                    "Continuum could not determine local capacity for same-hardware recruitment.";
            return assessment;
        }

        std::vector<std::string> hostPressureReasons;
        if (system.loadAvailable && system.loadPerCpu >= SAME_HARDWARE_MAX_LOAD_PER_CPU) {
            std::ostringstream reason;
            reason << "load per CPU " << system.loadPerCpu
                   << " exceeds " << SAME_HARDWARE_MAX_LOAD_PER_CPU;
            hostPressureReasons.push_back(reason.str());
        }
        if (system.memoryAvailable && system.memoryUsedRatio >= SAME_HARDWARE_MAX_MEMORY_USED_RATIO) {
            std::ostringstream reason;
            reason << "memory used " << (system.memoryUsedRatio * 100.0)
                   << "% exceeds " << (SAME_HARDWARE_MAX_MEMORY_USED_RATIO * 100.0) << "%";
            hostPressureReasons.push_back(reason.str());
        }
        if (!hostPressureReasons.empty()) {
            assessment.allowed = false;
            assessment.source = "system";
            assessment.details["system"]["pressure_reasons"] = hostPressureReasons;
            std::ostringstream summary;
            summary << "Local host metrics indicate insufficient spare capacity";
            summary << " (" << hostPressureReasons.front();
            for (size_t i = 1; i < hostPressureReasons.size(); ++i) {
                summary << ", " << hostPressureReasons[i];
            }
            summary << ").";
            assessment.reason = summary.str();
            return assessment;
        }

        return assessment;
    }

    void APIRoutes::handleRecruitNode(const httplib::Request& req, httplib::Response& res) {
        (void)req;
        try {
            const auto payload = nlohmann::json::parse(req.body);
            if (!payload.is_object()) {
                return sendErrorResponse(res, 400, "Invalid JSON body. Expected an object.");
            }

            const std::string host = trim(payload.value("host", ""));
            const std::string user = trim(payload.value("user", "ubuntu"));
            const int port = parsePortOrDefault(payload, 22);
            const bool dryRun = payload.value("dry_run", payload.value("dryRun", false));
            const bool useSudo = payload.value("sudo", true);
            const std::string nodeType = normalizeNodeType(payload.value("node_type", payload.value("nodeType", "bare-metal")));
            const std::string nodeName = trim(payload.value("name", host));
            const std::string region = trim(payload.value("region", ""));
            const std::string sshKeyPath = trim(payload.value("ssh_key_path", payload.value("sshKeyPath", "")));
            const std::string scriptPath = trim(payload.value("script_path", payload.value("scriptPath", "")));
            const std::string binaryPath = trim(payload.value("binary_path", payload.value("binaryPath", "")));
            std::string scriptContent = payload.value("script", "");
            const std::string rawRemotePath = trim(payload.value("remote_path", payload.value("remotePath", "")));
            const std::string continuumUrl = trim(payload.value("continuum_url", payload.value("continuumUrl", "")));
            std::string continuumAuthToken = trim(payload.value(
                    "continuum_auth_token",
                    payload.value("continuumAuthToken", "")
            ));
            const bool propagateServerAuthToken = payload.value(
                    "propagate_server_auth_token",
                    payload.value("propagateServerAuthToken", true)
            );
            const std::string recruitToken = trim(payload.value("recruit_token", payload.value("recruitToken", "")));
            const bool autoConfigure = payload.value("auto_configure", payload.value("autoConfigure", false));
            const std::string sudoPassword = trimLineEnd(trim(payload.value(
                    "sudo_password",
                    payload.value(
                            "sudoPassword",
                            payload.value("become_password", payload.value("becomePassword", ""))
                    )
            )));
            const bool ansibleBecome = payload.value(
                    "ansible_become",
                    payload.value("ansibleBecome", payload.value("become", true))
            );
            const std::string tenantId = trim(payload.value("tenant_id", payload.value("tenantId", "")));
            const std::string tenantName = trim(payload.value("tenant_name", payload.value("tenantName", "")));
            const std::string tenantEnvironment = trim(payload.value(
                    "tenant_environment",
                    payload.value("tenantEnvironment", "")
            ));
            const std::string ansiblePlaybookRequested = trim(payload.value(
                    "ansible_playbook",
                    payload.value("ansiblePlaybook", "")
            ));

            nlohmann::json ansibleExtraVars = nlohmann::json::object();
            if (payload.contains("ansible_extra_vars")) {
                ansibleExtraVars = payload["ansible_extra_vars"];
            } else if (payload.contains("ansibleExtraVars")) {
                ansibleExtraVars = payload["ansibleExtraVars"];
            }

            const char* recruitTokenEnv = std::getenv("NMC_RECRUIT_TOKEN");
            if (recruitTokenEnv && *recruitTokenEnv) {
                if (recruitToken != std::string(recruitTokenEnv)) {
                    return sendErrorResponse(res, 403, "Recruit token is required and does not match.");
                }
            }

            if (host.empty() || !isSafeHost(host)) {
                return sendErrorResponse(res, 400, "Invalid host. Use a hostname or IPv4 address.");
            }
            if (!isSafeUser(user)) {
                return sendErrorResponse(res, 400, "Invalid SSH user.");
            }
            if (port <= 0 || port > 65535) {
                return sendErrorResponse(res, 400, "Invalid SSH port. Expected 1-65535.");
            }
            if (!isKnownNodeType(nodeType)) {
                return sendErrorResponse(res, 400, "Invalid node_type. Supported: bare-metal, app-install, vm, podman, kubernetes, openstack.");
            }
            if (hasControlChars(nodeName) || hasControlChars(region) || hasControlChars(continuumUrl)) {
                return sendErrorResponse(res, 400, "Invalid control characters in request values.");
            }
            if (!isSafeTenantId(tenantId) || hasControlChars(tenantName) || hasControlChars(tenantEnvironment) ||
                tenantName.size() > 128 || tenantEnvironment.size() > 64) {
                return sendErrorResponse(res, 400, "Invalid tenant metadata.");
            }
            if (hasControlChars(ansiblePlaybookRequested)) {
                return sendErrorResponse(res, 400, "ansible_playbook contains invalid control characters.");
            }
            if (sudoPassword.size() > 1024 || hasControlChars(sudoPassword)) {
                return sendErrorResponse(res, 400, "Invalid sudo/become password value.");
            }
            if (!ansibleExtraVars.is_object()) {
                return sendErrorResponse(res, 400, "ansible_extra_vars must be a JSON object.");
            }
            for (const auto& item : ansibleExtraVars.items()) {
                if (!isSafeAnsibleVarKey(item.key())) {
                    return sendErrorResponse(res, 400, "Invalid ansible_extra_vars key: " + item.key());
                }
                if (item.value().is_string() && hasControlChars(item.value().get<std::string>())) {
                    return sendErrorResponse(res, 400, "ansible_extra_vars values cannot contain control characters.");
                }
            }

            std::vector<std::string> capabilities;
            std::string capabilitiesError;
            if (!parseCapabilitiesField(payload, "capabilities", capabilities, capabilitiesError) ||
                !parseCapabilitiesField(payload, "workloads", capabilities, capabilitiesError)) {
                return sendErrorResponse(res, 400, capabilitiesError);
            }
            if (capabilities.empty()) {
                capabilities = defaultCapabilitiesForNodeType(nodeType);
            }

            const RecruitCapacityAssessment capacityAssessment = assessRecruitCapacity(host);
            const nlohmann::json capacityCheck = {
                    {"same_hardware", capacityAssessment.sameHardware},
                    {"allowed", capacityAssessment.allowed},
                    {"source", capacityAssessment.source},
                    {"reason", capacityAssessment.reason},
                    {"details", capacityAssessment.details.is_null() ? nlohmann::json::object() : capacityAssessment.details}
            };
            if (capacityAssessment.sameHardware && !capacityAssessment.allowed && !dryRun) {
                Models::CloudResponse apiResponse;
                apiResponse.success = false;
                apiResponse.message = "Insufficient local capacity for same-hardware node recruitment.";
                apiResponse.data = {
                        {"target", {{"host", host}, {"port", port}, {"user", user}}},
                        {"node", {{"name", nodeName}, {"type", nodeType}, {"region", region}}},
                        {"capacity_check", capacityCheck}
                };
                sendJsonResponse(res, apiResponse);
                res.status = 409;
                return;
            }

            if (continuumAuthToken.empty() && propagateServerAuthToken && authMode == "token" && !authToken.empty()) {
                continuumAuthToken = authToken;
            }

            std::string gailBaseUrl;
            std::string gailApiToken;
            std::string gailError;
            if (!resolveOptionalRecruitGailEnvironment(gailBaseUrl, gailApiToken, gailError)) {
                return sendErrorResponse(res, 500, "Invalid Gail environment configuration: " + gailError);
            }

            std::string traceyAgentId;
            std::string traceyStatusAddr;
            std::string traceyReason;
            if (!extractTraceyProvisioningRequest(payload, traceyAgentId, traceyStatusAddr, traceyReason)) {
                return sendErrorResponse(res, 400, traceyReason);
            }

            std::string binaryArgsError;
            std::vector<std::string> binaryArgs = parseBinaryArgs(payload, binaryArgsError);
            if (!binaryArgsError.empty()) {
                return sendErrorResponse(res, 400, binaryArgsError);
            }
            for (const auto& arg : binaryArgs) {
                if (hasControlChars(arg)) {
                    return sendErrorResponse(res, 400, "Binary arguments cannot contain control characters.");
                }
            }

            const bool scriptBodyProvided = !scriptContent.empty();
            const bool scriptPathProvided = !scriptPath.empty();
            const bool binaryPathProvided = !binaryPath.empty();

            if (scriptBodyProvided && scriptPathProvided) {
                return sendErrorResponse(res, 400, "Provide either script or script_path, not both.");
            }
            if ((scriptBodyProvided || scriptPathProvided) && binaryPathProvided) {
                return sendErrorResponse(res, 400, "Provide either script input or binary_path, not both.");
            }

            if (scriptContent.find('\0') != std::string::npos) {
                return sendErrorResponse(res, 400, "Script content contains invalid null bytes.");
            }

            std::string scriptFileReadError;
            if (scriptPathProvided) {
                if (!isSafeLocalPath(scriptPath)) {
                    return sendErrorResponse(res, 400, "Invalid script_path. Must be an absolute filesystem path.");
                }
                if (!readTextFile(scriptPath, 1024 * 1024, scriptContent, scriptFileReadError)) {
                    return sendErrorResponse(res, 400, scriptFileReadError);
                }
            }

            if (!binaryPath.empty()) {
                if (!isSafeLocalPath(binaryPath)) {
                    return sendErrorResponse(res, 400, "Invalid binary_path. Must be an absolute filesystem path.");
                }
                try {
                    const std::filesystem::path fsPath(binaryPath);
                    if (!std::filesystem::exists(fsPath) || !std::filesystem::is_regular_file(fsPath)) {
                        return sendErrorResponse(res, 400, "binary_path does not exist or is not a file.");
                    }
                    if (std::filesystem::file_size(fsPath) > (100ULL * 1024ULL * 1024ULL)) {
                        return sendErrorResponse(res, 400, "binary_path exceeds 100 MiB size limit.");
                    }
                } catch (const std::exception& e) {
                    return sendErrorResponse(res, 400, "Unable to inspect binary_path: " + std::string(e.what()));
                }
            }

            if (!sshKeyPath.empty()) {
                if (!isSafeLocalPath(sshKeyPath)) {
                    return sendErrorResponse(res, 400, "Invalid ssh_key_path. Must be an absolute filesystem path.");
                }
                try {
                    const std::filesystem::path fsPath(sshKeyPath);
                    if (!std::filesystem::exists(fsPath) || !std::filesystem::is_regular_file(fsPath)) {
                        return sendErrorResponse(res, 400, "ssh_key_path does not exist or is not a file.");
                    }
                } catch (const std::exception& e) {
                    return sendErrorResponse(res, 400, "Unable to inspect ssh_key_path: " + std::string(e.what()));
                }
            }

            const bool useBinaryArtifact = binaryPathProvided;
            if (!useBinaryArtifact && scriptContent.empty()) {
                scriptContent = buildDefaultRecruitScript();
            }

            struct CleanupGuard {
                std::vector<std::string> paths;
                ~CleanupGuard() {
                    for (const auto& path : paths) {
                        if (!path.empty()) {
                            ::unlink(path.c_str());
                        }
                    }
                }
            } cleanupGuard;

            std::string localArtifactPath = binaryPath;
            if (!useBinaryArtifact) {
                std::string tempPath;
                std::string tempError;
                if (!writeTempFile(scriptContent, tempPath, tempError)) {
                    return sendErrorResponse(res, 500, tempError);
                }
                localArtifactPath = tempPath;
                cleanupGuard.paths.push_back(tempPath);
            }

            const std::string remotePath = rawRemotePath.empty()
                                           ? (useBinaryArtifact ? "/tmp/nmc-recruit.bin" : "/tmp/nmc-recruit.sh")
                                           : rawRemotePath;
            if (!isSafeRemotePath(remotePath)) {
                return sendErrorResponse(res, 400, "Invalid remote_path. Must be absolute and use only safe characters.");
            }

            const std::filesystem::path remoteFsPath(remotePath);
            const std::string remoteDir = remoteFsPath.parent_path().empty()
                                          ? std::string("/tmp")
                                          : remoteFsPath.parent_path().string();

            const std::string sshTarget = user + "@" + host;
            auto appendSshCommonArgs = [&](std::vector<std::string>& args, bool forScp) {
                args.push_back(forScp ? "-P" : "-p");
                args.push_back(std::to_string(port));
                args.push_back("-o");
                args.push_back("BatchMode=yes");
                args.push_back("-o");
                args.push_back("StrictHostKeyChecking=accept-new");
                args.push_back("-o");
                args.push_back("ConnectTimeout=15");
                args.push_back("-o");
                args.push_back("ServerAliveInterval=15");
                args.push_back("-o");
                args.push_back("ServerAliveCountMax=2");
                args.push_back("-o");
                args.push_back("LogLevel=ERROR");
                if (!sshKeyPath.empty()) {
                    args.push_back("-i");
                    args.push_back(sshKeyPath);
                }
            };

            std::vector<std::string> mkdirArgs{"ssh"};
            appendSshCommonArgs(mkdirArgs, false);
            mkdirArgs.push_back(sshTarget);
            mkdirArgs.push_back("mkdir -p " + shellQuote(remoteDir));

            std::vector<std::string> scpArgs{"scp"};
            appendSshCommonArgs(scpArgs, true);
            scpArgs.push_back(localArtifactPath);
            scpArgs.push_back(sshTarget + ":" + remotePath);

            std::ostringstream remoteExec;
            auto appendEnv = [&](const std::string& key, const std::string& value) {
                if (!value.empty()) {
                    remoteExec << key << "=" << shellQuote(value) << " ";
                }
            };
            appendEnv("NMC_NODE_TYPE", nodeType);
            appendEnv("NMC_NODE_REGION", region);
            appendEnv("NMC_NODE_NAME", nodeName);
            appendEnv("NMC_CONTINUUM_URL", continuumUrl);
            appendEnv("NMC_CONTINUUM_AUTH_TOKEN", continuumAuthToken);
            appendEnv("NMC_GAIL_BASE_URL", gailBaseUrl);
            appendEnv("NMC_GAIL_API_TOKEN", gailApiToken);
            appendEnv("TRACEY_AGENT_ID", traceyAgentId);
            appendEnv("TRACEY_STATUS_ADDR", traceyStatusAddr);

            if (useBinaryArtifact) {
                if (useSudo) {
                    if (!sudoPassword.empty()) {
                        remoteExec << "printf %s\\\\n " << shellQuote(sudoPassword)
                                   << " | sudo -S -p '' -E " << shellQuote(remotePath);
                    } else {
                        remoteExec << "sudo -E " << shellQuote(remotePath);
                    }
                } else {
                    remoteExec << shellQuote(remotePath);
                }
                for (const auto& arg : binaryArgs) {
                    remoteExec << " " << shellQuote(arg);
                }
            } else {
                if (useSudo) {
                    if (!sudoPassword.empty()) {
                        remoteExec << "printf %s\\\\n " << shellQuote(sudoPassword)
                                   << " | sudo -S -p '' -E bash " << shellQuote(remotePath);
                    } else {
                        remoteExec << "sudo -E bash " << shellQuote(remotePath);
                    }
                } else {
                    remoteExec << "bash " << shellQuote(remotePath);
                }
            }

            std::vector<std::string> execArgs{"ssh"};
            appendSshCommonArgs(execArgs, false);
            execArgs.push_back(sshTarget);
            execArgs.push_back("chmod +x " + shellQuote(remotePath) + " && " + remoteExec.str());

            const bool enableApps = std::find(capabilities.begin(), capabilities.end(), "apps") != capabilities.end();
            const bool enableVm = std::find(capabilities.begin(), capabilities.end(), "vm") != capabilities.end();
            const bool enablePodman = std::find(capabilities.begin(), capabilities.end(), "podman") != capabilities.end();
            const bool enableKubernetes = std::find(capabilities.begin(), capabilities.end(), "kubernetes") != capabilities.end();
            const bool enableOpenStack = std::find(capabilities.begin(), capabilities.end(), "openstack") != capabilities.end();

            std::vector<std::string> configureArgs;
            std::string ansiblePlaybookPath;
            if (autoConfigure) {
                ansiblePlaybookPath = resolveRecruitPlaybookPath(ansiblePlaybookRequested);
                if (ansiblePlaybookPath.empty()) {
                    return sendErrorResponse(res, 400, "Unable to resolve ansible playbook. Provide ansible_playbook or set NMC_RECRUIT_ANSIBLE_PLAYBOOK.");
                }

                nlohmann::json mergedAnsibleVars = ansibleExtraVars;
                mergedAnsibleVars["ansible_user"] = user;
                mergedAnsibleVars["ansible_port"] = port;
                mergedAnsibleVars["ansible_python_interpreter"] = "/usr/bin/python3";
                mergedAnsibleVars["ansible_ssh_common_args"] = "-o StrictHostKeyChecking=accept-new -o ConnectTimeout=15";
                if (!sshKeyPath.empty()) {
                    mergedAnsibleVars["ansible_ssh_private_key_file"] = sshKeyPath;
                }
                mergedAnsibleVars["ansible_become"] = ansibleBecome;
                if (ansibleBecome && !sudoPassword.empty()) {
                    mergedAnsibleVars["ansible_become_password"] = sudoPassword;
                    mergedAnsibleVars["ansible_become_pass"] = sudoPassword;
                }
                mergedAnsibleVars["nmc_tenant_id"] = tenantId;
                mergedAnsibleVars["nmc_tenant_name"] = tenantName;
                mergedAnsibleVars["nmc_tenant_environment"] = tenantEnvironment;
                mergedAnsibleVars["nmc_requested_capabilities"] = capabilities;
                mergedAnsibleVars["nmc_enable_apps"] = enableApps;
                mergedAnsibleVars["nmc_enable_vm"] = enableVm;
                mergedAnsibleVars["nmc_enable_podman"] = enablePodman;
                mergedAnsibleVars["nmc_enable_kubernetes"] = enableKubernetes;
                mergedAnsibleVars["nmc_enable_openstack"] = enableOpenStack;
                mergedAnsibleVars["nmc_node_host"] = host;
                mergedAnsibleVars["nmc_node_name"] = nodeName;
                mergedAnsibleVars["nmc_node_region"] = region;
                mergedAnsibleVars["nmc_node_type"] = nodeType;
                mergedAnsibleVars["nmc_continuum_url"] = continuumUrl;
                if (!continuumAuthToken.empty()) {
                    mergedAnsibleVars["nmc_continuum_auth_token"] = continuumAuthToken;
                }
                if (!gailBaseUrl.empty()) {
                    mergedAnsibleVars["nmc_gail_base_url"] = gailBaseUrl;
                }
                if (!gailApiToken.empty()) {
                    mergedAnsibleVars["nmc_gail_api_token"] = gailApiToken;
                }
                if (!traceyAgentId.empty()) {
                    mergedAnsibleVars["nmc_tracey_agent_id"] = traceyAgentId;
                }
                if (!traceyStatusAddr.empty()) {
                    mergedAnsibleVars["nmc_tracey_status_addr"] = traceyStatusAddr;
                }

                configureArgs = {
                        "env",
                        "ANSIBLE_HOST_KEY_CHECKING=False",
                        "ANSIBLE_TIMEOUT=45",
                        "ansible-playbook",
                        "-i",
                        host + ",",
                        ansiblePlaybookPath,
                        "--limit",
                        host,
                        "-u",
                        user,
                        "-e",
                        mergedAnsibleVars.dump()
                };
                if (ansibleBecome) {
                    configureArgs.push_back("--become");
                }
                if (!sshKeyPath.empty()) {
                    configureArgs.push_back("--private-key");
                    configureArgs.push_back(sshKeyPath);
                }

                if (!dryRun) {
                    const ExecResult ansibleCheck = runShellCommand({"which", "ansible-playbook"});
                    if (ansibleCheck.exitCode != 0) {
                        return sendErrorResponse(res, 500, "ansible-playbook is required for auto_configure but is not available on the server host.");
                    }
                }
            }

            auto redactSensitive = [&](std::string value) {
                const std::vector<std::string> sensitiveValues = {
                        continuumAuthToken,
                        gailApiToken,
                        sudoPassword
                };
                for (const auto& sensitive : sensitiveValues) {
                    if (sensitive.empty()) {
                        continue;
                    }
                    size_t offset = 0;
                    while (true) {
                        const auto found = value.find(sensitive, offset);
                        if (found == std::string::npos) {
                            break;
                        }
                        value.replace(found, sensitive.size(), "[redacted]");
                        offset = found + 10;
                    }
                }
                return value;
            };

            nlohmann::json diagnostics = {
                    {"target", {{"host", host}, {"port", port}, {"user", user}}},
                    {"node", {{"name", nodeName}, {"type", nodeType}, {"region", region}}},
                    {"mode", useBinaryArtifact ? "binary" : "script"},
                    {"auto_configure", {
                            {"enabled", autoConfigure},
                            {"become", ansibleBecome},
                            {"tenant", {
                                    {"id", tenantId},
                                    {"name", tenantName},
                                    {"environment", tenantEnvironment}
                            }},
                            {"capabilities", capabilities},
                            {"playbook", ansiblePlaybookPath}
                    }},
                    {"artifact", {
                            {"local_path", useBinaryArtifact ? localArtifactPath : "<generated-script>"},
                            {"remote_path", remotePath}
                    }},
                    {"commands", {
                            {"prepare", commandToShellString(mkdirArgs)},
                            {"transfer", commandToShellString(scpArgs)},
                            {"execute", redactSensitive(commandToShellString(execArgs))}
                    }}
            };
            if (capacityAssessment.sameHardware) {
                diagnostics["capacity_check"] = capacityCheck;
            }
            if (autoConfigure) {
                diagnostics["commands"]["configure"] = redactSensitive(commandToShellString(configureArgs));
            }

            if (dryRun) {
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = capacityAssessment.sameHardware && !capacityAssessment.allowed
                                      ? "Node recruitment dry-run generated; current local capacity would reject same-hardware execution."
                                      : "Node recruitment dry-run generated successfully.";
                apiResponse.data = diagnostics;
                sendJsonResponse(res, apiResponse);
                return;
            }

            auto sendStepFailure = [&](const std::string& stepName,
                                       const ExecResult& result,
                                       int statusCode,
                                       const std::string& message) {
                nlohmann::json failure = diagnostics;
                failure["failed_step"] = stepName;
                failure["step_exit_code"] = result.exitCode;
                failure["step_output"] = redactSensitive(result.output);

                Models::CloudResponse apiResponse;
                apiResponse.success = false;
                apiResponse.message = message;
                apiResponse.data = failure;
                sendJsonResponse(res, apiResponse);
                res.status = statusCode;
            };

            const ExecResult prepareResult = runShellCommand(mkdirArgs);
            diagnostics["prepare_output"] = prepareResult.output;
            diagnostics["prepare_exit_code"] = prepareResult.exitCode;
            if (prepareResult.exitCode != 0) {
                sendStepFailure("prepare", prepareResult, 502, "Failed to create remote directory for recruitment artifact.");
                return;
            }

            const ExecResult transferResult = runShellCommand(scpArgs);
            diagnostics["transfer_output"] = transferResult.output;
            diagnostics["transfer_exit_code"] = transferResult.exitCode;
            if (transferResult.exitCode != 0) {
                sendStepFailure("transfer", transferResult, 502, "Failed to transfer recruitment artifact to remote node.");
                return;
            }

            const ExecResult execResult = runShellCommand(execArgs);
            diagnostics["execute_output"] = redactSensitive(execResult.output);
            diagnostics["execute_exit_code"] = execResult.exitCode;
            if (execResult.exitCode != 0) {
                sendStepFailure("execute", execResult, 502, "Recruitment artifact executed with errors on remote node.");
                return;
            }

            if (autoConfigure) {
                const ExecResult configureResult = runShellCommand(configureArgs);
                diagnostics["configure_output"] = redactSensitive(configureResult.output);
                diagnostics["configure_exit_code"] = configureResult.exitCode;
                if (configureResult.exitCode != 0) {
                    sendStepFailure("configure", configureResult, 502, "Node recruited, but Ansible auto-configuration failed.");
                    return;
                }
            }

            if (!traceyAgentId.empty()) {
                std::lock_guard<std::mutex> lock(dataMutex);
                upsertTraceyRequirementLocked("node", nodeName, region, traceyAgentId, traceyStatusAddr, nowEpochMs());
                diagnostics["tracey"] = {
                        {"required", true},
                        {"agent_id", traceyAgentId},
                        {"status_addr", traceyStatusAddr},
                        {"compliance_state", "pending"}
                };
            } else {
                diagnostics["tracey"] = {{"required", traceyEnforceManagedResources}};
            }

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = autoConfigure
                                  ? "Node recruitment and auto-configuration completed successfully."
                                  : "Node recruitment completed successfully.";
            apiResponse.data = diagnostics;
            sendJsonResponse(res, apiResponse);

        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const nlohmann::json::type_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON field type: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

} // namespace NMC::Server
