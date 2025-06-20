#ifndef NMC_COMMANDS_ROOT_COMMAND_H
#define NMC_COMMANDS_ROOT_COMMAND_H

#include "CLI/Command.h"
#include <iostream>

namespace NMC::Commands {

class RootCommand : public CLI::Command {
public:
    RootCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

} // namespace NMC::Commands


#endif // NMC_COMMANDS_ROOT_COMMAND_H