#include "TraceyCVEIntel.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sys/wait.h>

namespace NMC::Server {

namespace {

constexpr int64_t kAssessmentProtocolVersion = 2;

int64_t nowEpochMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string toLower(std::string value) {
    for (auto& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string collapseSpaces(std::string value) {
    std::string out;
    out.reserve(value.size());
    bool pendingSpace = false;
    for (char ch : value) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out.push_back(' ');
            pendingSpace = false;
        }
        out.push_back(ch);
    }
    return trim(out);
}

std::string normalizeKey(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    bool pendingDash = false;
    for (char ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch)) {
            if (pendingDash && !out.empty()) {
                out.push_back('-');
                pendingDash = false;
            }
            out.push_back(static_cast<char>(std::tolower(uch)));
        } else {
            pendingDash = !out.empty();
        }
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    return out;
}

uint64_t fnv1a64(const std::string& value) {
    constexpr uint64_t kOffset = 14695981039346656037ull;
    constexpr uint64_t kPrime = 1099511628211ull;
    uint64_t hash = kOffset;
    for (unsigned char ch : value) {
        hash ^= static_cast<uint64_t>(ch);
        hash *= kPrime;
    }
    return hash;
}

std::string hex64(uint64_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (std::size_t index = 0; index < out.size(); ++index) {
        const std::size_t shift = (out.size() - index - 1) * 4;
        out[index] = digits[(value >> shift) & 0x0f];
    }
    return out;
}

std::vector<std::string> normalizeAgentIds(const std::vector<std::string>& orderedAgentIds,
                                           const std::string& requiredAgentId) {
    std::vector<std::string> ids;
    ids.reserve(orderedAgentIds.size() + 1);
    for (const auto& id : orderedAgentIds) {
        const std::string trimmed = trim(id);
        if (!trimmed.empty()) {
            ids.push_back(trimmed);
        }
    }
    if (!trim(requiredAgentId).empty()) {
        ids.push_back(trim(requiredAgentId));
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

std::string makePlanToken(const std::string& agentId,
                          int64_t cycleStartMs,
                          std::size_t slotIndex,
                          std::size_t slotCount,
                          std::size_t sliceCount,
                          int64_t slotStartMs,
                          int64_t slotEndMs) {
    std::ostringstream seed;
    seed << agentId << '|'
         << cycleStartMs << '|'
         << slotIndex << '|'
         << slotCount << '|'
         << sliceCount << '|'
         << slotStartMs << '|'
         << slotEndMs;
    return hex64(fnv1a64(seed.str()));
}

std::vector<std::string> tokenize(const std::string& value) {
    std::vector<std::string> tokens;
    std::string current;
    current.reserve(value.size());
    for (char ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch)) {
            current.push_back(static_cast<char>(std::tolower(uch)));
        } else if (!current.empty()) {
            if (current.size() > 1) {
                tokens.push_back(current);
            }
            current.clear();
        }
    }
    if (!current.empty() && current.size() > 1) {
        tokens.push_back(current);
    }

    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    return tokens;
}

std::string escapeShell(const std::string& value) {
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    out += "'";
    return out;
}

int decodeExitCode(int rc) {
    if (rc == -1) {
        return -1;
    }
    if (WIFEXITED(rc)) {
        return WEXITSTATUS(rc);
    }
    return rc;
}

int runCommand(const std::string& command, std::string* output = nullptr) {
    const std::string shellCommand = command + " 2>&1";
    FILE* pipe = ::popen(shellCommand.c_str(), "r");
    if (!pipe) {
        return -1;
    }

    if (output != nullptr) {
        std::array<char, 4096> buffer{};
        while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output->append(buffer.data());
        }
    } else {
        std::array<char, 1024> buffer{};
        while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        }
    }

    return decodeExitCode(::pclose(pipe));
}

std::filesystem::path resolveDefaultRoot() {
    if (const char* raw = std::getenv("NMC_TRACEY_CVE_ROOT")) {
        const std::string value = trim(raw);
        if (!value.empty()) {
            return std::filesystem::path(value);
        }
    }

    if (const char* stateHome = std::getenv("XDG_STATE_HOME")) {
        const std::string value = trim(stateHome);
        if (!value.empty()) {
            return std::filesystem::path(value) / "nmc" / "tracey_cve";
        }
    }
    if (const char* home = std::getenv("HOME")) {
        const std::string value = trim(home);
        if (!value.empty()) {
            return std::filesystem::path(value) / ".local" / "state" / "nmc" / "tracey_cve";
        }
    }
    return std::filesystem::temp_directory_path() / "nmc-tracey-cve";
}

int64_t parseEnvInt64(const char* key, int64_t fallback, int64_t minValue, int64_t maxValue) {
    const char* raw = std::getenv(key);
    if (!raw || !*raw) {
        return fallback;
    }
    try {
        int64_t parsed = std::stoll(raw);
        if (parsed < minValue) {
            return minValue;
        }
        if (parsed > maxValue) {
            return maxValue;
        }
        return parsed;
    } catch (const std::exception&) {
        return fallback;
    }
}

bool parseEnvBool(const char* key, bool fallback) {
    const char* raw = std::getenv(key);
    if (!raw || !*raw) {
        return fallback;
    }
    const std::string value = toLower(trim(raw));
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        return false;
    }
    return fallback;
}

std::string firstStringValue(const nlohmann::json& objectNode,
                             std::initializer_list<const char*> keys,
                             const std::string& fallback = "") {
    if (!objectNode.is_object()) {
        return fallback;
    }
    for (const char* key : keys) {
        const auto it = objectNode.find(key);
        if (it != objectNode.end() && it->is_string()) {
            const std::string value = trim(it->get<std::string>());
            if (!value.empty()) {
                return value;
            }
        }
    }
    return fallback;
}

double firstDoubleValue(const nlohmann::json& objectNode,
                        std::initializer_list<const char*> keys,
                        double fallback = 0.0) {
    if (!objectNode.is_object()) {
        return fallback;
    }
    for (const char* key : keys) {
        const auto it = objectNode.find(key);
        if (it == objectNode.end() || it->is_null()) {
            continue;
        }
        if (it->is_number()) {
            return it->get<double>();
        }
        if (it->is_string()) {
            try {
                return std::stod(trim(it->get<std::string>()));
            } catch (const std::exception&) {
                continue;
            }
        }
    }
    return fallback;
}

int64_t firstInt64Value(const nlohmann::json& objectNode,
                        std::initializer_list<const char*> keys,
                        int64_t fallback = 0) {
    if (!objectNode.is_object()) {
        return fallback;
    }
    for (const char* key : keys) {
        const auto it = objectNode.find(key);
        if (it == objectNode.end() || it->is_null()) {
            continue;
        }
        if (it->is_number_integer()) {
            return it->get<int64_t>();
        }
        if (it->is_number_unsigned()) {
            return static_cast<int64_t>(it->get<uint64_t>());
        }
        if (it->is_string()) {
            try {
                return std::stoll(trim(it->get<std::string>()));
            } catch (const std::exception&) {
                continue;
            }
        }
    }
    return fallback;
}

