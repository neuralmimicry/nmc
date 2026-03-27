#include "RefinerCommands.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include <vector>

#include <nlohmann/json.hpp>

namespace NMC::Commands {

namespace {

struct CommandResult {
    int exitCode{1};
    std::string output;
};

std::string shellQuote(const std::string& value) {
#ifdef _WIN32
    std::string quoted = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('"');
    return quoted;
#else
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
#endif
}

bool hasControlChars(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::iscntrl(ch) != 0;
    });
}

CommandResult runCommand(const std::vector<std::string>& args) {
    CommandResult result;
    if (args.empty()) {
        result.output = "No command specified.";
        return result;
    }

    std::ostringstream cmd;
    bool first = true;
    for (const auto& arg : args) {
        if (!first) {
            cmd << ' ';
        }
        cmd << shellQuote(arg);
        first = false;
    }
    cmd << " 2>&1";

#ifdef _WIN32
    FILE* pipe = _popen(cmd.str().c_str(), "r");
#else
    FILE* pipe = popen(cmd.str().c_str(), "r");
#endif
    if (!pipe) {
        result.output = "Unable to execute command.";
        return result;
    }

    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output.append(buffer.data());
    }

    int status = -1;
#ifdef _WIN32
    status = _pclose(pipe);
    if (status == -1) {
        result.exitCode = 1;
    } else {
        result.exitCode = status;
    }
#else
    status = pclose(pipe);
    if (status == -1) {
        result.exitCode = 1;
    } else if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    } else {
        result.exitCode = 1;
    }
#endif
    return result;
}

std::vector<std::string> kubectlBase(const std::string& context) {
    std::vector<std::string> args{"kubectl"};
    if (!context.empty()) {
        args.push_back("--context");
        args.push_back(context);
    }
    return args;
}

nlohmann::json parseJsonObject(const std::string& raw) {
    try {
        auto parsed = nlohmann::json::parse(raw);
        if (parsed.is_object()) {
            return parsed;
        }
    } catch (const std::exception&) {
    }
    return nlohmann::json::object();
}

nlohmann::json deploymentSummary(const nlohmann::json& deployment) {
    const int desired = deployment.value("spec", nlohmann::json::object()).value("replicas", 0);
    const int ready = deployment.value("status", nlohmann::json::object()).value("readyReplicas", 0);
    const int available = deployment.value("status", nlohmann::json::object()).value("availableReplicas", 0);
    const int unavailable = deployment.value("status", nlohmann::json::object()).value("unavailableReplicas", 0);

    nlohmann::json out = nlohmann::json::object();
    out["name"] = deployment.value("metadata", nlohmann::json::object()).value("name", "refiner");
    out["desired_replicas"] = desired;
    out["ready_replicas"] = ready;
    out["available_replicas"] = available;
    out["unavailable_replicas"] = unavailable;
    out["healthy"] = (desired == 0) ? true : (ready >= desired && unavailable == 0);
    return out;
}

Models::CloudResponse statusForNamespace(const std::string& namespaceName, const std::string& context) {
    Models::CloudResponse response;
    response.success = false;
    response.message = "Unable to determine Refiner status.";
    response.data = nlohmann::json::object();

    auto deployCmd = kubectlBase(context);
    deployCmd.insert(deployCmd.end(), {"-n", namespaceName, "get", "deployment", "refiner", "-o", "json"});
    const CommandResult deployResult = runCommand(deployCmd);
    response.data["deployment_raw"] = deployResult.output;
    if (deployResult.exitCode != 0) {
        response.message = "Failed to fetch deployment status.";
        return response;
    }

    const nlohmann::json deployment = parseJsonObject(deployResult.output);
    response.data["deployment"] = deployment;
    response.data["summary"] = deploymentSummary(deployment);

    auto podsCmd = kubectlBase(context);
    podsCmd.insert(podsCmd.end(), {"-n", namespaceName, "get", "pods", "-l", "app=refiner", "-o", "json"});
    const CommandResult podsResult = runCommand(podsCmd);
    response.data["pods_raw"] = podsResult.output;
    if (podsResult.exitCode == 0) {
        response.data["pods"] = parseJsonObject(podsResult.output);
    }

    auto svcCmd = kubectlBase(context);
    svcCmd.insert(svcCmd.end(), {"-n", namespaceName, "get", "service", "refiner", "-o", "json"});
    const CommandResult svcResult = runCommand(svcCmd);
    response.data["service_raw"] = svcResult.output;
    if (svcResult.exitCode == 0) {
        response.data["service"] = parseJsonObject(svcResult.output);
    }

    response.success = response.data.value("summary", nlohmann::json::object()).value("healthy", false);
    response.message = response.success
                           ? "Refiner deployment is healthy."
                           : "Refiner deployment is running but not healthy.";
    return response;
}

