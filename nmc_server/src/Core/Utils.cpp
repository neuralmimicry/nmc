// server/Utils.cpp
#include "Utils.h"

namespace NMC {
    namespace Server {
        namespace Utils {

            std::string generateUniqueId(const std::string& prefix) {
                auto now = std::chrono::high_resolution_clock::now();
                auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

                // Use a hash of the current time in nanoseconds and a random number
                std::mt19937_64 rng(nanoseconds);
                std::uniform_int_distribution<long long> dist(0, 9999999999LL); // Max 10 digits

                long long random_suffix = dist(rng);

                return prefix + "-" + std::to_string(nanoseconds) + "-" + std::to_string(random_suffix);
            }

        } // namespace Utils
    } // namespace Server
} // namespace NMC