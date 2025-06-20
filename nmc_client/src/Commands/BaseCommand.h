// nmc_client/src/Commands/BaseCommand.h
#ifndef NMC_COMMANDS_BASE_COMMAND_H
#define NMC_COMMANDS_BASE_COMMAND_H

#include "CLI/Command.h"
#include "CLI/GlobalFlags.h"
#include "Core/CloudAPIClient.h"
#include "Core/Utils.h"


namespace NMC::Commands {

// Base class for all specific nmc commands.
// It provides access to the CloudAPIClient.
    class BaseCommand : public CLI::Command {
    protected:
        // Initialize apiClient with host and port for the NMC server
        Core::CloudAPIClient apiClient{"127.0.0.1", 8080}; // Assuming server runs on localhost:8080

    public:
        BaseCommand(const std::string& name, const std::string& description)
                : CLI::Command(name, description) {}

        // Helper to print output based on global format
        static void printOutput(const Models::CloudResponse& response, const CLI::GlobalFlags& globalFlags) {
            if (!response.success) {
                std::cerr << "Error: " << response.message << std::endl;
                return;
            }

            if (globalFlags.outputFormat == "json") {
                // Ensure data is stringified if it's already a json object/array,
                // or directly print if it's a simple string message.
                if (response.data.is_string()) {
                    Core::Utils::printJson(response.data.get<std::string>());
                } else if (response.data.is_null()) {
                    Core::Utils::printJson(response.message);
                }
                else {
                    Core::Utils::printJson(response.data.dump());
                }
            } else if (globalFlags.outputFormat == "json-line") {
                // For simplicity, json-line will just print the json on one line
                if (response.data.is_string()) {
                    Core::Utils::printJson(response.data.get<std::string>());
                } else if (response.data.is_null()) {
                    Core::Utils::printJson(response.message);
                }
                else {
                    Core::Utils::printJson(response.data.dump());
                }
            } else if (globalFlags.outputFormat == "yaml") {
                // This would require a YAML library for proper conversion.
                // For now, if data is JSON, dump it as string for YAML placeholder.
                if (response.data.is_string()) {
                    Core::Utils::printYaml(response.data.get<std::string>());
                } else if (response.data.is_null()) {
                    Core::Utils::printYaml(response.message);
                }
                else {
                    Core::Utils::printYaml(response.data.dump());
                }
            } else {
                // Human-readable format
                Core::Utils::printHumanReadable(response.message);
                if (!response.data.empty()) { // Check if data is not an empty JSON object
                    // If data is a string (like kubeconfig), print it directly.
                    // Otherwise, dump it as JSON for readability.
                    if (response.data.is_string()) {
                        std::cout << "Details:\n" << response.data.get<std::string>() << std::endl;
                    } else {
                        std::cout << "Details:\n" << response.data.dump(4) << std::endl; // Pretty print JSON
                    }
                }
            }
        }
    };

} // namespace NMC::Commands


#endif // NMC_COMMANDS_BASE_COMMAND_H