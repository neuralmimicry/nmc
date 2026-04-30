// APIRoutes_TraceyRuntime.cpp
// Tracey runtime/discovery helpers, state snapshots, and per-agent control surfaces.

#include "APIRoutes.h"
#include "K8sHandlers.h"
#include "VersionCheck.h"
#include "Utils.h"

#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <ifaddrs.h>
#include <iomanip>
#include <iostream>
#include <map>
#include <netdb.h>
#include <poll.h>
#include <set>
#include <sstream>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <vector>

namespace NMC::Server {

    namespace {
#include "APIRoutes_InternalHelpers.inl"
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
        scheduleTraceyStateSnapshotLocked(nowMs);
    }

    void APIRoutes::removeTraceyRequirementLocked(const std::string& resourceType, const std::string& resourceName) {
        const std::string normalized = toLower(trim(resourceName));
        if (normalized.empty()) {
            return;
        }

        bool removed = false;

        const std::string directKey = resourceType + ":" + normalized;
        auto direct = traceyRequirements.find(directKey);
        if (direct != traceyRequirements.end()) {
            traceyRequirements.erase(direct);
            removed = true;
        } else {
            for (auto it = traceyRequirements.begin(); it != traceyRequirements.end();) {
                if (it->second.resourceType == resourceType &&
                    toLower(trim(it->second.resourceName)) == normalized) {
                    it = traceyRequirements.erase(it);
                    removed = true;
                } else {
                    ++it;
                }
            }
        }
        if (removed) {
            scheduleTraceyStateSnapshotLocked(nowEpochMs());
        }
    }


    nlohmann::json APIRoutes::traceyAgentToJson(const TraceyAgent& agent) const {
        return {
                {"agent_id", agent.agentId},
                {"cluster", agent.cluster},
                {"status", agent.status},
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
                {"last_announcement_epoch_ms", agent.lastAnnouncementEpochMs},
                {"last_query_epoch_ms", agent.lastQueryEpochMs},
                {"next_query_epoch_ms", agent.nextQueryEpochMs},
                {"query_failures", agent.queryFailures},
                {"stale", agent.stale},
                {"status_reachable", agent.statusReachable},
                {"signature_present", agent.signaturePresent},
                {"coordinator", agent.coordinator},
                {"coordinator_epoch", agent.coordinatorEpoch},
                {"score", agent.score}
        };
    }

    nlohmann::json APIRoutes::traceyRequirementToJson(const TraceyRequirement& requirement) const {
        return {
                {"key", requirement.key},
                {"resource_type", requirement.resourceType},
                {"resource_name", requirement.resourceName},
                {"region", requirement.region},
                {"expected_agent_id", requirement.expectedAgentId},
                {"expected_status_addr", requirement.expectedStatusAddr},
                {"created_epoch_ms", requirement.createdEpochMs}
        };
    }

    nlohmann::json APIRoutes::traceyStatusSampleToJson(const TraceyStatusSample& sample) const {
        return {
                {"ts_ms", sample.tsMs},
                {"status", sample.status},
                {"stale", sample.stale},
                {"status_reachable", sample.statusReachable},
                {"query_failures", sample.queryFailures},
                {"coordinator", sample.coordinator},
                {"score", sample.score},
                {"probe_watch_total_observations", sample.probeWatchTotalObservations},
                {"probe_watch_total_alerts", sample.probeWatchTotalAlerts},
                {"probe_watch_distinct_sources", sample.probeWatchDistinctSources},
                {"probe_watch_recent_alerts", sample.probeWatchRecentAlerts},
                {"probe_watch_high_alerts", sample.probeWatchHighAlerts},
                {"probe_watch_medium_alerts", sample.probeWatchMediumAlerts},
                {"probe_watch_unauthorized_control_alerts", sample.probeWatchUnauthorizedControlAlerts},
                {"probe_watch_cooperative_alerts", sample.probeWatchCooperativeAlerts},
                {"probe_watch_alerted_surfaces", sample.probeWatchAlertedSurfaces},
                {"tracey_guard_probe_failures", sample.tracey_guardProbeFailures},
                {"tracey_guard_probe_errors", sample.tracey_guardProbeErrors},
                {"tracey_guard_quarantined", sample.tracey_guardQuarantined},
                {"tracey_guard_remote_faults", sample.tracey_guardRemoteFaults},
                {"loader_threat_local_providers", sample.loaderThreatLocalProviders},
                {"loader_threat_local_artifacts", sample.loaderThreatLocalArtifacts},
                {"loader_threat_blocked_providers", sample.loaderThreatBlockedProviders},
                {"loader_threat_blocked_artifacts", sample.loaderThreatBlockedArtifacts},
                {"loader_threat_remote_reporters", sample.loaderThreatRemoteReporters},
                {"network_collector_backend", sample.networkCollectorBackend},
                {"network_attributed_total_bps", sample.networkAttributedTotalBps},
                {"network_cross_network_bps", sample.networkCrossNetworkBps},
                {"network_active_flows", sample.networkActiveFlows},
                {"network_listeners", sample.networkListeners},
                {"network_estimated_flows", sample.networkEstimatedFlows},
                {"network_udp_active_flows", sample.networkUdpActiveFlows},
                {"network_udp_drop_delta", sample.networkUdpDropDelta},
                {"network_udp_estimated_total_bps", sample.networkUdpEstimatedTotalBps},
                {"network_attribution_confidence", sample.networkAttributionConfidence},
                {"network_latency_pressure", sample.networkLatencyPressure},
                {"network_queue_pressure", sample.networkQueuePressure},
                {"network_traffic_growth_pct_per_min", sample.networkTrafficGrowthPctPerMin},
                {"network_cross_growth_pct_per_min", sample.networkCrossGrowthPctPerMin},
                {"network_flow_growth_pct_per_min", sample.networkFlowGrowthPctPerMin},
                {"projected_total_bps_5m", sample.projectedTotalBps5m},
                {"projected_total_bps_15m", sample.projectedTotalBps15m},
                {"projected_cross_network_bps_5m", sample.projectedCrossNetworkBps5m},
                {"projected_cross_network_bps_15m", sample.projectedCrossNetworkBps15m},
                {"projected_active_flows_5m", sample.projectedActiveFlows5m},
                {"projected_active_flows_15m", sample.projectedActiveFlows15m},
                {"estimated_network_bps", sample.estimatedNetworkBps},
                {"estimated_power_w", sample.estimatedPowerW},
                {"source", sample.source}
        };
    }

    nlohmann::json APIRoutes::traceyAgentLogToJson(const TraceyAgentLogEntry& entry) const {
        return {
                {"ts_ms", entry.tsMs},
                {"level", entry.level},
                {"category", entry.category},
                {"message", entry.message},
                {"context", entry.context.is_null() ? nlohmann::json::object() : entry.context}
        };
    }

    APIRoutes::TraceyAgent APIRoutes::traceyAgentFromJson(const nlohmann::json& payload) const {
        TraceyAgent agent;
        if (!payload.is_object()) {
            return agent;
        }
        agent.agentId = payload.value("agent_id", "");
        agent.cluster = payload.value("cluster", "");
        agent.status = payload.value("status", "");
        agent.version = payload.value("version", "");
        agent.host = payload.value("host", "");
        agent.source = payload.value("source", "");
        agent.announceAddr = payload.value("announce_addr", "");
        agent.statusAddr = payload.value("status_addr", "");
        agent.linkState = payload.value("link_state", "");
        agent.linkSecurity = payload.value("link_security", "");
        agent.lastError = payload.value("last_error", "");
        agent.metrics = payload.contains("metrics") ? payload["metrics"] : nlohmann::json::object();
        agent.capabilities = payload.contains("capabilities") ? payload["capabilities"] : nlohmann::json::object();
        agent.lastSeenEpochMs = payload.value("last_seen_epoch_ms", 0LL);
        agent.lastAnnouncementEpochMs = payload.value("last_announcement_epoch_ms", 0LL);
        agent.lastQueryEpochMs = payload.value("last_query_epoch_ms", 0LL);
        agent.nextQueryEpochMs = payload.value("next_query_epoch_ms", 0LL);
        agent.queryFailures = std::max(0, payload.value("query_failures", 0));
        agent.stale = payload.value("stale", false);
        agent.statusReachable = payload.value("status_reachable", false);
        agent.signaturePresent = payload.value("signature_present", false);
        agent.coordinator = payload.value("coordinator", false);
        agent.coordinatorEpoch = payload.value("coordinator_epoch", 0LL);
        agent.score = payload.value("score", 0LL);
        return agent;
    }

    APIRoutes::TraceyRequirement APIRoutes::traceyRequirementFromJson(const nlohmann::json& payload) const {
        TraceyRequirement requirement;
        if (!payload.is_object()) {
            return requirement;
        }
        requirement.key = payload.value("key", "");
        requirement.resourceType = payload.value("resource_type", "");
        requirement.resourceName = payload.value("resource_name", "");
        requirement.region = payload.value("region", "");
        requirement.expectedAgentId = payload.value("expected_agent_id", "");
        requirement.expectedStatusAddr = payload.value("expected_status_addr", "");
        requirement.createdEpochMs = payload.value("created_epoch_ms", 0LL);
        return requirement;
    }

