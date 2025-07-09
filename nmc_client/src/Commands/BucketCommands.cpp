#include "BucketCommands.h"
#include <iostream>


namespace NMC::Commands {

// --- BucketCommand (Parent) ---
    BucketCommand::BucketCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("bucket", "Manage Buckets in NMC", std::move(client)) {
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
    BucketCreateCommand::BucketCreateCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("create", "Create a new bucket", std::move(client)) {
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

        std::string name = parsedArgs[0];
        std::string location = parsedFlags.count("location") ? parsedFlags.at("location") : "";
        std::string type = parsedFlags.count("type") ? parsedFlags.at("type") : "";

        if (name.empty() || location.empty() || type.empty()) {
            std::cerr << "Error: Bucket name, location, and type are required." << std::endl;
            printHelp();
            return 1;
        }

        Models::CloudResponse response = apiClient->createBucket(name, location, type); // Corrected
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- BucketDeleteCommand ---
    BucketDeleteCommand::BucketDeleteCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("delete", "Delete an existing bucket", std::move(client)) {
        usage = "nmc bucket delete NAME";
        examples = "nmc bucket delete my-bucket";
        addArgument(CLI::Argument("NAME", "Name of the bucket to delete", true, 0));
    }

    int BucketDeleteCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        std::string name = parsedArgs[0];
        if (name.empty()) {
            std::cerr << "Error: Bucket name is required." << std::endl;
            printHelp();
            return 1;
        }

        Models::CloudResponse response = apiClient->deleteBucket(name); // Corrected
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- BucketGetCommand ---
    BucketGetCommand::BucketGetCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("get", "Get details of a specific bucket", std::move(client)) {
        usage = "nmc bucket get NAME";
        examples = "nmc bucket get my-bucket";
        addArgument(CLI::Argument("NAME", "Name of the bucket to retrieve", true, 0));
    }

    int BucketGetCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        std::string name = parsedArgs[0];
        if (name.empty()) {
            std::cerr << "Error: Bucket name is required." << std::endl;
            printHelp();
            return 1;
        }

        Models::CloudResponse response = apiClient->getBucket(name); // Corrected
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- BucketListCommand ---
    BucketListCommand::BucketListCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("list", "List all configured buckets", std::move(client)) {
        usage = "nmc bucket list";
        examples = "nmc bucket list";
        addAlias("ls");
    }

    int BucketListCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        Models::CloudResponse response = apiClient->listBuckets();
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- BucketListLocationsCommand ---
    BucketListLocationsCommand::BucketListLocationsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("list-locations", "List available bucket locations", std::move(client)) {
        addAlias("ls-loc");
        usage = "nmc bucket list-locations";
        examples = "nmc bucket list-locations";
    }

    int BucketListLocationsCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        Models::CloudResponse response = apiClient->listBucketLocations();
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- BucketListTypesCommand ---
    BucketListTypesCommand::BucketListTypesCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("list-types", "List of bucket types", std::move(client)) {
        addAlias("ls-typ");
        usage = "nmc bucket list-types";
        examples = "nmc bucket list-types";
    }

    int BucketListTypesCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        Models::CloudResponse response = apiClient->listBucketTypes();
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- BucketResetKeyCommand ---
    BucketResetKeyCommand::BucketResetKeyCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("reset-key", "Reset bucket access credentials", std::move(client)) {
        usage = "nmc bucket reset-key NAME";
        examples = "nmc bucket reset-key NAME";
        addArgument(CLI::Argument("NAME", "Name of the bucket to reset key for", true, 0));
    }

    int BucketResetKeyCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        const std::string& name = parsedArgs[0];
        Models::CloudResponse response = apiClient->resetBucketKey(name);
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

} // namespace NMC::Commands