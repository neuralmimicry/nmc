// Commands/ConnectionCommands.cpp
#include "ConnectionCommands.h"
#include <iostream>
#include <algorithm> // For std::remove_if, std::transform

namespace NMC::Commands {

// --- ConnectionCommand (Parent) ---
    ConnectionCommand::ConnectionCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("connection", "Manage cloud connections in NMC", std::move(client)) {
        usage = "nmc connection [command]";
        addAlias("conn");
    }

    int ConnectionCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
        printHelp();
        return 0;
    }

// --- ConnectionStatusCommand ---
    ConnectionStatusCommand::ConnectionStatusCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("status", "Show current connection status", std::move(client)) {
        usage = "nmc connection status";
        examples = "nmc connection status";
    }

    int ConnectionStatusCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        Models::CloudResponse response = apiClient->getConnectionStatus(); // Corrected
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- ConnectionMakeCommand ---
    ConnectionMakeCommand::ConnectionMakeCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("make", "Make a new connection", std::move(client)) {
        usage = "nmc connection make <name> <endpoint> [--default]";
        examples = "nmc connection make my-cloud https://api.mycloud.com --default";
        addArgument(CLI::Argument("name", "Name of the connection", true, 0));
        addArgument(CLI::Argument("endpoint", "Endpoint URL for the connection", true, 1));
        addFlag(CLI::Flag("d", "default", "Set this connection as the default", CLI::FlagType::Bool, false));
    }

    int ConnectionMakeCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        std::string name = parsedArgs[0];
        std::string endpoint = parsedArgs[1];
        bool setDefault = parsedFlags.count("default");

        if (name.empty() || endpoint.empty()) {
            std::cerr << "Error: Connection name and endpoint are required." << std::endl;
            printHelp();
            return 1;
        }

        Models::CloudResponse response = apiClient->makeConnection(name, endpoint, setDefault); // Corrected
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- ConnectionDropCommand ---
    ConnectionDropCommand::ConnectionDropCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("drop", "Drop an existing connection", std::move(client)) {
        usage = "nmc connection drop <name>";
        examples = "nmc connection drop my-cloud";
        addArgument(CLI::Argument("name", "Name of the connection to drop", true, 0));
    }

    int ConnectionDropCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        std::string name = parsedArgs[0];
        if (name.empty()) {
            std::cerr << "Error: Connection name is required." << std::endl;
            printHelp();
            return 1;
        }

        Models::CloudResponse response = apiClient->dropConnection(name); // Corrected
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- ConnectionListCommand ---
    ConnectionListCommand::ConnectionListCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("list", "List all configured connections", std::move(client)) {
        usage = "nmc connection list";
        examples = "nmc connection list";
    }

    int ConnectionListCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        Models::CloudResponse response = apiClient->listConnections();
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- ConnectionSelectCommand ---
    ConnectionSelectCommand::ConnectionSelectCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("select", "Select an active connection", std::move(client)) {
        usage = "nmc connection select <name>";
        examples = "nmc connection select my-cloud";
        addArgument(CLI::Argument("name", "Name of the connection to select", true, 0));
    }

    int ConnectionSelectCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        const std::string& name = parsedArgs[0];
        if (name.empty()) {
            std::cerr << "Error: Connection name is required." << std::endl;
            printHelp();
            return 1;
        }

        Models::CloudResponse response = apiClient->selectConnection(name); // Corrected
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- ConnectionUnsetDefaultCommand ---
    ConnectionUnsetDefaultCommand::ConnectionUnsetDefaultCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("unset-default", "Unset the current default connection", std::move(client)) {
        usage = "nmc connection unset-default";
        examples = "nmc connection unset-default";
    }

    int ConnectionUnsetDefaultCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        Models::CloudResponse response = apiClient->unsetDefaultConnection(); // Corrected
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

} // namespace NMC::Commands