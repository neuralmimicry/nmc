#include "ServerCommands.h"

namespace NMC::Commands {

ServerCommand::ServerCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("server", "Inspect nmc_server metadata and health", std::move(client)) {
    usage = "nmc server [command]";
}

int ServerCommand::execute(const std::map<std::string, std::string>&,
                           const std::vector<std::string>&,
                           const CLI::GlobalFlags&) {
    // Parent command is a namespace; default action is help output.
    printHelp();
    return 0;
}

ServerHealthCommand::ServerHealthCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("health", "Check server health endpoint", std::move(client)) {
    usage = "nmc server health";
    examples = "nmc server health";
}

int ServerHealthCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                 const std::vector<std::string>& parsedArgs,
                                 const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    Models::CloudResponse response = apiClient->getServerHealth();
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

ServerVersionCommand::ServerVersionCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("version", "Fetch server version metadata", std::move(client)) {
    usage = "nmc server version";
    examples = "nmc server version";
}

int ServerVersionCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                  const std::vector<std::string>& parsedArgs,
                                  const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    Models::CloudResponse response = apiClient->getServerVersion();
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

} // namespace NMC::Commands
