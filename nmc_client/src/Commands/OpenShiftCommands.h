// nmc_client/src/Commands/OpenShiftCommands.h
#ifndef NMC_OPENSHIFT_COMMANDS_H
#define NMC_OPENSHIFT_COMMANDS_H

#include "BaseCommand.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace NMC::Commands {

    // --- OpenShiftCommand (Parent) ---
    class OpenShiftCommand : public BaseCommand {
    public:
        OpenShiftCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags,
                    const std::vector<std::string>& parsedArgs,
                    const CLI::GlobalFlags& globalFlags) override;
    };

    // --- OpenShiftResourcesCommand ---
    class OpenShiftResourcesCommand : public BaseCommand {
    public:
        OpenShiftResourcesCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags,
                    const std::vector<std::string>& parsedArgs,
                    const CLI::GlobalFlags& globalFlags) override;
    };

    // --- OpenShiftClustersCommand ---
    class OpenShiftClustersCommand : public BaseCommand {
    public:
        OpenShiftClustersCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags,
                    const std::vector<std::string>& parsedArgs,
                    const CLI::GlobalFlags& globalFlags) override;
    };

    // --- OpenShiftRequestCommand ---
    class OpenShiftRequestCommand : public BaseCommand {
    public:
        OpenShiftRequestCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags,
                    const std::vector<std::string>& parsedArgs,
                    const CLI::GlobalFlags& globalFlags) override;
    };

    // --- OpenShiftStatusCommand ---
    class OpenShiftStatusCommand : public BaseCommand {
    public:
        OpenShiftStatusCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags,
                    const std::vector<std::string>& parsedArgs,
                    const CLI::GlobalFlags& globalFlags) override;
    };

} // namespace NMC::Commands

#endif // NMC_OPENSHIFT_COMMANDS_H
