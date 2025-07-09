// Commands/ConnectionCommands.h
#ifndef NMC_CONNECTION_COMMANDS_H
#define NMC_CONNECTION_COMMANDS_H

#include "BaseCommand.h"

#include <map>
#include <string>
#include <vector>
#include <memory> // For std::shared_ptr

namespace NMC::Commands {

// --- ConnectionCommand (Parent) ---
    class ConnectionCommand : public BaseCommand {
    public:
        ConnectionCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
    };

// --- ConnectionStatusCommand ---
    class ConnectionStatusCommand : public BaseCommand {
    public:
        ConnectionStatusCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
    };

// --- ConnectionMakeCommand ---
    class ConnectionMakeCommand : public BaseCommand {
    public:
        ConnectionMakeCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
    };

// --- ConnectionDropCommand ---
    class ConnectionDropCommand : public BaseCommand {
    public:
        ConnectionDropCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
    };

// --- ConnectionListCommand ---
    class ConnectionListCommand : public BaseCommand {
    public:
        ConnectionListCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
    };

// --- ConnectionSelectCommand ---
    class ConnectionSelectCommand : public BaseCommand {
    public:
        ConnectionSelectCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
    };

// --- ConnectionUnsetDefaultCommand ---
    class ConnectionUnsetDefaultCommand : public BaseCommand {
    public:
        ConnectionUnsetDefaultCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
        int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
    };

} // namespace NMC::Commands

#endif // NMC_CONNECTION_COMMANDS_H
