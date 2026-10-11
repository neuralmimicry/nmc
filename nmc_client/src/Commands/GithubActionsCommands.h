#ifndef NMC_COMMANDS_GITHUB_ACTIONS_COMMANDS_H
#define NMC_COMMANDS_GITHUB_ACTIONS_COMMANDS_H

#include "BaseCommand.h"

namespace NMC::Commands {

class GithubActionsCommand : public BaseCommand {
public:
    explicit GithubActionsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

} // namespace NMC::Commands

#endif // NMC_COMMANDS_GITHUB_ACTIONS_COMMANDS_H
