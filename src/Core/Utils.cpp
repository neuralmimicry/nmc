#include "Utils.h"

namespace NMC::Core::Utils {

std::string padRight(const std::string& s, size_t n) {
    if (n > s.length()) {
        return s + std::string(n - s.length(), ' ');
    }
    return s;
}

void printJson(const std::string& data) {
    std::cout << R"({"format": "json", "data": ")" << data << "\"}" << std::endl;
}

void printYaml(const std::string& data) {
    std::cout << "format: yaml\ndata: " << data << std::endl;
}

void printHumanReadable(const std::string& data) {
    std::cout << "Human-readable output: " << data << std::endl;
}

} // namespace NMC::Core::Utils