double clamp01(double value) {
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

std::string severityFromScore(double score) {
    if (score >= 9.0) {
        return "critical";
    }
    if (score >= 7.0) {
        return "high";
    }
    if (score >= 4.0) {
        return "medium";
    }
    if (score > 0.0) {
        return "low";
    }
    return "unknown";
}

double severityWeight(const std::string& severity) {
    const std::string normalized = toLower(trim(severity));
    if (normalized == "critical") {
        return 1.00;
    }
    if (normalized == "high") {
        return 0.80;
    }
    if (normalized == "medium") {
        return 0.55;
    }
    if (normalized == "low") {
        return 0.30;
    }
    return 0.20;
}

bool containsKeywordRecursive(const nlohmann::json& node, const std::vector<std::string>& keywords) {
    if (node.is_string()) {
        const std::string value = toLower(node.get<std::string>());
        for (const auto& keyword : keywords) {
            if (value.find(keyword) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    if (node.is_array()) {
        for (const auto& value : node) {
            if (containsKeywordRecursive(value, keywords)) {
                return true;
            }
        }
        return false;
    }

    if (node.is_object()) {
        for (const auto& [key, value] : node.items()) {
            if (containsKeywordRecursive(nlohmann::json(key), keywords) ||
                containsKeywordRecursive(value, keywords)) {
                return true;
            }
        }
    }
    return false;
}

struct CveVersionRule {
    std::string status;
    std::string version;
    std::string lessThan;
    std::string lessThanOrEqual;
    std::string versionType;
};

struct CveAffectedTarget {
    std::string vendor;
    std::string product;
    std::string packageUrl;
    std::string packageName;
    std::string defaultStatus;
    std::vector<std::string> modules;
    std::vector<std::string> platforms;
    std::vector<std::string> cpes;
    std::vector<CveVersionRule> versions;
    std::vector<std::string> tokens;
};

struct CveRecord {
    std::string cveId;
    std::string description;
    std::string severity;
    double cvss{0.0};
    bool kev{false};
    std::string updated;
    std::vector<std::string> tokens;
    std::vector<CveAffectedTarget> affected;
};

struct CveIndexFile {
    int version{1};
    std::string head;
    int64_t generatedEpochMs{0};
    std::vector<CveRecord> records;
};

struct AgentProgress {
    int64_t lastPlanMs{0};
    int64_t lastReportMs{0};
    int64_t lastSuccessMs{0};
    int64_t lastFailureMs{0};
    int64_t cycleStartMs{0};
    int64_t cycleDeadlineMs{0};
    int64_t slotStartMs{0};
    int64_t slotEndMs{0};
    std::size_t slotIndex{0};
    std::size_t slotCount{0};
    std::size_t sliceCount{0};
    std::set<int> completedSlices;
    std::size_t lastMatchCount{0};
    std::size_t criticalMatches{0};
    std::size_t highMatches{0};
    std::size_t mediumMatches{0};
    std::size_t lowMatches{0};
    std::size_t kevMatches{0};
    double highestCvss{0.0};
    std::size_t packagesSeen{0};
    std::size_t modulesSeen{0};
    std::size_t servicesSeen{0};
    std::size_t processesSeen{0};
    std::size_t reportsAccepted{0};
    std::size_t reportsRejected{0};
    std::size_t duplicateReports{0};
    std::size_t stalePlanReports{0};
    std::size_t semanticFaults{0};
    int64_t protocolVersion{1};
    std::string inventoryDigest;
    std::string planToken;
    std::string lastDisposition;
    std::string lastRequestId;
    std::string lastError;
};

struct CoordinationCycleState {
    int64_t cycleStartMs{0};
    int64_t cycleDeadlineMs{0};
    std::size_t slotCount{1};
    std::vector<std::string> frozenAgentIds;
    std::unordered_map<std::string, std::size_t> overflowSlotByAgent;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CveVersionRule, status, version, lessThan, lessThanOrEqual, versionType)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CveAffectedTarget, vendor, product, packageUrl, packageName, defaultStatus, modules, platforms, cpes, versions, tokens)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CveRecord, cveId, description, severity, cvss, kev, updated, tokens, affected)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CveIndexFile, version, head, generatedEpochMs, records)

void appendTokens(std::vector<std::string>& target, const std::vector<std::string>& values) {
    target.insert(target.end(), values.begin(), values.end());
}

void appendTokens(std::vector<std::string>& target, const std::string& value) {
    appendTokens(target, tokenize(value));
}

void dedupeTokens(std::vector<std::string>& tokens) {
    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
}

void extractCvssRecursive(const nlohmann::json& node, double& bestScore, std::string& bestSeverity) {
    if (!node.is_object()) {
        if (node.is_array()) {
            for (const auto& entry : node) {
                extractCvssRecursive(entry, bestScore, bestSeverity);
            }
        }
        return;
    }

    static const std::array<const char*, 6> cvssKeys = {
            "cvssV4_0", "cvssV4_1", "cvssV3_1", "cvssV3_0", "cvssV2_0", "cvssV2"
    };
    for (const char* key : cvssKeys) {
        const auto it = node.find(key);
        if (it != node.end() && it->is_object()) {
            const double score = firstDoubleValue(*it, {"baseScore"}, -1.0);
            if (score > bestScore) {
                bestScore = score;
                bestSeverity = toLower(firstStringValue(*it, {"baseSeverity"}, severityFromScore(score)));
            }
        }
    }

    for (const auto& [key, value] : node.items()) {
        if (key == "cvssV4_0" || key == "cvssV4_1" || key == "cvssV3_1" ||
            key == "cvssV3_0" || key == "cvssV2_0" || key == "cvssV2") {
            continue;
        }
        extractCvssRecursive(value, bestScore, bestSeverity);
    }
}

std::vector<CveVersionRule> parseVersionRules(const nlohmann::json& versionsNode) {
    std::vector<CveVersionRule> rules;
    if (!versionsNode.is_array()) {
        return rules;
    }
    for (const auto& item : versionsNode) {
        if (!item.is_object()) {
            continue;
        }
        CveVersionRule rule;
        rule.status = toLower(firstStringValue(item, {"status"}, "affected"));
        rule.version = firstStringValue(item, {"version"});
        rule.lessThan = firstStringValue(item, {"lessThan"});
        rule.lessThanOrEqual = firstStringValue(item, {"lessThanOrEqual"});
        rule.versionType = toLower(firstStringValue(item, {"versionType"}));
        rules.push_back(rule);
    }
    return rules;
}

std::vector<std::string> parseStringArray(const nlohmann::json& node) {
    std::vector<std::string> out;
    if (!node.is_array()) {
        return out;
    }
    out.reserve(node.size());
    for (const auto& value : node) {
        if (value.is_string()) {
            const std::string trimmed = trim(value.get<std::string>());
            if (!trimmed.empty()) {
                out.push_back(trimmed);
            }
        }
    }
    return out;
}

void appendAffectedTargets(const nlohmann::json& containerNode, std::vector<CveAffectedTarget>& out) {
    if (!containerNode.is_object()) {
        return;
    }
    const auto affectedIt = containerNode.find("affected");
    if (affectedIt == containerNode.end() || !affectedIt->is_array()) {
        return;
    }

    for (const auto& item : *affectedIt) {
        if (!item.is_object()) {
            continue;
        }
        CveAffectedTarget target;
        target.vendor = firstStringValue(item, {"vendor"});
        target.product = firstStringValue(item, {"product"});
        target.packageUrl = firstStringValue(item, {"packageURL", "packageUrl", "purl"});
        target.packageName = firstStringValue(item, {"packageName", "package_name"});
        target.defaultStatus = toLower(firstStringValue(item, {"defaultStatus"}, "unknown"));
        target.modules = parseStringArray(item.value("modules", nlohmann::json::array()));
        target.platforms = parseStringArray(item.value("platforms", nlohmann::json::array()));
        target.cpes = parseStringArray(item.value("cpes", nlohmann::json::array()));
        target.versions = parseVersionRules(item.value("versions", nlohmann::json::array()));

        appendTokens(target.tokens, target.vendor);
        appendTokens(target.tokens, target.product);
        appendTokens(target.tokens, target.packageUrl);
        appendTokens(target.tokens, target.packageName);
        for (const auto& module : target.modules) {
            appendTokens(target.tokens, module);
        }
        for (const auto& platform : target.platforms) {
            appendTokens(target.tokens, platform);
        }
        for (const auto& cpe : target.cpes) {
            appendTokens(target.tokens, cpe);
        }
        dedupeTokens(target.tokens);

        if (!target.product.empty() || !target.packageUrl.empty() || !target.packageName.empty() ||
            !target.tokens.empty()) {
            out.push_back(std::move(target));
        }
    }
}

bool parseCveFile(const std::filesystem::path& path, CveRecord& recordOut, std::string& errorOut) {
    std::ifstream input(path);
    if (!input.is_open()) {
        errorOut = "failed to open file";
        return false;
    }

    nlohmann::json root;
    try {
        input >> root;
    } catch (const std::exception& e) {
        errorOut = std::string("json parse failed: ") + e.what();
        return false;
    }

    if (!root.is_object()) {
        errorOut = "cve record root is not an object";
        return false;
    }

    const auto metadataIt = root.find("cveMetadata");
    if (metadataIt == root.end() || !metadataIt->is_object()) {
        errorOut = "missing cveMetadata";
        return false;
    }

    const std::string state = toLower(firstStringValue(*metadataIt, {"state"}));
    if (!state.empty() && state != "published") {
        errorOut = "record is not published";
        return false;
    }

    CveRecord record;
    record.cveId = firstStringValue(*metadataIt, {"cveId"});
    record.updated = firstStringValue(*metadataIt, {"dateUpdated"});
    if (record.cveId.empty()) {
        errorOut = "missing cveMetadata.cveId";
        return false;
    }

    const auto containersIt = root.find("containers");
    if (containersIt == root.end() || !containersIt->is_object()) {
        errorOut = "missing containers";
        return false;
    }

    const auto cnaIt = containersIt->find("cna");
    if (cnaIt != containersIt->end() && cnaIt->is_object()) {
        const auto descriptionsIt = cnaIt->find("descriptions");
        if (descriptionsIt != cnaIt->end() && descriptionsIt->is_array()) {
            for (const auto& description : *descriptionsIt) {
                if (!description.is_object()) {
                    continue;
                }
                const std::string lang = toLower(firstStringValue(description, {"lang"}, "en"));
                if (record.description.empty() || lang == "en") {
                    record.description = collapseSpaces(firstStringValue(description, {"value"}));
                    if (!record.description.empty() && lang == "en") {
                        break;
                    }
                }
            }
        }
        appendAffectedTargets(*cnaIt, record.affected);
    }

    const auto adpIt = containersIt->find("adp");
    if (adpIt != containersIt->end() && adpIt->is_array()) {
        for (const auto& adp : *adpIt) {
            appendAffectedTargets(adp, record.affected);
        }
    }

    double bestScore = -1.0;
    std::string bestSeverity;
    extractCvssRecursive(*containersIt, bestScore, bestSeverity);
    if (bestScore >= 0.0) {
        record.cvss = bestScore;
    }
    record.severity = !bestSeverity.empty() ? bestSeverity : severityFromScore(record.cvss);
    record.kev = containsKeywordRecursive(*containersIt, {"known exploited", "known-exploited", "kev"});

    appendTokens(record.tokens, record.description);
    for (const auto& target : record.affected) {
        appendTokens(record.tokens, target.tokens);
    }
    dedupeTokens(record.tokens);

    if (record.affected.empty() || record.tokens.empty()) {
        errorOut = "record had no usable affected data";
        return false;
    }

    recordOut = std::move(record);
    return true;
}

std::vector<std::string> splitVersionSegments(const std::string& raw) {
    std::vector<std::string> segments;
    std::string current;
    enum class Mode { None, Digit, Alpha };
    Mode mode = Mode::None;

    for (char ch : raw) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        const bool isDigit = std::isdigit(uch) != 0;
        const bool isAlpha = std::isalpha(uch) != 0;
        if (!isDigit && !isAlpha) {
            if (!current.empty()) {
                segments.push_back(current);
                current.clear();
                mode = Mode::None;
            }
            continue;
        }

        const Mode nextMode = isDigit ? Mode::Digit : Mode::Alpha;
        if (!current.empty() && mode != nextMode) {
            segments.push_back(current);
            current.clear();
        }
        current.push_back(static_cast<char>(std::tolower(uch)));
        mode = nextMode;
    }

    if (!current.empty()) {
        segments.push_back(current);
    }
    return segments;
}

