#include "BucketCommands.h"
#include <iostream>


namespace NMC::Commands {

// --- BucketCommand (Parent) ---
BucketCommand::BucketCommand() : BaseCommand("bucket", "Manage Buckets in NMC") {
    usage = "nmc bucket [command]";
    // No direct flags for 'bucket' command itself, only for its subcommands
}

int BucketCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    // This execute should typically not be hit if a subcommand is matched.
    // If it is, it means no subcommand was provided or a global help was asked.
    printHelp();
    return 0;
}

// --- BucketCreateCommand ---
BucketCreateCommand::BucketCreateCommand() : BaseCommand("create", "Create a new bucket") {
    usage = "nmc bucket create NAME --location eu-central --type standard";
    examples = "nmc bucket create NAME --location eu-central --type standard";
    addArgument(CLI::Argument("NAME", "Name of the bucket", true, 0));
    addFlag(CLI::Flag("r", "location", "Bucket location", CLI::FlagType::String, true));
    addFlag(CLI::Flag("t", "type", "Bucket type", CLI::FlagType::String, true));
}

int BucketCreateCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& name = parsedArgs[0];
    std::string location = parsedFlags.count("location") ? parsedFlags.at("location") : "";
    std::string type = parsedFlags.count("type") ? parsedFlags.at("type") : "";

    if (name.empty() || location.empty() || type.empty()) {
        std::cerr << "Error: Bucket name, location, and type are required." << std::endl;
        printHelp();
        return 1;
    }

    Models::CloudResponse response = apiClient.createBucket(name, location, type);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- BucketDeleteCommand ---
BucketDeleteCommand::BucketDeleteCommand() : BaseCommand("delete", "Delete a bucket") {
    usage = "nmc bucket delete NAME";
    examples = "nmc bucket delete NAME";
    addArgument(CLI::Argument("NAME", "Name of the bucket to delete", true, 0));
}

int BucketDeleteCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& name = parsedArgs[0];
    Models::CloudResponse response = apiClient.deleteBucket(name);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- BucketGetCommand ---
BucketGetCommand::BucketGetCommand() : BaseCommand("get", "Get bucket details") {
    addAlias("gs");
    usage = "nmc bucket get NAME";
    examples = "nmc bucket get NAME";
    addArgument(CLI::Argument("NAME", "Name of the bucket to get details for", true, 0));
}

int BucketGetCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& name = parsedArgs[0];
    Models::CloudResponse response = apiClient.getBucket(name);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- BucketListCommand ---
BucketListCommand::BucketListCommand() : BaseCommand("list", "List of buckets") {
    addAlias("ls");
    usage = "nmc bucket list";
    examples = "nmc bucket list";
    addFlag(CLI::Flag("f", "filter-name", "Filter by name", CLI::FlagType::String));
}

int BucketListCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    std::string filterName = parsedFlags.count("filter-name") ? parsedFlags.at("filter-name") : "";
    Models::CloudResponse response = apiClient.listBuckets(filterName);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- BucketListLocationsCommand ---
BucketListLocationsCommand::BucketListLocationsCommand() : BaseCommand("list-locations", "List of bucket locations") {
    addAlias("ls-loc");
    usage = "nmc bucket list-locations";
    examples = "nmc bucket list-locations";
}

int BucketListLocationsCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    Models::CloudResponse response = apiClient.listBucketLocations();
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- BucketListTypesCommand ---
BucketListTypesCommand::BucketListTypesCommand() : BaseCommand("list-types", "List of bucket types") {
    addAlias("ls-typ");
    usage = "nmc bucket list-types";
    examples = "nmc bucket list-types";
}

int BucketListTypesCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    Models::CloudResponse response = apiClient.listBucketTypes();
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

// --- BucketResetKeyCommand ---
BucketResetKeyCommand::BucketResetKeyCommand() : BaseCommand("reset-key", "Reset bucket access credentials") {
    usage = "nmc bucket reset-key NAME";
    examples = "nmc bucket reset-key NAME";
    addArgument(CLI::Argument("NAME", "Name of the bucket to reset key for", true, 0));
}

int BucketResetKeyCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string& name = parsedArgs[0];
    Models::CloudResponse response = apiClient.resetBucketKey(name);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}


} // namespace NMC::Commands
