#pragma once
#include "BaseCommand.h"

namespace NMC::Commands {
/** Read-only declared product identities; runtime observations and approval are
 * distinct contracts. The command never changes workload or access policy. */
class PlatformProductsCommand : public BaseCommand {
public:
    explicit PlatformProductsCommand(std::shared_ptr<Core::CloudAPIClient> client)
        : BaseCommand("products", "Read the platform product catalogue", std::move(client)) {
        usage = "nmc platform products [--output json]";
    }
    int execute(const std::map<std::string, std::string>& flags,
                const std::vector<std::string>& args,
                const CLI::GlobalFlags& globalFlags) override {
        if (!validateArguments(args) || !validateFlags(flags)) return 1;
        const auto response = apiClient->getPlatformProducts();
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }
};
class PlatformCommand : public BaseCommand {
public:
    explicit PlatformCommand(std::shared_ptr<Core::CloudAPIClient> client)
        : BaseCommand("platform", "Discover NeuralMimicry product identities", std::move(client)) {
        usage = "nmc platform products";
    }
    int execute(const std::map<std::string, std::string>&,
                const std::vector<std::string>&,
                const CLI::GlobalFlags&) override {
        printHelp();
        return 0;
    }
};
}
