#ifndef NMC_CORE_VERSION_CHECK_H
#define NMC_CORE_VERSION_CHECK_H

#include <string>

namespace NMC::Core::VersionCheck {

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

} // namespace NMC::Core::VersionCheck

#endif // NMC_CORE_VERSION_CHECK_H
