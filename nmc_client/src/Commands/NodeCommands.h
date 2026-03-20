#ifndef NMC_NODE_COMMANDS_H
#define NMC_NODE_COMMANDS_H

#include "BaseCommand.h"

namespace NMC::Commands {

class NodeCommand : public BaseCommand {
public:
    explicit NodeCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class NodeRecruitCommand : public BaseCommand {
public:
    explicit NodeRecruitCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

} // namespace NMC::Commands

#endif // NMC_NODE_COMMANDS_H
