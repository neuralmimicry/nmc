#include "VMCommands.h"
#include <iostream>

namespace NMC {
    namespace Commands {

// --- VmCommand (Parent) ---
        VmCommand::VmCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("vm", "Manage VMs in NMC", std::move(client)) {
            usage = "nmc vm [command]";
        }

        int VmCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
            printHelp();
            return 0;
        }

// --- VmCreateCommand ---
        VmCreateCommand::VmCreateCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("create", "Creates a new VM", std::move(client)) {
            usage = "nmc vm create my-vm --sku nmx-h100-80 --region us-east --os-image ubuntu-22.04 --public-key-id ID --init-script '#!/bin/bash example'";
            examples = "nmc vm create my-vm --sku nmx-h100-80 --region us-east --os-image ubuntu-22.04 --public-key-id ID --init-script '#!/bin/bash example'";
            addArgument(CLI::Argument("NAME", "Name of the VM", true, 0));
            addFlag(CLI::Flag("s", "sku", "VM Template SKU", CLI::FlagType::String, true));
            addFlag(CLI::Flag("r", "region", "Region", CLI::FlagType::String, true));
            addFlag(CLI::Flag("o", "os-image", "Operating System Image", CLI::FlagType::String, true));
            addFlag(CLI::Flag("p", "public-key-id", "Public Key ID", CLI::FlagType::String, true));
            addFlag(CLI::Flag("i", "init-script", "Path to cloud-init script", CLI::FlagType::String, false)); // Optional
        }

        int VmCreateCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
            if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
                return 1;
            }

            std::string name = parsedArgs[0];
            std::string sku = parsedFlags.count("sku") ? parsedFlags.at("sku") : "";
            std::string region = parsedFlags.count("region") ? parsedFlags.at("region") : "";
            std::string osImage = parsedFlags.count("os-image") ? parsedFlags.at("os-image") : "";
            std::string publicKeyId = parsedFlags.count("public-key-id") ? parsedFlags.at("public-key-id") : "";
            std::string initScript = parsedFlags.count("init-script") ? parsedFlags.at("init-script") : "";

            if (name.empty() || sku.empty() || region.empty() || osImage.empty() || publicKeyId.empty()) {
                std::cerr << "Error: Name, SKU, region, OS image, and public key ID are required." << std::endl;
                printHelp();
                return 1;
            }

            Models::CloudResponse response = apiClient->createVM(name, sku, region, osImage, publicKeyId, initScript);
            printOutput(response, globalFlags);
            return response.success ? 0 : 1;
        }

// --- VmDeleteCommand ---
        VmDeleteCommand::VmDeleteCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("delete", "Deletes a VM", std::move(client)) {
            usage = "nmc vm delete ID";
            examples = "nmc vm delete ID";
            addArgument(CLI::Argument("ID", "ID of the VM to delete", true, 0));
        }

        int VmDeleteCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
            if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
                return 1;
            }

            std::string id = parsedArgs[0];
            Models::CloudResponse response = apiClient->deleteVM(id);
            printOutput(response, globalFlags);
            return response.success ? 0 : 1;
        }

// --- VmGetCommand ---
        VmGetCommand::VmGetCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("get", "Get VM details", std::move(client)) {
            usage = "nmc vm get ID";
            examples = "nmc vm get ID";
            addArgument(CLI::Argument("ID", "ID of the VM to get", true, 0));
        }

        int VmGetCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
            if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
                return 1;
            }

            std::string id = parsedArgs[0];
            Models::CloudResponse response = apiClient->getVM(id);
            printOutput(response, globalFlags);
            return response.success ? 0 : 1;
        }

// --- VmListCommand ---
        VmListCommand::VmListCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("list", "List VMs", std::move(client)) {
            addAlias("ls");
            usage = "nmc vm list";
            examples = "nmc vm list";
        }

        int VmListCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
            if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
                return 1;
            }

            Models::CloudResponse response = apiClient->listVMs();
            printOutput(response, globalFlags);
            return response.success ? 0 : 1;
        }

// --- VmListLocationsCommand ---
        VmListLocationsCommand::VmListLocationsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("list-locations", "List VM locations", std::move(client)) {
            addAlias("ls-loc");
            usage = "nmc vm list-locations";
            examples = "nmc vm list-locations";
        }

        int VmListLocationsCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
            if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
                return 1;
            }

            Models::CloudResponse response = apiClient->listVMLocations(); // Corrected method name
            printOutput(response, globalFlags);
            return response.success ? 0 : 1;
        }

// --- VmListOSCommand ---
        VmListOSCommand::VmListOSCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("list-os", "List VM OS images", std::move(client)) {
            addAlias("ls-os");
            usage = "nmc vm list-os";
            examples = "nmc vm list-os";
        }

        int VmListOSCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
            if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
                return 1;
            }

            Models::CloudResponse response = apiClient->listVMOSImages(); // Corrected method name
            printOutput(response, globalFlags);
            return response.success ? 0 : 1;
        }

// --- VmListSkuCommand ---
        VmListSkuCommand::VmListSkuCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("list-sku", "List VM SKUs", std::move(client)) {
            addAlias("ls-sku");
            usage = "nmc vm list-sku";
            examples = "nmc vm list-sku";
        }

        int VmListSkuCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
            if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
                return 1;
            }

            Models::CloudResponse response = apiClient->listVMSKUs(); // Corrected method name
            printOutput(response, globalFlags);
            return response.success ? 0 : 1;
        }

// --- VmRestartCommand ---
        VmRestartCommand::VmRestartCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("restart", "Restarts a VM", std::move(client)) {
            usage = "nmc vm restart ID";
            examples = "nmc vm restart ID";
            addArgument(CLI::Argument("ID", "ID of the VM to restart", true, 0));
        }

        int VmRestartCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
            if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
                return 1;
            }

            std::string id = parsedArgs[0];
            Models::CloudResponse response = apiClient->restartVM(id);
            printOutput(response, globalFlags);
            return response.success ? 0 : 1;
        }

// --- VmResumeCommand ---
        VmResumeCommand::VmResumeCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("resume", "Resumes a VM", std::move(client)) {
            usage = "nmc vm resume ID";
            examples = "nmc vm resume ID";
            addArgument(CLI::Argument("ID", "ID of the VM to resume", true, 0));
        }

        int VmResumeCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
            if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
                return 1;
            }

            std::string id = parsedArgs[0];
            Models::CloudResponse response = apiClient->resumeVM(id);
            printOutput(response, globalFlags);
            return response.success ? 0 : 1;
        }

// --- VmSuspendCommand ---
        VmSuspendCommand::VmSuspendCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("suspend", "Suspends a VM", std::move(client)) {
            usage = "nmc vm suspend ID";
            examples = "nmc vm suspend ID";
            addArgument(CLI::Argument("ID", "ID of the VM to suspend", true, 0));
        }

        int VmSuspendCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
            if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
                return 1;
            }

            std::string id = parsedArgs[0];
            Models::CloudResponse response = apiClient->suspendVM(id);
            printOutput(response, globalFlags);
            return response.success ? 0 : 1;
        }

    } // namespace Commands
} // namespace NMC