// APIRoutes_ReleaseOperateExecution.cpp
// Release/operate implementation for VM lifecycle workflows and node recruitment execution.

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

// Legacy VM lifecycle handlers are explicitly disabled; use the scoped provider API.
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
