#include "VMCommands.h"
#include <iostream>

namespace NMC {
namespace Commands {

// --- VmCommand (Parent) ---
VmCommand::VmCommand() : BaseCommand("vm", "Manage VMs in NMC") {
    usage = "nmc vm [command]";
}

int VmCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    printHelp();
    return 0;
}

// --- VmCreateCommand ---
VmCreateCommand::VmCreateCommand() : BaseCommand("create", "Creates a new VM") {
    usage = "nmc vm create my-vm --sku nmx-h100-80 --region us-east --os-image ubuntu-22.04 --public-key-id ID --init-script '#!/bin/bash example'";
    examples = "nmc vm create my-vm --sku nmx-h100-80 --region us-east --os-image ubuntu-22.04 --public-key-id ID --init-script '#!/bin/bash example'";
    addArgument(CLI::Argument("NAME", "Name of the VM", true, 0));
    addFlag(CLI::Flag("s", "sku", "VM Template SKU", CLI::FlagType::String, true));
    addFlag(CLI::Flag("r", "region", "Region", CLI::FlagType::String, true));
    addFlag(CLI::Flag("i", "os-image", "OS Image SKU", CLI::FlagType::String, true));
    addFlag(CLI::Flag("k", "public-key-id", "Public Key ID", CLI::FlagType::String, true));
    addFlag(CLI::Flag("n", "init-script", "Init script", CLI::FlagType::String));
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
        std::cerr << "Error: VM name, SKU, region, OS image, and public key ID are required." << std::endl;
        printHelp();
        return 1;
    }

    Models::CloudResponse response = apiClient.createVM(name, sku, region, osImage, publicKeyId, initScript);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VmDeleteCommand ---
VmDeleteCommand::VmDeleteCommand() : BaseCommand("delete", "Deletes a VM") {
    usage = "nmc vm delete ID";
    examples = "nmc vm delete ID";
    addArgument(CLI::Argument("ID", "ID of the VM to delete", true, 0));
}

int VmDeleteCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    std::string id = parsedArgs[0];
    Models::CloudResponse response = apiClient.deleteVM(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VmGetCommand ---
VmGetCommand::VmGetCommand() : BaseCommand("get", "Get VM") {
    addAlias("gs");
    usage = "nmc vm get ID";
    examples = "nmc vm get ID";
    addArgument(CLI::Argument("ID", "ID of the VM to get details for", true, 0));
}

int VmGetCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    std::string id = parsedArgs[0];
    Models::CloudResponse response = apiClient.getVM(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VmListCommand ---
VmListCommand::VmListCommand() : BaseCommand("list", "List of VMs") {
    addAlias("ls");
    usage = "nmc vm list";
    examples = "nmc vm ls";
    addFlag(CLI::Flag("f", "filter-name", "Filter by name", CLI::FlagType::String));
}

int VmListCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    std::string filterName = parsedFlags.count("filter-name") ? parsedFlags.at("filter-name") : "";
    Models::CloudResponse response = apiClient.listVMs(filterName);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VmListLocationsCommand ---
VmListLocationsCommand::VmListLocationsCommand() : BaseCommand("list-locations", "List VM locations") {
    addAlias("ls-loc");
    usage = "nmc vm list-locations";
    examples = "nmc vm list-locations";
    addFlag(CLI::Flag("f", "filter-sku", "Filter by sku", CLI::FlagType::String));
}

int VmListLocationsCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    std::string filterSku = parsedFlags.count("filter-sku") ? parsedFlags.at("filter-sku") : "";
    Models::CloudResponse response = apiClient.listVMLocations(filterSku);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VmListOSCommand ---
VmListOSCommand::VmListOSCommand() : BaseCommand("list-os", "List VM OS Images") {
    addAlias("ls-os");
    usage = "nmc vm list-os";
    examples = "nmc vm list-os";
    addFlag(CLI::Flag("f", "filter-sku", "Filter by sku", CLI::FlagType::String));
}

int VmListOSCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    std::string filterSku = parsedFlags.count("filter-sku") ? parsedFlags.at("filter-sku") : "";
    Models::CloudResponse response = apiClient.listVMOSImages(filterSku);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VmListSkuCommand ---
VmListSkuCommand::VmListSkuCommand() : BaseCommand("list-sku", "List VM SKUs") {
    addAlias("ls-sku");
    usage = "nmc vm list-sku";
    examples = "nmc vm list-sku";
    addFlag(CLI::Flag("f", "filter-sku", "Filter by sku", CLI::FlagType::String));
}

int VmListSkuCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    std::string filterSku = parsedFlags.count("filter-sku") ? parsedFlags.at("filter-sku") : "";
    Models::CloudResponse response = apiClient.listVMSKUs(filterSku);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VmRestartCommand ---
VmRestartCommand::VmRestartCommand() : BaseCommand("restart", "Restarts a VM") {
    usage = "nmc vm restart ID";
    examples = "nmc vm restart ID";
    addArgument(CLI::Argument("ID", "ID of the VM to restart", true, 0));
}

int VmRestartCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    std::string id = parsedArgs[0];
    Models::CloudResponse response = apiClient.restartVM(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VmResumeCommand ---
VmResumeCommand::VmResumeCommand() : BaseCommand("resume", "Resumes a VM") {
    usage = "nmc vm resume ID";
    examples = "nmc vm resume ID";
    addArgument(CLI::Argument("ID", "ID of the VM to resume", true, 0));
}

int VmResumeCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    std::string id = parsedArgs[0];
    Models::CloudResponse response = apiClient.resumeVM(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- VmSuspendCommand ---
VmSuspendCommand::VmSuspendCommand() : BaseCommand("suspend", "Suspends a VM") {
    usage = "nmc vm suspend ID";
    examples = "nmc vm suspend ID";
    addArgument(CLI::Argument("ID", "ID of the VM to suspend", true, 0));
}

int VmSuspendCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    std::string id = parsedArgs[0];
    Models::CloudResponse response = apiClient.suspendVM(id);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

} // namespace Commands
} // namespace NMC