std::string requiredFlag(
    const std::map<std::string, std::string>& flags,
    const std::string& name
) {
    auto it = flags.find(name);
    return it == flags.end() ? std::string() : it->second;
}

std::string optionalFlag(
    const std::map<std::string, std::string>& flags,
    const std::string& name,
    const std::string& fallback
) {
    auto it = flags.find(name);
    if (it == flags.end() || it->second.empty()) {
        return fallback;
    }
    return it->second;
}

} // namespace

RefinerCommand::RefinerCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("refiner", "Deploy, monitor, and control Refiner workloads with Continuum", std::move(client)) {
    usage = "nmc refiner [deploy|status|scale|logs|remove]";
}

int RefinerCommand::execute(const std::map<std::string, std::string>&,
                            const std::vector<std::string>&,
                            const CLI::GlobalFlags&) {
    printHelp();
    return 0;
}

RefinerDeployCommand::RefinerDeployCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("deploy", "Deploy Refiner into Kubernetes", std::move(client)) {
    usage = "nmc refiner deploy --manifest ./refiner-k8s.yaml [--namespace refiner] [--context CONTEXT] [--timeout 300]";
    examples = "nmc refiner deploy --manifest ./refiner-k8s.yaml --namespace refiner --timeout 300";
    addFlag(CLI::Flag("m", "manifest", "Path to Refiner manifest yaml", CLI::FlagType::String, true));
    addFlag(CLI::Flag("n", "namespace", "Kubernetes namespace", CLI::FlagType::String, false, "refiner"));
    addFlag(CLI::Flag("c", "context", "kubectl context", CLI::FlagType::String, false));
    addFlag(CLI::Flag("t", "timeout", "Rollout timeout in seconds", CLI::FlagType::Int, false, "300"));
}

int RefinerDeployCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                  const std::vector<std::string>& parsedArgs,
                                  const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string manifest = requiredFlag(parsedFlags, "manifest");
    const std::string namespaceName = optionalFlag(parsedFlags, "namespace", "refiner");
    const std::string context = optionalFlag(parsedFlags, "context", "");
    const std::string timeoutRaw = optionalFlag(parsedFlags, "timeout", "300");

    Models::CloudResponse response;
    response.success = false;
    response.data = nlohmann::json::object();

    if (manifest.empty() || hasControlChars(manifest) || !std::filesystem::exists(manifest)) {
        response.message = "Invalid or missing --manifest path.";
        printOutput(response, globalFlags);
        return 1;
    }
    if (namespaceName.empty() || hasControlChars(namespaceName) || hasControlChars(context)) {
        response.message = "Invalid namespace/context value.";
        printOutput(response, globalFlags);
        return 1;
    }

    int timeoutSeconds = 300;
    try {
        timeoutSeconds = std::stoi(timeoutRaw);
    } catch (const std::exception&) {
        response.message = "Timeout must be an integer.";
        printOutput(response, globalFlags);
        return 1;
    }
    if (timeoutSeconds <= 0 || timeoutSeconds > 3600) {
        response.message = "Timeout must be between 1 and 3600 seconds.";
        printOutput(response, globalFlags);
        return 1;
    }

    auto getNs = kubectlBase(context);
    getNs.insert(getNs.end(), {"get", "namespace", namespaceName});
    const CommandResult getNsResult = runCommand(getNs);
    response.data["namespace_check"] = getNsResult.output;

    if (getNsResult.exitCode != 0) {
        auto createNs = kubectlBase(context);
        createNs.insert(createNs.end(), {"create", "namespace", namespaceName});
        const CommandResult createNsResult = runCommand(createNs);
        response.data["namespace_create"] = createNsResult.output;
        if (createNsResult.exitCode != 0) {
            response.message = "Failed to create namespace.";
            printOutput(response, globalFlags);
            return 1;
        }
    }

    auto applyCmd = kubectlBase(context);
    applyCmd.insert(applyCmd.end(), {"-n", namespaceName, "apply", "-f", manifest});
    const CommandResult applyResult = runCommand(applyCmd);
    response.data["apply"] = applyResult.output;
    if (applyResult.exitCode != 0) {
        response.message = "Manifest apply failed.";
        printOutput(response, globalFlags);
        return 1;
    }

    auto waitCmd = kubectlBase(context);
    waitCmd.insert(waitCmd.end(), {
        "-n",
        namespaceName,
        "rollout",
        "status",
        "deployment/refiner",
        "--timeout",
        std::to_string(timeoutSeconds) + "s"
    });
    const CommandResult waitResult = runCommand(waitCmd);
    response.data["rollout"] = waitResult.output;
    if (waitResult.exitCode != 0) {
        response.message = "Deploy applied, but rollout did not complete successfully.";
        printOutput(response, globalFlags);
        return 1;
    }

    Models::CloudResponse status = statusForNamespace(namespaceName, context);
    response.data["status"] = status.data;
    response.success = status.success;
    response.message = response.success
                           ? "Refiner deployed successfully and is healthy."
                           : "Refiner deployed but health checks are not passing yet.";

    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

