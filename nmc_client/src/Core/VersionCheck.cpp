#include "VersionCheck.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>
#ifndef _WIN32
#include <sys/wait.h>
#endif

#include <nlohmann/json.hpp>

namespace NMC::Core::VersionCheck {

namespace {

struct ExecResult {
    int exitCode{1};
    std::string output;
};

constexpr const char* kDefaultRepository = "neuralmimicry/nmc";

std::string shellQuote(const std::string& value) {
#ifdef _WIN32
    std::string quoted = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('"');
    return quoted;
#else
    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');
    return quoted;
#endif
}

ExecResult runShellCommand(const std::vector<std::string>& args) {
    ExecResult result;
    if (args.empty()) {
        result.output = "No command specified.";
        return result;
    }

    std::ostringstream command;
    bool first = true;
    for (const auto& arg : args) {
        if (!first) {
            command << ' ';
        }
        command << shellQuote(arg);
        first = false;
    }
    command << " 2>&1";

#ifdef _WIN32
    FILE* pipe = _popen(command.str().c_str(), "r");
#else
    FILE* pipe = ::popen(command.str().c_str(), "r");
#endif
    if (!pipe) {
        result.output = "Unable to execute command.";
        return result;
    }

    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output.append(buffer.data());
    }

    int status = -1;
#ifdef _WIN32
    status = _pclose(pipe);
    if (status != -1) {
        result.exitCode = status;
    }
#else
    status = ::pclose(pipe);
    if (status != -1 && WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    }
#endif
    if (status == -1) {
        result.exitCode = 1;
    }

    return result;
}

std::string trimCopy(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string normalizeVersionTag(std::string value) {
    value = trimCopy(value);
    if (!value.empty() && (value.front() == 'v' || value.front() == 'V')) {
        value.erase(value.begin());
    }
    return value;
}

std::vector<int> parseVersionParts(const std::string& rawVersion) {
    std::string cleaned = normalizeVersionTag(rawVersion);
    const auto cutoff = cleaned.find_first_not_of("0123456789.");
    if (cutoff != std::string::npos) {
        cleaned = cleaned.substr(0, cutoff);
    }
    if (cleaned.empty()) {
        return {};
    }

    std::vector<int> parts;
    std::stringstream ss(cleaned);
    std::string token;
    while (std::getline(ss, token, '.')) {
        if (token.empty() || token.size() > 9 ||
            !std::all_of(token.begin(), token.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
            return {};
        }
        parts.push_back(std::stoi(token));
    }
    return parts;
}

int compareVersions(const std::string& lhs, const std::string& rhs) {
    const std::vector<int> left = parseVersionParts(lhs);
    const std::vector<int> right = parseVersionParts(rhs);

    if (left.empty() || right.empty()) {
        return normalizeVersionTag(lhs).compare(normalizeVersionTag(rhs));
    }

    const size_t maxParts = std::max(left.size(), right.size());
    for (size_t i = 0; i < maxParts; ++i) {
        const int leftPart = i < left.size() ? left[i] : 0;
        const int rightPart = i < right.size() ? right[i] : 0;
        if (leftPart < rightPart) {
            return -1;
        }
        if (leftPart > rightPart) {
            return 1;
        }
    }
    return 0;
}

std::string releaseCheckUrl(const std::string& repository) {
    const char* overrideUrl = std::getenv("NMC_RELEASE_CHECK_URL");
    if (overrideUrl && *overrideUrl) {
        return overrideUrl;
    }
    return "https://api.github.com/repos/" + repository + "/releases/latest";
}

} // namespace

std::string currentVersion() {
#ifdef NMC_CLIENT_VERSION
    return NMC_CLIENT_VERSION;
#else
    return "0.0.2";
#endif
}

ReleaseCheckResult checkLatestRelease() {
    ReleaseCheckResult result;
    result.repository = kDefaultRepository;
    result.currentVersion = currentVersion();

    const std::string url = releaseCheckUrl(result.repository);
    const ExecResult commandResult = runShellCommand({
        "curl",
        "--silent",
        "--show-error",
        "--fail",
        "--connect-timeout",
        "3",
        "--max-time",
        "5",
        "-H",
        "Accept: application/vnd.github+json",
        url
    });

    if (commandResult.exitCode != 0) {
        result.checkSucceeded = false;
        result.message = trimCopy(commandResult.output);
        if (result.message.find("404") != std::string::npos) {
            result.message = "No published GitHub releases found for " + result.repository + ".";
        }
        if (result.message.empty()) {
            result.message = "Unable to query GitHub releases.";
        }
        return result;
    }

    try {
        const auto payload = nlohmann::json::parse(commandResult.output);
        if (!payload.contains("tag_name") || !payload["tag_name"].is_string()) {
            result.checkSucceeded = false;
            result.message = "GitHub response did not include a tag_name.";
            return result;
        }

        result.latestVersion = payload["tag_name"].get<std::string>();
        result.checkSucceeded = true;
        result.updateAvailable = compareVersions(result.currentVersion, result.latestVersion) < 0;
        result.message = result.updateAvailable
            ? "A newer release is available."
            : "Current version is up to date.";
        return result;

    } catch (const std::exception& e) {
        result.checkSucceeded = false;
        result.message = "Failed to parse GitHub release metadata: " + std::string(e.what());
        return result;
    }
}

} // namespace NMC::Core::VersionCheck
