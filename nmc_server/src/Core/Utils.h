// server/Utils.h
#ifndef NMC_SERVER_UTILS_H
#define NMC_SERVER_UTILS_H

#include <string>
#include <random>
#include <chrono>
#include <functional> // For std::hash
#include <algorithm> // For std::transform
#include <iostream>

namespace NMC {
    namespace Server {
        namespace Utils {

// Utility to generate a simple unique ID (e.g., for resources)
            std::string generateUniqueId(const std::string& prefix);

        } // namespace Utils
    } // namespace Server
} // namespace NMC

#endif // NMC_SERVER_UTILS_H