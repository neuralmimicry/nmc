#ifndef NMC_COMMANDS_SERVER_COMMANDS_H
#define NMC_COMMANDS_SERVER_COMMANDS_H

#include "BaseCommand.h"

namespace NMC::Commands {

// Parent command that groups server metadata/health operations.
class ServerCommand : public BaseCommand {
public:
    ServerCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

// Lightweight liveness check against GET /health.
class ServerHealthCommand : public BaseCommand {
public:
    ServerHealthCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

// Version/upgrade metadata check against GET /server/version.
class ServerVersionCommand : public BaseCommand {
public:
    ServerVersionCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

} // namespace NMC::Commands

#endif // NMC_COMMANDS_SERVER_COMMANDS_H
