#ifndef NMC_COMMANDS_VCLUSTER_COMMANDS_H
#define NMC_COMMANDS_VCLUSTER_COMMANDS_H

#include "BaseCommand.h"

namespace NMC::Commands {

class VClusterCommand : public BaseCommand {
public:
    VClusterCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VClusterCreateCommand : public BaseCommand {
public:
    VClusterCreateCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VClusterDeleteCommand : public BaseCommand {
public:
    VClusterDeleteCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VClusterGetCommand : public BaseCommand {
public:
    VClusterGetCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VClusterGetConfigCommand : public BaseCommand {
public:
    VClusterGetConfigCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VClusterListCommand : public BaseCommand {
public:
    VClusterListCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

// --- VCluster Lifecycle Commands ---
class VClusterPauseCommand : public BaseCommand {
public:
    VClusterPauseCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VClusterResumeCommand : public BaseCommand {
public:
    VClusterResumeCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VClusterBackupCommand : public BaseCommand {
public:
    VClusterBackupCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VClusterRestoreCommand : public BaseCommand {
public:
    VClusterRestoreCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VClusterUpgradeCommand : public BaseCommand {
public:
    VClusterUpgradeCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

// --- VCluster Configuration Commands ---
class VClusterConfigGetCommand : public BaseCommand {
public:
    VClusterConfigGetCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VClusterConfigUpdateCommand : public BaseCommand {
public:
    VClusterConfigUpdateCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

// --- VCluster Monitoring Commands ---
class VClusterMetricsCommand : public BaseCommand {
public:
    VClusterMetricsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VClusterHealthCommand : public BaseCommand {
public:
    VClusterHealthCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class VClusterResourcesCommand : public BaseCommand {
public:
    VClusterResourcesCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

} // namespace NMC::Commands

#endif // NMC_COMMANDS_VCLUSTER_COMMANDS_H
