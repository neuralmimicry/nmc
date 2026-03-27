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
        addFlag(CLI::Flag("s", "server", "Query server-side connection state instead of local config", CLI::FlagType::Bool, false));
    }

    int ConnectionStatusCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        const bool useServer = parsedFlags.count("server") > 0;
        // `--server` targets the remote server-side connection registry.
        // Without it, commands operate on the local ~/.nmc profile store.
        Models::CloudResponse response = useServer
                                         ? apiClient->getServerConnectionStatus()
                                         : apiClient->getConnectionStatus();
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- ConnectionMakeCommand ---
    ConnectionMakeCommand::ConnectionMakeCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("make", "Make a new connection", std::move(client)) {
        usage = "nmc connection make <name> <endpoint> [--default] [--token <token>] [--server]";
        examples = "nmc connection make my-cloud https://api.mycloud.com --default --token $TOKEN\nnmc connection make edge http://127.0.0.1:8080 --server";
        addArgument(CLI::Argument("name", "Name of the connection", true, 0));
        addArgument(CLI::Argument("endpoint", "Endpoint URL for the connection", true, 1));
        addFlag(CLI::Flag("d", "default", "Set this connection as the default", CLI::FlagType::Bool, false));
        addFlag(CLI::Flag("t", "token", "Bearer token for this connection", CLI::FlagType::String, false));
        addFlag(CLI::Flag("s", "server", "Create connection on the server-side connection store", CLI::FlagType::Bool, false));
    }

    int ConnectionMakeCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        std::string name = parsedArgs[0];
        std::string endpoint = parsedArgs[1];
        bool setDefault = parsedFlags.count("default");
        const bool useServer = parsedFlags.count("server") > 0;
        std::string token = "";
        auto itToken = parsedFlags.find("token");
        if (itToken != parsedFlags.end()) {
            token = itToken->second;
        }

        if (name.empty() || endpoint.empty()) {
            std::cerr << "Error: Connection name and endpoint are required." << std::endl;
            printHelp();
            return 1;
        }

        Models::CloudResponse response;
        if (useServer) {
            response = apiClient->makeServerConnection(name, endpoint, setDefault);
        } else {
            response = apiClient->makeConnection(name, endpoint, setDefault, token);
        }
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- ConnectionDropCommand ---
    ConnectionDropCommand::ConnectionDropCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("drop", "Drop an existing connection", std::move(client)) {
        usage = "nmc connection drop <name>";
        examples = "nmc connection drop my-cloud\nnmc connection drop my-cloud --server";
        addArgument(CLI::Argument("name", "Name of the connection to drop", true, 0));
        addFlag(CLI::Flag("s", "server", "Drop connection from the server-side connection store", CLI::FlagType::Bool, false));
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

        const bool useServer = parsedFlags.count("server") > 0;
        Models::CloudResponse response = useServer
                                         ? apiClient->dropServerConnection(name)
                                         : apiClient->dropConnection(name);
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- ConnectionListCommand ---
    ConnectionListCommand::ConnectionListCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("list", "List all configured connections", std::move(client)) {
        usage = "nmc connection list";
        examples = "nmc connection list\nnmc connection list --server";
        addFlag(CLI::Flag("s", "server", "List connections from the server-side connection store", CLI::FlagType::Bool, false));
    }

    int ConnectionListCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        const bool useServer = parsedFlags.count("server") > 0;
        Models::CloudResponse response = useServer
                                         ? apiClient->listServerConnections()
                                         : apiClient->listConnections();
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- ConnectionSelectCommand ---
    ConnectionSelectCommand::ConnectionSelectCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("select", "Select an active connection", std::move(client)) {
        usage = "nmc connection select <name>";
        examples = "nmc connection select my-cloud\nnmc connection select my-cloud --server";
        addArgument(CLI::Argument("name", "Name of the connection to select", true, 0));
        addFlag(CLI::Flag("s", "server", "Select active connection in the server-side connection store", CLI::FlagType::Bool, false));
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

        const bool useServer = parsedFlags.count("server") > 0;
        Models::CloudResponse response = useServer
                                         ? apiClient->selectServerConnection(name)
                                         : apiClient->selectConnection(name);
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

// --- ConnectionSetTokenCommand ---
    ConnectionSetTokenCommand::ConnectionSetTokenCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
        : BaseCommand("set-token", "Set or update the bearer token for a connection", std::move(client)) {
        usage = "nmc connection set-token <name> --token <token>";
        examples = "nmc connection set-token my-cloud --token $TOKEN";
        addArgument(CLI::Argument("name", "Name of the connection to update", true, 0));
        addFlag(CLI::Flag("t", "token", "Bearer token for this connection", CLI::FlagType::String, true));
    }

    int ConnectionSetTokenCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                           const std::vector<std::string>& parsedArgs,
                                           const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }
        const std::string& name = parsedArgs[0];
        auto itToken = parsedFlags.find("token");
        if (itToken == parsedFlags.end() || itToken->second.empty()) {
            std::cerr << "Error: --token is required." << std::endl;
            printHelp();
            return 1;
        }
        Models::CloudResponse response = apiClient->setConnectionToken(name, itToken->second);
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- ConnectionClearTokenCommand ---
    ConnectionClearTokenCommand::ConnectionClearTokenCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
        : BaseCommand("clear-token", "Clear the bearer token for a connection", std::move(client)) {
        usage = "nmc connection clear-token <name>";
        examples = "nmc connection clear-token my-cloud";
        addArgument(CLI::Argument("name", "Name of the connection to update", true, 0));
    }

    int ConnectionClearTokenCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                             const std::vector<std::string>& parsedArgs,
                                             const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }
        const std::string& name = parsedArgs[0];
        Models::CloudResponse response = apiClient->clearConnectionToken(name);
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

} // namespace NMC::Commands