bool isNumericSegment(const std::string& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

int compareVersions(const std::string& leftRaw, const std::string& rightRaw) {
    const auto left = splitVersionSegments(leftRaw);
    const auto right = splitVersionSegments(rightRaw);
    const std::size_t count = std::max(left.size(), right.size());
    for (std::size_t index = 0; index < count; ++index) {
        const std::string leftValue = index < left.size() ? left[index] : "0";
        const std::string rightValue = index < right.size() ? right[index] : "0";
        if (leftValue == rightValue) {
            continue;
        }

        const bool leftNumeric = isNumericSegment(leftValue);
        const bool rightNumeric = isNumericSegment(rightValue);
        if (leftNumeric && rightNumeric) {
            const long long leftNum = std::stoll(leftValue);
            const long long rightNum = std::stoll(rightValue);
            if (leftNum < rightNum) {
                return -1;
            }
            if (leftNum > rightNum) {
                return 1;
            }
            continue;
        }

        if (leftValue < rightValue) {
            return -1;
        }
        if (leftValue > rightValue) {
            return 1;
        }
    }
    return 0;
}

double versionMatchScore(const std::string& installedVersion,
                         const CveAffectedTarget& target,
                         std::string& reasonOut) {
    const std::string installed = trim(installedVersion);
    if (target.versions.empty()) {
        reasonOut = target.defaultStatus == "affected" ? "default affected status" : "no version bounds";
        return target.defaultStatus == "affected" ? 0.85 : 0.60;
    }
    if (installed.empty()) {
        reasonOut = "no installed version provided";
        return 0.55;
    }

    bool compared = false;
    bool explicitUnaffected = false;
    for (const auto& rule : target.versions) {
        if (trim(rule.version).empty() && trim(rule.lessThan).empty() && trim(rule.lessThanOrEqual).empty()) {
            continue;
        }

        bool matched = false;
        compared = true;
        if (!rule.version.empty() && !rule.lessThan.empty()) {
            matched = compareVersions(installed, rule.version) >= 0 &&
                      compareVersions(installed, rule.lessThan) < 0;
        } else if (!rule.version.empty() && !rule.lessThanOrEqual.empty()) {
            matched = compareVersions(installed, rule.version) >= 0 &&
                      compareVersions(installed, rule.lessThanOrEqual) <= 0;
        } else if (!rule.lessThan.empty()) {
            matched = compareVersions(installed, rule.lessThan) < 0;
        } else if (!rule.lessThanOrEqual.empty()) {
            matched = compareVersions(installed, rule.lessThanOrEqual) <= 0;
        } else if (!rule.version.empty()) {
            matched = compareVersions(installed, rule.version) == 0;
        }

        const std::string status = rule.status.empty() ? "affected" : rule.status;
        if (matched && status == "affected") {
            reasonOut = "installed version matched affected range";
            return 1.0;
        }
        if (matched && status != "affected") {
            explicitUnaffected = true;
        }
    }

    if (explicitUnaffected) {
        reasonOut = "installed version matched unaffected range";
        return 0.05;
    }
    if (compared) {
        reasonOut = "installed version did not match affected range";
        return 0.15;
    }
    reasonOut = "unable to compare versions";
    return 0.45;
}

double tokenOverlapScore(const std::vector<std::string>& left,
                         const std::vector<std::string>& right) {
    if (left.empty() || right.empty()) {
        return 0.0;
    }

    std::size_t intersection = 0;
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < left.size() && j < right.size()) {
        if (left[i] == right[j]) {
            ++intersection;
            ++i;
            ++j;
        } else if (left[i] < right[j]) {
            ++i;
        } else {
            ++j;
        }
    }

    if (intersection == 0) {
        return 0.0;
    }
    const double denom = static_cast<double>(left.size() + right.size() - intersection);
    return denom > 0.0 ? static_cast<double>(intersection) / denom : 0.0;
}

double nameSimilarity(const std::string& left, const std::string& right) {
    const std::string leftKey = normalizeKey(left);
    const std::string rightKey = normalizeKey(right);
    if (leftKey.empty() || rightKey.empty()) {
        return 0.0;
    }
    if (leftKey == rightKey) {
        return 1.0;
    }
    if (leftKey.find(rightKey) != std::string::npos || rightKey.find(leftKey) != std::string::npos) {
        return 0.92;
    }
    const auto leftTokens = tokenize(leftKey);
    const auto rightTokens = tokenize(rightKey);
    return tokenOverlapScore(leftTokens, rightTokens);
}