RefinerStatusCommand::RefinerStatusCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("status", "Show Refiner deployment health", std::move(client)) {
    usage = "nmc refiner status [--namespace refiner] [--context CONTEXT] [--server]";
    examples = "nmc refiner status --namespace refiner\nnmc refiner status --namespace refiner --server";
    addFlag(CLI::Flag("n", "namespace", "Kubernetes namespace", CLI::FlagType::String, false, "refiner"));
    addFlag(CLI::Flag("c", "context", "kubectl context", CLI::FlagType::String, false));
    addFlag(CLI::Flag("s", "server", "Use nmc_server API route instead of local kubectl", CLI::FlagType::Bool, false));
}

int RefinerStatusCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                  const std::vector<std::string>& parsedArgs,
                                  const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }
    const std::string namespaceName = optionalFlag(parsedFlags, "namespace", "refiner");
    const std::string context = optionalFlag(parsedFlags, "context", "");
    const bool useServer = parsedFlags.count("server") > 0;

    Models::CloudResponse response;
    if (namespaceName.empty() || hasControlChars(namespaceName) || hasControlChars(context)) {
        response.success = false;
        response.message = "Invalid namespace/context value.";
        printOutput(response, globalFlags);
        return 1;
    }
    if (useServer) {
        if (!context.empty()) {
            response.success = false;
            response.message = "--context is only supported in local kubectl mode.";
            printOutput(response, globalFlags);
            return 1;
        }
        response = apiClient->getRefinerDeploymentStatus(namespaceName, "refiner");
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

    response = statusForNamespace(namespaceName, context);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

RefinerScaleCommand::RefinerScaleCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("scale", "Scale Refiner deployment replicas", std::move(client)) {
    usage = "nmc refiner scale --replicas 2 [--namespace refiner] [--context CONTEXT] [--timeout 180] [--server]";
    examples = "nmc refiner scale --replicas 4 --namespace refiner\nnmc refiner scale --replicas 2 --namespace refiner --server";
    addFlag(CLI::Flag("r", "replicas", "Replica count", CLI::FlagType::Int, true));
    addFlag(CLI::Flag("n", "namespace", "Kubernetes namespace", CLI::FlagType::String, false, "refiner"));
    addFlag(CLI::Flag("c", "context", "kubectl context", CLI::FlagType::String, false));
    addFlag(CLI::Flag("t", "timeout", "Rollout timeout in seconds", CLI::FlagType::Int, false, "180"));
    addFlag(CLI::Flag("s", "server", "Use nmc_server API route instead of local kubectl", CLI::FlagType::Bool, false));
}

int RefinerScaleCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                 const std::vector<std::string>& parsedArgs,
                                 const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string replicasRaw = requiredFlag(parsedFlags, "replicas");
    const std::string namespaceName = optionalFlag(parsedFlags, "namespace", "refiner");
    const std::string context = optionalFlag(parsedFlags, "context", "");
    const std::string timeoutRaw = optionalFlag(parsedFlags, "timeout", "180");
    const bool useServer = parsedFlags.count("server") > 0;

    Models::CloudResponse response;
    response.success = false;
    response.data = nlohmann::json::object();

    int replicas = 0;
    int timeoutSeconds = 180;
    try {
        replicas = std::stoi(replicasRaw);
        timeoutSeconds = std::stoi(timeoutRaw);
    } catch (const std::exception&) {
        response.message = "Replicas/timeout must be integers.";
        printOutput(response, globalFlags);
        return 1;
    }
    if (replicas < 0 || replicas > 100 || timeoutSeconds <= 0 || timeoutSeconds > 3600
        || hasControlChars(namespaceName) || hasControlChars(context)) {
        response.message = "Invalid scale request.";
        printOutput(response, globalFlags);
        return 1;
    }
    if (useServer) {
        if (!context.empty()) {
            response.message = "--context is only supported in local kubectl mode.";
            printOutput(response, globalFlags);
            return 1;
        }
        Models::CloudResponse serverResponse = apiClient->scaleRefinerDeployment(replicas, namespaceName, "refiner");
        printOutput(serverResponse, globalFlags);
        return serverResponse.success ? 0 : 1;
    }

    auto scaleCmd = kubectlBase(context);
    scaleCmd.insert(scaleCmd.end(), {
        "-n",
        namespaceName,
        "scale",
        "deployment/refiner",
        "--replicas",
        std::to_string(replicas)
    });
    const CommandResult scaleResult = runCommand(scaleCmd);
    response.data["scale"] = scaleResult.output;
    if (scaleResult.exitCode != 0) {
        response.message = "Failed to scale deployment.";
        printOutput(response, globalFlags);
        return 1;
    }

    auto waitCmd = kubectlBase(context);
    waitCmd.insert(waitCmd.end(), {
        "-n",
        namespaceName,
        "rollout",
        "status",
        "deployment/refiner",
        "--timeout",
        std::to_string(timeoutSeconds) + "s"
    });
    const CommandResult waitResult = runCommand(waitCmd);
    response.data["rollout"] = waitResult.output;
    if (waitResult.exitCode != 0) {
        response.message = "Scale request applied, but rollout did not complete successfully.";
        printOutput(response, globalFlags);
        return 1;
    }

    Models::CloudResponse status = statusForNamespace(namespaceName, context);
    response.data["status"] = status.data;
    response.success = status.success;
    response.message = response.success
                           ? "Refiner scaled successfully."
                           : "Refiner scaled but health checks are not passing.";
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

RefinerLogsCommand::RefinerLogsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("logs", "Stream recent Refiner logs", std::move(client)) {
    usage = "nmc refiner logs [--namespace refiner] [--context CONTEXT] [--tail 200]";
    examples = "nmc refiner logs --tail 400";
    addFlag(CLI::Flag("n", "namespace", "Kubernetes namespace", CLI::FlagType::String, false, "refiner"));
    addFlag(CLI::Flag("c", "context", "kubectl context", CLI::FlagType::String, false));
    addFlag(CLI::Flag("t", "tail", "Number of log lines", CLI::FlagType::Int, false, "200"));
}

int RefinerLogsCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                const std::vector<std::string>& parsedArgs,
                                const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string namespaceName = optionalFlag(parsedFlags, "namespace", "refiner");
    const std::string context = optionalFlag(parsedFlags, "context", "");
    const std::string tailRaw = optionalFlag(parsedFlags, "tail", "200");

    Models::CloudResponse response;
    response.success = false;
    response.data = nlohmann::json::object();

    int tailLines = 200;
    try {
        tailLines = std::stoi(tailRaw);
    } catch (const std::exception&) {
        response.message = "Tail must be an integer.";
        printOutput(response, globalFlags);
        return 1;
    }

    if (tailLines <= 0 || tailLines > 10000 || hasControlChars(namespaceName) || hasControlChars(context)) {
        response.message = "Invalid log request.";
        printOutput(response, globalFlags);
        return 1;
    }

    auto logsCmd = kubectlBase(context);
    logsCmd.insert(logsCmd.end(), {"-n", namespaceName, "logs", "deployment/refiner", "--tail", std::to_string(tailLines)});
    const CommandResult logsResult = runCommand(logsCmd);
    response.data["logs"] = logsResult.output;
    response.success = (logsResult.exitCode == 0);
    response.message = response.success ? "Refiner logs retrieved." : "Failed to fetch Refiner logs.";
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

RefinerRemoveCommand::RefinerRemoveCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("remove", "Remove Refiner deployment resources", std::move(client)) {
    usage = "nmc refiner remove [--manifest ./refiner-k8s.yaml] [--namespace refiner] [--delete-storage]";
    examples = "nmc refiner remove --namespace refiner --delete-storage";
    addFlag(CLI::Flag("m", "manifest", "Optional manifest path used for deployment", CLI::FlagType::String, false));
    addFlag(CLI::Flag("n", "namespace", "Kubernetes namespace", CLI::FlagType::String, false, "refiner"));
    addFlag(CLI::Flag("c", "context", "kubectl context", CLI::FlagType::String, false));
    addFlag(CLI::Flag("s", "delete-storage", "Delete PVC refiner-job-data", CLI::FlagType::Bool, false));
}

int RefinerRemoveCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                  const std::vector<std::string>& parsedArgs,
                                  const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string manifest = optionalFlag(parsedFlags, "manifest", "");
    const std::string namespaceName = optionalFlag(parsedFlags, "namespace", "refiner");
    const std::string context = optionalFlag(parsedFlags, "context", "");
    const bool deleteStorage = parsedFlags.count("delete-storage") > 0 && parsedFlags.at("delete-storage") == "1";

    Models::CloudResponse response;
    response.success = false;
    response.data = nlohmann::json::object();

    if (hasControlChars(namespaceName) || hasControlChars(context) || hasControlChars(manifest)) {
        response.message = "Invalid remove request.";
        printOutput(response, globalFlags);
        return 1;
    }

    CommandResult removeResult;
    if (!manifest.empty()) {
        if (!std::filesystem::exists(manifest)) {
            response.message = "Manifest path does not exist.";
            printOutput(response, globalFlags);
            return 1;
        }
        auto removeCmd = kubectlBase(context);
        removeCmd.insert(removeCmd.end(), {"-n", namespaceName, "delete", "-f", manifest, "--ignore-not-found"});
        removeResult = runCommand(removeCmd);
    } else {
        auto removeCmd = kubectlBase(context);
        removeCmd.insert(removeCmd.end(), {"-n", namespaceName, "delete", "deployment", "refiner", "--ignore-not-found"});
        removeResult = runCommand(removeCmd);
        auto svcCmd = kubectlBase(context);
        svcCmd.insert(svcCmd.end(), {"-n", namespaceName, "delete", "service", "refiner", "--ignore-not-found"});
        const CommandResult svcResult = runCommand(svcCmd);
        response.data["service_remove"] = svcResult.output;
    }
    response.data["remove"] = removeResult.output;
    if (removeResult.exitCode != 0) {
        response.message = "Failed to remove Refiner resources.";
        printOutput(response, globalFlags);
        return 1;
    }

    if (deleteStorage) {
        auto pvcCmd = kubectlBase(context);
        pvcCmd.insert(pvcCmd.end(), {"-n", namespaceName, "delete", "pvc", "refiner-job-data", "--ignore-not-found"});
        const CommandResult pvcResult = runCommand(pvcCmd);
        response.data["pvc_remove"] = pvcResult.output;
        if (pvcResult.exitCode != 0) {
            response.message = "Refiner removed, but PVC deletion failed.";
            printOutput(response, globalFlags);
            return 1;
        }
    }

    response.success = true;
    response.message = "Refiner resources removed.";
    printOutput(response, globalFlags);
    return 0;
}

} // namespace NMC::Commands
