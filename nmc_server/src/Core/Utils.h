// server/Utils.h
#pragma once

#include <string>
#include <random> // For random number generation
#include <chrono> // For seeding random number generator
#include <vector>

namespace NMC::Server {
    /**
     * @brief Utility functions for the server.
     * This class provides helper functions such as unique ID generation.
     */
    class Utils {
    public:
        /**
         * @brief Generates a unique ID with a specified prefix.
         * @param prefix The prefix for the generated ID (e.g., "vm", "bucket").
         * @return A unique string identifier.
         */
        static std::string generateUniqueId(const std::string& prefix);

        /**
         * @brief Reads the entire content of a file into a string.
         * @param filePath The path to the file to read.
         * @return The content of the file as a string, or empty string on error.
         */
        static std::string readFile(const std::string& filePath);
    };
} // namespace NMC::Server

