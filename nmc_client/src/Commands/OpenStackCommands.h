// nmc_client/src/Commands/OpenStackCommands.h
#ifndef NMC_OPENSTACK_COMMANDS_H
#define NMC_OPENSTACK_COMMANDS_H

#include "BaseCommand.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace NMC::Commands {

    class OpenStackCommand : public BaseCommand {
    public:
        explicit OpenStackCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags,
                    const std::vector<std::string>& parsedArgs,
                    const CLI::GlobalFlags& globalFlags) override;
    };

    class OpenStackResourcesCommand : public BaseCommand {
    public:
        explicit OpenStackResourcesCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags,
                    const std::vector<std::string>& parsedArgs,
                    const CLI::GlobalFlags& globalFlags) override;
    };

    class OpenStackClustersCommand : public BaseCommand {
    public:
        explicit OpenStackClustersCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags,
                    const std::vector<std::string>& parsedArgs,
                    const CLI::GlobalFlags& globalFlags) override;
    };

    class OpenStackRequestCommand : public BaseCommand {
    public:
        explicit OpenStackRequestCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags,
                    const std::vector<std::string>& parsedArgs,
                    const CLI::GlobalFlags& globalFlags) override;
    };

    class OpenStackStatusCommand : public BaseCommand {
    public:
        explicit OpenStackStatusCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags,
                    const std::vector<std::string>& parsedArgs,
                    const CLI::GlobalFlags& globalFlags) override;
    };

} // namespace NMC::Commands

#endif // NMC_OPENSTACK_COMMANDS_H

