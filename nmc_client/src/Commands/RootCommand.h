#ifndef NMC_COMMANDS_ROOT_COMMAND_H
#define NMC_COMMANDS_ROOT_COMMAND_H

#include "CLI/Command.h"
#include "Core/CloudAPIClient.h"
#include <iostream>

namespace NMC::Commands {

class RootCommand : public CLI::Command {
public:
    RootCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
private:
    std::shared_ptr<NMC::Core::CloudAPIClient> client_;
};

} // namespace NMC::Commands


#endif // NMC_COMMANDS_ROOT_COMMAND_H