    APIRoutes::TraceyStatusSample APIRoutes::traceyStatusSampleFromJson(const nlohmann::json& payload) const {
        TraceyStatusSample sample;
        if (!payload.is_object()) {
            return sample;
        }
        sample.tsMs = payload.value("ts_ms", 0LL);
        sample.status = payload.value("status", "");
        sample.stale = payload.value("stale", false);
        sample.statusReachable = payload.value("status_reachable", false);
        sample.queryFailures = std::max(0, payload.value("query_failures", 0));
        sample.coordinator = payload.value("coordinator", false);
        sample.score = payload.value("score", 0LL);
        sample.probeWatchTotalObservations = std::max(0, payload.value("probe_watch_total_observations", 0));
        sample.probeWatchTotalAlerts = std::max(0, payload.value("probe_watch_total_alerts", 0));
        sample.probeWatchDistinctSources = std::max(0, payload.value("probe_watch_distinct_sources", 0));
        sample.probeWatchRecentAlerts = std::max(0, payload.value("probe_watch_recent_alerts", 0));
        sample.probeWatchHighAlerts = std::max(0, payload.value("probe_watch_high_alerts", 0));
        sample.probeWatchMediumAlerts = std::max(0, payload.value("probe_watch_medium_alerts", 0));
        sample.probeWatchUnauthorizedControlAlerts = std::max(0, payload.value("probe_watch_unauthorized_control_alerts", 0));
        sample.probeWatchCooperativeAlerts = std::max(0, payload.value("probe_watch_cooperative_alerts", 0));
        sample.probeWatchAlertedSurfaces = std::max(0, payload.value("probe_watch_alerted_surfaces", 0));
        sample.tracey_guardProbeFailures = std::max(0, payload.value("tracey_guard_probe_failures", 0));
        sample.tracey_guardProbeErrors = std::max(0, payload.value("tracey_guard_probe_errors", 0));
        sample.tracey_guardQuarantined = std::max(0, payload.value("tracey_guard_quarantined", 0));
        sample.tracey_guardRemoteFaults = std::max(0, payload.value("tracey_guard_remote_faults", 0));
        sample.loaderThreatLocalProviders = std::max(0, payload.value("loader_threat_local_providers", 0));
        sample.loaderThreatLocalArtifacts = std::max(0, payload.value("loader_threat_local_artifacts", 0));
        sample.loaderThreatBlockedProviders = std::max(0, payload.value("loader_threat_blocked_providers", 0));
        sample.loaderThreatBlockedArtifacts = std::max(0, payload.value("loader_threat_blocked_artifacts", 0));
        sample.loaderThreatRemoteReporters = std::max(0, payload.value("loader_threat_remote_reporters", 0));
        sample.networkCollectorBackend = payload.value("network_collector_backend", "");
        sample.networkAttributedTotalBps = std::max(0.0, payload.value("network_attributed_total_bps", 0.0));
        sample.networkCrossNetworkBps = std::max(0.0, payload.value("network_cross_network_bps", 0.0));
        sample.networkActiveFlows = std::max(0, payload.value("network_active_flows", 0));
        sample.networkListeners = std::max(0, payload.value("network_listeners", 0));
        sample.networkEstimatedFlows = std::max(0, payload.value("network_estimated_flows", 0));
        sample.networkUdpActiveFlows = std::max(0, payload.value("network_udp_active_flows", 0));
        sample.networkUdpDropDelta = std::max<int64_t>(0, payload.value("network_udp_drop_delta", 0LL));
        sample.networkUdpEstimatedTotalBps = std::max(0.0, payload.value("network_udp_estimated_total_bps", 0.0));
        sample.networkAttributionConfidence = std::max(0.0, payload.value("network_attribution_confidence", 0.0));
        sample.networkLatencyPressure = std::max(0.0, payload.value("network_latency_pressure", 0.0));
        sample.networkQueuePressure = std::max(0.0, payload.value("network_queue_pressure", 0.0));
        sample.networkTrafficGrowthPctPerMin = payload.value("network_traffic_growth_pct_per_min", 0.0);
        sample.networkCrossGrowthPctPerMin = payload.value("network_cross_growth_pct_per_min", 0.0);
        sample.networkFlowGrowthPctPerMin = payload.value("network_flow_growth_pct_per_min", 0.0);
        sample.projectedTotalBps5m = std::max(0.0, payload.value("projected_total_bps_5m", 0.0));
        sample.projectedTotalBps15m = std::max(0.0, payload.value("projected_total_bps_15m", 0.0));
        sample.projectedCrossNetworkBps5m = std::max(0.0, payload.value("projected_cross_network_bps_5m", 0.0));
        sample.projectedCrossNetworkBps15m = std::max(0.0, payload.value("projected_cross_network_bps_15m", 0.0));
        sample.projectedActiveFlows5m = std::max(0.0, payload.value("projected_active_flows_5m", 0.0));
        sample.projectedActiveFlows15m = std::max(0.0, payload.value("projected_active_flows_15m", 0.0));
        sample.estimatedNetworkBps = std::max(0.0, payload.value("estimated_network_bps", 0.0));
        sample.estimatedPowerW = std::max(0.0, payload.value("estimated_power_w", 0.0));
        sample.source = payload.value("source", "");
        return sample;
    }

    APIRoutes::TraceyAgentLogEntry APIRoutes::traceyAgentLogFromJson(const nlohmann::json& payload) const {
        TraceyAgentLogEntry entry;
        if (!payload.is_object()) {
            return entry;
        }
        entry.tsMs = payload.value("ts_ms", 0LL);
        entry.level = payload.value("level", "");
        entry.category = payload.value("category", "");
        entry.message = payload.value("message", "");
        entry.context = payload.contains("context") ? payload["context"] : nlohmann::json::object();
        return entry;
    }

    nlohmann::json APIRoutes::buildTraceyStateSnapshotLocked(int64_t nowMs) const {
        std::vector<std::string> agentIds;
        agentIds.reserve(traceyAgents.size());
        for (const auto& entry : traceyAgents) {
            agentIds.push_back(entry.first);
        }
        std::sort(agentIds.begin(), agentIds.end());

        nlohmann::json agents = nlohmann::json::array();
        nlohmann::json history = nlohmann::json::array();
        nlohmann::json logs = nlohmann::json::array();
        for (const auto& agentId : agentIds) {
            const auto agentIt = traceyAgents.find(agentId);
            if (agentIt != traceyAgents.end()) {
                agents.push_back(traceyAgentToJson(agentIt->second));
            }
            const auto historyIt = traceyAgentHistory.find(agentId);
            if (historyIt != traceyAgentHistory.end()) {
                nlohmann::json samples = nlohmann::json::array();
                for (const auto& sample : historyIt->second) {
                    samples.push_back(traceyStatusSampleToJson(sample));
                }
                history.push_back({
                        {"agent_id", agentId},
                        {"samples", std::move(samples)}
                });
            }
            const auto logsIt = traceyAgentLogs.find(agentId);
            if (logsIt != traceyAgentLogs.end()) {
                nlohmann::json entries = nlohmann::json::array();
                for (const auto& entry : logsIt->second) {
                    entries.push_back(traceyAgentLogToJson(entry));
                }
                logs.push_back({
                        {"agent_id", agentId},
                        {"entries", std::move(entries)}
                });
            }
        }

        std::vector<std::string> requirementKeys;
        requirementKeys.reserve(traceyRequirements.size());
        for (const auto& entry : traceyRequirements) {
            requirementKeys.push_back(entry.first);
        }
        std::sort(requirementKeys.begin(), requirementKeys.end());
        nlohmann::json requirements = nlohmann::json::array();
        for (const auto& key : requirementKeys) {
            const auto it = traceyRequirements.find(key);
            if (it != traceyRequirements.end()) {
                requirements.push_back(traceyRequirementToJson(it->second));
            }
        }

        return {
                {"schema_version", 1},
                {"generated_epoch_ms", nowMs > 0 ? nowMs : nowEpochMs()},
                {"tracey_history_max_samples", traceyHistoryMaxSamples},
                {"tracey_agent_log_max_entries", traceyAgentLogMaxEntries},
                {"tracey_agents", std::move(agents)},
                {"tracey_requirements", std::move(requirements)},
                {"tracey_agent_history", std::move(history)},
                {"tracey_agent_logs", std::move(logs)}
        };
    }

    void APIRoutes::restoreTraceyStateSnapshotLocked(const nlohmann::json& snapshot) {
        if (!snapshot.is_object()) {
            return;
        }

        traceyAgents.clear();
        traceyRequirements.clear();
        traceyAgentHistory.clear();
        traceyAgentLogs.clear();

        for (const auto& agentPayload : snapshot.value("tracey_agents", nlohmann::json::array())) {
            TraceyAgent agent = traceyAgentFromJson(agentPayload);
            if (!agent.agentId.empty()) {
                traceyAgents[agent.agentId] = std::move(agent);
            }
        }
        for (const auto& requirementPayload : snapshot.value("tracey_requirements", nlohmann::json::array())) {
            TraceyRequirement requirement = traceyRequirementFromJson(requirementPayload);
            if (!requirement.key.empty()) {
                traceyRequirements[requirement.key] = std::move(requirement);
            }
        }
        for (const auto& historyPayload : snapshot.value("tracey_agent_history", nlohmann::json::array())) {
            if (!historyPayload.is_object()) {
                continue;
            }
            const std::string agentId = historyPayload.value("agent_id", "");
            if (agentId.empty()) {
                continue;
            }
            std::vector<TraceyStatusSample> samples;
            for (const auto& samplePayload : historyPayload.value("samples", nlohmann::json::array())) {
                TraceyStatusSample sample = traceyStatusSampleFromJson(samplePayload);
                if (sample.tsMs > 0) {
                    samples.push_back(std::move(sample));
                }
            }
            std::sort(samples.begin(), samples.end(), [](const TraceyStatusSample& left, const TraceyStatusSample& right) {
                return left.tsMs < right.tsMs;
            });
            std::deque<TraceyStatusSample> dequeSamples;
            for (const auto& sample : samples) {
                dequeSamples.push_back(sample);
                while (dequeSamples.size() > traceyHistoryMaxSamples) {
                    dequeSamples.pop_front();
                }
            }
            if (!dequeSamples.empty()) {
                traceyAgentHistory[agentId] = std::move(dequeSamples);
            }
        }
        for (const auto& logPayload : snapshot.value("tracey_agent_logs", nlohmann::json::array())) {
            if (!logPayload.is_object()) {
                continue;
            }
            const std::string agentId = logPayload.value("agent_id", "");
            if (agentId.empty()) {
                continue;
            }
            std::vector<TraceyAgentLogEntry> entries;
            for (const auto& entryPayload : logPayload.value("entries", nlohmann::json::array())) {
                TraceyAgentLogEntry entry = traceyAgentLogFromJson(entryPayload);
                if (entry.tsMs > 0) {
                    entries.push_back(std::move(entry));
                }
            }
            std::sort(entries.begin(), entries.end(), [](const TraceyAgentLogEntry& left, const TraceyAgentLogEntry& right) {
                return left.tsMs < right.tsMs;
            });
            std::deque<TraceyAgentLogEntry> dequeEntries;
            for (const auto& entry : entries) {
                dequeEntries.push_back(entry);
                while (dequeEntries.size() > traceyAgentLogMaxEntries) {
                    dequeEntries.pop_front();
                }
            }
            if (!dequeEntries.empty()) {
                traceyAgentLogs[agentId] = std::move(dequeEntries);
            }
        }
    }

