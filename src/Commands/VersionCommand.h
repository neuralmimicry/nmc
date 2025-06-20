#ifndef NMC_COMMANDS_VERSION_COMMAND_H
#define NMC_COMMANDS_VERSION_COMMAND_H

#include "BaseCommand.h"

namespace NMC::Commands {

class VersionCommand : public BaseCommand {
public:
    VersionCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

} // namespace NMC::Commands


#endif // NMC_COMMANDS_VERSION_COMMAND_H