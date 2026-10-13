#include "DeviceCommands.h"

#include <nlohmann/json.hpp>

namespace NMC::Commands {

DeviceCommand::DeviceCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("device", "Inventory and diagnose managed infrastructure devices", std::move(client)) {
    usage = "nmc device [command]";
}

int DeviceCommand::execute(const std::map<std::string, std::string>&,
                           const std::vector<std::string>&,
                           const CLI::GlobalFlags&) {
    printHelp();
    return 0;
}

DeviceRecoveryHistoryCommand::DeviceRecoveryHistoryCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("recovery-history", "Read bounded warm-restart episodes for startup log review", std::move(client)) {
    usage = "nmc device recovery-history";
}
int DeviceRecoveryHistoryCommand::execute(const std::map<std::string, std::string>& flags,
    const std::vector<std::string>& args, const CLI::GlobalFlags& global) {
    if (!validateArguments(args) || !validateFlags(flags)) return 1;
    const auto response = apiClient->getRecoveryHistory();
    printOutput(response, global);
    return response.success ? 0 : 1;
}

DeviceInventoryCommand::DeviceInventoryCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("inventory", "Show the versioned network, host and controller inventory", std::move(client)) {
    usage = "nmc device inventory";
    examples = "nmc device inventory";
}

int DeviceInventoryCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                     const std::vector<std::string>& parsedArgs,
                                     const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) return 1;
    const auto response = apiClient->getDeviceInventory();
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

DeviceHomeAssistantCommand::DeviceHomeAssistantCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("home-assistant", "Show allowlisted device information from Home Assistant", std::move(client)) {
    usage = "nmc device home-assistant";
    examples = "nmc device home-assistant";
}

int DeviceHomeAssistantCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                        const std::vector<std::string>& parsedArgs,
                                        const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) return 1;
    const auto response = apiClient->getHomeAssistantDevices();
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

DeviceHomeAssistantReconciliationCommand::DeviceHomeAssistantReconciliationCommand(
        std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("home-assistant-reconcile", "Reconcile Home Assistant entities to registered devices", std::move(client)) {
    usage = "nmc device home-assistant-reconcile";
    examples = "nmc device home-assistant-reconcile --output json";
}

int DeviceHomeAssistantReconciliationCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                                       const std::vector<std::string>& parsedArgs,
                                                       const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) return 1;
    const auto response = apiClient->getHomeAssistantReconciliation();
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

DeviceDhcpCommand::DeviceDhcpCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("dhcp", "Show read-only DHCP leases and client option evidence", std::move(client)) {
    usage = "nmc device dhcp";
    examples = "nmc device dhcp";
}

int DeviceDhcpCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                               const std::vector<std::string>& parsedArgs,
                               const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) return 1;
    const auto response = apiClient->getDhcpObservations();
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

DeviceDiagnosticsCommand::DeviceDiagnosticsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("diagnostics", "Collect read-only BMC/controller diagnostics", std::move(client)) {
    usage = "nmc device diagnostics --controller-id <id>";
    examples = "nmc device diagnostics --controller-id qc00-bmc";
    addFlag(CLI::Flag("c", "controller-id", "Controller id from the verified inventory", CLI::FlagType::String, true));
}

int DeviceDiagnosticsCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                      const std::vector<std::string>& parsedArgs,
                                      const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) return 1;
    const auto id = parsedFlags.find("controller-id");
    if (id == parsedFlags.end()) return 1;
    const auto response = apiClient->getControllerDiagnostics(id->second);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

DeviceActionCommand::DeviceActionCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("action", "Submit an audited, preflighted BMC power action", std::move(client)) {
    usage = "nmc device action --request-json '<preflighted-request>'";
    examples = "nmc device action --request-json '{\"controller_id\":\"bmc01\",...}'";
    addFlag(CLI::Flag("r", "request-json", "Complete JSON action request including fresh dependency preflight", CLI::FlagType::String, true));
}

int DeviceActionCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                 const std::vector<std::string>& parsedArgs,
                                 const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) return 1;
    const auto requestFlag = parsedFlags.find("request-json");
    if (requestFlag == parsedFlags.end()) return 1;
    try {
        const auto request = nlohmann::json::parse(requestFlag->second);
        if (!request.is_object()) {
            std::cerr << "Error: request-json must be a JSON object." << std::endl;
            return 1;
        }
        const auto response = apiClient->executeControllerAction(request);
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << "Error: invalid request-json: " << exception.what() << std::endl;
        return 1;
    }
}

DeviceEventSubscribeCommand::DeviceEventSubscribeCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("event-subscribe", "Submit an audited, operator-configured BMC event subscription", std::move(client)) {
    usage = "nmc device event-subscribe --request-json '<enrollment-request>'";
    examples = "nmc device event-subscribe --request-json '{\"controller_id\":\"bmc01\",...}'";
    addFlag(CLI::Flag("r", "request-json", "JSON enrollment request with exact fresh inventory revision and audit IDs", CLI::FlagType::String, true));
}

int DeviceEventSubscribeCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                 const std::vector<std::string>& parsedArgs,
                                 const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) return 1;
    const auto requestFlag = parsedFlags.find("request-json");
    if (requestFlag == parsedFlags.end()) return 1;
    try {
        const auto request = nlohmann::json::parse(requestFlag->second);
        if (!request.is_object()) {
            std::cerr << "Error: request-json must be a JSON object." << std::endl;
            return 1;
        }
        const auto response = apiClient->configureControllerEventSubscription(request);
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << "Error: invalid request-json: " << exception.what() << std::endl;
        return 1;
    }
}

} // namespace NMC::Commands
