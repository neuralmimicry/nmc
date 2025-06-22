#include "CLI/CLIParser.h"
#include "Commands/RootCommand.h"
#include "Commands/BucketCommands.h"
#include "Commands/K8sCommands.h"
#include "Commands/ModelCommands.h"
#include "Commands/SSHCommands.h"
#include "Commands/VMCommands.h"
#include "Commands/VersionCommand.h"
#include <memory> // For std::make_shared


int main(int argc, char* argv[]) {
    NMC::CLI::CLIParser parser;

    // Register Root Command (mainly for help output)
    auto rootCmd = std::make_shared<NMC::Commands::RootCommand>();

    // Register top-level commands
    auto bucketCmd = std::make_shared<NMC::Commands::BucketCommand>();
    bucketCmd->addSubcommand(std::make_shared<NMC::Commands::BucketCreateCommand>());
    bucketCmd->addSubcommand(std::make_shared<NMC::Commands::BucketDeleteCommand>());
    bucketCmd->addSubcommand(std::make_shared<NMC::Commands::BucketGetCommand>());
    bucketCmd->addSubcommand(std::make_shared<NMC::Commands::BucketListCommand>());
    bucketCmd->addSubcommand(std::make_shared<NMC::Commands::BucketListLocationsCommand>());
    bucketCmd->addSubcommand(std::make_shared<NMC::Commands::BucketListTypesCommand>());
    bucketCmd->addSubcommand(std::make_shared<NMC::Commands::BucketResetKeyCommand>());
    parser.registerCommand(bucketCmd);

    auto k8sCmd = std::make_shared<NMC::Commands::K8sCommand>();
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sCreateCommand>());
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sDeleteCommand>());
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sGetCommand>());
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sGetConfigCommand>());
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sListCommand>());
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sListLocationsCommand>());
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sResumeCommand>());
    k8sCmd->addSubcommand(std::make_shared<NMC::Commands::K8sSuspendCommand>());
    parser.registerCommand(k8sCmd);

    auto modelCmd = std::make_shared<NMC::Commands::ModelCommand>();
    modelCmd->addSubcommand(std::make_shared<NMC::Commands::ModelUploadCommand>());
    parser.registerCommand(modelCmd);

    auto sshCmd = std::make_shared<NMC::Commands::SSHCommand>();
    sshCmd->addSubcommand(std::make_shared<NMC::Commands::SSHCreateCommand>());
    sshCmd->addSubcommand(std::make_shared<NMC::Commands::SSHDeleteCommand>());
    sshCmd->addSubcommand(std::make_shared<NMC::Commands::SSHListCommand>());
    parser.registerCommand(sshCmd);

    auto vmCmd = std::make_shared<NMC::Commands::VmCommand>();
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmCreateCommand>());
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmDeleteCommand>());
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmGetCommand>());
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmListCommand>());
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmListLocationsCommand>());
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmListOSCommand>());
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmListSkuCommand>());
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmRestartCommand>());
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmResumeCommand>());
    vmCmd->addSubcommand(std::make_shared<NMC::Commands::VmSuspendCommand>());
    parser.registerCommand(vmCmd);

    // Register standalone commands
    parser.registerCommand(std::make_shared<NMC::Commands::VersionCommand>());

    // Note: "completion" command would typically involve shell-specific script generation,
    // which is beyond the scope of this core CLI structure.

    return parser.parseAndExecute(argc, argv);
}