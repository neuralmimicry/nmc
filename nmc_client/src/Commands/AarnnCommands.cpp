#include "AarnnCommands.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace NMC::Commands {

namespace {

std::string trimCopy(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string toLowerCopy(std::string value) {
    for (auto& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string toUpperCopy(std::string value) {
    for (auto& ch : value) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string optionalFlag(const std::map<std::string, std::string>& parsedFlags,
                         const std::string& flagName,
                         const std::string& fallback = "") {
    const auto it = parsedFlags.find(flagName);
    if (it == parsedFlags.end()) {
        return fallback;
    }
    return it->second;
}

bool hasFlag(const std::map<std::string, std::string>& parsedFlags, const std::string& flagName) {
    return parsedFlags.find(flagName) != parsedFlags.end();
}

bool readTextFile(const std::string& path, std::string& contentOut, std::string& errorOut) {
    contentOut.clear();
    errorOut.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        errorOut = "Unable to open file: " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    contentOut = buffer.str();
    return true;
}

bool parseJsonText(const std::string& text, std::string& normalizedOut, std::string& errorOut) {
    normalizedOut.clear();
    errorOut.clear();
    const auto parsed = nlohmann::json::parse(text, nullptr, false);
    if (parsed.is_discarded()) {
        errorOut = "Invalid JSON payload.";
        return false;
    }
    normalizedOut = parsed.dump();
    return true;
}

bool parseJsonFlag(const std::map<std::string, std::string>& parsedFlags,
                   const std::string& flagName,
                   nlohmann::json& valueOut,
                   std::string& errorOut,
                   bool requireObject) {
    const auto it = parsedFlags.find(flagName);
    if (it == parsedFlags.end() || it->second.empty()) {
        return false;
    }
    const auto parsed = nlohmann::json::parse(it->second, nullptr, false);
    if (parsed.is_discarded()) {
        errorOut = "Invalid JSON in --" + flagName + ".";
        return false;
    }
    if (requireObject && !parsed.is_object()) {
        errorOut = "--" + flagName + " must be a JSON object.";
        return false;
    }
    valueOut = parsed;
    return true;
}

bool parseJsonFlagOrFile(const std::map<std::string, std::string>& parsedFlags,
                         const std::string& jsonFlag,
                         const std::string& fileFlag,
                         nlohmann::json& valueOut,
                         std::string& errorOut,
                         bool requireObject) {
    const bool hasJson = hasFlag(parsedFlags, jsonFlag);
    const bool hasFile = hasFlag(parsedFlags, fileFlag);
    if (hasJson && hasFile) {
        errorOut = "Use either --" + jsonFlag + " or --" + fileFlag + ", not both.";
        return false;
    }
    if (hasJson) {
        return parseJsonFlag(parsedFlags, jsonFlag, valueOut, errorOut, requireObject);
    }
    if (!hasFile) {
        return false;
    }

    std::string raw;
    if (!readTextFile(optionalFlag(parsedFlags, fileFlag), raw, errorOut)) {
        return false;
    }
    const auto parsed = nlohmann::json::parse(raw, nullptr, false);
    if (parsed.is_discarded()) {
        errorOut = "Invalid JSON in --" + fileFlag + ".";
        return false;
    }
    if (requireObject && !parsed.is_object()) {
        errorOut = "--" + fileFlag + " must contain a JSON object.";
        return false;
    }
    valueOut = parsed;
    return true;
}

bool loadJsonStringValue(const std::map<std::string, std::string>& parsedFlags,
                         const std::string& jsonFlag,
                         const std::string& fileFlag,
                         std::string& valueOut,
                         std::string& errorOut) {
    valueOut.clear();
    errorOut.clear();
    const bool hasJson = hasFlag(parsedFlags, jsonFlag);
    const bool hasFile = hasFlag(parsedFlags, fileFlag);
    if (hasJson && hasFile) {
        errorOut = "Use either --" + jsonFlag + " or --" + fileFlag + ", not both.";
        return false;
    }
    if (!hasJson && !hasFile) {
        return false;
    }

    std::string raw = hasJson ? optionalFlag(parsedFlags, jsonFlag) : "";
    if (hasFile && !readTextFile(optionalFlag(parsedFlags, fileFlag), raw, errorOut)) {
        return false;
    }
    return parseJsonText(raw, valueOut, errorOut);
}

bool loadRawStringValue(const std::map<std::string, std::string>& parsedFlags,
                        const std::string& valueFlag,
                        const std::string& fileFlag,
                        std::string& valueOut,
                        std::string& errorOut) {
    valueOut.clear();
    errorOut.clear();
    const bool hasValue = hasFlag(parsedFlags, valueFlag);
    const bool hasFile = hasFlag(parsedFlags, fileFlag);
    if (hasValue && hasFile) {
        errorOut = "Use either --" + valueFlag + " or --" + fileFlag + ", not both.";
        return false;
    }
    if (!hasValue && !hasFile) {
        return false;
    }
    if (hasValue) {
        valueOut = optionalFlag(parsedFlags, valueFlag);
        return true;
    }
    return readTextFile(optionalFlag(parsedFlags, fileFlag), valueOut, errorOut);
}

bool parseOptionalDoubleFlag(const std::map<std::string, std::string>& parsedFlags,
                             const std::string& flagName,
                             double& valueOut,
                             std::string& errorOut) {
    valueOut = 0.0;
    const auto it = parsedFlags.find(flagName);
    if (it == parsedFlags.end() || it->second.empty()) {
        return false;
    }
    try {
        valueOut = std::stod(it->second);
    } catch (const std::exception&) {
        errorOut = "--" + flagName + " must be a number.";
        return false;
    }
    return true;
}

bool parseOptionalIntFlag(const std::map<std::string, std::string>& parsedFlags,
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

bool parseCommaSeparatedInts(const std::string& raw,
                             std::vector<int>& valuesOut,
                             const std::string& flagName,
                             std::string& errorOut) {
    valuesOut.clear();
    if (trimCopy(raw).empty()) {
        return false;
    }
    std::stringstream stream(raw);
    std::string item;
    while (std::getline(stream, item, ',')) {
        item = trimCopy(item);
        if (item.empty()) {
            continue;
        }
        try {
            valuesOut.push_back(std::stoi(item));
        } catch (const std::exception&) {
            errorOut = "--" + flagName + " must be a comma-separated list of integers.";
            valuesOut.clear();
            return false;
        }
    }
    if (valuesOut.empty()) {
        errorOut = "--" + flagName + " must contain at least one integer.";
        return false;
    }
    return true;
}

bool parseCommaSeparatedDoubles(const std::string& raw,
                                std::vector<double>& valuesOut,
                                const std::string& flagName,
                                std::string& errorOut) {
    valuesOut.clear();
    if (trimCopy(raw).empty()) {
        return false;
    }
    std::stringstream stream(raw);
    std::string item;
    while (std::getline(stream, item, ',')) {
        item = trimCopy(item);
        if (item.empty()) {
            continue;
        }
        try {
            valuesOut.push_back(std::stod(item));
        } catch (const std::exception&) {
            errorOut = "--" + flagName + " must be a comma-separated list of numbers.";
            valuesOut.clear();
            return false;
        }
    }
    if (valuesOut.empty()) {
        errorOut = "--" + flagName + " must contain at least one number.";
        return false;
    }
    return true;
}

void appendQueryParameter(std::string& path, const std::string& key, const std::string& value) {
    if (value.empty()) {
        return;
    }
    path += (path.find('?') == std::string::npos) ? "?" : "&";
    path += key + "=" + value;
}

void appendQueryParameter(std::string& path, const std::string& key, int value) {
    path += (path.find('?') == std::string::npos) ? "?" : "&";
    path += key + "=" + std::to_string(value);
}

int printAndReturn(const Models::CloudResponse& response, const CLI::GlobalFlags& globalFlags) {
    BaseCommand::printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

bool validProxyPlane(const std::string& plane) {
    const std::string normalized = toLowerCopy(trimCopy(plane));
    return normalized == "runtime" || normalized == "control";
}

bool validNetworkAction(const std::string& action) {
    const std::string normalized = toLowerCopy(trimCopy(action));
    return normalized == "start"
           || normalized == "stop"
           || normalized == "repeat"
           || normalized == "reset"
           || normalized == "new";
}

bool validRuntimeAction(const std::string& action) {
    const std::string normalized = toLowerCopy(trimCopy(action));
    return validNetworkAction(normalized)
           || normalized == "save"
           || normalized == "step";
}

bool resolveRuntimePlane(const std::map<std::string, std::string>& parsedFlags,
                         std::string& planeOut,
                         std::string& errorOut) {
    planeOut = toLowerCopy(trimCopy(optionalFlag(parsedFlags, "plane", "runtime")));
    if (planeOut.empty()) {
        planeOut = "runtime";
    }
    if (!validProxyPlane(planeOut)) {
        errorOut = "--plane must be either 'runtime' or 'control'.";
        return false;
    }
    return true;
}

bool addOptionalString(nlohmann::json& payload,
                       const std::map<std::string, std::string>& parsedFlags,
                       const std::string& flagName,
                       const std::string& fieldName) {
    const auto value = trimCopy(optionalFlag(parsedFlags, flagName));
    if (value.empty()) {
        return false;
    }
    payload[fieldName] = value;
    return true;
}

bool resolveAarnnInventoryTarget(const std::map<std::string, std::string>& parsedFlags,
                                 std::string& clusterIdOut,
                                 std::string& orchestratorIdOut,
                                 std::string& errorOut) {
    clusterIdOut = trimCopy(optionalFlag(parsedFlags, "cluster"));
    orchestratorIdOut = trimCopy(optionalFlag(parsedFlags, "orchestrator"));
    errorOut.clear();
    if (!clusterIdOut.empty() && !orchestratorIdOut.empty()) {
        errorOut = "Use either --cluster or --orchestrator, not both.";
        return false;
    }
    return true;
}

bool resolveAarnnNetworkTarget(const std::map<std::string, std::string>& parsedFlags,
                               std::string& clusterIdOut,
                               std::string& orchestratorIdOut,
                               std::string& addrOut,
                               std::string& errorOut) {
    if (!resolveAarnnInventoryTarget(parsedFlags, clusterIdOut, orchestratorIdOut, errorOut)) {
        return false;
    }
    addrOut = trimCopy(optionalFlag(parsedFlags, "addr"));
    if (!addrOut.empty() && (!clusterIdOut.empty() || !orchestratorIdOut.empty())) {
        errorOut = "Use either --addr or --cluster/--orchestrator, not both.";
        return false;
    }
    return true;
}

} // namespace

AarnnCommand::AarnnCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("aarnn", "Inspect and control AARNN services through Continuum", std::move(client)) {
    usage = "nmc aarnn [command]";
}

int AarnnCommand::execute(const std::map<std::string, std::string>&,
                          const std::vector<std::string>&,
                          const CLI::GlobalFlags&) {
    printHelp();
    return 0;
}

AarnnEndpointsCommand::AarnnEndpointsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("endpoints", "Discover AARNN runtime/control/orchestrator endpoints", std::move(client)) {
    usage = "nmc aarnn endpoints";
}

int AarnnEndpointsCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                   const std::vector<std::string>& parsedArgs,
                                   const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    const Models::CloudResponse response = apiClient->aarnnEndpoints();
    return printAndReturn(response, globalFlags);
}

AarnnInventoryCommand::AarnnInventoryCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("inventory", "List AARNN orchestrators, clusters, nodes, and networks known to Continuum", std::move(client)) {
    usage = "nmc aarnn inventory [--cluster CLUSTER_ID | --orchestrator ORCHESTRATOR_ID]";
    addFlag(CLI::Flag("c", "cluster", "Filter inventory to one discovered cluster", CLI::FlagType::String, false));
    addFlag(CLI::Flag("O", "orchestrator", "Filter inventory to one discovered orchestrator", CLI::FlagType::String, false));
}

int AarnnInventoryCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                   const std::vector<std::string>& parsedArgs,
                                   const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    std::string clusterId;
    std::string orchestratorId;
    std::string parseError;
    if (!resolveAarnnInventoryTarget(parsedFlags, clusterId, orchestratorId, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    const Models::CloudResponse response = apiClient->aarnnInventory(clusterId, orchestratorId);
    return printAndReturn(response, globalFlags);
}

AarnnProxyCommand::AarnnProxyCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("proxy", "Forward a raw AARNN API call through Continuum", std::move(client)) {
    usage = "nmc aarnn proxy PLANE --path /api/... [--method GET] [--json '{...}']";
    examples = "nmc aarnn proxy control --method POST --path /api/update_network --json '{\"network_id\":\"tenant-aarnn\"}'";
    addArgument(CLI::Argument("PLANE", "Target plane: runtime or control", true, 0));
    addFlag(CLI::Flag("m", "method", "HTTP method", CLI::FlagType::String, false));
    addFlag(CLI::Flag("p", "path", "Upstream AARNN path beginning with /", CLI::FlagType::String, true));
    addFlag(CLI::Flag("k", "cluster", "Resolve the request against one discovered cluster", CLI::FlagType::String, false));
    addFlag(CLI::Flag("O", "orchestrator", "Resolve the request against one discovered orchestrator", CLI::FlagType::String, false));
    addFlag(CLI::Flag("j", "json", "Raw JSON body payload", CLI::FlagType::String, false));
    addFlag(CLI::Flag("J", "json-file", "Read JSON body payload from file", CLI::FlagType::String, false));
    addFlag(CLI::Flag("b", "body", "Raw string body payload", CLI::FlagType::String, false));
    addFlag(CLI::Flag("B", "body-file", "Read raw string body payload from file", CLI::FlagType::String, false));
    addFlag(CLI::Flag("c", "content-type", "Override body Content-Type", CLI::FlagType::String, false));
}

int AarnnProxyCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                               const std::vector<std::string>& parsedArgs,
                               const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string plane = toLowerCopy(trimCopy(parsedArgs[0]));
    if (!validProxyPlane(plane)) {
        Models::CloudResponse response{false, "PLANE must be either 'runtime' or 'control'.", nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    nlohmann::json jsonPayload = nullptr;
    std::string parseError;
    if (!parseJsonFlagOrFile(parsedFlags, "json", "json-file", jsonPayload, parseError, false) && !parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    std::string rawBody;
    if (!loadRawStringValue(parsedFlags, "body", "body-file", rawBody, parseError) && !parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    if (!jsonPayload.is_null() && !rawBody.empty()) {
        Models::CloudResponse response{false, "Use either --json/--json-file or --body/--body-file, not both.", nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    const std::string method = toUpperCopy(trimCopy(optionalFlag(parsedFlags, "method", "GET")));
    const std::string path = trimCopy(optionalFlag(parsedFlags, "path"));
    const std::string contentType = trimCopy(optionalFlag(parsedFlags, "content-type"));
    std::string clusterId;
    std::string orchestratorId;
    if (!resolveAarnnInventoryTarget(parsedFlags, clusterId, orchestratorId, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    const Models::CloudResponse response = apiClient->aarnnProxy(
            plane,
            method,
            path,
            jsonPayload,
            rawBody,
            contentType,
            clusterId,
            orchestratorId
    );
    return printAndReturn(response, globalFlags);
}

AarnnNetworkCommand::AarnnNetworkCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("network", "Control AARNN shared cluster networks via the control API plane", std::move(client)) {
    usage = "nmc aarnn network [command]";
}

int AarnnNetworkCommand::execute(const std::map<std::string, std::string>&,
                                 const std::vector<std::string>&,
                                 const CLI::GlobalFlags&) {
    printHelp();
    return 0;
}

AarnnNetworkStatusCommand::AarnnNetworkStatusCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("status", "List cluster nodes and networks from the AARNN control plane", std::move(client)) {
    usage = "nmc aarnn network status [--addr ORCHESTRATOR_URL | --cluster ID | --orchestrator ID]";
    addFlag(CLI::Flag("a", "addr", "Override orchestrator address", CLI::FlagType::String, false));
    addFlag(CLI::Flag("c", "cluster", "Resolve status for one discovered cluster", CLI::FlagType::String, false));
    addFlag(CLI::Flag("O", "orchestrator", "Resolve status for one discovered orchestrator", CLI::FlagType::String, false));
}

int AarnnNetworkStatusCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                       const std::vector<std::string>& parsedArgs,
                                       const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    std::string clusterId;
    std::string orchestratorId;
    std::string addr;
    std::string parseError;
    if (!resolveAarnnNetworkTarget(parsedFlags, clusterId, orchestratorId, addr, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    if (addr.empty() && clusterId.empty() && orchestratorId.empty()) {
        const Models::CloudResponse response = apiClient->aarnnInventory();
        return printAndReturn(response, globalFlags);
    }

    std::string path = "/api/status";
    appendQueryParameter(path, "addr", addr);
    const Models::CloudResponse response = apiClient->aarnnProxy(
            "control",
            "GET",
            path,
            nlohmann::json(),
            "",
            "",
            clusterId,
            orchestratorId
    );
    return printAndReturn(response, globalFlags);
}

AarnnNetworkSnapshotCommand::AarnnNetworkSnapshotCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("snapshot", "Fetch a network snapshot from the AARNN control plane", std::move(client)) {
    usage = "nmc aarnn network snapshot NETWORK_ID [--node-id NODE] [--addr ORCHESTRATOR_URL | --cluster ID | --orchestrator ID]";
    addArgument(CLI::Argument("NETWORK_ID", "AARNN network identifier", true, 0));
    addFlag(CLI::Flag("n", "node-id", "Restrict snapshot lookup to a specific node", CLI::FlagType::String, false));
    addFlag(CLI::Flag("a", "addr", "Override orchestrator address", CLI::FlagType::String, false));
    addFlag(CLI::Flag("c", "cluster", "Resolve snapshot for one discovered cluster", CLI::FlagType::String, false));
    addFlag(CLI::Flag("O", "orchestrator", "Resolve snapshot for one discovered orchestrator", CLI::FlagType::String, false));
}

int AarnnNetworkSnapshotCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                         const std::vector<std::string>& parsedArgs,
                                         const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    std::string clusterId;
    std::string orchestratorId;
    std::string addr;
    std::string parseError;
    if (!resolveAarnnNetworkTarget(parsedFlags, clusterId, orchestratorId, addr, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    std::string path = "/api/snapshot";
    appendQueryParameter(path, "network_id", parsedArgs[0]);
    appendQueryParameter(path, "node_id", trimCopy(optionalFlag(parsedFlags, "node-id")));
    appendQueryParameter(path, "addr", addr);
    const Models::CloudResponse response = apiClient->aarnnProxy(
            "control",
            "GET",
            path,
            nlohmann::json(),
            "",
            "",
            clusterId,
            orchestratorId
    );
    return printAndReturn(response, globalFlags);
}

AarnnNetworkActivityCommand::AarnnNetworkActivityCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("activity", "Fetch live activity for one AARNN network", std::move(client)) {
    usage = "nmc aarnn network activity NETWORK_ID [--node-id NODE] [--addr ORCHESTRATOR_URL | --cluster ID | --orchestrator ID]";
    addArgument(CLI::Argument("NETWORK_ID", "AARNN network identifier", true, 0));
    addFlag(CLI::Flag("n", "node-id", "Restrict activity lookup to a specific node", CLI::FlagType::String, false));
    addFlag(CLI::Flag("a", "addr", "Override orchestrator address", CLI::FlagType::String, false));
    addFlag(CLI::Flag("c", "cluster", "Resolve activity for one discovered cluster", CLI::FlagType::String, false));
    addFlag(CLI::Flag("O", "orchestrator", "Resolve activity for one discovered orchestrator", CLI::FlagType::String, false));
}

int AarnnNetworkActivityCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                         const std::vector<std::string>& parsedArgs,
                                         const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    std::string clusterId;
    std::string orchestratorId;
    std::string addr;
    std::string parseError;
    if (!resolveAarnnNetworkTarget(parsedFlags, clusterId, orchestratorId, addr, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    std::string path = "/api/activity";
    appendQueryParameter(path, "network_id", parsedArgs[0]);
    appendQueryParameter(path, "node_id", trimCopy(optionalFlag(parsedFlags, "node-id")));
    appendQueryParameter(path, "addr", addr);
    const Models::CloudResponse response = apiClient->aarnnProxy(
            "control",
            "GET",
            path,
            nlohmann::json(),
            "",
            "",
            clusterId,
            orchestratorId
    );
    return printAndReturn(response, globalFlags);
}

AarnnNetworkUpdateCommand::AarnnNetworkUpdateCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("update", "Update an AARNN shared network configuration", std::move(client)) {
    usage = "nmc aarnn network update NETWORK_ID [--config-json JSON|--config-file FILE] [--neuron-model NAME] [--learning-rule NAME]";
    addArgument(CLI::Argument("NETWORK_ID", "AARNN network identifier", true, 0));
    addFlag(CLI::Flag("p", "payload", "Full JSON request payload", CLI::FlagType::String, false));
    addFlag(CLI::Flag("P", "payload-file", "Read full JSON request payload from file", CLI::FlagType::String, false));
    addFlag(CLI::Flag("j", "config-json", "Network config JSON", CLI::FlagType::String, false));
    addFlag(CLI::Flag("J", "config-file", "Read network config JSON from file", CLI::FlagType::String, false));
    addFlag(CLI::Flag("m", "neuron-model", "Neuron model name", CLI::FlagType::String, false));
    addFlag(CLI::Flag("l", "learning-rule", "Learning rule name", CLI::FlagType::String, false));
    addFlag(CLI::Flag("a", "addr", "Override orchestrator address", CLI::FlagType::String, false));
    addFlag(CLI::Flag("c", "cluster", "Resolve the update against one discovered cluster", CLI::FlagType::String, false));
    addFlag(CLI::Flag("O", "orchestrator", "Resolve the update against one discovered orchestrator", CLI::FlagType::String, false));
}

int AarnnNetworkUpdateCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                       const std::vector<std::string>& parsedArgs,
                                       const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    nlohmann::json payload = nlohmann::json::object();
    std::string parseError;
    if (!parseJsonFlagOrFile(parsedFlags, "payload", "payload-file", payload, parseError, true) && !parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    std::string configJson;
    if (!loadJsonStringValue(parsedFlags, "config-json", "config-file", configJson, parseError) && !parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    payload["network_id"] = parsedArgs[0];
    std::string clusterId;
    std::string orchestratorId;
    std::string addr;
    if (!resolveAarnnNetworkTarget(parsedFlags, clusterId, orchestratorId, addr, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    if (!addr.empty()) {
        payload["addr"] = addr;
    }
    addOptionalString(payload, parsedFlags, "neuron-model", "neuron_model");
    addOptionalString(payload, parsedFlags, "learning-rule", "learning_rule");
    if (!configJson.empty()) {
        payload["config_json"] = configJson;
    }

    if (payload.size() <= 1) {
        Models::CloudResponse response{false, "Specify at least one update field.", nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    const Models::CloudResponse response = apiClient->aarnnProxy(
            "control",
            "POST",
            "/api/update_network",
            payload,
            "",
            "",
            clusterId,
            orchestratorId
    );
    return printAndReturn(response, globalFlags);
}

AarnnNetworkControlCommand::AarnnNetworkControlCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("control", "Send a control action to an AARNN shared network", std::move(client)) {
    usage = "nmc aarnn network control NETWORK_ID --action start|stop|repeat|reset|new";
    addArgument(CLI::Argument("NETWORK_ID", "AARNN network identifier", true, 0));
    addFlag(CLI::Flag("a", "action", "Network action", CLI::FlagType::String, true));
    addFlag(CLI::Flag("p", "payload", "Full JSON request payload", CLI::FlagType::String, false));
    addFlag(CLI::Flag("P", "payload-file", "Read full JSON request payload from file", CLI::FlagType::String, false));
    addFlag(CLI::Flag("o", "addr", "Override orchestrator address", CLI::FlagType::String, false));
    addFlag(CLI::Flag("c", "cluster", "Resolve the action against one discovered cluster", CLI::FlagType::String, false));
    addFlag(CLI::Flag("O", "orchestrator", "Resolve the action against one discovered orchestrator", CLI::FlagType::String, false));
}

int AarnnNetworkControlCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                        const std::vector<std::string>& parsedArgs,
                                        const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    nlohmann::json payload = nlohmann::json::object();
    std::string parseError;
    if (!parseJsonFlagOrFile(parsedFlags, "payload", "payload-file", payload, parseError, true) && !parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    const std::string action = toLowerCopy(trimCopy(optionalFlag(parsedFlags, "action")));
    if (!validNetworkAction(action)) {
        Models::CloudResponse response{false, "--action must be one of: start, stop, repeat, reset, new.", nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    payload["network_id"] = parsedArgs[0];
    payload["action"] = action;
    std::string clusterId;
    std::string orchestratorId;
    std::string addr;
    if (!resolveAarnnNetworkTarget(parsedFlags, clusterId, orchestratorId, addr, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    if (!addr.empty()) {
        payload["addr"] = addr;
    }

    const Models::CloudResponse response = apiClient->aarnnProxy(
            "control",
            "POST",
            "/api/control_network",
            payload,
            "",
            "",
            clusterId,
            orchestratorId
    );
    return printAndReturn(response, globalFlags);
}

AarnnNetworkAerInjectCommand::AarnnNetworkAerInjectCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("aer-inject", "Inject one AER exchange into an AARNN shared network", std::move(client)) {
    usage = "nmc aarnn network aer-inject NETWORK_ID [--spike-indices 0,2,4] [--input-values 1.0,0.0] [--aer-base 4096]";
    addArgument(CLI::Argument("NETWORK_ID", "AARNN network identifier", true, 0));
    addFlag(CLI::Flag("p", "payload", "Full JSON request payload", CLI::FlagType::String, false));
    addFlag(CLI::Flag("P", "payload-file", "Read full JSON request payload from file", CLI::FlagType::String, false));
    addFlag(CLI::Flag("a", "addr", "Override orchestrator address", CLI::FlagType::String, false));
    addFlag(CLI::Flag("c", "cluster", "Resolve injection against one discovered cluster", CLI::FlagType::String, false));
    addFlag(CLI::Flag("O", "orchestrator", "Resolve injection against one discovered orchestrator", CLI::FlagType::String, false));
    addFlag(CLI::Flag("n", "node-id", "Restrict injection to a specific node", CLI::FlagType::String, false));
    addFlag(CLI::Flag("s", "step-index", "Simulation step index", CLI::FlagType::Int, false));
    addFlag(CLI::Flag("t", "time-ms", "Timestamp in milliseconds", CLI::FlagType::String, false));
    addFlag(CLI::Flag("d", "dt-ms", "Frame delta in milliseconds", CLI::FlagType::String, false));
    addFlag(CLI::Flag("b", "aer-base", "AER base address", CLI::FlagType::Int, false));
    addFlag(CLI::Flag("x", "aer-payload-hex", "Hex-encoded AER payload", CLI::FlagType::String, false));
    addFlag(CLI::Flag("i", "spike-indices", "Comma-separated spike indices", CLI::FlagType::String, false));
    addFlag(CLI::Flag("v", "input-values", "Comma-separated input values", CLI::FlagType::String, false));
    addFlag(CLI::Flag("k", "spike-io", "Spike IO policy JSON", CLI::FlagType::String, false));
    addFlag(CLI::Flag("B", "is-backward", "Set is_backward=true", CLI::FlagType::Bool, false));
}

int AarnnNetworkAerInjectCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                          const std::vector<std::string>& parsedArgs,
                                          const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    nlohmann::json payload = nlohmann::json::object();
    std::string parseError;
    if (!parseJsonFlagOrFile(parsedFlags, "payload", "payload-file", payload, parseError, true) && !parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    payload["network_id"] = parsedArgs[0];
    std::string clusterId;
    std::string orchestratorId;
    std::string addr;
    if (!resolveAarnnNetworkTarget(parsedFlags, clusterId, orchestratorId, addr, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    if (!addr.empty()) {
        payload["addr"] = addr;
    }
    addOptionalString(payload, parsedFlags, "node-id", "node_id");
    addOptionalString(payload, parsedFlags, "aer-payload-hex", "aer_payload_hex");
    if (hasFlag(parsedFlags, "is-backward")) {
        payload["is_backward"] = true;
    }

    int aerBase = 0;
    if (parseOptionalIntFlag(parsedFlags, "aer-base", aerBase, parseError)) {
        payload["aer_base"] = aerBase;
    } else if (!parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    int stepIndex = 0;
    if (parseOptionalIntFlag(parsedFlags, "step-index", stepIndex, parseError)) {
        payload["step_index"] = stepIndex;
    } else if (!parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    double timeMs = 0.0;
    if (parseOptionalDoubleFlag(parsedFlags, "time-ms", timeMs, parseError)) {
        payload["time_ms"] = timeMs;
    } else if (!parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    double dtMs = 0.0;
    if (parseOptionalDoubleFlag(parsedFlags, "dt-ms", dtMs, parseError)) {
        payload["dt_ms"] = dtMs;
    } else if (!parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    std::vector<int> spikeIndices;
    if (!parseCommaSeparatedInts(optionalFlag(parsedFlags, "spike-indices"), spikeIndices, "spike-indices", parseError) && !parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    if (!spikeIndices.empty()) {
        payload["spike_indices"] = spikeIndices;
    }

    std::vector<double> inputValues;
    if (!parseCommaSeparatedDoubles(optionalFlag(parsedFlags, "input-values"), inputValues, "input-values", parseError) && !parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    if (!inputValues.empty()) {
        payload["input_values"] = inputValues;
    }

    nlohmann::json spikeIo;
    if (!parseJsonFlag(parsedFlags, "spike-io", spikeIo, parseError, true) && !parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    if (!spikeIo.is_null() && !spikeIo.empty()) {
        payload["spike_io"] = spikeIo;
    }

    const bool hasExplicitInput = payload.contains("aer_payload_hex")
                                  || payload.contains("spike_indices")
                                  || payload.contains("input_values");
    if (!hasExplicitInput) {
        Models::CloudResponse response{
                false,
                "Provide --aer-payload-hex, --spike-indices, --input-values, or a full --payload.",
                nlohmann::json::object(),
                400
        };
        return printAndReturn(response, globalFlags);
    }

    const Models::CloudResponse response = apiClient->aarnnProxy(
            "control",
            "POST",
            "/api/aer/inject",
            payload,
            "",
            "",
            clusterId,
            orchestratorId
    );
    return printAndReturn(response, globalFlags);
}

AarnnNetworkAerStreamCommand::AarnnNetworkAerStreamCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("aer-stream", "Stream NDJSON AER frames into an AARNN shared network", std::move(client)) {
    usage = "nmc aarnn network aer-stream NETWORK_ID --body-file frames.ndjson [--aer-base 4096]";
    addArgument(CLI::Argument("NETWORK_ID", "AARNN network identifier", true, 0));
    addFlag(CLI::Flag("b", "body", "Inline NDJSON body", CLI::FlagType::String, false));
    addFlag(CLI::Flag("B", "body-file", "Read NDJSON body from file", CLI::FlagType::String, false));
    addFlag(CLI::Flag("a", "addr", "Override orchestrator address", CLI::FlagType::String, false));
    addFlag(CLI::Flag("c", "cluster", "Resolve the stream against one discovered cluster", CLI::FlagType::String, false));
    addFlag(CLI::Flag("O", "orchestrator", "Resolve the stream against one discovered orchestrator", CLI::FlagType::String, false));
    addFlag(CLI::Flag("n", "node-id", "Restrict stream to a specific node", CLI::FlagType::String, false));
    addFlag(CLI::Flag("r", "aer-base", "AER base address", CLI::FlagType::Int, false));
}

int AarnnNetworkAerStreamCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                          const std::vector<std::string>& parsedArgs,
                                          const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    std::string parseError;
    std::string rawBody;
    if (!loadRawStringValue(parsedFlags, "body", "body-file", rawBody, parseError) && !parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    if (rawBody.empty()) {
        Models::CloudResponse response{false, "AER stream requires --body or --body-file.", nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    std::string clusterId;
    std::string orchestratorId;
    std::string addr;
    if (!resolveAarnnNetworkTarget(parsedFlags, clusterId, orchestratorId, addr, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    std::string path = "/api/aer/stream";
    appendQueryParameter(path, "network_id", parsedArgs[0]);
    appendQueryParameter(path, "addr", addr);
    appendQueryParameter(path, "node_id", trimCopy(optionalFlag(parsedFlags, "node-id")));

    int aerBase = 0;
    if (parseOptionalIntFlag(parsedFlags, "aer-base", aerBase, parseError)) {
        appendQueryParameter(path, "aer_base", aerBase);
    } else if (!parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    const Models::CloudResponse response = apiClient->aarnnProxy(
            "control",
            "POST",
            path,
            nlohmann::json(),
            rawBody,
            "application/x-ndjson",
            clusterId,
            orchestratorId
    );
    return printAndReturn(response, globalFlags);
}

AarnnRuntimeCommand::AarnnRuntimeCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("runtime", "Manage AARNN runtime workspaces through Continuum", std::move(client)) {
    usage = "nmc aarnn runtime [command]";
}

int AarnnRuntimeCommand::execute(const std::map<std::string, std::string>&,
                                 const std::vector<std::string>&,
                                 const CLI::GlobalFlags&) {
    printHelp();
    return 0;
}

AarnnRuntimeStatusCommand::AarnnRuntimeStatusCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("status", "Fetch AARNN runtime scheduler status", std::move(client)) {
    usage = "nmc aarnn runtime status [--plane runtime|control]";
    addFlag(CLI::Flag("p", "plane", "Proxy plane: runtime or control", CLI::FlagType::String, false));
}

int AarnnRuntimeStatusCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                       const std::vector<std::string>& parsedArgs,
                                       const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    std::string plane;
    std::string parseError;
    if (!resolveRuntimePlane(parsedFlags, plane, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    const Models::CloudResponse response = apiClient->aarnnProxy(plane, "GET", "/api/runtime/status");
    return printAndReturn(response, globalFlags);
}

AarnnRuntimeListCommand::AarnnRuntimeListCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("list", "List AARNN runtime workspaces", std::move(client)) {
    usage = "nmc aarnn runtime list [--plane runtime|control]";
    addFlag(CLI::Flag("p", "plane", "Proxy plane: runtime or control", CLI::FlagType::String, false));
}

int AarnnRuntimeListCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                     const std::vector<std::string>& parsedArgs,
                                     const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    std::string plane;
    std::string parseError;
    if (!resolveRuntimePlane(parsedFlags, plane, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    const Models::CloudResponse response = apiClient->aarnnProxy(plane, "GET", "/api/runtime/workspaces");
    return printAndReturn(response, globalFlags);
}

AarnnRuntimeCreateCommand::AarnnRuntimeCreateCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("create", "Create an AARNN runtime workspace", std::move(client)) {
    usage = "nmc aarnn runtime create [--workspace-id ID] [--config-file FILE] [--auto-start] [--plane runtime|control]";
    addFlag(CLI::Flag("p", "plane", "Proxy plane: runtime or control", CLI::FlagType::String, false));
    addFlag(CLI::Flag("P", "payload", "Full JSON request payload", CLI::FlagType::String, false));
    addFlag(CLI::Flag("F", "payload-file", "Read full JSON request payload from file", CLI::FlagType::String, false));
    addFlag(CLI::Flag("w", "workspace-id", "Explicit workspace identifier", CLI::FlagType::String, false));
    addFlag(CLI::Flag("n", "name", "Workspace display name", CLI::FlagType::String, false));
    addFlag(CLI::Flag("j", "config-json", "Workspace config JSON", CLI::FlagType::String, false));
    addFlag(CLI::Flag("J", "config-file", "Read workspace config JSON from file", CLI::FlagType::String, false));
    addFlag(CLI::Flag("s", "snapshot-json", "Workspace snapshot JSON", CLI::FlagType::String, false));
    addFlag(CLI::Flag("S", "snapshot-file", "Read workspace snapshot JSON from file", CLI::FlagType::String, false));
    addFlag(CLI::Flag("m", "neuron-model", "Neuron model name", CLI::FlagType::String, false));
    addFlag(CLI::Flag("l", "learning-rule", "Learning rule name", CLI::FlagType::String, false));
    addFlag(CLI::Flag("A", "auto-start", "Set auto_start=true", CLI::FlagType::Bool, false));
}

int AarnnRuntimeCreateCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                       const std::vector<std::string>& parsedArgs,
                                       const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    std::string plane;
    std::string parseError;
    if (!resolveRuntimePlane(parsedFlags, plane, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    nlohmann::json payload = nlohmann::json::object();
    if (!parseJsonFlagOrFile(parsedFlags, "payload", "payload-file", payload, parseError, true) && !parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    std::string configJson;
    if (!loadJsonStringValue(parsedFlags, "config-json", "config-file", configJson, parseError) && !parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    std::string snapshotJson;
    if (!loadJsonStringValue(parsedFlags, "snapshot-json", "snapshot-file", snapshotJson, parseError) && !parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    addOptionalString(payload, parsedFlags, "workspace-id", "workspace_id");
    addOptionalString(payload, parsedFlags, "name", "name");
    addOptionalString(payload, parsedFlags, "neuron-model", "neuron_model");
    addOptionalString(payload, parsedFlags, "learning-rule", "learning_rule");
    if (!configJson.empty()) {
        payload["config_json"] = configJson;
    }
    if (!snapshotJson.empty()) {
        payload["snapshot_json"] = snapshotJson;
    }
    if (hasFlag(parsedFlags, "auto-start")) {
        payload["auto_start"] = true;
    }

    const Models::CloudResponse response = apiClient->aarnnProxy(plane, "POST", "/api/runtime/workspaces", payload);
    return printAndReturn(response, globalFlags);
}

AarnnRuntimeGetCommand::AarnnRuntimeGetCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("get", "Fetch one AARNN runtime workspace", std::move(client)) {
    usage = "nmc aarnn runtime get WORKSPACE_ID [--plane runtime|control]";
    addArgument(CLI::Argument("WORKSPACE_ID", "Workspace identifier", true, 0));
    addFlag(CLI::Flag("p", "plane", "Proxy plane: runtime or control", CLI::FlagType::String, false));
}

int AarnnRuntimeGetCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                    const std::vector<std::string>& parsedArgs,
                                    const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    std::string plane;
    std::string parseError;
    if (!resolveRuntimePlane(parsedFlags, plane, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    const Models::CloudResponse response = apiClient->aarnnProxy(
            plane,
            "GET",
            "/api/runtime/workspaces/" + parsedArgs[0]
    );
    return printAndReturn(response, globalFlags);
}

AarnnRuntimeDeleteCommand::AarnnRuntimeDeleteCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("delete", "Delete an AARNN runtime workspace", std::move(client)) {
    usage = "nmc aarnn runtime delete WORKSPACE_ID [--plane runtime|control]";
    addArgument(CLI::Argument("WORKSPACE_ID", "Workspace identifier", true, 0));
    addFlag(CLI::Flag("p", "plane", "Proxy plane: runtime or control", CLI::FlagType::String, false));
}

int AarnnRuntimeDeleteCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                       const std::vector<std::string>& parsedArgs,
                                       const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    std::string plane;
    std::string parseError;
    if (!resolveRuntimePlane(parsedFlags, plane, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    const Models::CloudResponse response = apiClient->aarnnProxy(
            plane,
            "DELETE",
            "/api/runtime/workspaces/" + parsedArgs[0]
    );
    return printAndReturn(response, globalFlags);
}

AarnnRuntimeControlCommand::AarnnRuntimeControlCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("control", "Send a control action to an AARNN runtime workspace", std::move(client)) {
    usage = "nmc aarnn runtime control WORKSPACE_ID --action start|stop|repeat|reset|new|save|step [--plane runtime|control]";
    addArgument(CLI::Argument("WORKSPACE_ID", "Workspace identifier", true, 0));
    addFlag(CLI::Flag("p", "plane", "Proxy plane: runtime or control", CLI::FlagType::String, false));
    addFlag(CLI::Flag("a", "action", "Workspace action", CLI::FlagType::String, true));
    addFlag(CLI::Flag("j", "payload", "Full JSON request payload", CLI::FlagType::String, false));
    addFlag(CLI::Flag("J", "payload-file", "Read full JSON request payload from file", CLI::FlagType::String, false));
}

int AarnnRuntimeControlCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                        const std::vector<std::string>& parsedArgs,
                                        const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    std::string plane;
    std::string parseError;
    if (!resolveRuntimePlane(parsedFlags, plane, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    nlohmann::json payload = nlohmann::json::object();
    if (!parseJsonFlagOrFile(parsedFlags, "payload", "payload-file", payload, parseError, true) && !parseError.empty()) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }

    const std::string action = toLowerCopy(trimCopy(optionalFlag(parsedFlags, "action")));
    if (!validRuntimeAction(action)) {
        Models::CloudResponse response{false, "--action must be one of: start, stop, repeat, reset, new, save, step.", nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    payload["action"] = action;

    const Models::CloudResponse response = apiClient->aarnnProxy(
            plane,
            "POST",
            "/api/runtime/workspaces/" + parsedArgs[0] + "/control",
            payload
    );
    return printAndReturn(response, globalFlags);
}

AarnnRuntimeSnapshotCommand::AarnnRuntimeSnapshotCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("snapshot", "Fetch a saved snapshot for one AARNN runtime workspace", std::move(client)) {
    usage = "nmc aarnn runtime snapshot WORKSPACE_ID [--plane runtime|control]";
    addArgument(CLI::Argument("WORKSPACE_ID", "Workspace identifier", true, 0));
    addFlag(CLI::Flag("p", "plane", "Proxy plane: runtime or control", CLI::FlagType::String, false));
}

int AarnnRuntimeSnapshotCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                         const std::vector<std::string>& parsedArgs,
                                         const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    std::string plane;
    std::string parseError;
    if (!resolveRuntimePlane(parsedFlags, plane, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    const Models::CloudResponse response = apiClient->aarnnProxy(
            plane,
            "GET",
            "/api/runtime/workspaces/" + parsedArgs[0] + "/snapshot"
    );
    return printAndReturn(response, globalFlags);
}

AarnnRuntimeActivityCommand::AarnnRuntimeActivityCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("activity", "Fetch live activity for one AARNN runtime workspace", std::move(client)) {
    usage = "nmc aarnn runtime activity WORKSPACE_ID [--plane runtime|control]";
    addArgument(CLI::Argument("WORKSPACE_ID", "Workspace identifier", true, 0));
    addFlag(CLI::Flag("p", "plane", "Proxy plane: runtime or control", CLI::FlagType::String, false));
}

int AarnnRuntimeActivityCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                         const std::vector<std::string>& parsedArgs,
                                         const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    std::string plane;
    std::string parseError;
    if (!resolveRuntimePlane(parsedFlags, plane, parseError)) {
        Models::CloudResponse response{false, parseError, nlohmann::json::object(), 400};
        return printAndReturn(response, globalFlags);
    }
    const Models::CloudResponse response = apiClient->aarnnProxy(
            plane,
            "GET",
            "/api/runtime/workspaces/" + parsedArgs[0] + "/activity"
    );
    return printAndReturn(response, globalFlags);
}

} // namespace NMC::Commands
