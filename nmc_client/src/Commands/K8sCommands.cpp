#include "K8sCommands.h"
#include <iostream>

namespace NMC::Commands {

// --- K8sCommand (Parent) ---
K8sCommand::K8sCommand() : BaseCommand("k8s", "Manage k8s clusters in NMC") {
    usage = "nmc k8s [command]";
}

int K8sCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    printHelp();
    return 0;
}

// --- K8sCreateCommand ---
K8sCreateCommand::K8sCreateCommand() : BaseCommand("create", "Creates a new k8s cluster") {
    usage = "nmc k8s create NAME --region lille-4";
    examples = "nmc k8s create NAME --region lille-4";
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

    Models::CloudResponse response = apiClient.createK8sCluster(name, region);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- K8sDeleteCommand ---
K8sDeleteCommand::K8sDeleteCommand() : BaseCommand("delete", "Deletes a K8s cluster") {
    usage = "nmc k8s delete ID";
    examples = "nmc k8s delete ID";
    addArgument(CLI::Argument("ID", "ID of the K8s cluster to delete", true, 0));
}

int K8sDeleteCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient.deleteK8sCluster(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- K8sGetCommand ---
K8sGetCommand::K8sGetCommand() : BaseCommand("get", "Get k8s cluster") {
    addAlias("gk");
    usage = "nmc k8s get ID";
    examples = "nmc k8s get ID";
    addArgument(CLI::Argument("ID", "ID of the K8s cluster to get", true, 0));
}

int K8sGetCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient.getK8sCluster(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- K8sGetConfigCommand ---
K8sGetConfigCommand::K8sGetConfigCommand() : BaseCommand("get-config", "Get the KubeConfig file") {
    usage = "nmc k8s get-config ID";
    examples = "nmc k8s get-config ID";
    addArgument(CLI::Argument("ID", "ID of the K8s cluster to get config for", true, 0));
}

int K8sGetConfigCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient.getKubeConfig(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- K8sListCommand ---
K8sListCommand::K8sListCommand() : BaseCommand("list", "List of k8s clusters") {
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

    Models::CloudResponse response = apiClient.listK8sClusters(filterName);
    printOutput(response, globalFlags);
    if (watch) {
        std::cout << "Watching for changes... (Not implemented in mock)" << std::endl;
    }
    return response.success ? 0 : 1;
}

// --- K8sListLocationsCommand ---
K8sListLocationsCommand::K8sListLocationsCommand() : BaseCommand("list-locations", "List k8s cluster locations") {
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
    Models::CloudResponse response = apiClient.listK8sLocations(filterSku);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- K8sResumeCommand ---
K8sResumeCommand::K8sResumeCommand() : BaseCommand("resume", "Resumes a k8s cluster") {
    usage = "nmc k8s resume ID";
    examples = "nmc k8s resume ID";
    addArgument(CLI::Argument("ID", "ID of the K8s cluster to resume", true, 0));
}

int K8sResumeCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient.resumeK8sCluster(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- K8sSuspendCommand ---
K8sSuspendCommand::K8sSuspendCommand() : BaseCommand("suspend", "Suspends a k8s cluster") {
    usage = "nmc k8s suspend ID";
    examples = "nmc k8s suspend ID";
    addArgument(CLI::Argument("ID", "ID of the K8s cluster to suspend", true, 0));
}

int K8sSuspendCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient.suspendK8sCluster(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

} // namespace NMC::Commands
