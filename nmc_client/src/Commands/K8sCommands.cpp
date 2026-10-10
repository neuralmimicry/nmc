#include "K8sCommands.h"
#include <algorithm>
#include <iostream>
#include <string>

namespace NMC::Commands {

// --- K8sCommand (Parent) ---
K8sCommand::K8sCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("k8s", "Manage k8s clusters in NMC", std::move(client)) {
    usage = "nmc k8s [command]";
}

int K8sCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    printHelp();
    return 0;
}

// --- K8sCreateCommand ---
K8sCreateCommand::K8sCreateCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("create", "Creates a new k8s cluster", std::move(client)) {
    usage = "nmc k8s create NAME --region rugby-1";
    examples = "nmc k8s create NAME --region rugby-1";
    addArgument(CLI::Argument("NAME", "Name of the k8s cluster", true, 0));
    addFlag(CLI::Flag("r", "region", "Region", CLI::FlagType::String, true));
}

int K8sCreateCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& name = parsedArgs[0];
    std::string region = parsedFlags.count("region") ? parsedFlags.at("region") : "";

    if (name.empty() || region.empty()) {
        std::cerr << "Error: K8s cluster name and region are required." << std::endl;
        printHelp();
        return 1;
    }

    Models::CloudResponse response = apiClient->createK8sCluster(name, region);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- K8sDeleteCommand ---
K8sDeleteCommand::K8sDeleteCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("delete", "Deletes a K8s cluster", std::move(client)) {
    usage = "nmc k8s delete ID";
    examples = "nmc k8s delete ID";
    addArgument(CLI::Argument("ID", "ID of the K8s cluster to delete", true, 0));
}

