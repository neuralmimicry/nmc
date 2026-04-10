// nmc_client/src/Commands/ProxmoxCommands.h
#ifndef NMC_PROXMOX_COMMANDS_H
#define NMC_PROXMOX_COMMANDS_H

#include "BaseCommand.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace NMC::Commands {

    class ProxmoxCommand : public BaseCommand {
    public:
        explicit ProxmoxCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags,
                    const std::vector<std::string>& parsedArgs,
                    const CLI::GlobalFlags& globalFlags) override;
    };

    class ProxmoxResourcesCommand : public BaseCommand {
    public:
        explicit ProxmoxResourcesCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags,
                    const std::vector<std::string>& parsedArgs,
                    const CLI::GlobalFlags& globalFlags) override;
    };

    class ProxmoxClustersCommand : public BaseCommand {
    public:
        explicit ProxmoxClustersCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags,
                    const std::vector<std::string>& parsedArgs,
                    const CLI::GlobalFlags& globalFlags) override;
    };

    class ProxmoxRequestCommand : public BaseCommand {
    public:
        explicit ProxmoxRequestCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags,
                    const std::vector<std::string>& parsedArgs,
                    const CLI::GlobalFlags& globalFlags) override;
    };

    class ProxmoxStatusCommand : public BaseCommand {
    public:
        explicit ProxmoxStatusCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags,
                    const std::vector<std::string>& parsedArgs,
                    const CLI::GlobalFlags& globalFlags) override;
    };

} // namespace NMC::Commands

#endif // NMC_PROXMOX_COMMANDS_H
