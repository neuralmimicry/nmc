#include "ModelCommands.h"
#include <iostream>

namespace NMC {
    namespace Commands {

// --- ModelCommand (Parent) ---
        ModelCommand::ModelCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("model", "Manage models in NMC", std::move(client)) {
            usage = "nmc model [command]";
        }

        int ModelCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
            printHelp();
            return 0;
        }

// --- ModelUploadCommand ---
        ModelUploadCommand::ModelUploadCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("upload", "Uploads a new Model Version", std::move(client)) {
            usage = "nmc model upload <file-path> --sku <model-sku> --registry-name <registry-name> --tag v0.0.1";
            examples = "nmc model upload <file-path> --sku <model-sku> --registry-name <registry-name> --tag v0.0.1";
            addArgument(CLI::Argument("file-path", "Path to the model file", true, 0));
            addFlag(CLI::Flag("s", "sku", "Model SKU", CLI::FlagType::String, true));
            addFlag(CLI::Flag("r", "registry-name", "Model Registry Name", CLI::FlagType::String, true));
            addFlag(CLI::Flag("t", "tag", "Model Version Tag", CLI::FlagType::String, true));
        }

        int ModelUploadCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
            if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
                return 1;
            }

            std::string filePath = parsedArgs[0];
            std::string sku = parsedFlags.count("sku") ? parsedFlags.at("sku") : "";
            std::string registryName = parsedFlags.count("registry-name") ? parsedFlags.at("registry-name") : "";
            std::string tag = parsedFlags.count("tag") ? parsedFlags.at("tag") : "";

            if (filePath.empty() || sku.empty() || registryName.empty() || tag.empty()) {
                std::cerr << "Error: File path, SKU, registry name, and tag are required." << std::endl;
                printHelp();
                return 1;
            }

            Models::CloudResponse response = apiClient->uploadModel(filePath, sku, registryName, tag); // Corrected
            printOutput(response, globalFlags);
            return response.success ? 0 : 1;
        }

    } // namespace Commands
} // namespace NMC
