#include "SSHCommands.h"
#include <iostream>

namespace NMC {
    namespace Commands {

// --- SSHCommand (Parent) ---
        SSHCommand::SSHCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("ssh", "Manage SSH keys in NMC", std::move(client)) {
            usage = "nmc ssh [command]";
        }

        int SSHCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
            printHelp();
            return 0;
        }

// --- SSHCreateCommand ---
        SSHCreateCommand::SSHCreateCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("create", "Create a new SSH key", std::move(client)) {
            usage = "nmc ssh create NAME --public-key 'ssh-rsa AAAA... user@host' --description 'My new key'";
            examples = "nmc ssh create my-key --public-key \"ssh-rsa AAAA... user@host\" --description \"Key for my laptop\"";
            addArgument(CLI::Argument("NAME", "Name of the SSH key", true, 0));
            addFlag(CLI::Flag("p", "public-key", "Public key string (e.g., 'ssh-rsa AAAA... user@host')", CLI::FlagType::String, true));
            addFlag(CLI::Flag("d", "description", "Description of the SSH key", CLI::FlagType::String, false));
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

            Models::CloudResponse response = apiClient->createSSHKey(name, publicKey, description);
            printOutput(response, globalFlags);
            return response.success ? 0 : 1;
        }

// --- SSHDeleteCommand ---
        SSHDeleteCommand::SSHDeleteCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("delete", "Delete an existing SSH key", std::move(client)) {
            usage = "nmc ssh delete NAME";
            examples = "nmc ssh delete my-key";
            addArgument(CLI::Argument("NAME", "Name of the SSH key to delete", true, 0));
        }

        int SSHDeleteCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
            if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
                return 1;
            }

            std::string name = parsedArgs[0];
            if (name.empty()) {
                std::cerr << "Error: SSH key name is required." << std::endl;
                printHelp();
                return 1;
            }

            Models::CloudResponse response = apiClient->deleteSSHKey(name);
            printOutput(response, globalFlags);
            return response.success ? 0 : 1;
        }

// --- SSHGetCommand ---
        SSHGetCommand::SSHGetCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("get", "Get details of a specific SSH key", std::move(client)) {
            usage = "nmc ssh get NAME";
            examples = "nmc ssh get my-key";
            addArgument(CLI::Argument("NAME", "Name of the SSH key to retrieve", true, 0));
        }

        int SSHGetCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
            if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
                return 1;
            }

            std::string name = parsedArgs[0];
            if (name.empty()) {
                std::cerr << "Error: SSH key name is required." << std::endl;
                printHelp();
                return 1;
            }

            Models::CloudResponse response = apiClient->getSSHKey(name);
            printOutput(response, globalFlags);
            return response.success ? 0 : 1;
        }

// --- SSHListCommand ---
        SSHListCommand::SSHListCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("list", "List all configured SSH keys", std::move(client)) {
            usage = "nmc ssh list";
            examples = "nmc ssh list";
            addAlias("ls");
        }

        int SSHListCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
            if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
                return 1;
            }

            Models::CloudResponse response = apiClient->listSSHKeys();
            printOutput(response, globalFlags);
            return response.success ? 0 : 1;
        }

    } // namespace Commands
} // namespace NMC