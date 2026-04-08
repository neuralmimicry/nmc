#include "TraceyCommands.h"

#include <iostream>
#include <cctype>
#include <nlohmann/json.hpp>

namespace NMC::Commands {

namespace {

std::string normalizeAdaptivePolicyValue(std::string value) {
    for (auto& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (value == "balanced" || value == "throughput" || value == "risk" || value == "energy") {
        return value;
    }
    return "";
}

// Parse optional integer flag that must be positive when provided.
bool parseOptionalPositiveInt(const std::map<std::string, std::string>& parsedFlags,
                              const std::string& flagName,
                              int& valueOut,
                              std::string& errorOut) {
    valueOut = -1;
    const auto it = parsedFlags.find(flagName);
    if (it == parsedFlags.end() || it->second.empty()) {
        return true;
    }
    try {
        valueOut = std::stoi(it->second);
    } catch (const std::exception&) {
        errorOut = "--" + flagName + " must be an integer.";
        return false;
    }
    if (valueOut <= 0) {
        errorOut = "--" + flagName + " must be greater than zero.";
        return false;
    }
    return true;
}

// Parse optional integer flag (accepts any integer, including zero/negative).
bool parseOptionalInt(const std::map<std::string, std::string>& parsedFlags,
                      const std::string& flagName,
                      int& valueOut,
                      std::string& errorOut) {
    valueOut = 0;
    const auto it = parsedFlags.find(flagName);
    if (it == parsedFlags.end() || it->second.empty()) {
        return false;
    }
    try {
        valueOut = std::stoi(it->second);
    } catch (const std::exception&) {
        errorOut = "--" + flagName + " must be an integer.";
        return false;
    }
    return true;
}

// Parse optional JSON flag and optionally enforce object type for payload safety.
bool parseJsonFlag(const std::map<std::string, std::string>& parsedFlags,
                   const std::string& flagName,
                   nlohmann::json& valueOut,
                   std::string& errorOut,
                   bool requireObject) {
    const auto it = parsedFlags.find(flagName);
    if (it == parsedFlags.end() || it->second.empty()) {
        return false;
    }
    try {
        valueOut = nlohmann::json::parse(it->second);
    } catch (const std::exception& e) {
        errorOut = "Invalid JSON in --" + flagName + ": " + std::string(e.what());
        return false;
    }
    if (requireObject && !valueOut.is_object()) {
        errorOut = "--" + flagName + " must be a JSON object.";
        return false;
    }
    return true;
}

bool parseAdaptivePolicyFlag(const std::map<std::string, std::string>& parsedFlags,
                             std::string& valueOut,
                             std::string& errorOut) {
    valueOut.clear();
    const auto it = parsedFlags.find("policy");
    if (it == parsedFlags.end() || it->second.empty()) {
        return true;
    }
    valueOut = normalizeAdaptivePolicyValue(it->second);
    if (!valueOut.empty()) {
        return true;
    }
    errorOut = "--policy must be one of: balanced, throughput, risk, energy.";
    return false;
}

} // namespace

TraceyCommand::TraceyCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("tracey", "Inspect and control Tracey agents", std::move(client)) {
    usage = "nmc tracey [command]";
}

int TraceyCommand::execute(const std::map<std::string, std::string>&,
                           const std::vector<std::string>&,
                           const CLI::GlobalFlags&) {
    printHelp();
    return 0;
}

TraceyHeartbeatCommand::TraceyHeartbeatCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("heartbeat", "Submit a Tracey heartbeat payload", std::move(client)) {
    usage = "nmc tracey heartbeat --agent-id AGENT_ID [--status healthy] [--cluster CLUSTER]";
    examples = "nmc tracey heartbeat --agent-id tracey-1 --status healthy --status-addr http://10.0.0.2:8081";
    addFlag(CLI::Flag("p", "payload", "Raw JSON payload (object)", CLI::FlagType::String, false));
    addFlag(CLI::Flag("a", "agent-id", "Agent identifier", CLI::FlagType::String, false));
    addFlag(CLI::Flag("c", "cluster", "Cluster identifier", CLI::FlagType::String, false));
    addFlag(CLI::Flag("s", "status", "Agent status", CLI::FlagType::String, false));
    addFlag(CLI::Flag("v", "version", "Agent version", CLI::FlagType::String, false));
    addFlag(CLI::Flag("H", "host", "Agent host", CLI::FlagType::String, false));
    addFlag(CLI::Flag("u", "status-addr", "Agent status URL", CLI::FlagType::String, false));
    addFlag(CLI::Flag("m", "metrics", "Metrics JSON payload", CLI::FlagType::String, false));
    addFlag(CLI::Flag("k", "capabilities", "Capabilities JSON payload", CLI::FlagType::String, false));
    addFlag(CLI::Flag("C", "is-coordinator", "Set is_coordinator=true", CLI::FlagType::Bool, false));
    addFlag(CLI::Flag("e", "coordinator-epoch", "Coordinator epoch", CLI::FlagType::Int, false));
    addFlag(CLI::Flag("S", "score", "Agent score", CLI::FlagType::Int, false));
}

int TraceyHeartbeatCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                    const std::vector<std::string>& parsedArgs,
                                    const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    nlohmann::json payload = nlohmann::json::object();
    std::string parseError;

