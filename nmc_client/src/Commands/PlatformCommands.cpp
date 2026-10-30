#include "PlatformCommands.h"

namespace NMC::Commands {

PlatformProductsCommand::PlatformProductsCommand(std::shared_ptr<Core::CloudAPIClient> client)
    : BaseCommand("products", "Read the platform product catalogue", std::move(client)) {
    usage = "nmc platform products [--output json]";
}

int PlatformProductsCommand::execute(const std::map<std::string, std::string>& flags,
                                     const std::vector<std::string>& args,
                                     const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(args) || !validateFlags(flags)) return 1;
    // Preserve the client's HTTP/auth failure semantics; catalogue metadata is
    // descriptive and never authorizes workload changes or recovery actions.
    const auto response = apiClient->getPlatformProducts();
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

PlatformCommand::PlatformCommand(std::shared_ptr<Core::CloudAPIClient> client)
    : BaseCommand("platform", "Discover NeuralMimicry product identities", std::move(client)) {
    usage = "nmc platform products";
}

int PlatformCommand::execute(const std::map<std::string, std::string>&,
                             const std::vector<std::string>&,
                             const CLI::GlobalFlags&) {
    printHelp();
    return 0;
}

} // namespace NMC::Commands