struct InventoryItem {
    std::string type;
    std::string name;
    std::string version;
    std::vector<std::string> tokens;
    nlohmann::json raw;
};

InventoryItem makeInventoryItem(const std::string& type,
                                const std::string& name,
                                const std::string& version,
                                const nlohmann::json& raw) {
    InventoryItem item;
    item.type = type;
    item.name = trim(name);
    item.version = trim(version);
    item.raw = raw;
    appendTokens(item.tokens, item.name);
    if (raw.is_object()) {
        appendTokens(item.tokens, firstStringValue(raw, {"package_url", "packageUrl", "purl"}));
        appendTokens(item.tokens, firstStringValue(raw, {"path", "exe", "command", "unit"}));
        appendTokens(item.tokens, firstStringValue(raw, {"manager", "vendor"}));
    }
    dedupeTokens(item.tokens);
    return item;
}

std::vector<InventoryItem> collectInventoryItems(const nlohmann::json& report,
                                                 std::size_t& packagesOut,
                                                 std::size_t& modulesOut,
                                                 std::size_t& servicesOut,
                                                 std::size_t& processesOut) {
    std::vector<InventoryItem> items;
    const nlohmann::json inventory = report.contains("inventory") ? report["inventory"] : nlohmann::json::object();

    const auto addArrayItems = [&](const nlohmann::json& node,
                                   const std::string& type,
                                   std::size_t& counter) {
        if (!node.is_array()) {
            return;
        }
        for (const auto& entry : node) {
            if (entry.is_string()) {
                items.push_back(makeInventoryItem(type, entry.get<std::string>(), "", entry));
                counter++;
            } else if (entry.is_object()) {
                items.push_back(makeInventoryItem(
                        type,
                        firstStringValue(entry, {"name", "package", "module", "service", "unit", "process"}),
                        firstStringValue(entry, {"version", "release"}),
                        entry
                ));
                counter++;
            }
        }
    };

    addArrayItems(inventory.value("packages", nlohmann::json::array()), "package", packagesOut);
    addArrayItems(inventory.value("modules", nlohmann::json::array()), "module", modulesOut);
    addArrayItems(inventory.value("services", nlohmann::json::array()), "service", servicesOut);
    addArrayItems(inventory.value("processes", nlohmann::json::array()), "process", processesOut);

    const auto kernelIt = inventory.find("kernel");
    if (kernelIt != inventory.end()) {
        if (kernelIt->is_string()) {
            items.push_back(makeInventoryItem("kernel", "linux-kernel", kernelIt->get<std::string>(), *kernelIt));
        } else if (kernelIt->is_object()) {
            items.push_back(makeInventoryItem(
                    "kernel",
                    firstStringValue(*kernelIt, {"name"}, "linux-kernel"),
                    firstStringValue(*kernelIt, {"release", "version"}),
                    *kernelIt
            ));
        }
    }

    items.erase(
            std::remove_if(items.begin(), items.end(), [](const InventoryItem& item) {
                return item.name.empty() || item.tokens.empty();
            }),
            items.end()
    );
    return items;
}

std::string summarizeMatchReason(const std::string& itemName,
                                 const std::string& itemType,
                                 const std::string& product,
                                 const std::string& vendor,
                                 const std::string& versionReason) {
    std::ostringstream out;
    out << itemType << " '" << itemName << "'";
    if (!product.empty()) {
        out << " matched " << product;
    }
    if (!vendor.empty()) {
        out << " (" << vendor << ")";
    }
    if (!versionReason.empty()) {
        out << "; " << versionReason;
    }
    return out.str();
}

nlohmann::json progressToJson(const AgentProgress& progress, int64_t nowMs) {
    nlohmann::json completed = nlohmann::json::array();
    for (int slice : progress.completedSlices) {
        completed.push_back(slice);
    }
    return {
            {"last_plan_ms", progress.lastPlanMs},
            {"last_report_ms", progress.lastReportMs},
            {"last_report_age_ms", progress.lastReportMs > 0 && nowMs >= progress.lastReportMs
                                   ? nowMs - progress.lastReportMs
                                   : 0},
            {"cycle_start_ms", progress.cycleStartMs},
            {"cycle_deadline_ms", progress.cycleDeadlineMs},
            {"slot_start_ms", progress.slotStartMs},
            {"slot_end_ms", progress.slotEndMs},
            {"slot_index", progress.slotIndex},
            {"slot_count", progress.slotCount},
            {"slice_count", progress.sliceCount},
            {"completed_slices", completed},
            {"completed_slice_count", progress.completedSlices.size()},
            {"last_match_count", progress.lastMatchCount},
            {"critical_matches", progress.criticalMatches},
            {"high_matches", progress.highMatches},
            {"medium_matches", progress.mediumMatches},
            {"low_matches", progress.lowMatches},
            {"kev_matches", progress.kevMatches},
            {"highest_cvss", progress.highestCvss},
            {"packages_seen", progress.packagesSeen},
            {"modules_seen", progress.modulesSeen},
            {"services_seen", progress.servicesSeen},
            {"processes_seen", progress.processesSeen},
            {"reports_accepted", progress.reportsAccepted},
            {"reports_rejected", progress.reportsRejected},
            {"duplicate_reports", progress.duplicateReports},
            {"stale_plan_reports", progress.stalePlanReports},
            {"semantic_faults", progress.semanticFaults},
            {"protocol_version", progress.protocolVersion},
            {"inventory_digest", progress.inventoryDigest},
            {"plan_token", progress.planToken},
            {"last_disposition", progress.lastDisposition},
            {"last_request_id", progress.lastRequestId},
            {"last_success_ms", progress.lastSuccessMs},
            {"last_failure_ms", progress.lastFailureMs},
            {"last_error", progress.lastError}
    };
}

nlohmann::json inventorySummaryFromProgress(const AgentProgress& progress) {
    return {
            {"items", static_cast<int64_t>(
                    progress.packagesSeen + progress.modulesSeen + progress.servicesSeen + progress.processesSeen
            )},
            {"packages", static_cast<int64_t>(progress.packagesSeen)},
            {"modules", static_cast<int64_t>(progress.modulesSeen)},
            {"services", static_cast<int64_t>(progress.servicesSeen)},
            {"processes", static_cast<int64_t>(progress.processesSeen)}
    };
}

nlohmann::json findingSummaryFromProgress(const AgentProgress& progress) {
    return {
            {"matches", static_cast<int64_t>(progress.lastMatchCount)},
            {"critical", static_cast<int64_t>(progress.criticalMatches)},
            {"high", static_cast<int64_t>(progress.highMatches)},
            {"medium", static_cast<int64_t>(progress.mediumMatches)},
            {"low", static_cast<int64_t>(progress.lowMatches)},
            {"kev", static_cast<int64_t>(progress.kevMatches)},
            {"highest_cvss", progress.highestCvss}
    };
}

} // namespace

struct TraceyCVEIntel::Impl {
    mutable std::mutex mutex;
    bool enabled{true};
    std::string repoUrl{"git@github.com:CVEProject/cvelistV5.git"};
    std::string fallbackRepoUrl{"https://github.com/CVEProject/cvelistV5.git"};
    std::filesystem::path rootPath;
    std::filesystem::path repoPath;
    std::filesystem::path indexPath;
    int64_t syncIntervalMs{6LL * 60LL * 60LL * 1000LL};
    int64_t cycleMs{8LL * 60LL * 60LL * 1000LL};
    int64_t minSliceMs{10LL * 60LL * 1000LL};
    std::size_t defaultSliceCount{6};
    std::size_t maxMatchesPerReport{64};
    std::thread worker;
    std::atomic<bool> stopRequested{false};
    bool workerStarted{false};
    bool syncInProgress{false};
    int64_t lastSyncMs{0};
    int64_t lastSuccessMs{0};
    std::string lastHead;
    std::string lastIndexedHead;
    std::string lastError;
    std::unordered_map<std::string, CveRecord> recordsById;
    std::unordered_map<std::string, std::vector<std::string>> tokenIndex;
    std::unordered_map<std::string, AgentProgress> progressByAgent;
    CoordinationCycleState coordination;

