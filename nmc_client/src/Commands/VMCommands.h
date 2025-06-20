#ifndef NMC_COMMANDS_VM_COMMANDS_H
#define NMC_COMMANDS_VM_COMMANDS_H

#include "BaseCommand.h"

namespace NMC::Commands {

class VmCommand : public BaseCommand {
public:
    VmCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VmCreateCommand : public BaseCommand {
public:
    VmCreateCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VmDeleteCommand : public BaseCommand {
public:
    VmDeleteCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VmGetCommand : public BaseCommand {
public:
    VmGetCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VmListCommand : public BaseCommand {
public:
    VmListCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VmListLocationsCommand : public BaseCommand {
public:
    VmListLocationsCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VmListOSCommand : public BaseCommand {
public:
    VmListOSCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VmListSkuCommand : public BaseCommand {
public:
    VmListSkuCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VmRestartCommand : public BaseCommand {
public:
    VmRestartCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VmResumeCommand : public BaseCommand {
public:
    VmResumeCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VmSuspendCommand : public BaseCommand {
public:
    VmSuspendCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

} // namespace NMC::Commands


#endif // NMC_COMMANDS_VM_COMMANDS_H