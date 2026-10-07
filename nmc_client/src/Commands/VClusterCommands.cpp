#include "VClusterCommands.h"
#include <cstdint>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {

constexpr std::uintmax_t MAX_VCLUSTER_CONFIG_BYTES = 1024 * 1024;

/** Read a bounded JSON object without exposing file contents in error output. */
bool loadAdvancedConfig(const std::string& configPath, nlohmann::json& config, std::string& error) {
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(configPath, fileError) || fileError) {
        error = "--config-file must name a readable regular file.";
        return false;
    }

    const auto fileSize = std::filesystem::file_size(configPath, fileError);
    if (fileError) {
        error = "Could not inspect --config-file size.";
        return false;
    }
    if (fileSize > MAX_VCLUSTER_CONFIG_BYTES) {
        error = "--config-file must not exceed 1 MiB.";
        return false;
    }

    std::ifstream input(configPath, std::ios::in | std::ios::binary);
    if (!input) {
        error = "Could not open --config-file.";
        return false;
    }

    std::string contents(static_cast<std::size_t>(MAX_VCLUSTER_CONFIG_BYTES + 1), '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    const auto bytesRead = input.gcount();
    if (bytesRead < 0 || static_cast<std::uintmax_t>(bytesRead) > MAX_VCLUSTER_CONFIG_BYTES) {
        error = "--config-file must not exceed 1 MiB.";
        return false;
    }
    if (input.bad() || (input.fail() && !input.eof())) {
        error = "Could not read --config-file.";
        return false;
    }
    contents.resize(static_cast<std::size_t>(bytesRead));

    try {
        config = nlohmann::json::parse(contents);
    } catch (const nlohmann::json::exception&) {
        // Do not include parser excerpts: the file may contain sensitive values.
        error = "--config-file must contain valid JSON.";
        return false;
    }

    if (!config.is_object()) {
        error = "--config-file must contain a JSON object.";
        return false;
    }
    return true;
}

} // namespace

namespace NMC::Commands {

// --- VClusterCommand (Parent) ---
VClusterCommand::VClusterCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("vcluster", "Manage virtual Kubernetes clusters (vcluster)", std::move(client)) {
    usage = "nmc vcluster [command]";
}

int VClusterCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    printHelp();
    return 0;
}

// --- VClusterCreateCommand ---
VClusterCreateCommand::VClusterCreateCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("create", "Creates a new vcluster", std::move(client)) {
    usage = "nmc vcluster create NAME [--namespace NAMESPACE] [--config-file PATH]";
    examples = "nmc vcluster create my-vcluster\n"
               "nmc vcluster create my-vcluster --namespace vcluster-my-vcluster\n"
               "nmc vcluster create my-vcluster --config-file ./vcluster-config.json";
    addArgument(CLI::Argument("NAME", "Name of the vcluster", true, 0));
    addFlag(CLI::Flag("n", "namespace", "Namespace for the vcluster (optional, defaults to vcluster-NAME)", CLI::FlagType::String, false));
    addFlag(CLI::Flag("c", "config-file", "JSON file containing advanced vcluster configuration", CLI::FlagType::String, false));
}

int VClusterCreateCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& name = parsedArgs[0];
    std::string vclusterNamespace = parsedFlags.count("namespace") ? parsedFlags.at("namespace") : "";
    nlohmann::json advancedConfig = nlohmann::json::object();

    if (name.empty()) {
        std::cerr << "Error: VCluster name is required." << std::endl;
        printHelp();
        return 1;
    }

    const auto configFlag = parsedFlags.find("config-file");
    if (configFlag != parsedFlags.end()) {
        std::string error;
        if (!loadAdvancedConfig(configFlag->second, advancedConfig, error)) {
            std::cerr << "Error: " << error << std::endl;
            return 1;
        }
    }

    Models::CloudResponse response = apiClient->createVCluster(name, vclusterNamespace, advancedConfig);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VClusterDeleteCommand ---