    Impl() {
        rootPath = resolveDefaultRoot();
        repoPath = rootPath / "repo";
        indexPath = rootPath / "tracey_cve_index.json";

        if (const char* raw = std::getenv("NMC_TRACEY_CVE_REPO")) {
            const std::string value = trim(raw);
            if (!value.empty()) {
                repoUrl = value;
            }
        }
        enabled = parseEnvBool("NMC_TRACEY_CVE_ENABLED", true);
        syncIntervalMs = parseEnvInt64("NMC_TRACEY_CVE_SYNC_MS", syncIntervalMs, 60'000, 7LL * 24LL * 60LL * 60LL * 1000LL);
        cycleMs = parseEnvInt64("NMC_TRACEY_ASSESSMENT_CYCLE_MS", cycleMs, 60'000, 8LL * 60LL * 60LL * 1000LL);
        minSliceMs = parseEnvInt64("NMC_TRACEY_ASSESSMENT_MIN_SLICE_MS", minSliceMs, 60'000, 60LL * 60LL * 1000LL);
        defaultSliceCount = static_cast<std::size_t>(parseEnvInt64("NMC_TRACEY_ASSESSMENT_SLICE_COUNT", static_cast<int64_t>(defaultSliceCount), 1, 32));
        maxMatchesPerReport = static_cast<std::size_t>(parseEnvInt64("NMC_TRACEY_CVE_MAX_MATCHES", static_cast<int64_t>(maxMatchesPerReport), 8, 512));

        if (!enabled) {
            return;
        }

        std::error_code ec;
        std::filesystem::create_directories(rootPath, ec);
        loadCachedIndex();
    }

    ~Impl() = default;

    void loadCachedIndex() {
        std::ifstream input(indexPath);
        if (!input.is_open()) {
            return;
        }

        try {
            nlohmann::json root;
            input >> root;
            const CveIndexFile file = root.get<CveIndexFile>();
            lastIndexedHead = file.head;
            recordsById.clear();
            for (const auto& record : file.records) {
                recordsById[record.cveId] = record;
            }
            rebuildTokenIndexLocked();
        } catch (const std::exception& e) {
            lastError = std::string("failed to load cached CVE index: ") + e.what();
        }
    }

    void persistIndexLocked() const {
        CveIndexFile file;
        file.version = 1;
        file.head = lastIndexedHead;
        file.generatedEpochMs = nowEpochMs();
        file.records.reserve(recordsById.size());
        for (const auto& [id, record] : recordsById) {
            (void)id;
            file.records.push_back(record);
        }

        const auto tmpPath = indexPath.string() + ".tmp";
        std::ofstream output(tmpPath, std::ios::trunc);
        if (!output.is_open()) {
            return;
        }
        output << nlohmann::json(file).dump();
        output.close();

        std::error_code ec;
        std::filesystem::rename(tmpPath, indexPath, ec);
        if (ec) {
            std::filesystem::remove(indexPath, ec);
            std::filesystem::rename(tmpPath, indexPath, ec);
        }
    }

    void rebuildTokenIndexLocked() {
        tokenIndex.clear();
        for (const auto& [id, record] : recordsById) {
            auto allTokens = record.tokens;
            for (const auto& affected : record.affected) {
                appendTokens(allTokens, affected.tokens);
            }
            dedupeTokens(allTokens);
            for (const auto& token : allTokens) {
                if (token.size() < 2) {
                    continue;
                }
                tokenIndex[token].push_back(id);
            }
        }
    }

    std::string resolveHead() const {
        std::string output;
        if (runCommand("git -C " + escapeShell(repoPath.string()) + " rev-parse HEAD", &output) != 0) {
            return "";
        }
        return trim(output);
    }

    bool ensureRepoUpToDate(std::string& errorOut) {
        std::error_code ec;
        std::filesystem::create_directories(rootPath, ec);

        auto cloneRepository = [&](const std::string& url) -> bool {
            const std::string command =
                    "git clone --depth 1 --single-branch " + escapeShell(url) + " " + escapeShell(repoPath.string());
            std::string output;
            const int rc = runCommand(command, &output);
            if (rc != 0) {
                errorOut = trim(output);
                return false;
            }
            return true;
        };

        if (!std::filesystem::exists(repoPath / ".git")) {
            std::filesystem::remove_all(repoPath, ec);
            if (!cloneRepository(repoUrl) && !fallbackRepoUrl.empty()) {
                if (!cloneRepository(fallbackRepoUrl)) {
                    if (errorOut.empty()) {
                        errorOut = "git clone failed";
                    }
                    return false;
                }
            }
            return true;
        }

        std::string output;
        const int fetchRc = runCommand(
                "git -C " + escapeShell(repoPath.string()) + " fetch --depth 1 origin main",
                &output
        );
        if (fetchRc != 0) {
            errorOut = trim(output);
            return false;
        }
        const int resetRc = runCommand(
                "git -C " + escapeShell(repoPath.string()) + " reset --hard FETCH_HEAD",
                &output
        );
        if (resetRc != 0) {
            errorOut = trim(output);
            return false;
        }
        return true;
    }

    bool rebuildIndex(std::string& errorOut) {
        std::unordered_map<std::string, CveRecord> nextRecords;
        const auto cvesRoot = repoPath / "cves";
        if (!std::filesystem::exists(cvesRoot)) {
            errorOut = "cves directory missing after repository sync";
            return false;
        }

        std::size_t parsed = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(cvesRoot)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            if (entry.path().extension() != ".json") {
                continue;
            }

            CveRecord record;
            std::string parseError;
            if (!parseCveFile(entry.path(), record, parseError)) {
                continue;
            }
            nextRecords[record.cveId] = std::move(record);
            parsed++;
        }

        if (nextRecords.empty()) {
            errorOut = "no usable CVE records were parsed";
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            recordsById = std::move(nextRecords);
            lastIndexedHead = lastHead;
            rebuildTokenIndexLocked();
            persistIndexLocked();
            lastError.clear();
        }
        (void)parsed;
        return true;
    }

    void syncOnce() {
        if (!enabled) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            syncInProgress = true;
            lastSyncMs = nowEpochMs();
        }

        std::string error;
        if (!ensureRepoUpToDate(error)) {
            std::lock_guard<std::mutex> lock(mutex);
            syncInProgress = false;
            lastError = error.empty() ? "failed to synchronize CVE repository" : error;
            return;
        }

        const std::string head = resolveHead();
        if (head.empty()) {
            std::lock_guard<std::mutex> lock(mutex);
            syncInProgress = false;
            lastError = "failed to resolve synchronized CVE repository head";
            return;
        }

        bool rebuildRequired = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            lastHead = head;
            rebuildRequired = recordsById.empty() || lastIndexedHead != lastHead;
        }

        if (rebuildRequired && !rebuildIndex(error)) {
            std::lock_guard<std::mutex> lock(mutex);
            syncInProgress = false;
            lastError = error.empty() ? "failed to rebuild CVE index" : error;
            return;
        }

        std::lock_guard<std::mutex> lock(mutex);
        syncInProgress = false;
        lastSuccessMs = nowEpochMs();
        lastError.clear();
    }

    void run() {
        syncOnce();
        while (!stopRequested.load()) {
            int64_t slept = 0;
            while (!stopRequested.load() && slept < syncIntervalMs) {
                const int64_t step = std::min<int64_t>(1000, syncIntervalMs - slept);
                std::this_thread::sleep_for(std::chrono::milliseconds(step));
                slept += step;
            }
            if (stopRequested.load()) {
                break;
            }
            syncOnce();
        }
    }