int K8sDeleteCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient->deleteK8sCluster(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- K8sGetCommand ---
K8sGetCommand::K8sGetCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("get", "Get k8s cluster", std::move(client)) {
    addAlias("gk");
    usage = "nmc k8s get ID_OR_NAME";
    examples = "nmc k8s get refiner-local";
    addArgument(CLI::Argument("ID_OR_NAME", "ID or name of the K8s cluster to get", true, 0));
}

int K8sGetCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient->getK8sCluster(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- K8sGetConfigCommand ---
K8sGetConfigCommand::K8sGetConfigCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("get-config", "Get the KubeConfig file", std::move(client)) {
    usage = "nmc k8s get-config ID";
    examples = "nmc k8s get-config ID";
    addArgument(CLI::Argument("ID", "ID of the K8s cluster to get config for", true, 0));
}

int K8sGetConfigCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient->getKubeConfig(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- K8sListCommand ---
K8sListCommand::K8sListCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("list", "List of k8s clusters", std::move(client)) {
    addAlias("ls");
    usage = "nmc k8s list";
    examples = "nmc k8s ls";
    addFlag(CLI::Flag("f", "filter-name", "Filter by name", CLI::FlagType::String));
    addFlag(CLI::Flag("w", "watch", "Watch changes to list", CLI::FlagType::Bool));
}

int K8sListCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    std::string filterName = parsedFlags.count("filter-name") ? parsedFlags.at("filter-name") : "";
    bool watch = parsedFlags.count("watch") && parsedFlags.at("watch") == "1"; // "1" for true from Flag::setValue

    Models::CloudResponse response = apiClient->listK8sClusters(filterName);
    printOutput(response, globalFlags);
    if (watch) {
        std::cout << "Watching for changes... (Not implemented in mock)" << std::endl;
    }
    return response.success ? 0 : 1;
}

// --- K8sListLocationsCommand ---
K8sListLocationsCommand::K8sListLocationsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("list-locations", "List k8s cluster locations", std::move(client)) {
    addAlias("k8s-loc");
    usage = "nmc k8s list-locations";
    examples = "nmc k8s list-locations";
    addFlag(CLI::Flag("f", "filter-sku", "Filter by sku", CLI::FlagType::String));
}

int K8sListLocationsCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    std::string filterSku = parsedFlags.count("filter-sku") ? parsedFlags.at("filter-sku") : "";
    Models::CloudResponse response = apiClient->listK8sLocations(filterSku);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- K8sHealthCommand ---
K8sHealthCommand::K8sHealthCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("health", "Check K8s API health endpoint", std::move(client)) {
    usage = "nmc k8s health";
    examples = "nmc k8s health";
}

K8sLogsCommand::K8sLogsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("logs", "Read bounded Kubernetes workload logs through Continuum", std::move(client)) {
    usage = "nmc k8s logs --namespace NAMESPACE --deployment DEPLOYMENT [--since-seconds 300] [--tail-lines 500] [--previous]";
    examples = "nmc k8s logs --namespace octobot --deployment octobot --since-seconds 1200 --tail-lines 1000";
    addFlag(CLI::Flag("n", "namespace", "Kubernetes namespace", CLI::FlagType::String, true));
    addFlag(CLI::Flag("d", "deployment", "Kubernetes Deployment", CLI::FlagType::String, true));
    addFlag(CLI::Flag("s", "since-seconds", "Maximum age of log entries (1-86400)", CLI::FlagType::Int, false, "300"));
    addFlag(CLI::Flag("t", "tail-lines", "Maximum lines per container (1-2000)", CLI::FlagType::Int, false, "500"));
    addFlag(CLI::Flag("p", "previous", "Read the previous terminated container instance", CLI::FlagType::Bool, false));
}

int K8sLogsCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                            const std::vector<std::string>& parsedArgs,
                            const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) return 1;
    int sinceSeconds = 0;
    int tailLines = 0;
    try {
        sinceSeconds = std::stoi(parsedFlags.count("since-seconds") ? parsedFlags.at("since-seconds") : "300");
        tailLines = std::stoi(parsedFlags.count("tail-lines") ? parsedFlags.at("tail-lines") : "500");
    } catch (const std::exception&) {
        std::cerr << "Error: since-seconds and tail-lines must be integers." << std::endl;
        return 1;
    }
    const std::string namespaceName = parsedFlags.at("namespace");
    const std::string deploymentName = parsedFlags.at("deployment");
    const bool previous = parsedFlags.count("previous") > 0 && parsedFlags.at("previous") == "1";
    if (sinceSeconds < 1 || sinceSeconds > 86400 || tailLines < 1 || tailLines > 2000) {
        std::cerr << "Error: since-seconds must be 1-86400 and tail-lines must be 1-2000." << std::endl;
        return 1;
    }
    const auto response = apiClient->getK8sWorkloadLogs(namespaceName, deploymentName, sinceSeconds, tailLines, previous);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

int K8sHealthCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                              const std::vector<std::string>& parsedArgs,
                              const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    Models::CloudResponse response = apiClient->getK8sHealth();
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- K8sRecoveryStatusCommand ---
K8sRecoveryStatusCommand::K8sRecoveryStatusCommand(
        std::shared_ptr<NMC::Core::CloudAPIClient> client
) : BaseCommand("recovery-status", "Inspect whether a workload restart is currently safe", std::move(client)) {
    usage = "nmc k8s recovery-status --cluster-id <id> --namespace <name> --deployment <name>";
    examples = "nmc k8s recovery-status --cluster-id rk1 --namespace gail --deployment gail";
    addFlag(CLI::Flag("c", "cluster-id", "Stable identity of the Continuum Kubernetes context", CLI::FlagType::String, true));
    addFlag(CLI::Flag("n", "namespace", "Workload namespace", CLI::FlagType::String, true));
    addFlag(CLI::Flag("d", "deployment", "Deployment name", CLI::FlagType::String, true));
}

int K8sRecoveryStatusCommand::execute(
        const std::map<std::string, std::string>& parsedFlags,
        const std::vector<std::string>& parsedArgs,
        const CLI::GlobalFlags& globalFlags
) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) return 1;
    const auto response = apiClient->getDeploymentRecoveryStatus(
            parsedFlags.at("cluster-id"), parsedFlags.at("namespace"), parsedFlags.at("deployment"));
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- K8sRestartDeploymentCommand ---
K8sRestartDeploymentCommand::K8sRestartDeploymentCommand(
        std::shared_ptr<NMC::Core::CloudAPIClient> client
) : BaseCommand("restart-deployment", "Safely restart one eligible workload Deployment", std::move(client)) {
    usage = "nmc k8s restart-deployment --cluster-id <id> --namespace <name> --deployment <name> --request-id <id>";
    examples = "nmc k8s restart-deployment --cluster-id rk1 --namespace gail --deployment gail --request-id incident-20261007-01";
    addFlag(CLI::Flag("c", "cluster-id", "Stable identity of the Continuum Kubernetes context", CLI::FlagType::String, true));
    addFlag(CLI::Flag("n", "namespace", "Workload namespace", CLI::FlagType::String, true));
    addFlag(CLI::Flag("d", "deployment", "Deployment name", CLI::FlagType::String, true));
    addFlag(CLI::Flag("r", "request-id", "Stable idempotency key for this recovery attempt", CLI::FlagType::String, true));
}

