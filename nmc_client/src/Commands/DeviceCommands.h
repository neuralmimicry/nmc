#ifndef NMC_DEVICE_COMMANDS_H
#define NMC_DEVICE_COMMANDS_H

#include "BaseCommand.h"

namespace NMC::Commands {

/** Parent command for authenticated device, controller and discovery queries. */
class DeviceCommand : public BaseCommand {
public:
    explicit DeviceCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    /** Display command usage; concrete operations are available as subcommands. */
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class DeviceInventoryCommand : public BaseCommand {
public:
    explicit DeviceInventoryCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    /** Print the current verified registry without exposing secret values. */
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class DeviceHomeAssistantCommand : public BaseCommand {
public:
    explicit DeviceHomeAssistantCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    /** Print state for the registry's explicit Home Assistant allowlist. */
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class DeviceDhcpCommand : public BaseCommand {
public:
    explicit DeviceDhcpCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    /** Print read-only DHCP lease and option-source observations. */
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class DeviceDiagnosticsCommand : public BaseCommand {
public:
    explicit DeviceDiagnosticsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    /** Query structured health and capabilities for one registered controller. */
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class DeviceActionCommand : public BaseCommand {
public:
    explicit DeviceActionCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    /** Submit one operator-reviewed controller action as a JSON request. */
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

} // namespace NMC::Commands

#endif // NMC_DEVICE_COMMANDS_H