    nlohmann::json mirrorStatusLocked(int64_t nowMs) const {
        return {
                {"assessment_protocol_version", kAssessmentProtocolVersion},
                {"enabled", enabled},
                {"repo_url", repoUrl},
                {"repo_path", repoPath.string()},
                {"index_path", indexPath.string()},
                {"sync_in_progress", syncInProgress},
                {"sync_interval_ms", syncIntervalMs},
                {"assessment_cycle_ms", cycleMs},
                {"assessment_min_slice_ms", minSliceMs},
                {"default_slice_count", static_cast<int64_t>(defaultSliceCount)},
                {"max_matches_per_report", static_cast<int64_t>(maxMatchesPerReport)},
                {"last_sync_ms", lastSyncMs},
                {"last_success_ms", lastSuccessMs},
                {"last_success_age_ms", lastSuccessMs > 0 && nowMs >= lastSuccessMs ? nowMs - lastSuccessMs : 0},
                {"head", lastHead},
                {"indexed_head", lastIndexedHead},
                {"indexed_records", static_cast<int64_t>(recordsById.size())},
                {"last_error", lastError}
        };
    }

    std::size_t stableSlotForAgentLocked(const std::string& agentId) {
        if (coordination.slotCount == 0) {
            coordination.slotCount = 1;
        }
        const auto it = coordination.overflowSlotByAgent.find(agentId);
        if (it != coordination.overflowSlotByAgent.end()) {
            return it->second;
        }
        const std::size_t slot = static_cast<std::size_t>(
                fnv1a64(agentId + "|" + std::to_string(coordination.cycleStartMs)) %
                static_cast<uint64_t>(coordination.slotCount)
        );
        coordination.overflowSlotByAgent[agentId] = slot;
        return slot;
    }

    nlohmann::json buildReportResponseLocked(const std::string& agentId,
                                             const AgentProgress& progress,
                                             const nlohmann::json& plan,
                                             int64_t nowMs,
                                             bool accepted,
                                             const std::string& disposition,
                                             const std::string& reason,
                                             const std::string& requestId,
                                             int64_t protocolVersion,
                                             const nlohmann::json& matches) const {
        return {
                {"accepted", accepted},
                {"disposition", disposition},
                {"reason", reason},
                {"request_id", requestId},
                {"protocol_version", protocolVersion},
                {"generated_epoch_ms", nowMs},
                {"agent_id", agentId},
                {"plan", plan},
                {"progress", progressToJson(progress, nowMs)},
                {"mirror", mirrorStatusLocked(nowMs)},
                {"inventory", inventorySummaryFromProgress(progress)},
                {"summary", findingSummaryFromProgress(progress)},
                {"matches", matches}
        };
    }

    AgentProgress& syncAgentCycleLocked(const std::string& agentId,
                                        const std::vector<std::string>& orderedAgentIds,
                                        int64_t nowMs,
                                        nlohmann::json& planOut) {
        const int64_t cycleStartMs = (nowMs / cycleMs) * cycleMs;
        const int64_t cycleDeadlineMs = cycleStartMs + cycleMs;
        const auto ids = normalizeAgentIds(orderedAgentIds, agentId);
        if (coordination.cycleStartMs != cycleStartMs ||
            coordination.cycleDeadlineMs != cycleDeadlineMs ||
            coordination.frozenAgentIds.empty()) {
            coordination.cycleStartMs = cycleStartMs;
            coordination.cycleDeadlineMs = cycleDeadlineMs;
            coordination.frozenAgentIds = ids;
            coordination.slotCount = std::max<std::size_t>(1, coordination.frozenAgentIds.size());
            coordination.overflowSlotByAgent.clear();
        }

        const auto it = std::find(coordination.frozenAgentIds.begin(), coordination.frozenAgentIds.end(), agentId);
        const bool lateJoiner = it == coordination.frozenAgentIds.end();
        const std::size_t slotCount = std::max<std::size_t>(1, coordination.slotCount);
        const std::size_t slotIndex = lateJoiner
                                      ? stableSlotForAgentLocked(agentId)
                                      : static_cast<std::size_t>(std::distance(coordination.frozenAgentIds.begin(), it));
        const int64_t slotStartMs = cycleStartMs + static_cast<int64_t>((static_cast<long double>(cycleMs) * static_cast<long double>(slotIndex)) / static_cast<long double>(slotCount));
        const int64_t slotEndMs = cycleStartMs + static_cast<int64_t>((static_cast<long double>(cycleMs) * static_cast<long double>(slotIndex + 1)) / static_cast<long double>(slotCount));
        const int64_t slotDurationMs = std::max<int64_t>(1, slotEndMs - slotStartMs);
        const std::size_t effectiveSliceCount = std::max<std::size_t>(
                1,
                std::min<std::size_t>(
                        defaultSliceCount,
                        static_cast<std::size_t>(std::max<int64_t>(1, slotDurationMs / std::max<int64_t>(minSliceMs, 1)))
                )
        );
        const int64_t sliceIntervalMs = std::max<int64_t>(1, slotDurationMs / static_cast<int64_t>(effectiveSliceCount));

        auto& progress = progressByAgent[agentId];
        if (progress.cycleStartMs != cycleStartMs) {
            progress.completedSlices.clear();
            progress.lastMatchCount = 0;
            progress.criticalMatches = 0;
            progress.highMatches = 0;
            progress.mediumMatches = 0;
            progress.lowMatches = 0;
            progress.kevMatches = 0;
            progress.highestCvss = 0.0;
            progress.packagesSeen = 0;
            progress.modulesSeen = 0;
            progress.servicesSeen = 0;
            progress.processesSeen = 0;
            progress.reportsAccepted = 0;
            progress.reportsRejected = 0;
            progress.duplicateReports = 0;
            progress.stalePlanReports = 0;
            progress.semanticFaults = 0;
            progress.protocolVersion = 1;
            progress.planToken.clear();
            progress.lastDisposition.clear();
            progress.lastRequestId.clear();
            progress.lastSuccessMs = 0;
            progress.lastFailureMs = 0;
            progress.lastError.clear();
        }

        progress.lastPlanMs = nowMs;
        progress.cycleStartMs = cycleStartMs;
        progress.cycleDeadlineMs = cycleDeadlineMs;
        progress.slotStartMs = slotStartMs;
        progress.slotEndMs = slotEndMs;
        progress.slotIndex = slotIndex;
        progress.slotCount = slotCount;
        progress.sliceCount = effectiveSliceCount;
        progress.planToken = makePlanToken(agentId, cycleStartMs, slotIndex, slotCount, effectiveSliceCount, slotStartMs, slotEndMs);

        nlohmann::json slices = nlohmann::json::array();
        for (std::size_t index = 0; index < effectiveSliceCount; ++index) {
            const int64_t sliceStartMs = slotStartMs + static_cast<int64_t>(index) * sliceIntervalMs;
            const int64_t sliceEndMs = (index + 1 == effectiveSliceCount)
                                       ? slotEndMs
                                       : std::min(slotEndMs, sliceStartMs + sliceIntervalMs);
            slices.push_back({
                    {"slice_index", static_cast<int64_t>(index)},
                    {"start_ms", sliceStartMs},
                    {"end_ms", sliceEndMs},
                    {"completed", progress.completedSlices.find(static_cast<int>(index)) != progress.completedSlices.end()}
            });
        }

        planOut = {
                {"protocol_version", kAssessmentProtocolVersion},
                {"generated_epoch_ms", nowMs},
                {"agent_id", agentId},
                {"plan_token", progress.planToken},
                {"coordination_mode", "frozen_roster_hash_overflow"},
                {"coordination_epoch_ms", coordination.cycleStartMs},
                {"late_joiner_assignment", lateJoiner},
                {"cycle_start_ms", cycleStartMs},
                {"cycle_deadline_ms", cycleDeadlineMs},
                {"cycle_duration_ms", cycleMs},
                {"slot_index", static_cast<int64_t>(slotIndex)},
                {"slot_count", static_cast<int64_t>(slotCount)},
                {"slot_start_ms", slotStartMs},
                {"slot_end_ms", slotEndMs},
                {"slot_duration_ms", slotDurationMs},
                {"slice_count", static_cast<int64_t>(effectiveSliceCount)},
                {"slice_interval_ms", sliceIntervalMs},
                {"completed_slice_count", static_cast<int64_t>(progress.completedSlices.size())},
                {"slices", slices},
                {"mirror", mirrorStatusLocked(nowMs)}
        };

        return progress;
    }

