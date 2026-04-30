// APIRoutes_TraceyAdaptiveAssessment.cpp
// Tracey adaptive placement assembly and compromise-assessment handler workflows.

#include "APIRoutes.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>

namespace NMC::Server {

    namespace {
#include "APIRoutes_ExtractedCommon.inl"

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

} // namespace NMC::Server