VClusterDeleteCommand::VClusterDeleteCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("delete", "Deletes a vcluster", std::move(client)) {
    usage = "nmc vcluster delete ID_OR_NAME";
    examples = "nmc vcluster delete my-vcluster";
    addArgument(CLI::Argument("ID_OR_NAME", "ID or name of the vcluster to delete", true, 0));
}

int VClusterDeleteCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient->deleteVCluster(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VClusterGetCommand ---
VClusterGetCommand::VClusterGetCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("get", "Get vcluster details", std::move(client)) {
    addAlias("gv");
    usage = "nmc vcluster get ID_OR_NAME";
    examples = "nmc vcluster get my-vcluster";
    addArgument(CLI::Argument("ID_OR_NAME", "ID or name of the vcluster to get", true, 0));
}

int VClusterGetCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient->getVCluster(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VClusterGetConfigCommand ---
VClusterGetConfigCommand::VClusterGetConfigCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("get-config", "Get the kubeconfig for a vcluster", std::move(client)) {
    usage = "nmc vcluster get-config ID_OR_NAME";
    examples = "nmc vcluster get-config my-vcluster";
    addArgument(CLI::Argument("ID_OR_NAME", "ID or name of the vcluster to get config for", true, 0));
}

int VClusterGetConfigCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient->getVClusterKubeConfig(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VClusterListCommand ---
VClusterListCommand::VClusterListCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("list", "List vclusters", std::move(client)) {
    addAlias("ls");
    usage = "nmc vcluster list [--filter-name NAME_FILTER]";
    examples = "nmc vcluster list\nnmc vcluster list --filter-name my-vcluster";
    addFlag(CLI::Flag("f", "filter-name", "Filter vclusters by name", CLI::FlagType::String, false));
}

int VClusterListCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateFlags(parsedFlags)) {
        return 1;
    }

    std::string filterName = parsedFlags.count("filter-name") ? parsedFlags.at("filter-name") : "";
    Models::CloudResponse response = apiClient->listVClusters(filterName);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VCluster Lifecycle Commands ---

// --- VClusterPauseCommand ---
VClusterPauseCommand::VClusterPauseCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("pause", "Pause a vcluster (scale to 0 replicas)", std::move(client)) {
    usage = "nmc vcluster pause ID_OR_NAME";
    examples = "nmc vcluster pause my-vcluster";
    addArgument(CLI::Argument("ID_OR_NAME", "ID or name of the vcluster to pause", true, 0));
}

int VClusterPauseCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient->pauseVCluster(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VClusterResumeCommand ---
VClusterResumeCommand::VClusterResumeCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("resume", "Resume a paused vcluster", std::move(client)) {
    usage = "nmc vcluster resume ID_OR_NAME";
    examples = "nmc vcluster resume my-vcluster";
    addArgument(CLI::Argument("ID_OR_NAME", "ID or name of the vcluster to resume", true, 0));
}

int VClusterResumeCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient->resumeVCluster(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VClusterBackupCommand ---
VClusterBackupCommand::VClusterBackupCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("backup", "Backup a vcluster configuration", std::move(client)) {
    usage = "nmc vcluster backup ID_OR_NAME [--name BACKUP_NAME]";
    examples = "nmc vcluster backup my-vcluster\nnmc vcluster backup my-vcluster --name my-backup-2024";
    addArgument(CLI::Argument("ID_OR_NAME", "ID or name of the vcluster to backup", true, 0));
    addFlag(CLI::Flag("n", "name", "Name for the backup (optional)", CLI::FlagType::String, false));
}

int VClusterBackupCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    std::string backupName = parsedFlags.count("name") ? parsedFlags.at("name") : "";
    Models::CloudResponse response = apiClient->backupVCluster(id, backupName);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VClusterRestoreCommand ---
VClusterRestoreCommand::VClusterRestoreCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("restore", "Restore a vcluster from backup", std::move(client)) {
    usage = "nmc vcluster restore BACKUP_NAME [--target TARGET_NAME]";
    examples = "nmc vcluster restore my-backup-2024\nnmc vcluster restore my-backup-2024 --target restored-vcluster";
    addArgument(CLI::Argument("BACKUP_NAME", "Name of the backup to restore", true, 0));
    addFlag(CLI::Flag("t", "target", "Target name for the restored vcluster (optional)", CLI::FlagType::String, false));
}

int VClusterRestoreCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& backupName = parsedArgs[0];
    std::string targetName = parsedFlags.count("target") ? parsedFlags.at("target") : "";
    Models::CloudResponse response = apiClient->restoreVCluster(backupName, targetName);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VClusterUpgradeCommand ---
VClusterUpgradeCommand::VClusterUpgradeCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("upgrade", "Upgrade a vcluster to a new version", std::move(client)) {
    usage = "nmc vcluster upgrade ID_OR_NAME --version NEW_VERSION";
    examples = "nmc vcluster upgrade my-vcluster --version 0.20.0";
    addArgument(CLI::Argument("ID_OR_NAME", "ID or name of the vcluster to upgrade", true, 0));
    addFlag(CLI::Flag("v", "version", "New vcluster version", CLI::FlagType::String, true));
}

int VClusterUpgradeCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    const std::string& newVersion = parsedFlags.at("version");
    Models::CloudResponse response = apiClient->upgradeVCluster(id, newVersion);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VCluster Configuration Commands ---

// --- VClusterConfigGetCommand ---
VClusterConfigGetCommand::VClusterConfigGetCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("config-get", "Get vcluster configuration", std::move(client)) {
    usage = "nmc vcluster config-get ID_OR_NAME";
    examples = "nmc vcluster config-get my-vcluster";
    addArgument(CLI::Argument("ID_OR_NAME", "ID or name of the vcluster", true, 0));
}

int VClusterConfigGetCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient->getVClusterConfig(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VClusterConfigUpdateCommand ---
VClusterConfigUpdateCommand::VClusterConfigUpdateCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("config-update", "Update vcluster configuration", std::move(client)) {
    usage = "nmc vcluster config-update ID_OR_NAME --config CONFIG_JSON";
    examples = "nmc vcluster config-update my-vcluster --config '{\"resources\":{\"cpu_request\":\"500m\"}}'";
    addArgument(CLI::Argument("ID_OR_NAME", "ID or name of the vcluster", true, 0));
    addFlag(CLI::Flag("c", "config", "Configuration JSON string", CLI::FlagType::String, true));
}

int VClusterConfigUpdateCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    const std::string& configStr = parsedFlags.at("config");

    try {
        nlohmann::json config = nlohmann::json::parse(configStr);
        Models::CloudResponse response = apiClient->updateVClusterConfig(id, config);
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "Error: Invalid JSON in --config flag: " << e.what() << std::endl;
        return 1;
    }
}

// --- VCluster Monitoring Commands ---

// --- VClusterMetricsCommand ---
VClusterMetricsCommand::VClusterMetricsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("metrics", "Get vcluster metrics", std::move(client)) {
    usage = "nmc vcluster metrics ID_OR_NAME";
    examples = "nmc vcluster metrics my-vcluster";
    addArgument(CLI::Argument("ID_OR_NAME", "ID or name of the vcluster", true, 0));
}

int VClusterMetricsCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient->getVClusterMetrics(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VClusterHealthCommand ---
VClusterHealthCommand::VClusterHealthCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("health", "Get vcluster health status", std::move(client)) {
    usage = "nmc vcluster health ID_OR_NAME";
    examples = "nmc vcluster health my-vcluster";
    addArgument(CLI::Argument("ID_OR_NAME", "ID or name of the vcluster", true, 0));
}

int VClusterHealthCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient->getVClusterHealth(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VClusterResourcesCommand ---
VClusterResourcesCommand::VClusterResourcesCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("resources", "Get vcluster resources", std::move(client)) {
    usage = "nmc vcluster resources ID_OR_NAME";
    examples = "nmc vcluster resources my-vcluster";
    addArgument(CLI::Argument("ID_OR_NAME", "ID or name of the vcluster", true, 0));
}

int VClusterResourcesCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& id = parsedArgs[0];
    Models::CloudResponse response = apiClient->getVClusterResources(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

} // namespace NMC::Commands
