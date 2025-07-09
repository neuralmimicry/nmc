// Models/Connection.h
#ifndef NMC_CONNECTION_H
#define NMC_CONNECTION_H

#include <string>
#include <vector>

namespace NMC::Models {

    struct Connection {
        std::string name;
        std::string endpoint;
        bool isActive;
        // Potentially add more fields like userId, token/key identifier, lastUsed, etc.
        // For simplicity, we'll assume authentication is handled internally by CloudAPIClient
        // using connection name to retrieve relevant secure credentials.

        // Default constructor
        Connection() : name(""), endpoint(""), isActive(false) {}

        // Parameterized constructor
        Connection(std::string name, std::string endpoint, bool isActive)
                : name(std::move(name)), endpoint(std::move(endpoint)), isActive(isActive) {}
    };

// CloudResponse can also contain a list of connections for 'list' command
// Add a field to CloudResponse for connections if not already present, or create a new response type.
// For now, assume CloudResponse can encapsulate various data types, or a specific ConnectionResponse struct.
// Given the existing CloudResponse, let's assume the data field can hold a JSON string or similar
// that represents a list of connections, which the CLI will then parse.
// Alternatively, if we want type-safety, we would extend CloudResponse or create a specific one.
// For this example, let's assume CloudResponse's 'data' string can hold a serialized list of connections.

} // namespace NMC::Models

#endif // NMC_CONNECTION_H