#include "VersionCommand.h"

#include "Core/VersionCheck.h"

#include <iostream>

namespace NMC::Commands {

VersionCommand::VersionCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client) : BaseCommand("version", "CLI version", std::move(client)) {
    usage = "nmc version";
    examples = "nmc version\nnmc version --no-check";
    addFlag(CLI::Flag("n", "no-check", "Skip GitHub latest-release check", CLI::FlagType::Bool, false));
}

int VersionCommand::execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) {
    (void)parsedArgs;

    Models::CloudResponse response;
    response.success = true;
    response.statusCode = 200;
    response.message = "Version information retrieved successfully.";
    response.data = nlohmann::json::object();

    const std::string current = NMC::Core::VersionCheck::currentVersion();
    response.data["component"] = "nmc_client";
    response.data["current_version"] = current;
    response.data["repository"] = "neuralmimicry/nmc";

    const bool skipCheck = parsedFlags.find("no-check") != parsedFlags.end();
    if (skipCheck) {
        response.data["release_check"] = {
            {"attempted", false},
            {"succeeded", false},
            {"message", "Release check skipped (--no-check)."}
        };
        printOutput(response, globalFlags);
        return 0;
    }

    const NMC::Core::VersionCheck::ReleaseCheckResult check = NMC::Core::VersionCheck::checkLatestRelease();
    response.data["release_check"] = {
        {"attempted", true},
        {"succeeded", check.checkSucceeded},
        {"latest_version", check.latestVersion},
        {"update_available", check.updateAvailable},
        {"message", check.message}
    };

    if (!check.checkSucceeded) {
        response.message = "Version retrieved; latest release check failed.";
    }

    printOutput(response, globalFlags);
    return 0;
}

} // namespace NMC::Commands