    nlohmann::json buildPlan(const std::string& agentId,
                             const std::vector<std::string>& orderedAgentIds,
                             int64_t nowMs) {
        std::lock_guard<std::mutex> lock(mutex);
        nlohmann::json plan = nlohmann::json::object();
        syncAgentCycleLocked(agentId, orderedAgentIds, nowMs, plan);
        return plan;
    }

    nlohmann::json ingestReport(const std::string& agentId,
                                const nlohmann::json& report,
                                const std::vector<std::string>& orderedAgentIds,
                                int64_t nowMs) {
        const int64_t protocolVersion = std::max<int64_t>(1, firstInt64Value(report, {"protocol_version", "protocolVersion"}, 1));
        const std::string requestId = firstStringValue(report, {"request_id", "requestId"});
        const int64_t reportedCycleStartMs = firstInt64Value(report, {"cycle_start_ms", "cycleStartMs"}, 0);
        const int64_t reportedSlotIndex = firstInt64Value(report, {"slot_index", "slotIndex"}, -1);
        const int64_t reportedSlotCount = firstInt64Value(report, {"slot_count", "slotCount"}, 0);
        const int64_t reportedSliceCount = firstInt64Value(report, {"slice_count", "sliceCount"}, 0);
        const int sliceIndex = static_cast<int>(firstInt64Value(report, {"slice_index", "sliceIndex"}, -1));
        const std::string reportedPlanToken = firstStringValue(report, {"plan_token", "planToken"});
        const auto inventoryIt = report.find("inventory");
        const bool hasInventory = inventoryIt != report.end();

        nlohmann::json plan;
        {
            std::lock_guard<std::mutex> lock(mutex);
            AgentProgress& progress = syncAgentCycleLocked(agentId, orderedAgentIds, nowMs, plan);
            progress.lastReportMs = nowMs;
            progress.protocolVersion = protocolVersion;
            progress.lastRequestId = requestId;

            auto reject = [&](const std::string& disposition, const std::string& reason, bool duplicate = false) {
                if (duplicate) {
                    progress.duplicateReports++;
                    progress.lastSuccessMs = nowMs;
                    progress.lastDisposition = disposition;
                    progress.lastError.clear();
                    return buildReportResponseLocked(
                            agentId,
                            progress,
                            plan,
                            nowMs,
                            true,
                            disposition,
                            reason,
                            requestId,
                            protocolVersion,
                            nlohmann::json::array()
                    );
                }
                progress.reportsRejected++;
                progress.semanticFaults++;
                if (disposition == "stale_plan") {
                    progress.stalePlanReports++;
                }
                progress.lastFailureMs = nowMs;
                progress.lastDisposition = disposition;
                progress.lastError = reason;
                return buildReportResponseLocked(
                        agentId,
                        progress,
                        plan,
                        nowMs,
                        false,
                        disposition,
                        reason,
                        requestId,
                        protocolVersion,
                        nlohmann::json::array()
                );
            };

            if (sliceIndex < 0 || sliceIndex >= static_cast<int>(progress.sliceCount)) {
                return reject("rejected", "slice_index is missing or outside the current assessment plan.");
            }
            if (protocolVersion >= kAssessmentProtocolVersion && requestId.empty()) {
                return reject("rejected", "request_id is required for the current assessment protocol.");
            }
            if (protocolVersion >= kAssessmentProtocolVersion && !hasInventory) {
                return reject("rejected", "inventory object is required for the current assessment protocol.");
            }
            if (hasInventory && !inventoryIt->is_object()) {
                return reject("rejected", "inventory must be a JSON object.");
            }
            if (protocolVersion >= kAssessmentProtocolVersion && reportedPlanToken.empty()) {
                return reject("rejected", "plan_token is required for the current assessment protocol.");
            }
            if (!reportedPlanToken.empty() && reportedPlanToken != progress.planToken) {
                return reject("stale_plan", "assessment plan token no longer matches Continuum's current coordination state.");
            }
            if (reportedCycleStartMs > 0 && reportedCycleStartMs != progress.cycleStartMs) {
                return reject("stale_plan", "assessment cycle_start_ms no longer matches Continuum's current coordination state.");
            }
            if (reportedSlotIndex >= 0 && static_cast<std::size_t>(reportedSlotIndex) != progress.slotIndex) {
                return reject("stale_plan", "assessment slot_index no longer matches Continuum's current coordination state.");
            }
            if (reportedSlotCount > 0 && static_cast<std::size_t>(reportedSlotCount) != progress.slotCount) {
                return reject("stale_plan", "assessment slot_count no longer matches Continuum's current coordination state.");
            }
            if (reportedSliceCount > 0 && static_cast<std::size_t>(reportedSliceCount) != progress.sliceCount) {
                return reject("stale_plan", "assessment slice_count no longer matches Continuum's current coordination state.");
            }
            if (progress.completedSlices.find(sliceIndex) != progress.completedSlices.end()) {
                return reject("duplicate", "assessment slice was already accepted for the current cycle.", true);
            }
            progress.inventoryDigest = firstStringValue(report, {"inventory_digest", "inventoryDigest"});
        }

        std::size_t packagesSeen = 0;
        std::size_t modulesSeen = 0;
        std::size_t servicesSeen = 0;
        std::size_t processesSeen = 0;
        const auto items = collectInventoryItems(report, packagesSeen, modulesSeen, servicesSeen, processesSeen);

        std::vector<CveRecord> candidateRecords;

        {
            std::lock_guard<std::mutex> lock(mutex);
            AgentProgress& progress = syncAgentCycleLocked(agentId, orderedAgentIds, nowMs, plan);
            std::unordered_set<std::string> candidateIds;
            for (const auto& item : items) {
                for (const auto& token : item.tokens) {
                    const auto tokenIt = tokenIndex.find(token);
                    if (tokenIt == tokenIndex.end()) {
                        continue;
                    }
                    candidateIds.insert(tokenIt->second.begin(), tokenIt->second.end());
                }
            }
            candidateRecords.reserve(candidateIds.size());
            for (const auto& cveId : candidateIds) {
                const auto recordIt = recordsById.find(cveId);
                if (recordIt != recordsById.end()) {
                    candidateRecords.push_back(recordIt->second);
                }
            }
        }

        struct MatchCandidate {
            std::string cveId;
            std::string severity;
            double cvss{0.0};
            bool kev{false};
            double matchScore{0.0};
            double confidence{0.0};
            std::string reason;
            std::string description;
            std::string updated;
            std::string product;
            std::string vendor;
            std::string itemName;
            std::string itemType;
            std::string installedVersion;
            std::vector<std::string> matchedAssets;
        };

        std::unordered_map<std::string, MatchCandidate> bestByCve;
        for (const auto& item : items) {
            for (const auto& record : candidateRecords) {
                double bestScore = 0.0;
                double bestConfidence = 0.0;
                std::string bestReason;
                std::string bestProduct;
                std::string bestVendor;

                for (const auto& target : record.affected) {
                    const double productScore = std::max({
                            nameSimilarity(item.name, target.product),
                            nameSimilarity(item.name, target.packageName),
                            nameSimilarity(item.name, target.packageUrl),
                            tokenOverlapScore(item.tokens, target.tokens)
                    });
                    if (productScore <= 0.0) {
                        continue;
                    }

                    const double vendorScore = target.vendor.empty()
                                               ? 0.60
                                               : std::max(0.20, nameSimilarity(item.name, target.vendor));
                    std::string versionReason;
                    const double versionScore = versionMatchScore(item.version, target, versionReason);
                    double baseScore = productScore * 0.62 + versionScore * 0.28 + vendorScore * 0.10;
                    if (item.type == "process" || item.type == "service") {
                        baseScore *= 0.92;
                    }
                    if (item.type == "kernel" && productScore < 0.70) {
                        baseScore *= 0.75;
                    }

                    const double confidence = clamp01(productScore * 0.55 + versionScore * 0.30 + vendorScore * 0.15);
                    if (baseScore > bestScore) {
                        bestScore = baseScore;
                        bestConfidence = confidence;
                        bestReason = summarizeMatchReason(item.name, item.type, target.product, target.vendor, versionReason);
                        bestProduct = target.product.empty() ? target.packageName : target.product;
                        bestVendor = target.vendor;
                    }
                }

                const double threshold = record.kev ? 0.55 : 0.65;
                if (bestScore < threshold) {
                    continue;
                }

                auto& match = bestByCve[record.cveId];
                const double ranking = bestScore * (0.55 + severityWeight(record.severity) * 0.45) * (record.kev ? 1.15 : 1.0);
                const double existingRanking = match.matchScore * (0.55 + severityWeight(match.severity) * 0.45) * (match.kev ? 1.15 : 1.0);
                if (match.cveId.empty() || ranking > existingRanking) {
                    match.cveId = record.cveId;
                    match.severity = record.severity;
                    match.cvss = record.cvss;
                    match.kev = record.kev;
                    match.matchScore = clamp01(bestScore);
                    match.confidence = clamp01(bestConfidence);
                    match.reason = bestReason;
                    match.description = record.description;
                    match.updated = record.updated;
                    match.product = bestProduct;
                    match.vendor = bestVendor;
                    match.itemName = item.name;
                    match.itemType = item.type;
                    match.installedVersion = item.version;
                    match.matchedAssets.clear();
                }
                if (std::find(match.matchedAssets.begin(), match.matchedAssets.end(), item.name) == match.matchedAssets.end()) {
                    match.matchedAssets.push_back(item.name);
                }
            }
        }

        std::vector<MatchCandidate> matches;
        matches.reserve(bestByCve.size());
        for (auto& [id, match] : bestByCve) {
            (void)id;
            matches.push_back(std::move(match));
        }

        std::sort(matches.begin(), matches.end(), [](const MatchCandidate& left, const MatchCandidate& right) {
            const double leftRank = left.matchScore * (0.55 + severityWeight(left.severity) * 0.45) * (left.kev ? 1.15 : 1.0);
            const double rightRank = right.matchScore * (0.55 + severityWeight(right.severity) * 0.45) * (right.kev ? 1.15 : 1.0);
            if (leftRank == rightRank) {
                return left.cveId < right.cveId;
            }
            return leftRank > rightRank;
        });
        if (matches.size() > maxMatchesPerReport) {
            matches.resize(maxMatchesPerReport);
        }

        std::size_t critical = 0;
        std::size_t high = 0;
        std::size_t medium = 0;
        std::size_t low = 0;
        std::size_t kev = 0;
        double highestCvss = 0.0;
        nlohmann::json matchRows = nlohmann::json::array();
        for (const auto& match : matches) {
            if (match.severity == "critical") {
                critical++;
            } else if (match.severity == "high") {
                high++;
            } else if (match.severity == "medium") {
                medium++;
            } else {
                low++;
            }
            if (match.kev) {
                kev++;
            }
            highestCvss = std::max(highestCvss, match.cvss);
            matchRows.push_back({
                    {"cve_id", match.cveId},
                    {"severity", match.severity},
                    {"cvss", match.cvss},
                    {"kev", match.kev},
                    {"match_score", match.matchScore},
                    {"confidence", match.confidence},
                    {"product", match.product},
                    {"vendor", match.vendor},
                    {"inventory_item", match.itemName},
                    {"inventory_type", match.itemType},
                    {"installed_version", match.installedVersion},
                    {"matched_assets", match.matchedAssets},
                    {"reason", match.reason},
                    {"description", match.description},
                    {"updated", match.updated}
            });
        }

        nlohmann::json progressJson;
        nlohmann::json mirrorJson;
        {
            std::lock_guard<std::mutex> lock(mutex);
            AgentProgress& progress = progressByAgent[agentId];
            progress.packagesSeen = packagesSeen;
            progress.modulesSeen = modulesSeen;
            progress.servicesSeen = servicesSeen;
            progress.processesSeen = processesSeen;
            progress.lastMatchCount = matches.size();
            progress.criticalMatches = critical;
            progress.highMatches = high;
            progress.mediumMatches = medium;
            progress.lowMatches = low;
            progress.kevMatches = kev;
            progress.highestCvss = highestCvss;
            if (sliceIndex >= 0) {
                progress.completedSlices.insert(sliceIndex);
            }
            progress.reportsAccepted++;
            progress.lastSuccessMs = nowMs;
            progress.lastDisposition = "applied";
            progress.lastError.clear();
            progressJson = progressToJson(progress, nowMs);
            mirrorJson = mirrorStatusLocked(nowMs);
        }

        return {
                {"accepted", true},
                {"disposition", "applied"},
                {"reason", ""},
                {"request_id", requestId},
                {"protocol_version", protocolVersion},
                {"generated_epoch_ms", nowMs},
                {"agent_id", agentId},
                {"plan", plan},
                {"progress", progressJson},
                {"mirror", mirrorJson},
                {"inventory", {
                        {"items", static_cast<int64_t>(items.size())},
                        {"packages", static_cast<int64_t>(packagesSeen)},
                        {"modules", static_cast<int64_t>(modulesSeen)},
                        {"services", static_cast<int64_t>(servicesSeen)},
                        {"processes", static_cast<int64_t>(processesSeen)}
                }},
                {"summary", {
                        {"matches", static_cast<int64_t>(matches.size())},
                        {"critical", static_cast<int64_t>(critical)},
                        {"high", static_cast<int64_t>(high)},
                        {"medium", static_cast<int64_t>(medium)},
                        {"low", static_cast<int64_t>(low)},
                        {"kev", static_cast<int64_t>(kev)},
                        {"highest_cvss", highestCvss}
                }},
                {"matches", matchRows}
        };
    }

