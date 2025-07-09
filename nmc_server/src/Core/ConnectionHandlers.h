// nmc_server/src/Core/ConnectionHandlers.h
#ifndef NMC_SERVER_CONNECTION_HANDLERS_H
#define NMC_SERVER_CONNECTION_HANDLERS_H

#include "Models/CloudResponse.h"
#include "Models/Connection.h"
#include "httplib.h" // Assuming httplib is used for the server
#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <memory> // For shared_ptr if needed for state management

namespace NMC::Server {

    class ConnectionHandlers {
    public:
        // Static methods to handle various connection-related API requests
        static void handleGetConnectionStatus(const httplib::Request& req, httplib::Response& res);
        static void handleMakeConnection(const httplib::Request& req, httplib::Response& res);
        static void handleDropConnection(const httplib::Request& req, httplib::Response& res);
        static void handleListConnections(const httplib::Request& req, httplib::Response& res);
        static void handleSelectConnection(const httplib::Request& req, httplib::Response& res);

    private:
        // Mock storage for connections. In a real app, this would be a database or persistent config.
        static std::vector<NMC::Server::Models::Connection> s_connections;
        static std::string s_currentConnectionName;

        // Helper to parse JSON from request body (very basic, for demonstration)
        // static std::map<std::string, std::string> parseJsonBody(const std::string& body);

        // Helper to simulate health check (for status command)
        static std::string simulateHealthCheck(const std::string& endpoint);
        static nlohmann::json parseJsonBody(const std::string& body);
    };

} // namespace NMC::Server

#endif // NMC_SERVER_CONNECTION_HANDLERS_H