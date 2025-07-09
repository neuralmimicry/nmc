#ifndef NMC_COMMANDS_SSH_COMMANDS_H
#define NMC_COMMANDS_SSH_COMMANDS_H

#include "BaseCommand.h"

namespace NMC::Commands {

class SSHCommand : public BaseCommand {
public:
    SSHCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class SSHCreateCommand : public BaseCommand {
public:
    SSHCreateCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class SSHDeleteCommand : public BaseCommand {
public:
    SSHDeleteCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class SSHListCommand : public BaseCommand {
public:
    SSHListCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class SSHGetCommand : public BaseCommand {
public:
    SSHGetCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};
} // namespace NMC::Commands


#endif // NMC_COMMANDS_SSH_COMMANDS_H