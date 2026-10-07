#include "Utils.h"

namespace NMC::Core::Utils {

std::string padRight(const std::string& s, size_t n) {
    if (n > s.length()) {
        return s + std::string(n - s.length(), ' ');
    }
    return s;
}

void printJson(const nlohmann::json& data) {
    std::cout << data.dump() << std::endl;
}

void printYaml(const std::string& data) {
    std::cout << "format: yaml\ndata: " << data << std::endl;
}

void printHumanReadable(const std::string& data) {
    std::cout << "Human-readable output: " << data << std::endl;
}

// Helper function to determine the config path.
std::string getConfigPath() {
    std::string path;
    const char* homeDir = std::getenv("HOME");
    if (homeDir) {
        path = std::string(homeDir) + "/.nmc/config.json";
    } else {
        path = "config.json"; // Fallback
        std::cerr << "Warning: HOME environment variable not set. Using 'config.json' in current directory for API client." << std::endl;
    }
    return path;
}

} // namespace NMC::Core::Utils
