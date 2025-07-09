// nmc_client/src/Commands/BaseCommand.h
#ifndef NMC_COMMANDS_BASE_COMMAND_H
#define NMC_COMMANDS_BASE_COMMAND_H

#include "CLI/Command.h"
#include "CLI/GlobalFlags.h"
#include "Core/CloudAPIClient.h"
#include "Core/Utils.h"
#include <string>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace NMC::Commands {

// Base class for all specific nmc commands.
// It provides access to the CloudAPIClient.
    class BaseCommand : public CLI::Command {
    private:

    protected:
        std::shared_ptr<Core::CloudAPIClient> apiClient;

    public:
        // Constructor now uses the member initializer list for apiClient
        BaseCommand(const std::string& name,
                    const std::string& description)
              : CLI::Command(name, description),
                apiClient(std::make_shared<Core::CloudAPIClient>(
                   "127.0.0.1",
                   8080,
                   Core::Utils::getConfigPath()
                 )) {
            // Initialization is complete. Constructor body can be empty or used for other tasks.
        }

        BaseCommand(const std::string& name,
                    const std::string& description,
                    std::shared_ptr<Core::CloudAPIClient> client)
              : CLI::Command(name, description),
                apiClient(std::move(client))
            {}

        // Helper to print output based on global format (no changes needed here)
        static void printOutput(const Models::CloudResponse& response, const CLI::GlobalFlags& globalFlags) {
            if (!response.success) {
                std::cerr << "Error: " << response.message << std::endl;
                return;
            }

            if (globalFlags.outputFormat == "json") {
                if (response.data.is_string()) {
                    Core::Utils::printJson(response.data.get<std::string>());
                } else if (response.data.is_null()) {
                    Core::Utils::printJson(response.message);
                }
                else {
                    Core::Utils::printJson(response.data.dump());
                }
            } else if (globalFlags.outputFormat == "yaml") {
                if (response.data.is_string()) {
                    Core::Utils::printYaml(response.data.get<std::string>());
                } else if (response.data.is_null()) {
                    Core::Utils::printYaml(response.message);
                }
                else {
                    Core::Utils::printYaml(response.data.dump());
                }
            } else {
                Core::Utils::printHumanReadable(response.message);
                if (!response.data.empty()) {
                    if (response.data.is_string()) {
                        std::cout << "Details:\n" << response.data.get<std::string>() << std::endl;
                    } else {
                        std::cout << "Details:\n" << response.data.dump(4) << std::endl;
                    }
                }
            }
        }
    };

} // namespace NMC::Commands

#endif // NMC_COMMANDS_BASE_COMMAND_H