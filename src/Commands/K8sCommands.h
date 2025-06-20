#ifndef NMC_COMMANDS_K8S_COMMANDS_H
#define NMC_COMMANDS_K8S_COMMANDS_H

#include "BaseCommand.h"

namespace NMC::Commands {

class K8sCommand : public BaseCommand {
public:
    K8sCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class K8sCreateCommand : public BaseCommand {
public:
    K8sCreateCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class K8sDeleteCommand : public BaseCommand {
public:
    K8sDeleteCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class K8sGetCommand : public BaseCommand {
public:
    K8sGetCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class K8sGetConfigCommand : public BaseCommand {
public:
    K8sGetConfigCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class K8sListCommand : public BaseCommand {
public:
    K8sListCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class K8sListLocationsCommand : public BaseCommand {
public:
    K8sListLocationsCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class K8sResumeCommand : public BaseCommand {
public:
    K8sResumeCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class K8sSuspendCommand : public BaseCommand {
public:
    K8sSuspendCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

} // namespace NMC::Commands


#endif // NMC_COMMANDS_K8S_COMMANDS_H