    nlohmann::json jsonFlag;
    if (parseJsonFlag(parsedFlags, "payload", jsonFlag, parseError, true)) {
        // Seed with raw payload first, then let explicit flags override fields.
        payload = jsonFlag;
    } else if (!parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        printOutput(response, globalFlags);
        return 1;
    }

    auto copyStringFlag = [&](const std::string& flagName, const std::string& fieldName) {
        auto it = parsedFlags.find(flagName);
        if (it != parsedFlags.end() && !it->second.empty()) {
            payload[fieldName] = it->second;
        }
    };

    copyStringFlag("agent-id", "agent_id");
    copyStringFlag("cluster", "cluster");
    copyStringFlag("status", "status");
    copyStringFlag("version", "version");
    copyStringFlag("host", "host");
    copyStringFlag("status-addr", "status_addr");

    if (!payload.contains("agent_id") || !payload["agent_id"].is_string() || payload["agent_id"].get<std::string>().empty()) {
        Models::CloudResponse response{false, "Tracey heartbeat requires agent_id (--agent-id or in --payload).", nlohmann::json::object(), 400};
        printOutput(response, globalFlags);
        return 1;
    }

    nlohmann::json metrics;
    if (parseJsonFlag(parsedFlags, "metrics", metrics, parseError, false)) {
        payload["metrics"] = metrics;
    } else if (!parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        printOutput(response, globalFlags);
        return 1;
    }

    nlohmann::json capabilities;
    if (parseJsonFlag(parsedFlags, "capabilities", capabilities, parseError, false)) {
        payload["capabilities"] = capabilities;
    } else if (!parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        printOutput(response, globalFlags);
        return 1;
    }

    if (parsedFlags.count("is-coordinator")) {
        payload["is_coordinator"] = true;
    }

    int coordinatorEpoch = 0;
    bool hasCoordinatorEpoch = parseOptionalInt(parsedFlags, "coordinator-epoch", coordinatorEpoch, parseError);
    if (!parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        printOutput(response, globalFlags);
        return 1;
    }
    if (hasCoordinatorEpoch) {
        payload["coordinator_epoch"] = coordinatorEpoch;
    }

    int score = 0;
    bool hasScore = parseOptionalInt(parsedFlags, "score", score, parseError);
    if (!parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        printOutput(response, globalFlags);
        return 1;
    }
    if (hasScore) {
        payload["score"] = score;
    }