    nlohmann::json agentProgressJson(const std::string& agentId, int64_t nowMs) const {
        std::lock_guard<std::mutex> lock(mutex);
        const auto it = progressByAgent.find(agentId);
        if (it == progressByAgent.end()) {
            return nlohmann::json::object();
        }
        return progressToJson(it->second, nowMs);
    }
};

TraceyCVEIntel::TraceyCVEIntel()
        : impl_(std::make_unique<Impl>()) {
}

TraceyCVEIntel::~TraceyCVEIntel() {
    stop();
}

void TraceyCVEIntel::start() {
    if (!impl_ || !impl_->enabled || impl_->workerStarted) {
        return;
    }
    impl_->stopRequested.store(false);
    impl_->workerStarted = true;
    impl_->worker = std::thread([this]() {
        impl_->run();
    });
}

void TraceyCVEIntel::stop() {
    if (!impl_) {
        return;
    }
    impl_->stopRequested.store(true);
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
    impl_->workerStarted = false;
}

nlohmann::json TraceyCVEIntel::mirrorStatus() const {
    if (!impl_) {
        return nlohmann::json::object();
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->mirrorStatusLocked(nowEpochMs());
}

nlohmann::json TraceyCVEIntel::buildAgentPlan(const std::string& agentId,
                                              const std::vector<std::string>& orderedAgentIds,
                                              int64_t nowMs) {
    if (!impl_) {
        return nlohmann::json::object();
    }
    return impl_->buildPlan(agentId, orderedAgentIds, nowMs);
}

nlohmann::json TraceyCVEIntel::ingestAssessmentReport(const std::string& agentId,
                                                      const nlohmann::json& report,
                                                      const std::vector<std::string>& orderedAgentIds,
                                                      int64_t nowMs) {
    if (!impl_) {
        return nlohmann::json::object();
    }
    return impl_->ingestReport(agentId, report, orderedAgentIds, nowMs);
}

nlohmann::json TraceyCVEIntel::agentProgress(const std::string& agentId) const {
    if (!impl_) {
        return nlohmann::json::object();
    }
    return impl_->agentProgressJson(agentId, nowEpochMs());
}

} // namespace NMC::Server
