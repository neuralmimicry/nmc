#include "RootCommand.h"
#include <iostream>

namespace NMC::Commands {

RootCommand::RootCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : CLI::Command("nmc", "CLI tool for NeuralMimicry Continuum Cloud Service") {
    // Root command doesn't have direct execution logic, it just lists subcommands.
    // Its `execute` method should typically not be called directly if subcommands are found.
    // The help flag is added by the base CLI::Command.
    usage = "nmc [command]";
}

int RootCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    // This execute should ideally not be hit if a subcommand is matched.
    // If it is, it means no subcommand was provided or a global help was asked.
    printHelp(); // Print root help
    return 0;
}

} // namespace NMC::Commands