    Models::CloudResponse response = apiClient->traceyHeartbeat(payload);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

TraceyAgentsCommand::TraceyAgentsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("agents", "List Tracey agents and compliance summary", std::move(client)) {
    usage = "nmc tracey agents";
    examples = "nmc tracey agents";
}

int TraceyAgentsCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                 const std::vector<std::string>& parsedArgs,
                                 const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    Models::CloudResponse response = apiClient->listTraceyAgents();
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

TraceyAnalyticsCommand::TraceyAnalyticsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("analytics", "Fetch Tracey fleet analytics", std::move(client)) {
    usage = "nmc tracey analytics [--window-seconds 3600] [--bucket-seconds 60] [--log-limit 200]";
    examples = "nmc tracey analytics --window-seconds 7200 --bucket-seconds 120";
    addFlag(CLI::Flag("w", "window-seconds", "Analytics window in seconds", CLI::FlagType::Int, false));
    addFlag(CLI::Flag("b", "bucket-seconds", "Bucket size in seconds", CLI::FlagType::Int, false));
    addFlag(CLI::Flag("l", "log-limit", "Maximum logs to include", CLI::FlagType::Int, false));
}

int TraceyAnalyticsCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                    const std::vector<std::string>& parsedArgs,
                                    const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    int windowSeconds = -1;
    int bucketSeconds = -1;
    int logLimit = -1;
    std::string parseError;
    if (!parseOptionalPositiveInt(parsedFlags, "window-seconds", windowSeconds, parseError) ||
        !parseOptionalPositiveInt(parsedFlags, "bucket-seconds", bucketSeconds, parseError) ||
        !parseOptionalPositiveInt(parsedFlags, "log-limit", logLimit, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        printOutput(response, globalFlags);
        return 1;
    }

    Models::CloudResponse response = apiClient->getTraceyAnalytics(windowSeconds, bucketSeconds, logLimit);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

TraceyFleetCommand::TraceyFleetCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("fleet", "Fetch Tracey fleet-wide topology and telemetry view", std::move(client)) {
    usage = "nmc tracey fleet";
    examples = "nmc tracey fleet";
}

int TraceyFleetCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                const std::vector<std::string>& parsedArgs,
                                const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    Models::CloudResponse response = apiClient->getTraceyFleet();
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

TraceyAdaptiveCommand::TraceyAdaptiveCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("adaptive", "Fetch the integrated plan/ramp/optimize/repeat Tracey loop", std::move(client)) {
    usage = "nmc tracey adaptive [--policy balanced|throughput|risk|energy]";
    examples = "nmc tracey adaptive --policy risk";
    addFlag(CLI::Flag("p", "policy", "Placement policy: balanced, throughput, risk, energy", CLI::FlagType::String, false));
}

int TraceyAdaptiveCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                   const std::vector<std::string>& parsedArgs,
                                   const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    std::string policy;
    std::string parseError;
    if (!parseAdaptivePolicyFlag(parsedFlags, policy, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        printOutput(response, globalFlags);
        return 1;
    }

    Models::CloudResponse response = apiClient->getTraceyAdaptive(policy);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

TraceyCveCommand::TraceyCveCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("cve", "Fetch Tracey CVE mirror status from Continuum", std::move(client)) {
    usage = "nmc tracey cve";
    examples = "nmc tracey cve";
}

int TraceyCveCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                              const std::vector<std::string>& parsedArgs,
                              const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    Models::CloudResponse response = apiClient->getTraceyCveStatus();
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

TraceyAssessmentCommand::TraceyAssessmentCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("assessment", "Fetch fleet-wide Tracey compromise assessment status", std::move(client)) {
    usage = "nmc tracey assessment";
    examples = "nmc tracey assessment";
}

int TraceyAssessmentCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                     const std::vector<std::string>& parsedArgs,
                                     const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    Models::CloudResponse response = apiClient->getTraceyAssessmentFleet();
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

TraceyAssessmentPlanCommand::TraceyAssessmentPlanCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("assessment-plan", "Fetch the server-generated Tracey assessment plan for one agent", std::move(client)) {
    usage = "nmc tracey assessment-plan AGENT_ID";
    examples = "nmc tracey assessment-plan tracey-1";
    addArgument(CLI::Argument("AGENT_ID", "Tracey agent ID", true, 0));
}

int TraceyAssessmentPlanCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                         const std::vector<std::string>& parsedArgs,
                                         const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    Models::CloudResponse response = apiClient->getTraceyAssessmentPlan(parsedArgs[0]);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

TraceyAssessmentReportCommand::TraceyAssessmentReportCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("assessment-report", "Submit a Tracey assessment report payload for one agent", std::move(client)) {
    usage = "nmc tracey assessment-report AGENT_ID --payload JSON";
    examples = R"(nmc tracey assessment-report tracey-1 --payload '{"slice_id":0,"result":"ok"}')";
    addArgument(CLI::Argument("AGENT_ID", "Tracey agent ID", true, 0));
    addFlag(CLI::Flag("p", "payload", "Assessment report JSON payload (object)", CLI::FlagType::String, true));
}

int TraceyAssessmentReportCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                           const std::vector<std::string>& parsedArgs,
                                           const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    std::string parseError;
    nlohmann::json payload;
    if (!parseJsonFlag(parsedFlags, "payload", payload, parseError, true)) {
        if (parseError.empty()) {
            parseError = "Tracey assessment report requires --payload JSON object.";
        }
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        printOutput(response, globalFlags);
        return 1;
    }

    Models::CloudResponse response = apiClient->submitTraceyAssessmentReport(parsedArgs[0], payload);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

TraceyRacksCommand::TraceyRacksCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("racks", "List Tracey rack-level summaries", std::move(client)) {
    usage = "nmc tracey racks";
    examples = "nmc tracey racks";
}

int TraceyRacksCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                const std::vector<std::string>& parsedArgs,
                                const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    Models::CloudResponse response = apiClient->listTraceyRacks();
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

TraceyRackCommand::TraceyRackCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("rack", "Fetch Tracey telemetry for a single rack", std::move(client)) {
    usage = "nmc tracey rack RACK_ID";
    examples = "nmc tracey rack R09";
    addArgument(CLI::Argument("RACK_ID", "Rack identifier", true, 0));
}

int TraceyRackCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                               const std::vector<std::string>& parsedArgs,
                               const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    Models::CloudResponse response = apiClient->getTraceyRackDetails(parsedArgs[0]);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

TraceyAnalysisCommand::TraceyAnalysisCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("analysis", "Fetch analytics for a single Tracey agent", std::move(client)) {
    usage = "nmc tracey analysis AGENT_ID [--window-seconds 3600] [--bucket-seconds 60] [--log-limit 300]";
    examples = "nmc tracey analysis tracey-1 --window-seconds 7200";
    addArgument(CLI::Argument("AGENT_ID", "Tracey agent ID", true, 0));
    addFlag(CLI::Flag("w", "window-seconds", "Analysis window in seconds", CLI::FlagType::Int, false));
    addFlag(CLI::Flag("b", "bucket-seconds", "Bucket size in seconds", CLI::FlagType::Int, false));
    addFlag(CLI::Flag("l", "log-limit", "Maximum logs to include", CLI::FlagType::Int, false));
}

int TraceyAnalysisCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                   const std::vector<std::string>& parsedArgs,
                                   const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& agentId = parsedArgs[0];
    int windowSeconds = -1;
    int bucketSeconds = -1;
    int logLimit = -1;
    std::string parseError;
    if (!parseOptionalPositiveInt(parsedFlags, "window-seconds", windowSeconds, parseError) ||
        !parseOptionalPositiveInt(parsedFlags, "bucket-seconds", bucketSeconds, parseError) ||
        !parseOptionalPositiveInt(parsedFlags, "log-limit", logLimit, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        printOutput(response, globalFlags);
        return 1;
    }

    Models::CloudResponse response = apiClient->getTraceyAgentAnalysis(agentId, windowSeconds, bucketSeconds, logLimit);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

TraceyServerCommand::TraceyServerCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("server", "Fetch full server telemetry for a Tracey agent", std::move(client)) {
    usage = "nmc tracey server AGENT_ID";
    examples = "nmc tracey server tracey-1";
    addArgument(CLI::Argument("AGENT_ID", "Tracey agent ID", true, 0));
}

int TraceyServerCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                 const std::vector<std::string>& parsedArgs,
                                 const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    Models::CloudResponse response = apiClient->getTraceyAgentServer(parsedArgs[0]);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

TraceyGpuCommand::TraceyGpuCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("gpu", "Fetch deep telemetry for a single GPU on a Tracey agent", std::move(client)) {
    usage = "nmc tracey gpu AGENT_ID GPU_ID";
    examples = "nmc tracey gpu tracey-1 nvidia:0";
    addArgument(CLI::Argument("AGENT_ID", "Tracey agent ID", true, 0));
    addArgument(CLI::Argument("GPU_ID", "GPU identifier", true, 1));
}

int TraceyGpuCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                              const std::vector<std::string>& parsedArgs,
                              const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    Models::CloudResponse response = apiClient->getTraceyAgentGpu(parsedArgs[0], parsedArgs[1]);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

TraceyCompromiseCommand::TraceyCompromiseCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("compromise", "Fetch compromise assessment for a single Tracey agent", std::move(client)) {
    usage = "nmc tracey compromise AGENT_ID";
    examples = "nmc tracey compromise tracey-1";
    addArgument(CLI::Argument("AGENT_ID", "Tracey agent ID", true, 0));
}

int TraceyCompromiseCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                     const std::vector<std::string>& parsedArgs,
                                     const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    Models::CloudResponse response = apiClient->getTraceyAgentCompromise(parsedArgs[0]);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

TraceyControlCommand::TraceyControlCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("control", "Forward control payload to a Tracey agent", std::move(client)) {
    usage = "nmc tracey control AGENT_ID [--action ACTION] [--reason TEXT] [--payload JSON]";
    examples = "nmc tracey control tracey-1 --action clear_quarantine --reason maintenance";
    addArgument(CLI::Argument("AGENT_ID", "Tracey agent ID", true, 0));
    addFlag(CLI::Flag("p", "payload", "Raw JSON control payload (object)", CLI::FlagType::String, false));
    addFlag(CLI::Flag("a", "action", "Control action to include in payload", CLI::FlagType::String, false));
    addFlag(CLI::Flag("r", "reason", "Control reason to include in payload", CLI::FlagType::String, false));
}

int TraceyControlCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                  const std::vector<std::string>& parsedArgs,
                                  const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& agentId = parsedArgs[0];
    nlohmann::json payload = nlohmann::json::object();
    std::string parseError;
    nlohmann::json parsedPayload;
    if (parseJsonFlag(parsedFlags, "payload", parsedPayload, parseError, true)) {
        payload = parsedPayload;
    } else if (!parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        printOutput(response, globalFlags);
        return 1;
    }

    const auto itAction = parsedFlags.find("action");
    if (itAction != parsedFlags.end() && !itAction->second.empty()) {
        payload["action"] = itAction->second;
    }
    const auto itReason = parsedFlags.find("reason");
    if (itReason != parsedFlags.end() && !itReason->second.empty()) {
        payload["reason"] = itReason->second;
    }

    Models::CloudResponse response = apiClient->controlTraceyAgent(agentId, payload);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

TraceyDeepDiveCommand::TraceyDeepDiveCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("deepdive", "Fetch deep-dive diagnostics from a Tracey agent", std::move(client)) {
    usage = "nmc tracey deepdive AGENT_ID";
    examples = "nmc tracey deepdive tracey-1";
    addArgument(CLI::Argument("AGENT_ID", "Tracey agent ID", true, 0));
}

int TraceyDeepDiveCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                   const std::vector<std::string>& parsedArgs,
                                   const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& agentId = parsedArgs[0];
    Models::CloudResponse response = apiClient->getTraceyAgentDeepDive(agentId);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

} // namespace NMC::Commands
