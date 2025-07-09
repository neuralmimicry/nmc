#include "VersionCommand.h"
#include <iostream>

namespace NMC::Commands {

VersionCommand::VersionCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("version", "CLI version", std::move(client)) {
    usage = "nmc version";
}

int VersionCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    std::cout << "nmc version 0.0.1 (alpha)" << std::endl;
    return 0;
}

} // namespace NMC::Commands
