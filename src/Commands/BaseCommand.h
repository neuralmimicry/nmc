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
    Core::CloudAPIClient apiClient;

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
            Core::Utils::printJson(response.data.empty() ? response.message : response.data);
        } else if (globalFlags.outputFormat == "json-line") {
            // For simplicity, json-line will just print the json on one line
            Core::Utils::printJson(response.data.empty() ? response.message : response.data);
        } else if (globalFlags.outputFormat == "yaml") {
            Core::Utils::printYaml(response.data.empty() ? response.message : response.data);
        } else {
            Core::Utils::printHumanReadable(response.message);
            if (!response.data.empty()) {
                std::cout << "Details:\n" << response.data << std::endl;
            }
        }
    }
};

} // namespace NMC::Commands


#endif // NMC_COMMANDS_BASE_COMMAND_H