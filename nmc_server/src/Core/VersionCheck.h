#ifndef NMC_SERVER_CORE_VERSION_CHECK_H
#define NMC_SERVER_CORE_VERSION_CHECK_H

#include <string>

namespace NMC::Server::VersionCheck {

struct ReleaseCheckResult {
    std::string repository;
    std::string currentVersion;
    std::string latestVersion;
    bool checkSucceeded{false};
    bool updateAvailable{false};
    std::string message;
};

std::string currentVersion();
ReleaseCheckResult checkLatestRelease();

} // namespace NMC::Server::VersionCheck

#endif // NMC_SERVER_CORE_VERSION_CHECK_H
