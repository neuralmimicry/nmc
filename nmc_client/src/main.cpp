#include "CLI/CLIParser.h"
#include "Commands/RootCommand.h"
#include "Commands/BucketCommands.h"
#include "Commands/AarnnCommands.h"
#include "Commands/K8sCommands.h"
#include "Commands/VClusterCommands.h"
#include "Commands/ModelCommands.h"
#include "Commands/SSHCommands.h"
#include "Commands/OpenShiftCommands.h"
#include "Commands/ProxmoxCommands.h"
#include "Commands/ServerCommands.h"
#include "Commands/TraceyCommands.h"
#include "Commands/VMCommands.h"
#include "Commands/VersionCommand.h"
#include "Commands/ConnectionCommands.h"
#include "Commands/GailCommands.h"
#include "Commands/RefinerCommands.h"
#include "Commands/NodeCommands.h"
#include "Commands/OpenStackCommands.h"
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

    // Build command graph once per process invocation.
    NMC::CLI::CLIParser parser;

    // Single shared API client for all commands in this process.
    auto apiClient = std::make_shared<NMC::Core::CloudAPIClient>(
            "127.0.0.1",
            8080,
            NMC::Core::Utils::getConfigPath());

    // Register top-level commands and their subcommand workflows.
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
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sHealthCommand>(apiClient));
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sResumeCommand>(apiClient));
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sSuspendCommand>(apiClient));
    parser.registerCommand(k8sCmd);

    auto vclusterCmd = std::make_shared<NMC::Commands::VClusterCommand>(apiClient);
    vclusterCmd->addSubcommand(std::make_shared<NMC::Commands::VClusterCreateCommand>(apiClient));
    vclusterCmd->addSubcommand(std::make_shared<NMC::Commands::VClusterDeleteCommand>(apiClient));
    vclusterCmd->addSubcommand(std::make_shared<NMC::Commands::VClusterGetCommand>(apiClient));
    vclusterCmd->addSubcommand(std::make_shared<NMC::Commands::VClusterGetConfigCommand>(apiClient));
    vclusterCmd->addSubcommand(std::make_shared<NMC::Commands::VClusterListCommand>(apiClient));
    vclusterCmd->addSubcommand(std::make_shared<NMC::Commands::VClusterPauseCommand>(apiClient));
    vclusterCmd->addSubcommand(std::make_shared<NMC::Commands::VClusterResumeCommand>(apiClient));
    vclusterCmd->addSubcommand(std::make_shared<NMC::Commands::VClusterBackupCommand>(apiClient));
    vclusterCmd->addSubcommand(std::make_shared<NMC::Commands::VClusterRestoreCommand>(apiClient));
    vclusterCmd->addSubcommand(std::make_shared<NMC::Commands::VClusterUpgradeCommand>(apiClient));
    vclusterCmd->addSubcommand(std::make_shared<NMC::Commands::VClusterConfigGetCommand>(apiClient));
    vclusterCmd->addSubcommand(std::make_shared<NMC::Commands::VClusterConfigUpdateCommand>(apiClient));
    vclusterCmd->addSubcommand(std::make_shared<NMC::Commands::VClusterMetricsCommand>(apiClient));
    vclusterCmd->addSubcommand(std::make_shared<NMC::Commands::VClusterHealthCommand>(apiClient));
    vclusterCmd->addSubcommand(std::make_shared<NMC::Commands::VClusterResourcesCommand>(apiClient));
    parser.registerCommand(vclusterCmd);

    auto modelCmd = std::make_shared<NMC::Commands::ModelCommand>(apiClient);
    modelCmd->addSubcommand(std::make_shared<NMC::Commands::ModelUploadCommand>(apiClient));
    parser.registerCommand(modelCmd);

    auto sshCmd = std::make_shared<NMC::Commands::SSHCommand>(apiClient);
    sshCmd->addSubcommand(std::make_shared<NMC::Commands::SSHCreateCommand>(apiClient));
    sshCmd->addSubcommand(std::make_shared<NMC::Commands::SSHDeleteCommand>(apiClient));
    sshCmd->addSubcommand(std::make_shared<NMC::Commands::SSHGetCommand>(apiClient));
    sshCmd->addSubcommand(std::make_shared<NMC::Commands::SSHListCommand>(apiClient));
    parser.registerCommand(sshCmd);

    auto openShiftCmd = std::make_shared<NMC::Commands::OpenShiftCommand>(apiClient);
    openShiftCmd->addSubcommand(std::make_shared<NMC::Commands::OpenShiftResourcesCommand>(apiClient));
    openShiftCmd->addSubcommand(std::make_shared<NMC::Commands::OpenShiftClustersCommand>(apiClient));
    openShiftCmd->addSubcommand(std::make_shared<NMC::Commands::OpenShiftRequestCommand>(apiClient));
    openShiftCmd->addSubcommand(std::make_shared<NMC::Commands::OpenShiftStatusCommand>(apiClient));
    parser.registerCommand(openShiftCmd);

    auto openStackCmd = std::make_shared<NMC::Commands::OpenStackCommand>(apiClient);
    openStackCmd->addSubcommand(std::make_shared<NMC::Commands::OpenStackResourcesCommand>(apiClient));
    openStackCmd->addSubcommand(std::make_shared<NMC::Commands::OpenStackClustersCommand>(apiClient));
    openStackCmd->addSubcommand(std::make_shared<NMC::Commands::OpenStackRequestCommand>(apiClient));
    openStackCmd->addSubcommand(std::make_shared<NMC::Commands::OpenStackStatusCommand>(apiClient));
    parser.registerCommand(openStackCmd);

    auto proxmoxCmd = std::make_shared<NMC::Commands::ProxmoxCommand>(apiClient);
    proxmoxCmd->addSubcommand(std::make_shared<NMC::Commands::ProxmoxResourcesCommand>(apiClient));
    proxmoxCmd->addSubcommand(std::make_shared<NMC::Commands::ProxmoxClustersCommand>(apiClient));
    proxmoxCmd->addSubcommand(std::make_shared<NMC::Commands::ProxmoxRequestCommand>(apiClient));
    proxmoxCmd->addSubcommand(std::make_shared<NMC::Commands::ProxmoxStatusCommand>(apiClient));
    parser.registerCommand(proxmoxCmd);

    auto serverCmd = std::make_shared<NMC::Commands::ServerCommand>(apiClient);
    serverCmd->addSubcommand(std::make_shared<NMC::Commands::ServerHealthCommand>(apiClient));
    serverCmd->addSubcommand(std::make_shared<NMC::Commands::ServerVersionCommand>(apiClient));
    parser.registerCommand(serverCmd);

    auto traceyCmd = std::make_shared<NMC::Commands::TraceyCommand>(apiClient);
    traceyCmd->addSubcommand(std::make_shared<NMC::Commands::TraceyHeartbeatCommand>(apiClient));
    traceyCmd->addSubcommand(std::make_shared<NMC::Commands::TraceyAgentsCommand>(apiClient));
    traceyCmd->addSubcommand(std::make_shared<NMC::Commands::TraceyAnalyticsCommand>(apiClient));
    traceyCmd->addSubcommand(std::make_shared<NMC::Commands::TraceyFleetCommand>(apiClient));
    traceyCmd->addSubcommand(std::make_shared<NMC::Commands::TraceyAdaptiveCommand>(apiClient));
    traceyCmd->addSubcommand(std::make_shared<NMC::Commands::TraceyCveCommand>(apiClient));
    traceyCmd->addSubcommand(std::make_shared<NMC::Commands::TraceyAssessmentCommand>(apiClient));
    traceyCmd->addSubcommand(std::make_shared<NMC::Commands::TraceyAssessmentPlanCommand>(apiClient));
    traceyCmd->addSubcommand(std::make_shared<NMC::Commands::TraceyAssessmentReportCommand>(apiClient));
    traceyCmd->addSubcommand(std::make_shared<NMC::Commands::TraceyRacksCommand>(apiClient));
    traceyCmd->addSubcommand(std::make_shared<NMC::Commands::TraceyRackCommand>(apiClient));
    traceyCmd->addSubcommand(std::make_shared<NMC::Commands::TraceyAnalysisCommand>(apiClient));
    traceyCmd->addSubcommand(std::make_shared<NMC::Commands::TraceyServerCommand>(apiClient));
    traceyCmd->addSubcommand(std::make_shared<NMC::Commands::TraceyGpuCommand>(apiClient));
    traceyCmd->addSubcommand(std::make_shared<NMC::Commands::TraceyCompromiseCommand>(apiClient));
    traceyCmd->addSubcommand(std::make_shared<NMC::Commands::TraceyControlCommand>(apiClient));
    traceyCmd->addSubcommand(std::make_shared<NMC::Commands::TraceyDeepDiveCommand>(apiClient));
    parser.registerCommand(traceyCmd);

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

    auto gailCmd = std::make_shared<NMC::Commands::GailCommand>(apiClient);
    NMC::Commands::attachDirectGailSubcommands(gailCmd);
    gailCmd->addSubcommand(std::make_shared<NMC::Commands::GailApiIssuesCommand>(apiClient));

    auto gailTradingCmd = std::make_shared<NMC::Commands::GailTradingCommand>(apiClient);
    gailTradingCmd->addSubcommand(std::make_shared<NMC::Commands::GailTradingStatusCommand>(apiClient));
    gailTradingCmd->addSubcommand(std::make_shared<NMC::Commands::GailTradingPortfolioCommand>(apiClient));
    gailTradingCmd->addSubcommand(std::make_shared<NMC::Commands::GailTradingPositionsCommand>(apiClient));
    gailTradingCmd->addSubcommand(std::make_shared<NMC::Commands::GailTradingHistoryCommand>(apiClient));
    gailTradingCmd->addSubcommand(std::make_shared<NMC::Commands::GailTradingLogsCommand>(apiClient));
    gailTradingCmd->addSubcommand(std::make_shared<NMC::Commands::GailTradingExchangesCommand>(apiClient));
    gailTradingCmd->addSubcommand(std::make_shared<NMC::Commands::GailTradingCurrenciesCommand>(apiClient));
    gailTradingCmd->addSubcommand(std::make_shared<NMC::Commands::GailTradingConfigCommand>(apiClient));
    gailTradingCmd->addSubcommand(std::make_shared<NMC::Commands::GailTradingConfigSetCommand>(apiClient));
    gailTradingCmd->addSubcommand(std::make_shared<NMC::Commands::GailTradingPauseCommand>(apiClient));
    gailTradingCmd->addSubcommand(std::make_shared<NMC::Commands::GailTradingResumeCommand>(apiClient));
    gailTradingCmd->addSubcommand(std::make_shared<NMC::Commands::GailTradingOverrideCommand>(apiClient));
    gailTradingCmd->addSubcommand(std::make_shared<NMC::Commands::GailTradingEvaluateCommand>(apiClient));
    gailCmd->addSubcommand(gailTradingCmd);
    parser.registerCommand(gailCmd);

    auto aarnnCmd = std::make_shared<NMC::Commands::AarnnCommand>(apiClient);
    aarnnCmd->addSubcommand(std::make_shared<NMC::Commands::AarnnEndpointsCommand>(apiClient));
    aarnnCmd->addSubcommand(std::make_shared<NMC::Commands::AarnnInventoryCommand>(apiClient));
    aarnnCmd->addSubcommand(std::make_shared<NMC::Commands::AarnnProxyCommand>(apiClient));

    auto aarnnNetworkCmd = std::make_shared<NMC::Commands::AarnnNetworkCommand>(apiClient);
    aarnnNetworkCmd->addSubcommand(std::make_shared<NMC::Commands::AarnnNetworkStatusCommand>(apiClient));
    aarnnNetworkCmd->addSubcommand(std::make_shared<NMC::Commands::AarnnNetworkSnapshotCommand>(apiClient));
    aarnnNetworkCmd->addSubcommand(std::make_shared<NMC::Commands::AarnnNetworkActivityCommand>(apiClient));
    aarnnNetworkCmd->addSubcommand(std::make_shared<NMC::Commands::AarnnNetworkUpdateCommand>(apiClient));
    aarnnNetworkCmd->addSubcommand(std::make_shared<NMC::Commands::AarnnNetworkControlCommand>(apiClient));
    aarnnNetworkCmd->addSubcommand(std::make_shared<NMC::Commands::AarnnNetworkAerInjectCommand>(apiClient));
    aarnnNetworkCmd->addSubcommand(std::make_shared<NMC::Commands::AarnnNetworkAerStreamCommand>(apiClient));
    aarnnCmd->addSubcommand(aarnnNetworkCmd);

    auto aarnnRuntimeCmd = std::make_shared<NMC::Commands::AarnnRuntimeCommand>(apiClient);
    aarnnRuntimeCmd->addSubcommand(std::make_shared<NMC::Commands::AarnnRuntimeStatusCommand>(apiClient));
    aarnnRuntimeCmd->addSubcommand(std::make_shared<NMC::Commands::AarnnRuntimeListCommand>(apiClient));
    aarnnRuntimeCmd->addSubcommand(std::make_shared<NMC::Commands::AarnnRuntimeCreateCommand>(apiClient));
    aarnnRuntimeCmd->addSubcommand(std::make_shared<NMC::Commands::AarnnRuntimeGetCommand>(apiClient));
    aarnnRuntimeCmd->addSubcommand(std::make_shared<NMC::Commands::AarnnRuntimeDeleteCommand>(apiClient));
    aarnnRuntimeCmd->addSubcommand(std::make_shared<NMC::Commands::AarnnRuntimeControlCommand>(apiClient));
    aarnnRuntimeCmd->addSubcommand(std::make_shared<NMC::Commands::AarnnRuntimeSnapshotCommand>(apiClient));
    aarnnRuntimeCmd->addSubcommand(std::make_shared<NMC::Commands::AarnnRuntimeActivityCommand>(apiClient));
    aarnnCmd->addSubcommand(aarnnRuntimeCmd);

    parser.registerCommand(aarnnCmd);

    // Register standalone commands
    parser.registerCommand(std::make_shared<NMC::Commands::VersionCommand>(apiClient));

    // Note: "completion" command would typically involve shell-specific script generation,
    // which is beyond the scope of this core CLI structure.

    return parser.parseAndExecute(argc, argv);
}
