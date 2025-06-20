#ifndef NMC_COMMANDS_SSH_COMMANDS_H
#define NMC_COMMANDS_SSH_COMMANDS_H

#include "BaseCommand.h"

namespace NMC::Commands {

class SSHCommand : public BaseCommand {
public:
    SSHCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class SSHCreateCommand : public BaseCommand {
public:
    SSHCreateCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class SSHDeleteCommand : public BaseCommand {
public:
    SSHDeleteCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class SSHListCommand : public BaseCommand {
public:
    SSHListCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

} // namespace NMC::Commands


#endif // NMC_COMMANDS_SSH_COMMANDS_H