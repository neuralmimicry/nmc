#include "SSHCommands.h"
#include <iostream>

namespace NMC {
namespace Commands {

// --- SSHCommand (Parent) ---
SSHCommand::SSHCommand() : BaseCommand("ssh", "Manage SSH key in NMC") {
    usage = "nmc ssh [command]";
}

int SSHCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    printHelp();
    return 0;
}

// --- SSHCreateCommand ---
SSHCreateCommand::SSHCreateCommand() : BaseCommand("create", "Creates a new SSH key") {
    usage = "nmc ssh create NAME --public-key <SSH_KEY> --description <DESCRIPTION>";
    examples = "nmc ssh create NAME --public-key <SSH_KEY> --description <DESCRIPTION>";
    addArgument(CLI::Argument("NAME", "Name of the SSH key", true, 0));
    addFlag(CLI::Flag("k", "public-key", "Public Key", CLI::FlagType::String, true));
    addFlag(CLI::Flag("d", "description", "Description", CLI::FlagType::String));
}

int SSHCreateCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    std::string name = parsedArgs[0];
    std::string publicKey = parsedFlags.count("public-key") ? parsedFlags.at("public-key") : "";
    std::string description = parsedFlags.count("description") ? parsedFlags.at("description") : "";

    if (name.empty() || publicKey.empty()) {
        std::cerr << "Error: SSH key name and public key are required." << std::endl;
        printHelp();
        return 1;
    }

    Models::CloudResponse response = apiClient.createSSHKey(name, publicKey, description);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- SSHDeleteCommand ---
SSHDeleteCommand::SSHDeleteCommand() : BaseCommand("delete", "Deletes a SSH key") {
    usage = "nmc ssh delete ID";
    examples = "nmc ssh delete ID";
    addArgument(CLI::Argument("ID", "ID of the SSH key to delete", true, 0));
}

int SSHDeleteCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    std::string id = parsedArgs[0];
    Models::CloudResponse response = apiClient.deleteSSHKey(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- SSHListCommand ---
SSHListCommand::SSHListCommand() : BaseCommand("list", "List SSH keys") {
    addAlias("ls");
    usage = "nmc ssh list";
    examples = "nmc ssh list";
    addFlag(CLI::Flag("f", "filter-name", "Filter by name", CLI::FlagType::String));
}

int SSHListCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    std::string filterName = parsedFlags.count("filter-name") ? parsedFlags.at("filter-name") : "";
    Models::CloudResponse response = apiClient.listSSHKeys(filterName);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

} // namespace Commands
} // namespace NMC