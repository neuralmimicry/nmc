#ifndef NMC_CORE_UTILS_H
#define NMC_CORE_UTILS_H

#include <string>
#include <vector>
#include <iostream>
#include <nlohmann/json.hpp>

namespace NMC::Core::Utils {

    // Function to pad a string to the right with spaces
    std::string padRight(const std::string& s, size_t n);

    /// Serialise one JSON value as a complete, machine-readable line.
    void printJson(const nlohmann::json& data);
    void printYaml(const std::string& data);
    void printHumanReadable(const std::string& data);
    /// Returns the path to the user’s NMC config file.
    std::string getConfigPath();
} // namespace NMC::Core::Utils



#endif // NMC_CORE_UTILS_H