    void APIRoutes::scheduleTraceyStateSnapshotLocked(int64_t nowMs) {
        if (!traceyStateStore) {
            return;
        }
        const int64_t snapshotTs = nowMs > 0 ? nowMs : nowEpochMs();
        traceyStateStore->scheduleSnapshot(snapshotTs);
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
        sample.probeWatchTotalObservations = 0;
        sample.probeWatchTotalAlerts = 0;
        sample.probeWatchDistinctSources = 0;
        sample.probeWatchRecentAlerts = 0;
        sample.probeWatchHighAlerts = 0;
        sample.probeWatchMediumAlerts = 0;
        sample.probeWatchUnauthorizedControlAlerts = 0;
        sample.probeWatchCooperativeAlerts = 0;
        sample.probeWatchAlertedSurfaces = 0;
        sample.tracey_guardProbeFailures = 0;
        sample.tracey_guardProbeErrors = 0;
        sample.tracey_guardQuarantined = 0;
        sample.tracey_guardRemoteFaults = 0;
        sample.loaderThreatLocalProviders = 0;
        sample.loaderThreatLocalArtifacts = 0;
        sample.loaderThreatBlockedProviders = 0;
        sample.loaderThreatBlockedArtifacts = 0;
        sample.loaderThreatRemoteReporters = 0;
        sample.networkCollectorBackend = "";
        sample.networkAttributedTotalBps = 0.0;
        sample.networkCrossNetworkBps = 0.0;
        sample.networkActiveFlows = 0;
        sample.networkListeners = 0;
        sample.networkEstimatedFlows = 0;
        sample.networkUdpActiveFlows = 0;
        sample.networkUdpDropDelta = 0;
        sample.networkUdpEstimatedTotalBps = 0.0;
        sample.networkAttributionConfidence = 0.0;
        sample.networkLatencyPressure = 0.0;
        sample.networkQueuePressure = 0.0;
        sample.networkTrafficGrowthPctPerMin = 0.0;
        sample.networkCrossGrowthPctPerMin = 0.0;
        sample.networkFlowGrowthPctPerMin = 0.0;
        sample.projectedTotalBps5m = 0.0;
        sample.projectedTotalBps15m = 0.0;
        sample.projectedCrossNetworkBps5m = 0.0;
        sample.projectedCrossNetworkBps15m = 0.0;
        sample.projectedActiveFlows5m = 0.0;
        sample.projectedActiveFlows15m = 0.0;
        sample.estimatedNetworkBps = 0.0;
        sample.estimatedPowerW = 0.0;
        if (agent.metrics.is_object()) {
            const auto* statusIt = extractTraceyStatusSnapshotNode(agent.metrics);
            if (statusIt != nullptr) {
                const auto probeWatchSummary = extractTraceyProbeWatchSummary(*statusIt);
                if (probeWatchSummary.is_object()) {
                    sample.probeWatchTotalObservations = std::max(0, probeWatchSummary.value("total_observations", 0));
                    sample.probeWatchTotalAlerts = std::max(0, probeWatchSummary.value("total_alerts", 0));
                    sample.probeWatchDistinctSources = std::max(0, probeWatchSummary.value("distinct_sources", 0));
                    sample.probeWatchRecentAlerts = std::max(0, probeWatchSummary.value("recent_alert_count", 0));
                    sample.probeWatchHighAlerts = std::max(0, probeWatchSummary.value("high_severity_alerts", 0));
                    sample.probeWatchMediumAlerts = std::max(0, probeWatchSummary.value("medium_severity_alerts", 0));
                    sample.probeWatchUnauthorizedControlAlerts = std::max(0, probeWatchSummary.value("unauthorized_control_alerts", 0));
                    sample.probeWatchCooperativeAlerts = std::max(0, probeWatchSummary.value("cooperative_alerts", 0));
                    sample.probeWatchAlertedSurfaces = std::max(0, probeWatchSummary.value("alerted_surface_count", 0));
                }
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

                const auto* continuum = extractContinuumTelemetryNode(agent.metrics);
                const auto* serverNode = continuum != nullptr ? firstObjectValue(*continuum, {"server"}) : nullptr;
                const auto* networkNode = serverNode != nullptr ? firstObjectValue(*serverNode, {"network"}) : nullptr;
                const auto* networkSummary = networkNode != nullptr ? firstObjectValue(*networkNode, {"summary"}) : nullptr;
                const auto* forecastNode = firstObjectValue(
                        *statusIt,
                        {"resource_forecast", "resourceForecast", "forecast"}
                );
                const auto* forecastSimulation = forecastNode != nullptr
                                                 ? firstObjectValue(*forecastNode, {"simulation"})
                                                 : nullptr;
                if (networkSummary != nullptr) {
                    sample.networkCollectorBackend = firstStringValue(
                            *networkSummary,
                            {"collector_backend", "collectorBackend"}
                    );
                    sample.networkAttributedTotalBps = std::max(0.0, firstDoubleValue(
                            *networkSummary,
                            {"attributed_total_bps", "attributedTotalBps"},
                            0.0
                    ));
                    sample.networkCrossNetworkBps = std::max(0.0, firstDoubleValue(
                            *networkSummary,
                            {"cross_network_bps", "crossNetworkBps"},
                            0.0
                    ));
                    sample.networkActiveFlows = std::max(0, static_cast<int>(firstInt64Value(
                            *networkSummary,
                            {"active_flows", "activeFlows"},
                            0
                    )));
                    sample.networkListeners = std::max(0, static_cast<int>(firstInt64Value(
                            *networkSummary,
                            {"listeners"},
                            0
                    )));
                    sample.networkEstimatedFlows = std::max(0, static_cast<int>(firstInt64Value(
                            *networkSummary,
                            {"estimated_flows", "estimatedFlows"},
                            0
                    )));
                    sample.networkUdpActiveFlows = std::max(0, static_cast<int>(firstInt64Value(
                            *networkSummary,
                            {"udp_active_flows", "udpActiveFlows"},
                            0
                    )));
                    sample.networkUdpDropDelta = std::max<int64_t>(0, firstInt64Value(
                            *networkSummary,
                            {"udp_drop_delta", "udpDropDelta"},
                            0
                    ));
                    sample.networkUdpEstimatedTotalBps = std::max(0.0, firstDoubleValue(
                            *networkSummary,
                            {"udp_estimated_total_bps", "udpEstimatedTotalBps"},
                            0.0
                    ));
                    sample.networkAttributionConfidence = std::max(0.0, firstDoubleValue(
                            *networkSummary,
                            {"attribution_confidence", "attributionConfidence"},
                            0.0
                    ));
                    sample.networkLatencyPressure = std::max(0.0, firstDoubleValue(
                            *networkSummary,
                            {"latency_pressure", "latencyPressure"},
                            0.0
                    ));
                    sample.networkQueuePressure = std::max(0.0, firstDoubleValue(
                            *networkSummary,
                            {"queue_pressure", "queuePressure"},
                            0.0
                    ));
                    sample.networkTrafficGrowthPctPerMin = firstDoubleValue(
                            *networkSummary,
                            {"traffic_growth_pct_per_min", "trafficGrowthPctPerMin"},
                            0.0
                    );
                    sample.networkCrossGrowthPctPerMin = firstDoubleValue(
                            *networkSummary,
                            {"cross_network_growth_pct_per_min", "crossNetworkGrowthPctPerMin"},
                            0.0
                    );
                    sample.networkFlowGrowthPctPerMin = firstDoubleValue(
                            *networkSummary,
                            {"flow_growth_pct_per_min", "flowGrowthPctPerMin"},
                            0.0
                    );
                }
                if (forecastNode != nullptr) {
                    sample.projectedTotalBps5m = std::max(0.0, firstDoubleValue(
                            *forecastNode,
                            {"projected_total_bps_5m", "projectedTotalBps5m"},
                            0.0
                    ));
                    sample.projectedTotalBps15m = std::max(0.0, firstDoubleValue(
                            *forecastNode,
                            {"projected_total_bps_15m", "projectedTotalBps15m"},
                            0.0
                    ));
                    sample.projectedCrossNetworkBps5m = std::max(0.0, firstDoubleValue(
                            *forecastNode,
                            {"projected_cross_network_bps_5m", "projectedCrossNetworkBps5m"},
                            0.0
                    ));
                    sample.projectedCrossNetworkBps15m = std::max(0.0, firstDoubleValue(
                            *forecastNode,
                            {"projected_cross_network_bps_15m", "projectedCrossNetworkBps15m"},
                            0.0
                    ));
                    sample.projectedActiveFlows5m = std::max(0.0, firstDoubleValue(
                            *forecastNode,
                            {"projected_active_flows_5m", "projectedActiveFlows5m"},
                            0.0
                    ));
                    sample.projectedActiveFlows15m = std::max(0.0, firstDoubleValue(
                            *forecastNode,
                            {"projected_active_flows_15m", "projectedActiveFlows15m"},
                            0.0
                    ));
                }
                if (forecastSimulation != nullptr) {
                    sample.estimatedNetworkBps = std::max(0.0, firstDoubleValue(
                            *forecastSimulation,
                            {"estimated_network_bps", "estimatedNetworkBps"},
                            0.0
                    ));
                    sample.estimatedPowerW = std::max(0.0, firstDoubleValue(
                            *forecastSimulation,
                            {"estimated_power_w", "estimatedPowerW"},
                            0.0
                    ));
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
                last.probeWatchTotalObservations == sample.probeWatchTotalObservations &&
                last.probeWatchTotalAlerts == sample.probeWatchTotalAlerts &&
                last.probeWatchDistinctSources == sample.probeWatchDistinctSources &&
                last.probeWatchRecentAlerts == sample.probeWatchRecentAlerts &&
                last.probeWatchHighAlerts == sample.probeWatchHighAlerts &&
                last.probeWatchMediumAlerts == sample.probeWatchMediumAlerts &&
                last.probeWatchUnauthorizedControlAlerts == sample.probeWatchUnauthorizedControlAlerts &&
                last.probeWatchCooperativeAlerts == sample.probeWatchCooperativeAlerts &&
                last.probeWatchAlertedSurfaces == sample.probeWatchAlertedSurfaces &&
                last.tracey_guardProbeFailures == sample.tracey_guardProbeFailures &&
                last.tracey_guardProbeErrors == sample.tracey_guardProbeErrors &&
                last.tracey_guardQuarantined == sample.tracey_guardQuarantined &&
                last.tracey_guardRemoteFaults == sample.tracey_guardRemoteFaults &&
                last.loaderThreatLocalProviders == sample.loaderThreatLocalProviders &&
                last.loaderThreatLocalArtifacts == sample.loaderThreatLocalArtifacts &&
                last.loaderThreatBlockedProviders == sample.loaderThreatBlockedProviders &&
                last.loaderThreatBlockedArtifacts == sample.loaderThreatBlockedArtifacts &&
                last.loaderThreatRemoteReporters == sample.loaderThreatRemoteReporters &&
                last.networkCollectorBackend == sample.networkCollectorBackend &&
                last.networkAttributedTotalBps == sample.networkAttributedTotalBps &&
                last.networkCrossNetworkBps == sample.networkCrossNetworkBps &&
                last.networkActiveFlows == sample.networkActiveFlows &&
                last.networkListeners == sample.networkListeners &&
                last.networkEstimatedFlows == sample.networkEstimatedFlows &&
                last.networkUdpActiveFlows == sample.networkUdpActiveFlows &&
                last.networkUdpDropDelta == sample.networkUdpDropDelta &&
                last.networkUdpEstimatedTotalBps == sample.networkUdpEstimatedTotalBps &&
                last.networkAttributionConfidence == sample.networkAttributionConfidence &&
                last.networkLatencyPressure == sample.networkLatencyPressure &&
                last.networkQueuePressure == sample.networkQueuePressure &&
                last.networkTrafficGrowthPctPerMin == sample.networkTrafficGrowthPctPerMin &&
                last.networkCrossGrowthPctPerMin == sample.networkCrossGrowthPctPerMin &&
                last.networkFlowGrowthPctPerMin == sample.networkFlowGrowthPctPerMin &&
                last.projectedTotalBps5m == sample.projectedTotalBps5m &&
                last.projectedTotalBps15m == sample.projectedTotalBps15m &&
                last.projectedCrossNetworkBps5m == sample.projectedCrossNetworkBps5m &&
                last.projectedCrossNetworkBps15m == sample.projectedCrossNetworkBps15m &&
                last.projectedActiveFlows5m == sample.projectedActiveFlows5m &&
                last.projectedActiveFlows15m == sample.projectedActiveFlows15m &&
                last.estimatedNetworkBps == sample.estimatedNetworkBps &&
                last.estimatedPowerW == sample.estimatedPowerW &&
                last.source == sample.source) {
                return;
            }
        }

        history.push_back(std::move(sample));
        while (history.size() > traceyHistoryMaxSamples) {
            history.pop_front();
        }
        const auto& persistedSample = history.back();
        if (traceyStateStore) {
            traceyStateStore->recordStatusSample(agent.agentId, traceyStatusSampleToJson(persistedSample));
        }
        scheduleTraceyStateSnapshotLocked(persistedSample.tsMs);
    }

    void APIRoutes::appendTraceyProbeWatchLogsLocked(TraceyAgent& agent, int64_t nowMs) {
        if (agent.agentId.empty() || !agent.metrics.is_object()) {
            return;
        }
        const auto* statusSnapshot = extractTraceyStatusSnapshotNode(agent.metrics);
        if (statusSnapshot == nullptr) {
            return;
        }
        const auto probeWatchSummary = extractTraceyProbeWatchSummary(*statusSnapshot);
        if (!probeWatchSummary.is_object()) {
            return;
        }
        const auto alertsIt = probeWatchSummary.find("recent_alerts");
        if (alertsIt == probeWatchSummary.end() || !alertsIt->is_array()) {
            return;
        }

        struct PendingProbeAlert {
            int64_t tsMs{0};
            std::string fingerprint;
            nlohmann::json alert;
        };
        std::vector<PendingProbeAlert> pending;
        for (const auto& alert : *alertsIt) {
            if (!alert.is_object()) {
                continue;
            }
            const int64_t tsMs = std::max<int64_t>(0, firstInt64Value(alert, {"ts_ms", "tsMs"}, 0));
            const std::string fingerprint = firstStringValue(alert, {"surface"}) + "|" +
                                            firstStringValue(alert, {"classification"}) + "|" +
                                            firstStringValue(alert, {"source"}) + "|" +
                                            firstStringValue(alert, {"path"}) + "|" +
                                            std::to_string(tsMs);
            if (tsMs < agent.lastProbeAlertTsMs) {
                continue;
            }
            if (tsMs == agent.lastProbeAlertTsMs && fingerprint == agent.lastProbeAlertFingerprint) {
                continue;
            }
            pending.push_back({tsMs, fingerprint, alert});
        }
        std::sort(pending.begin(), pending.end(), [](const PendingProbeAlert& a, const PendingProbeAlert& b) {
            if (a.tsMs == b.tsMs) {
                return a.fingerprint < b.fingerprint;
            }
            return a.tsMs < b.tsMs;
        });

        for (const auto& entry : pending) {
            const auto& alert = entry.alert;
            const std::string surface = firstStringValue(alert, {"surface"});
            const std::string classification = firstStringValue(alert, {"classification"});
            const std::string severity = toLower(firstStringValue(alert, {"severity"}, "info"));
            const std::string source = firstStringValue(alert, {"source"});
            const std::string path = firstStringValue(alert, {"path"});
            const std::string reason = firstStringValue(alert, {"reason"});
            const std::string runId = firstStringValue(alert, {"run_id", "runId"});
            const int64_t statusCode = std::max<int64_t>(0, firstInt64Value(alert, {"status_code", "statusCode"}, 0));
            const double signal = std::max(0.0, firstDoubleValue(alert, {"signal"}, 0.0));
            const bool authorized = firstValue(alert, {"authorized"}) != nullptr
                                    ? jsonBoolValue(*firstValue(alert, {"authorized"}), false)
                                    : false;
            const std::string level = classification == "cooperative_probe"
                                      ? "info"
                                      : ((severity == "high" || severity == "critical") ? "warn" : "info");
            const std::string message = !surface.empty()
                                        ? "Tracey detected network probe activity on " + surface + "."
                                        : "Tracey detected network probe activity.";
            appendTraceyAgentLogLocked(
                    agent.agentId,
                    entry.tsMs > 0 ? entry.tsMs : nowMs,
                    level,
                    "security",
                    message,
                    {
                            {"surface", surface},
                            {"classification", classification},
                            {"severity", severity},
                            {"source", source},
                            {"path", path},
                            {"status_code", statusCode},
                            {"signal", signal},
                            {"authorized", authorized},
                            {"reason", reason},
                            {"run_id", runId}
                    }
            );
            if (entry.tsMs > agent.lastProbeAlertTsMs ||
                (entry.tsMs == agent.lastProbeAlertTsMs &&
                 entry.fingerprint != agent.lastProbeAlertFingerprint)) {
                agent.lastProbeAlertTsMs = entry.tsMs;
                agent.lastProbeAlertFingerprint = entry.fingerprint;
            }
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
        const auto& persistedEntry = logs.back();
        if (traceyStateStore) {
            traceyStateStore->recordAgentLog(agentId, traceyAgentLogToJson(persistedEntry));
        }
        scheduleTraceyStateSnapshotLocked(persistedEntry.tsMs);
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
        appendTraceyProbeWatchLogsLocked(agent, nowMs);
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
            appendTraceyProbeWatchLogsLocked(agent, nowMs);
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
        const auto* networkNode = serverNode != nullptr ? firstObjectValue(*serverNode, {"network"}) : nullptr;
        const auto* networkSummary = networkNode != nullptr ? firstObjectValue(*networkNode, {"summary"}) : nullptr;
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
        const auto* resourceForecast = statusSnapshot != nullptr
                                       ? firstObjectValue(
                                               *statusSnapshot,
                                               {"resource_forecast", "resourceForecast", "forecast"}
                                       )
                                       : nullptr;
        const auto* continuumLoop = statusSnapshot != nullptr
                                    ? firstObjectValue(
                                            *statusSnapshot,
                                            {"continuum_loop", "continuumLoop", "adaptive_loop", "adaptiveLoop"}
                                    )
                                    : nullptr;
        const nlohmann::json probeWatch = statusSnapshot != nullptr
                                          ? extractTraceyProbeWatchSummary(*statusSnapshot)
                                          : nlohmann::json::object();
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

        nlohmann::json resourceForecastView = resourceForecast != nullptr ? *resourceForecast : nlohmann::json::object();
        enrichTraceyResourceForecastView(server, networkSummary, gpus, resourceForecastView, nowMs);
        const auto* resourceSimulation = firstObjectValue(resourceForecastView, {"simulation"});

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
                {"probe_watch", probeWatch},
                {"tracey_guard", traceyGuard != nullptr ? *traceyGuard : nlohmann::json::object()},
                {"tracey_guard_summary", traceyGuardSummary != nullptr ? *traceyGuardSummary : nlohmann::json::object()},
                {"recent_executions", traceyGuardRecentExecutions != nullptr ? *traceyGuardRecentExecutions : nlohmann::json::array()},
                {"recent_faults", traceyGuardRecentFaults != nullptr ? *traceyGuardRecentFaults : nlohmann::json::array()},
                {"remote_faults", traceyGuardRemoteFaults != nullptr ? *traceyGuardRemoteFaults : nlohmann::json::array()},
                {"continuum_assessment", continuumAssessment != nullptr ? *continuumAssessment : nlohmann::json::object()},
                {"continuum_assessment_summary", continuumAssessmentSummary != nullptr ? *continuumAssessmentSummary : nlohmann::json::object()},
                {"continuum_autoscaler", continuumAutoscaler != nullptr ? *continuumAutoscaler : nlohmann::json::object()},
                {"resource_forecast", resourceForecastView},
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
                        {"probe_alerts", std::max<int64_t>(0, probeWatch.value("total_alerts", 0))},
                        {"probe_high_alerts", std::max<int64_t>(0, probeWatch.value("high_severity_alerts", 0))},
                        {"probe_alerted_surfaces", std::max<int64_t>(0, probeWatch.value("alerted_surface_count", 0))},
                        {"probe_latest_surface", probeWatch.value("latest_surface", "")},
                        {"probe_latest_classification", probeWatch.value("latest_classification", "")},
                        {"ecc_corrected_total", eccNode != nullptr ? std::max<int64_t>(0, firstInt64Value(*eccNode, {"corrected_total", "correctedTotal"}, 0)) : 0},
                        {"ecc_uncorrected_total", eccNode != nullptr ? std::max<int64_t>(0, firstInt64Value(*eccNode, {"uncorrected_total", "uncorrectedTotal"}, 0)) : 0},
                        {"compromise_risk", continuumAssessmentSummary != nullptr ? std::max(0.0, firstDoubleValue(*continuumAssessmentSummary, {"compromise_risk", "compromiseRisk"}, 0.0)) : 0.0},
                        {"compromise_confidence", continuumAssessmentSummary != nullptr ? std::max(0.0, firstDoubleValue(*continuumAssessmentSummary, {"compromise_confidence", "compromiseConfidence"}, 0.0)) : 0.0},
                        {"cve_matches", continuumAssessmentSummary != nullptr ? std::max<int64_t>(0, firstInt64Value(*continuumAssessmentSummary, {"cve_matches", "cveMatches"}, 0)) : 0},
                        {"kev_matches", continuumAssessmentSummary != nullptr ? std::max<int64_t>(0, firstInt64Value(*continuumAssessmentSummary, {"kev_matches", "kevMatches"}, 0)) : 0},
                        {"assessment_status", continuumAssessmentSummary != nullptr ? firstStringValue(*continuumAssessmentSummary, {"status"}, "unknown") : "unknown"},
                        {"assessment_action", continuumAssessmentSummary != nullptr ? firstStringValue(*continuumAssessmentSummary, {"recommended_action", "recommendedAction"}) : ""},
                        {"attributed_total_bps", networkSummary != nullptr ? std::max(0.0, firstDoubleValue(*networkSummary, {"attributed_total_bps", "attributedTotalBps"}, 0.0)) : 0.0},
                        {"cross_network_bps", networkSummary != nullptr ? std::max(0.0, firstDoubleValue(*networkSummary, {"cross_network_bps", "crossNetworkBps"}, 0.0)) : 0.0},
                        {"network_collector_backend", networkSummary != nullptr ? firstStringValue(*networkSummary, {"collector_backend", "collectorBackend"}) : ""},
                        {"network_attribution_confidence", networkSummary != nullptr ? std::max(0.0, firstDoubleValue(*networkSummary, {"attribution_confidence", "attributionConfidence"}, 0.0)) : 0.0},
                        {"network_active_flows", networkSummary != nullptr ? std::max<int64_t>(0, firstInt64Value(*networkSummary, {"active_flows", "activeFlows"}, 0)) : 0},
                        {"network_listeners", networkSummary != nullptr ? std::max<int64_t>(0, firstInt64Value(*networkSummary, {"listeners"}, 0)) : 0},
                        {"network_cross_flows", networkSummary != nullptr ? std::max<int64_t>(0, firstInt64Value(*networkSummary, {"cross_network_flows", "crossNetworkFlows"}, 0)) : 0},
                        {"network_estimated_flows", networkSummary != nullptr ? std::max<int64_t>(0, firstInt64Value(*networkSummary, {"estimated_flows", "estimatedFlows"}, 0)) : 0},
                        {"network_udp_active_flows", networkSummary != nullptr ? std::max<int64_t>(0, firstInt64Value(*networkSummary, {"udp_active_flows", "udpActiveFlows"}, 0)) : 0},
                        {"network_udp_drop_delta", networkSummary != nullptr ? std::max<int64_t>(0, firstInt64Value(*networkSummary, {"udp_drop_delta", "udpDropDelta"}, 0)) : 0},
                        {"network_udp_estimated_total_bps", networkSummary != nullptr ? std::max(0.0, firstDoubleValue(*networkSummary, {"udp_estimated_total_bps", "udpEstimatedTotalBps"}, 0.0)) : 0.0},
                        {"network_latency_pressure", networkSummary != nullptr ? std::max(0.0, firstDoubleValue(*networkSummary, {"latency_pressure", "latencyPressure"}, 0.0)) : 0.0},
                        {"network_queue_pressure", networkSummary != nullptr ? std::max(0.0, firstDoubleValue(*networkSummary, {"queue_pressure", "queuePressure"}, 0.0)) : 0.0},
                        {"network_traffic_growth_pct_per_min", networkSummary != nullptr ? firstDoubleValue(*networkSummary, {"traffic_growth_pct_per_min", "trafficGrowthPctPerMin"}, 0.0) : 0.0},
                        {"network_cross_network_growth_pct_per_min", networkSummary != nullptr ? firstDoubleValue(*networkSummary, {"cross_network_growth_pct_per_min", "crossNetworkGrowthPctPerMin"}, 0.0) : 0.0},
                        {"network_flow_growth_pct_per_min", networkSummary != nullptr ? firstDoubleValue(*networkSummary, {"flow_growth_pct_per_min", "flowGrowthPctPerMin"}, 0.0) : 0.0},
                        {"forecast_status", firstStringValue(resourceForecastView, {"status"}, "disabled")},
                        {"projected_total_bps_5m", std::max(0.0, firstDoubleValue(resourceForecastView, {"projected_total_bps_5m", "projectedTotalBps5m"}, 0.0))},
                        {"projected_total_bps_15m", std::max(0.0, firstDoubleValue(resourceForecastView, {"projected_total_bps_15m", "projectedTotalBps15m"}, 0.0))},
                        {"projected_cross_network_bps_5m", std::max(0.0, firstDoubleValue(resourceForecastView, {"projected_cross_network_bps_5m", "projectedCrossNetworkBps5m"}, 0.0))},
                        {"projected_cross_network_bps_15m", std::max(0.0, firstDoubleValue(resourceForecastView, {"projected_cross_network_bps_15m", "projectedCrossNetworkBps15m"}, 0.0))},
                        {"projected_active_flows_5m", std::max(0.0, firstDoubleValue(resourceForecastView, {"projected_active_flows_5m", "projectedActiveFlows5m"}, 0.0))},
                        {"projected_active_flows_15m", std::max(0.0, firstDoubleValue(resourceForecastView, {"projected_active_flows_15m", "projectedActiveFlows15m"}, 0.0))},
                        {"estimated_cpu_cores", resourceSimulation != nullptr ? std::max(0.0, firstDoubleValue(*resourceSimulation, {"estimated_cpu_cores", "estimatedCpuCores"}, 0.0)) : 0.0},
                        {"estimated_memory_working_set_bytes", resourceSimulation != nullptr ? std::max(0.0, firstDoubleValue(*resourceSimulation, {"estimated_memory_working_set_bytes", "estimatedMemoryWorkingSetBytes"}, 0.0)) : 0.0},
                        {"estimated_network_bps", resourceSimulation != nullptr ? std::max(0.0, firstDoubleValue(*resourceSimulation, {"estimated_network_bps", "estimatedNetworkBps"}, 0.0)) : 0.0},
                        {"estimated_power_w", resourceSimulation != nullptr ? std::max(0.0, firstDoubleValue(*resourceSimulation, {"estimated_power_w", "estimatedPowerW"}, 0.0)) : 0.0},
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
        int probeWatchAgentsWithAlerts = 0;
        int probeWatchTotalAlerts = 0;
        int probeWatchHighAlertsTotal = 0;
        int probeWatchUnauthorizedControlTotal = 0;
        int probeWatchCooperativeTotal = 0;
        int probeWatchAlertedSurfacesTotal = 0;
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

            nlohmann::json probeWatchSummary = nlohmann::json::object();
            nlohmann::json tracey_guardSummary = nlohmann::json::object();
            nlohmann::json loaderThreatSummary = nlohmann::json::object();
            nlohmann::json continuumAssessmentSummary = nlohmann::json::object();
            if (agent.metrics.is_object()) {
                auto statusIt = agent.metrics.find("status_snapshot");
                if (statusIt != agent.metrics.end() && statusIt->is_object()) {
                    probeWatchSummary = extractTraceyProbeWatchSummary(*statusIt);
                    if (probeWatchSummary.is_object()) {
                        const int totalAlerts = std::max(0, probeWatchSummary.value("total_alerts", 0));
                        if (totalAlerts > 0) {
                            probeWatchAgentsWithAlerts++;
                        }
                        probeWatchTotalAlerts += totalAlerts;
                        probeWatchHighAlertsTotal += std::max(0, probeWatchSummary.value("high_severity_alerts", 0));
                        probeWatchUnauthorizedControlTotal += std::max(0, probeWatchSummary.value("unauthorized_control_alerts", 0));
                        probeWatchCooperativeTotal += std::max(0, probeWatchSummary.value("cooperative_alerts", 0));
                        probeWatchAlertedSurfacesTotal += std::max(0, probeWatchSummary.value("alerted_surface_count", 0));
                    }
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
                    {"probe_watch", probeWatchSummary},
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
                {"probe_watch_summary", {
                        {"agents_with_alerts", probeWatchAgentsWithAlerts},
                        {"total_alerts", probeWatchTotalAlerts},
                        {"high_severity_alerts", probeWatchHighAlertsTotal},
                        {"unauthorized_control_alerts", probeWatchUnauthorizedControlTotal},
                        {"cooperative_alerts", probeWatchCooperativeTotal},
                        {"alerted_surfaces", probeWatchAlertedSurfacesTotal}
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
            double networkAttributedTotalBpsSum{0.0};
            double networkCrossNetworkBpsSum{0.0};
            int64_t networkActiveFlowsSum{0};
            int64_t networkListenersSum{0};
            int64_t networkEstimatedFlowsSum{0};
            int64_t networkUdpActiveFlowsSum{0};
            int64_t networkUdpDropDeltaSum{0};
            double networkUdpEstimatedTotalBpsSum{0.0};
            double networkAttributionConfidenceSum{0.0};
            double networkLatencyPressureSum{0.0};
            double networkQueuePressureSum{0.0};
            double networkProjectedTotalBps15mSum{0.0};
            double networkProjectedCrossBps15mSum{0.0};
            double networkProjectedActiveFlows15mSum{0.0};
            int networkSamples{0};
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
                bucket.networkAttributedTotalBpsSum += std::max(0.0, sample.networkAttributedTotalBps);
                bucket.networkCrossNetworkBpsSum += std::max(0.0, sample.networkCrossNetworkBps);
                bucket.networkActiveFlowsSum += std::max(0, sample.networkActiveFlows);
                bucket.networkListenersSum += std::max(0, sample.networkListeners);
                bucket.networkEstimatedFlowsSum += std::max(0, sample.networkEstimatedFlows);
                bucket.networkUdpActiveFlowsSum += std::max(0, sample.networkUdpActiveFlows);
                bucket.networkUdpDropDeltaSum += std::max<int64_t>(0, sample.networkUdpDropDelta);
                bucket.networkUdpEstimatedTotalBpsSum += std::max(0.0, sample.networkUdpEstimatedTotalBps);
                bucket.networkAttributionConfidenceSum += std::max(0.0, sample.networkAttributionConfidence);
                bucket.networkLatencyPressureSum += std::max(0.0, sample.networkLatencyPressure);
                bucket.networkQueuePressureSum += std::max(0.0, sample.networkQueuePressure);
                bucket.networkProjectedTotalBps15mSum += std::max(0.0, sample.projectedTotalBps15m);
                bucket.networkProjectedCrossBps15mSum += std::max(0.0, sample.projectedCrossNetworkBps15m);
                bucket.networkProjectedActiveFlows15mSum += std::max(0.0, sample.projectedActiveFlows15m);
                bucket.networkSamples++;
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
            const double avgNetworkAttributionConfidence = bucket.networkSamples > 0
                                                           ? bucket.networkAttributionConfidenceSum / static_cast<double>(bucket.networkSamples)
                                                           : 0.0;
            const double avgNetworkLatencyPressure = bucket.networkSamples > 0
                                                     ? bucket.networkLatencyPressureSum / static_cast<double>(bucket.networkSamples)
                                                     : 0.0;
            const double avgNetworkQueuePressure = bucket.networkSamples > 0
                                                   ? bucket.networkQueuePressureSum / static_cast<double>(bucket.networkSamples)
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
                    {"avg_loader_threat_remote_reporters", avgLoaderThreatRemoteReporters},
                    {"network_attributed_total_bps", bucket.networkAttributedTotalBpsSum},
                    {"network_cross_network_bps", bucket.networkCrossNetworkBpsSum},
                    {"network_active_flows", bucket.networkActiveFlowsSum},
                    {"network_listeners", bucket.networkListenersSum},
                    {"network_estimated_flows", bucket.networkEstimatedFlowsSum},
                    {"network_udp_active_flows", bucket.networkUdpActiveFlowsSum},
                    {"network_udp_drop_delta", bucket.networkUdpDropDeltaSum},
                    {"network_udp_estimated_total_bps", bucket.networkUdpEstimatedTotalBpsSum},
                    {"avg_network_attribution_confidence", avgNetworkAttributionConfidence},
                    {"avg_network_latency_pressure", avgNetworkLatencyPressure},
                    {"avg_network_queue_pressure", avgNetworkQueuePressure},
                    {"network_projected_total_bps_15m", bucket.networkProjectedTotalBps15mSum},
                    {"network_projected_cross_network_bps_15m", bucket.networkProjectedCrossBps15mSum},
                    {"network_projected_active_flows_15m", bucket.networkProjectedActiveFlows15mSum}
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
            double networkAttributedTotalBpsSum = 0.0;
            double networkCrossNetworkBpsSum = 0.0;
            int64_t networkActiveFlowsSum = 0;
            int64_t networkListenersSum = 0;
            int64_t networkEstimatedFlowsSum = 0;
            int64_t networkUdpActiveFlowsSum = 0;
            int64_t networkUdpDropDeltaSum = 0;
            double networkUdpEstimatedTotalBpsSum = 0.0;
            double networkAttributionConfidenceSum = 0.0;
            double networkLatencyPressureSum = 0.0;
            double networkQueuePressureSum = 0.0;
            double projectedTotalBps15mSum = 0.0;
            double projectedCrossNetworkBps15mSum = 0.0;
            double projectedActiveFlows15mSum = 0.0;
            int networkSamples = 0;

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
                    networkAttributedTotalBpsSum += std::max(0.0, sample.networkAttributedTotalBps);
                    networkCrossNetworkBpsSum += std::max(0.0, sample.networkCrossNetworkBps);
                    networkActiveFlowsSum += std::max(0, sample.networkActiveFlows);
                    networkListenersSum += std::max(0, sample.networkListeners);
                    networkEstimatedFlowsSum += std::max(0, sample.networkEstimatedFlows);
                    networkUdpActiveFlowsSum += std::max(0, sample.networkUdpActiveFlows);
                    networkUdpDropDeltaSum += std::max<int64_t>(0, sample.networkUdpDropDelta);
                    networkUdpEstimatedTotalBpsSum += std::max(0.0, sample.networkUdpEstimatedTotalBps);
                    networkAttributionConfidenceSum += std::max(0.0, sample.networkAttributionConfidence);
                    networkLatencyPressureSum += std::max(0.0, sample.networkLatencyPressure);
                    networkQueuePressureSum += std::max(0.0, sample.networkQueuePressure);
                    projectedTotalBps15mSum += std::max(0.0, sample.projectedTotalBps15m);
                    projectedCrossNetworkBps15mSum += std::max(0.0, sample.projectedCrossNetworkBps15m);
                    projectedActiveFlows15mSum += std::max(0.0, sample.projectedActiveFlows15m);
                    networkSamples++;
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
                    {"avg_network_attributed_total_bps", networkSamples > 0 ? networkAttributedTotalBpsSum / static_cast<double>(networkSamples) : 0.0},
                    {"avg_network_cross_network_bps", networkSamples > 0 ? networkCrossNetworkBpsSum / static_cast<double>(networkSamples) : 0.0},
                    {"avg_network_active_flows", networkSamples > 0 ? static_cast<double>(networkActiveFlowsSum) / static_cast<double>(networkSamples) : 0.0},
                    {"avg_network_listeners", networkSamples > 0 ? static_cast<double>(networkListenersSum) / static_cast<double>(networkSamples) : 0.0},
                    {"avg_network_estimated_flows", networkSamples > 0 ? static_cast<double>(networkEstimatedFlowsSum) / static_cast<double>(networkSamples) : 0.0},
                    {"avg_network_udp_active_flows", networkSamples > 0 ? static_cast<double>(networkUdpActiveFlowsSum) / static_cast<double>(networkSamples) : 0.0},
                    {"avg_network_udp_drop_delta", networkSamples > 0 ? static_cast<double>(networkUdpDropDeltaSum) / static_cast<double>(networkSamples) : 0.0},
                    {"avg_network_udp_estimated_total_bps", networkSamples > 0 ? networkUdpEstimatedTotalBpsSum / static_cast<double>(networkSamples) : 0.0},
                    {"avg_network_attribution_confidence", networkSamples > 0 ? networkAttributionConfidenceSum / static_cast<double>(networkSamples) : 0.0},
                    {"avg_network_latency_pressure", networkSamples > 0 ? networkLatencyPressureSum / static_cast<double>(networkSamples) : 0.0},
                    {"avg_network_queue_pressure", networkSamples > 0 ? networkQueuePressureSum / static_cast<double>(networkSamples) : 0.0},
                    {"avg_projected_total_bps_15m", networkSamples > 0 ? projectedTotalBps15mSum / static_cast<double>(networkSamples) : 0.0},
                    {"avg_projected_cross_network_bps_15m", networkSamples > 0 ? projectedCrossNetworkBps15mSum / static_cast<double>(networkSamples) : 0.0},
                    {"avg_projected_active_flows_15m", networkSamples > 0 ? projectedActiveFlows15mSum / static_cast<double>(networkSamples) : 0.0},
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
        const int64_t nowMs = nowEpochMs();
        const TraceySimulationQuery simulationQuery = parseTraceySimulationQuery(req);
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
        apiResponse.message = simulationQuery.requested()
                              ? "Tracey fleet view and simulation forecast generated successfully."
                              : "Tracey fleet view generated successfully.";
        apiResponse.data = buildTraceyFleetViewFromAgents(agentViews, nowMs);
        apiResponse.data["resource_forecast"] = buildTraceyAggregateResourceForecastView(agentViews, nowMs);
        nlohmann::json simulationRequest;
        nlohmann::json simulationForecast;
        if (buildTraceyRequestedSimulation(
                apiResponse.data.value("resource_forecast", nlohmann::json::object()),
                simulationQuery,
                "fleet",
                simulationRequest,
                simulationForecast
        )) {
            apiResponse.data["simulation_request"] = simulationRequest;
            apiResponse.data["simulation_forecast"] = simulationForecast;
        }
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
        const TraceySimulationQuery simulationQuery = parseTraceySimulationQuery(req);
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
                    {"network", view.value("server", nlohmann::json::object()).value("network", nlohmann::json::object())},
                    {"resource_forecast", view.value("resource_forecast", nlohmann::json::object())},
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
        apiResponse.message = simulationQuery.requested()
                              ? "Tracey rack detail and simulation forecast generated successfully."
                              : "Tracey rack detail generated successfully.";
        apiResponse.data = {
                {"generated_epoch_ms", nowMs},
                {"rack", firstStringValue(rackSummary, {"rack"})},
                {"zone", firstStringValue(rackSummary, {"zone"})},
                {"summary", rackSummary},
                {"servers", servers},
                {"gpu_heatmap", rackHeatmap.value("cells", nlohmann::json::array())},
                {"recent_actions", fleet.value("recent_actions", nlohmann::json::array())},
                {"resource_forecast", buildTraceyAggregateResourceForecastView(filtered, nowMs)}
        };
        nlohmann::json simulationRequest;
        nlohmann::json simulationForecast;
        if (buildTraceyRequestedSimulation(
                apiResponse.data.value("resource_forecast", nlohmann::json::object()),
                simulationQuery,
                "rack",
                simulationRequest,
                simulationForecast
        )) {
            apiResponse.data["simulation_request"] = simulationRequest;
            apiResponse.data["simulation_forecast"] = simulationForecast;
        }
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleTraceyAgentServer(const httplib::Request& req, httplib::Response& res) {
        const std::string agentId = req.matches.size() > 1 ? trim(req.matches[1].str()) : "";
        if (agentId.empty()) {
            return sendErrorResponse(res, 400, "Missing required parameter: agent_id.");
        }
        const TraceySimulationQuery simulationQuery = parseTraceySimulationQuery(req);

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
        apiResponse.message = simulationQuery.requested()
                              ? "Tracey server telemetry and simulation forecast generated successfully."
                              : "Tracey server telemetry generated successfully.";
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
                {"resource_forecast", view.value("resource_forecast", nlohmann::json::object())},
                {"tracey_guard_summary", view.value("tracey_guard_summary", nlohmann::json::object())},
                {"loader_threats", view.value("loader_threats", nlohmann::json::object())}
        };
        nlohmann::json simulationRequest;
        nlohmann::json simulationForecast;
        if (buildTraceyRequestedSimulation(
                apiResponse.data.value("resource_forecast", nlohmann::json::object()),
                simulationQuery,
                "server",
                simulationRequest,
                simulationForecast
        )) {
            apiResponse.data["simulation_request"] = simulationRequest;
            apiResponse.data["simulation_forecast"] = simulationForecast;
        }
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleTraceyAgentGpu(const httplib::Request& req, httplib::Response& res) {
        const std::string agentId = req.matches.size() > 1 ? trim(req.matches[1].str()) : "";
        const std::string gpuId = req.matches.size() > 2 ? trim(req.matches[2].str()) : "";
        if (agentId.empty() || gpuId.empty()) {
            return sendErrorResponse(res, 400, "Missing required parameter: agent_id or gpu_id.");
        }
        const TraceySimulationQuery simulationQuery = parseTraceySimulationQuery(req);

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
        apiResponse.message = simulationQuery.requested()
                              ? "Tracey GPU telemetry and simulation forecast generated successfully."
                              : "Tracey GPU telemetry generated successfully.";
        apiResponse.data = {
                {"generated_epoch_ms", nowMs},
                {"agent_id", firstStringValue(view, {"agent_id"})},
                {"gpu_id", firstStringValue(gpuDetail, {"gpu_id", "gpuId"})},
                {"host", firstStringValue(view, {"host"})},
                {"rack", firstStringValue(view, {"rack"})},
                {"zone", firstStringValue(view, {"zone"})},
                {"identity", view.value("identity", nlohmann::json::object())},
                {"server_summary", view.value("summary", nlohmann::json::object())},
                {"network", view.value("server", nlohmann::json::object()).value("network", nlohmann::json::object())},
                {"resource_forecast", view.value("resource_forecast", nlohmann::json::object())},
                {"summary", gpuDetail},
                {"sm_heatmap", smHeatmap},
                {"probe_mix", probeMix},
                {"recent_executions", recentExecutions},
                {"recent_faults", recentFaults},
                {"remote_faults", remoteFaults},
                {"recent_actions", recentActions}
        };
        nlohmann::json simulationRequest;
        nlohmann::json simulationForecast;
        if (buildTraceyRequestedSimulation(
                apiResponse.data.value("resource_forecast", nlohmann::json::object()),
                simulationQuery,
                "gpu",
                simulationRequest,
                simulationForecast
        )) {
            apiResponse.data["simulation_request"] = simulationRequest;
            apiResponse.data["simulation_forecast"] = simulationForecast;
            apiResponse.data["gpu_simulation_forecast"] = buildTraceyGpuSimulationForecast(
                    gpuDetail,
                    apiResponse.data.value("resource_forecast", nlohmann::json::object()),
                    simulationForecast
            );
        }
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
            int64_t probeWatchObservationSum{0};
            int64_t probeWatchAlertSum{0};
            int64_t probeWatchDistinctSourceSum{0};
            int64_t probeWatchRecentAlertSum{0};
            int64_t probeWatchHighAlertSum{0};
            int64_t probeWatchMediumAlertSum{0};
            int64_t probeWatchUnauthorizedControlSum{0};
            int64_t probeWatchCooperativeSum{0};
            int64_t probeWatchAlertedSurfaceSum{0};
            int64_t tracey_guardFailureSum{0};
            int64_t tracey_guardErrorSum{0};
            int64_t tracey_guardQuarantineSum{0};
            int64_t tracey_guardRemoteFaultSum{0};
            int64_t loaderThreatProviderSum{0};
            int64_t loaderThreatArtifactSum{0};
            int64_t loaderThreatBlockedProviderSum{0};
            int64_t loaderThreatBlockedArtifactSum{0};
            int64_t loaderThreatRemoteReporterSum{0};
            double networkAttributedTotalBpsSum{0.0};
            double networkCrossNetworkBpsSum{0.0};
            int64_t networkActiveFlowsSum{0};
            int64_t networkListenersSum{0};
            int64_t networkEstimatedFlowsSum{0};
            int64_t networkUdpActiveFlowsSum{0};
            int64_t networkUdpDropDeltaSum{0};
            double networkUdpEstimatedTotalBpsSum{0.0};
            double networkAttributionConfidenceSum{0.0};
            double networkLatencyPressureSum{0.0};
            double networkQueuePressureSum{0.0};
            double projectedTotalBps15mSum{0.0};
            double projectedCrossNetworkBps15mSum{0.0};
            double projectedActiveFlows15mSum{0.0};
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
            bucket.probeWatchObservationSum += std::max(0, sample.probeWatchTotalObservations);
            bucket.probeWatchAlertSum += std::max(0, sample.probeWatchTotalAlerts);
            bucket.probeWatchDistinctSourceSum += std::max(0, sample.probeWatchDistinctSources);
            bucket.probeWatchRecentAlertSum += std::max(0, sample.probeWatchRecentAlerts);
            bucket.probeWatchHighAlertSum += std::max(0, sample.probeWatchHighAlerts);
            bucket.probeWatchMediumAlertSum += std::max(0, sample.probeWatchMediumAlerts);
            bucket.probeWatchUnauthorizedControlSum += std::max(0, sample.probeWatchUnauthorizedControlAlerts);
            bucket.probeWatchCooperativeSum += std::max(0, sample.probeWatchCooperativeAlerts);
            bucket.probeWatchAlertedSurfaceSum += std::max(0, sample.probeWatchAlertedSurfaces);
            bucket.tracey_guardFailureSum += std::max(0, sample.tracey_guardProbeFailures);
            bucket.tracey_guardErrorSum += std::max(0, sample.tracey_guardProbeErrors);
            bucket.tracey_guardQuarantineSum += std::max(0, sample.tracey_guardQuarantined);
            bucket.tracey_guardRemoteFaultSum += std::max(0, sample.tracey_guardRemoteFaults);
            bucket.loaderThreatProviderSum += std::max(0, sample.loaderThreatLocalProviders);
            bucket.loaderThreatArtifactSum += std::max(0, sample.loaderThreatLocalArtifacts);
            bucket.loaderThreatBlockedProviderSum += std::max(0, sample.loaderThreatBlockedProviders);
            bucket.loaderThreatBlockedArtifactSum += std::max(0, sample.loaderThreatBlockedArtifacts);
            bucket.loaderThreatRemoteReporterSum += std::max(0, sample.loaderThreatRemoteReporters);
            bucket.networkAttributedTotalBpsSum += std::max(0.0, sample.networkAttributedTotalBps);
            bucket.networkCrossNetworkBpsSum += std::max(0.0, sample.networkCrossNetworkBps);
            bucket.networkActiveFlowsSum += std::max(0, sample.networkActiveFlows);
            bucket.networkListenersSum += std::max(0, sample.networkListeners);
            bucket.networkEstimatedFlowsSum += std::max(0, sample.networkEstimatedFlows);
            bucket.networkUdpActiveFlowsSum += std::max(0, sample.networkUdpActiveFlows);
            bucket.networkUdpDropDeltaSum += std::max<int64_t>(0, sample.networkUdpDropDelta);
            bucket.networkUdpEstimatedTotalBpsSum += std::max(0.0, sample.networkUdpEstimatedTotalBps);
            bucket.networkAttributionConfidenceSum += std::max(0.0, sample.networkAttributionConfidence);
            bucket.networkLatencyPressureSum += std::max(0.0, sample.networkLatencyPressure);
            bucket.networkQueuePressureSum += std::max(0.0, sample.networkQueuePressure);
            bucket.projectedTotalBps15mSum += std::max(0.0, sample.projectedTotalBps15m);
            bucket.projectedCrossNetworkBps15mSum += std::max(0.0, sample.projectedCrossNetworkBps15m);
            bucket.projectedActiveFlows15mSum += std::max(0.0, sample.projectedActiveFlows15m);
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
            const double avgProbeWatchObservations = bucket.sampleCount > 0
                                                     ? static_cast<double>(bucket.probeWatchObservationSum) / static_cast<double>(bucket.sampleCount)
                                                     : 0.0;
            const double avgProbeWatchAlerts = bucket.sampleCount > 0
                                               ? static_cast<double>(bucket.probeWatchAlertSum) / static_cast<double>(bucket.sampleCount)
                                               : 0.0;
            const double avgProbeWatchDistinctSources = bucket.sampleCount > 0
                                                        ? static_cast<double>(bucket.probeWatchDistinctSourceSum) / static_cast<double>(bucket.sampleCount)
                                                        : 0.0;
            const double avgProbeWatchRecentAlerts = bucket.sampleCount > 0
                                                     ? static_cast<double>(bucket.probeWatchRecentAlertSum) / static_cast<double>(bucket.sampleCount)
                                                     : 0.0;
            const double avgProbeWatchHighAlerts = bucket.sampleCount > 0
                                                   ? static_cast<double>(bucket.probeWatchHighAlertSum) / static_cast<double>(bucket.sampleCount)
                                                   : 0.0;
            const double avgProbeWatchMediumAlerts = bucket.sampleCount > 0
                                                     ? static_cast<double>(bucket.probeWatchMediumAlertSum) / static_cast<double>(bucket.sampleCount)
                                                     : 0.0;
            const double avgProbeWatchUnauthorized = bucket.sampleCount > 0
                                                     ? static_cast<double>(bucket.probeWatchUnauthorizedControlSum) / static_cast<double>(bucket.sampleCount)
                                                     : 0.0;
            const double avgProbeWatchCooperative = bucket.sampleCount > 0
                                                    ? static_cast<double>(bucket.probeWatchCooperativeSum) / static_cast<double>(bucket.sampleCount)
                                                    : 0.0;
            const double avgProbeWatchAlertedSurfaces = bucket.sampleCount > 0
                                                        ? static_cast<double>(bucket.probeWatchAlertedSurfaceSum) / static_cast<double>(bucket.sampleCount)
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
            const double avgNetworkAttributedTotalBps = bucket.sampleCount > 0
                                                        ? bucket.networkAttributedTotalBpsSum / static_cast<double>(bucket.sampleCount)
                                                        : 0.0;
            const double avgNetworkCrossNetworkBps = bucket.sampleCount > 0
                                                     ? bucket.networkCrossNetworkBpsSum / static_cast<double>(bucket.sampleCount)
                                                     : 0.0;
            const double avgNetworkActiveFlows = bucket.sampleCount > 0
                                                 ? static_cast<double>(bucket.networkActiveFlowsSum) / static_cast<double>(bucket.sampleCount)
                                                 : 0.0;
            const double avgNetworkListeners = bucket.sampleCount > 0
                                               ? static_cast<double>(bucket.networkListenersSum) / static_cast<double>(bucket.sampleCount)
                                               : 0.0;
            const double avgNetworkEstimatedFlows = bucket.sampleCount > 0
                                                    ? static_cast<double>(bucket.networkEstimatedFlowsSum) / static_cast<double>(bucket.sampleCount)
                                                    : 0.0;
            const double avgNetworkUdpActiveFlows = bucket.sampleCount > 0
                                                    ? static_cast<double>(bucket.networkUdpActiveFlowsSum) / static_cast<double>(bucket.sampleCount)
                                                    : 0.0;
            const double avgNetworkUdpDropDelta = bucket.sampleCount > 0
                                                  ? static_cast<double>(bucket.networkUdpDropDeltaSum) / static_cast<double>(bucket.sampleCount)
                                                  : 0.0;
            const double avgNetworkUdpEstimatedTotalBps = bucket.sampleCount > 0
                                                          ? bucket.networkUdpEstimatedTotalBpsSum / static_cast<double>(bucket.sampleCount)
                                                          : 0.0;
            const double avgNetworkAttributionConfidence = bucket.sampleCount > 0
                                                           ? bucket.networkAttributionConfidenceSum / static_cast<double>(bucket.sampleCount)
                                                           : 0.0;
            const double avgNetworkLatencyPressure = bucket.sampleCount > 0
                                                     ? bucket.networkLatencyPressureSum / static_cast<double>(bucket.sampleCount)
                                                     : 0.0;
            const double avgNetworkQueuePressure = bucket.sampleCount > 0
                                                   ? bucket.networkQueuePressureSum / static_cast<double>(bucket.sampleCount)
                                                   : 0.0;
            const double avgProjectedTotalBps15m = bucket.sampleCount > 0
                                                   ? bucket.projectedTotalBps15mSum / static_cast<double>(bucket.sampleCount)
                                                   : 0.0;
            const double avgProjectedCrossNetworkBps15m = bucket.sampleCount > 0
                                                          ? bucket.projectedCrossNetworkBps15mSum / static_cast<double>(bucket.sampleCount)
                                                          : 0.0;
            const double avgProjectedActiveFlows15m = bucket.sampleCount > 0
                                                      ? bucket.projectedActiveFlows15mSum / static_cast<double>(bucket.sampleCount)
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
                    {"avg_probe_watch_observations", avgProbeWatchObservations},
                    {"avg_probe_watch_alerts", avgProbeWatchAlerts},
                    {"avg_probe_watch_distinct_sources", avgProbeWatchDistinctSources},
                    {"avg_probe_watch_recent_alerts", avgProbeWatchRecentAlerts},
                    {"avg_probe_watch_high_alerts", avgProbeWatchHighAlerts},
                    {"avg_probe_watch_medium_alerts", avgProbeWatchMediumAlerts},
                    {"avg_probe_watch_unauthorized_control_alerts", avgProbeWatchUnauthorized},
                    {"avg_probe_watch_cooperative_alerts", avgProbeWatchCooperative},
                    {"avg_probe_watch_alerted_surfaces", avgProbeWatchAlertedSurfaces},
                    {"avg_tracey_guard_failures", avgTraceyGuardFailures},
                    {"avg_tracey_guard_errors", avgTraceyGuardErrors},
                    {"avg_tracey_guard_quarantined", avgTraceyGuardQuarantine},
                    {"avg_tracey_guard_remote_faults", avgTraceyGuardRemoteFaults},
                    {"avg_loader_threat_providers", avgLoaderThreatProviders},
                    {"avg_loader_threat_artifacts", avgLoaderThreatArtifacts},
                    {"avg_loader_threat_blocked_providers", avgLoaderThreatBlockedProviders},
                    {"avg_loader_threat_blocked_artifacts", avgLoaderThreatBlockedArtifacts},
                    {"avg_loader_threat_remote_reporters", avgLoaderThreatRemoteReporters},
                    {"avg_network_attributed_total_bps", avgNetworkAttributedTotalBps},
                    {"avg_network_cross_network_bps", avgNetworkCrossNetworkBps},
                    {"avg_network_active_flows", avgNetworkActiveFlows},
                    {"avg_network_listeners", avgNetworkListeners},
                    {"avg_network_estimated_flows", avgNetworkEstimatedFlows},
                    {"avg_network_udp_active_flows", avgNetworkUdpActiveFlows},
                    {"avg_network_udp_drop_delta", avgNetworkUdpDropDelta},
                    {"avg_network_udp_estimated_total_bps", avgNetworkUdpEstimatedTotalBps},
                    {"avg_network_attribution_confidence", avgNetworkAttributionConfidence},
                    {"avg_network_latency_pressure", avgNetworkLatencyPressure},
                    {"avg_network_queue_pressure", avgNetworkQueuePressure},
                    {"avg_projected_total_bps_15m", avgProjectedTotalBps15m},
                    {"avg_projected_cross_network_bps_15m", avgProjectedCrossNetworkBps15m},
                    {"avg_projected_active_flows_15m", avgProjectedActiveFlows15m}
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
        nlohmann::json probeWatchSummary = nlohmann::json::object();
        nlohmann::json tracey_guardSummary = nlohmann::json::object();
        nlohmann::json loaderThreatSummary = nlohmann::json::object();
        if (agent.metrics.is_object()) {
            auto statusIt = agent.metrics.find("status_snapshot");
            if (statusIt != agent.metrics.end() && statusIt->is_object()) {
                probeWatchSummary = extractTraceyProbeWatchSummary(*statusIt);
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
                        {"probe_watch", probeWatchSummary},
                        {"tracey_guard", tracey_guardSummary},
                        {"loader_threats", loaderThreatSummary}
                }},
                {"security_summary", {
                        {"probe_watch", probeWatchSummary},
                        {"security_log_count", static_cast<int64_t>(std::count_if(
                                logs.begin(),
                                logs.end(),
                                [](const TraceyAgentLogEntry& entry) {
                                    return toLower(entry.category) == "security";
                                }
                        ))}
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
            scheduleTraceyStateSnapshotLocked(nowEpochMs());
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
            scheduleTraceyStateSnapshotLocked(nowEpochMs());
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


} // namespace NMC::Server
