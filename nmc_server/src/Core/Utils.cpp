// server/Utils.cpp
#include "Utils.h"
#include <sstream> // For std::stringstream
#include <fstream> // For std::ifstream
#include <iostream> // For std::cerr

namespace NMC::Server {
    // Static member to ensure random device is initialized once
    static std::random_device rd;
    // Mersenne Twister engine seeded once
    static std::mt19937_64 eng(rd());
    // Distribution for generating random alphanumeric characters
    static std::uniform_int_distribution<> dist(0, 15); // For hex chars

    std::string Utils::generateUniqueId(const std::string& prefix) {
        std::stringstream ss;
        ss << prefix << "-";
        for (int i = 0; i < 12; ++i) { // Generate 12 random hex characters
            ss << std::hex << dist(eng);
        }
        return ss.str();
    }

    std::string Utils::readFile(const std::string& filePath) {
        std::ifstream fileStream(filePath);
        if (!fileStream.is_open()) {
            // Log error to standard error stream
            std::cerr << "Error: Could not open file " << filePath << std::endl;
            return ""; // Return empty string on error
        }
        std::stringstream buffer;
        buffer << fileStream.rdbuf();
        return buffer.str();
    }

} // namespace NMC::Server

