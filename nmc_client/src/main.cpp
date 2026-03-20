#include "CLI/CLIParser.h"
#include "Commands/RootCommand.h"
#include "Commands/BucketCommands.h"
#include "Commands/K8sCommands.h"
#include "Commands/ModelCommands.h"
#include "Commands/SSHCommands.h"
#include "Commands/OpenShiftCommands.h"
#include "Commands/VMCommands.h"
#include "Commands/VersionCommand.h"
#include "Commands/ConnectionCommands.h"
#include "Commands/RefinerCommands.h"
#include "Commands/NodeCommands.h"
#include "Core/VersionCheck.h"
#include <iostream>
#include <memory> // For std::make_shared
#include <string>
#include "Core/Utils.h"

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--version" || arg == "-V") {
            std::cout << "nmc version " << NMC::Core::VersionCheck::currentVersion() << std::endl;
            return 0;
        }
    }

    NMC::CLI::CLIParser parser;

    // Single shared API client for all commands in this process.
    auto apiClient = std::make_shared<NMC::Core::CloudAPIClient>(
            "127.0.0.1",
            8080,
            NMC::Core::Utils::getConfigPath());

    // Register Root Command (mainly for help output)
    auto rootCmd = std::make_shared<NMC::Commands::RootCommand>(apiClient);

    // Register top-level commands
    auto bucketCmd = std::make_shared<NMC::Commands::BucketCommand>(apiClient);
    bucketCmd->addSubcommand(std::make_shared<NMC::Commands::BucketCreateCommand>(apiClient));
    bucketCmd->addSubcommand(std::make_shared<NMC::Commands::BucketDeleteCommand>(apiClient));
    bucketCmd->addSubcommand(std::make_shared<NMC::Commands::BucketGetCommand>(apiClient));
    bucketCmd->addSubcommand(std::make_shared<NMC::Commands::BucketListCommand>(apiClient));
    bucketCmd->addSubcommand(std::make_shared<NMC::Commands::BucketListLocationsCommand>(apiClient));
    bucketCmd->addSubcommand(std::make_shared<NMC::Commands::BucketListTypesCommand>(apiClient));
    bucketCmd->addSubcommand(std::make_shared<NMC::Commands::BucketResetKeyCommand>(apiClient));
    parser.registerCommand(bucketCmd);

    auto k8sCmd = std::make_shared<NMC::Commands::K8sCommand>(apiClient);
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sCreateCommand>(apiClient));
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sDeleteCommand>(apiClient));
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sGetCommand>(apiClient));
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sGetConfigCommand>(apiClient));
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sListCommand>(apiClient));
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sListLocationsCommand>(apiClient));
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sResumeCommand>(apiClient));
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sSuspendCommand>(apiClient));
    parser.registerCommand(k8sCmd);

    auto modelCmd = std::make_shared<NMC::Commands::ModelCommand>(apiClient);
    modelCmd->addSubcommand(std::make_shared<NMC::Commands::ModelUploadCommand>(apiClient));
    parser.registerCommand(modelCmd);

    auto sshCmd = std::make_shared<NMC::Commands::SSHCommand>(apiClient);
    sshCmd->addSubcommand(std::make_shared<NMC::Commands::SSHCreateCommand>(apiClient));
    sshCmd->addSubcommand(std::make_shared<NMC::Commands::SSHDeleteCommand>(apiClient));
    sshCmd->addSubcommand(std::make_shared<NMC::Commands::SSHListCommand>(apiClient));
    parser.registerCommand(sshCmd);

    auto openShiftCmd = std::make_shared<NMC::Commands::OpenShiftCommand>(apiClient);
    openShiftCmd->addSubcommand(std::make_shared<NMC::Commands::OpenShiftResourcesCommand>(apiClient));
    openShiftCmd->addSubcommand(std::make_shared<NMC::Commands::OpenShiftClustersCommand>(apiClient));
    openShiftCmd->addSubcommand(std::make_shared<NMC::Commands::OpenShiftRequestCommand>(apiClient));
    openShiftCmd->addSubcommand(std::make_shared<NMC::Commands::OpenShiftStatusCommand>(apiClient));
    parser.registerCommand(openShiftCmd);

    auto vmCmd = std::make_shared<NMC::Commands::VmCommand>(apiClient);
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmCreateCommand>(apiClient));
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmDeleteCommand>(apiClient));
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmGetCommand>(apiClient));
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmListCommand>(apiClient));
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmListLocationsCommand>(apiClient));
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmListOSCommand>(apiClient));
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmListSkuCommand>(apiClient));
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmRestartCommand>(apiClient));
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmResumeCommand>(apiClient));
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmSuspendCommand>(apiClient));
    parser.registerCommand(vmCmd);

    auto connectionCmd = std::make_shared<NMC::Commands::ConnectionCommand>(apiClient);
    connectionCmd->addSubcommand(std::make_shared<NMC::Commands::ConnectionStatusCommand>(apiClient));
    connectionCmd->addSubcommand(std::make_shared<NMC::Commands::ConnectionMakeCommand>(apiClient));
    connectionCmd->addSubcommand(std::make_shared<NMC::Commands::ConnectionDropCommand>(apiClient));
    connectionCmd->addSubcommand(std::make_shared<NMC::Commands::ConnectionListCommand>(apiClient));
    connectionCmd->addSubcommand(std::make_shared<NMC::Commands::ConnectionSelectCommand>(apiClient));
    connectionCmd->addSubcommand(std::make_shared<NMC::Commands::ConnectionUnsetDefaultCommand>(apiClient));
    connectionCmd->addSubcommand(std::make_shared<NMC::Commands::ConnectionSetTokenCommand>(apiClient));
    connectionCmd->addSubcommand(std::make_shared<NMC::Commands::ConnectionClearTokenCommand>(apiClient));
    parser.registerCommand(connectionCmd);

    auto nodeCmd = std::make_shared<NMC::Commands::NodeCommand>(apiClient);
    nodeCmd->addSubcommand(std::make_shared<NMC::Commands::NodeRecruitCommand>(apiClient));
    parser.registerCommand(nodeCmd);

    auto refinerCmd = std::make_shared<NMC::Commands::RefinerCommand>(apiClient);
    refinerCmd->addSubcommand(std::make_shared<NMC::Commands::RefinerDeployCommand>(apiClient));
    refinerCmd->addSubcommand(std::make_shared<NMC::Commands::RefinerStatusCommand>(apiClient));
    refinerCmd->addSubcommand(std::make_shared<NMC::Commands::RefinerScaleCommand>(apiClient));
    refinerCmd->addSubcommand(std::make_shared<NMC::Commands::RefinerLogsCommand>(apiClient));
    refinerCmd->addSubcommand(std::make_shared<NMC::Commands::RefinerRemoveCommand>(apiClient));
    parser.registerCommand(refinerCmd);

    // Register standalone commands
    parser.registerCommand(std::make_shared<NMC::Commands::VersionCommand>(apiClient));

    // Note: "completion" command would typically involve shell-specific script generation,
    // which is beyond the scope of this core CLI structure.

    return parser.parseAndExecute(argc, argv);
}