int K8sRestartDeploymentCommand::execute(
        const std::map<std::string, std::string>& parsedFlags,
        const std::vector<std::string>& parsedArgs,
        const CLI::GlobalFlags& globalFlags
) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) return 1;
    const auto& clusterId = parsedFlags.at("cluster-id");
    const auto& namespaceName = parsedFlags.at("namespace");
    const auto& deploymentName = parsedFlags.at("deployment");
    const auto& requestId = parsedFlags.at("request-id");
    const auto safeLabel = [](const std::string& value) {
        return !value.empty() && value.size() <= 63 && value.front() != '-' && value.back() != '-'
                && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
                    return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-';
                });
    };
    const bool validRequestId = requestId.size() >= 8 && requestId.size() <= 128
            && std::all_of(requestId.begin(), requestId.end(), [](unsigned char ch) {
                return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
                       || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
            });
    if (!safeLabel(clusterId) || !safeLabel(namespaceName) || !safeLabel(deploymentName) || !validRequestId) {
        std::cerr << "Error: cluster, namespace and Deployment must be Kubernetes DNS labels, and request-id must be 8-128 safe ASCII characters." << std::endl;
        return 1;
    }

    // Show the operator the fresh live decision and never submit an ineligible action.
    const auto preflight = apiClient->getDeploymentRecoveryStatus(clusterId, namespaceName, deploymentName);
    const auto data = preflight.data.is_object()
            ? preflight.data.value("data", nlohmann::json::object())
            : nlohmann::json::object();
    if (!preflight.success || !data.value("eligible", false)) {
        printOutput(preflight, globalFlags);
        return 1;
    }

    const auto response = apiClient->restartDeployment(clusterId, namespaceName, deploymentName, requestId);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- K8sResumeCommand ---
K8sResumeCommand::K8sResumeCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("resume", "Resumes a k8s cluster", std::move(client)) {
    usage = "nmc k8s resume ID";
    examples = "nmc k8s resume ID";
    addArgument(CLI::Argument("ID", "ID of the K8s cluster to resume", true, 0));
}

int K8sResumeCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient->resumeK8sCluster(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- K8sSuspendCommand ---
K8sSuspendCommand::K8sSuspendCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("suspend", "Suspends a k8s cluster", std::move(client)) {
    usage = "nmc k8s suspend ID";
    examples = "nmc k8s suspend ID";
    addArgument(CLI::Argument("ID", "ID of the K8s cluster to suspend", true, 0));
}

int K8sSuspendCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient->suspendK8sCluster(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

} // namespace NMC::Commands
