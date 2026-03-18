#ifndef NMC_COMMANDS_REFINER_COMMANDS_H
#define NMC_COMMANDS_REFINER_COMMANDS_H

#include "BaseCommand.h"

namespace NMC::Commands {

class RefinerCommand : public BaseCommand {
public:
    explicit RefinerCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class RefinerDeployCommand : public BaseCommand {
public:
    explicit RefinerDeployCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class RefinerStatusCommand : public BaseCommand {
public:
    explicit RefinerStatusCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class RefinerScaleCommand : public BaseCommand {
public:
    explicit RefinerScaleCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class RefinerLogsCommand : public BaseCommand {
public:
    explicit RefinerLogsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class RefinerRemoveCommand : public BaseCommand {
public:
    explicit RefinerRemoveCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

} // namespace NMC::Commands

#endif // NMC_COMMANDS_REFINER_COMMANDS_H
