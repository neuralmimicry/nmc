#include "NodeCommands.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include <nlohmann/json.hpp>

namespace NMC::Commands {

namespace {

struct ExecResult {
    int exitCode{1};
    std::string output;
};

std::string trimCopy(const std::string& value) {
    const auto start = value.find_first_not_of(" \t");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t");
    return value.substr(start, end - start + 1);
}

std::string trimLineEndCopy(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

std::string toLowerCopy(std::string value) {
    for (auto& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool hasControlChars(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](const unsigned char ch) {
        return std::iscntrl(ch) != 0;
    });
}

bool isSafeHost(const std::string& host) {
    if (host.empty() || host.size() > 253 || host.front() == '-' || host.back() == '-') {
        return false;
    }
    return std::all_of(host.begin(), host.end(), [](const unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '.' || ch == '-';
    });
}

bool isSafeUser(const std::string& user) {
    if (user.empty() || user.size() > 64) {
        return false;
    }
    const unsigned char first = static_cast<unsigned char>(user.front());
    if (std::isalpha(first) == 0 && first != '_') {
        return false;
    }
    return std::all_of(user.begin() + 1, user.end(), [](const unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '_' || ch == '-' || ch == '.';
    });
}

bool isSafeLocalPath(const std::string& path) {
    if (path.empty() || path.size() > 4096 || hasControlChars(path)) {
        return false;
    }
    return path.front() == '/';
}

bool isSafeRemotePath(const std::string& path) {
    if (path.empty() || path.size() > 4096 || path.front() != '/' || hasControlChars(path)) {
        return false;
    }
    return std::all_of(path.begin(), path.end(), [](const unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '/' || ch == '-' || ch == '_' || ch == '.';
    });
}

bool isKnownNodeType(const std::string& nodeType) {
    static const std::vector<std::string> allowed = {
        "bare-metal",
        "app-install",
        "vm",
        "virtual-machine",
        "podman",
        "kubernetes"
    };
    return std::find(allowed.begin(), allowed.end(), nodeType) != allowed.end();
}

std::string normalizeNodeType(const std::string& nodeType) {
    std::string normalized = toLowerCopy(trimCopy(nodeType));
    if (normalized.empty()) {
        normalized = "bare-metal";
    }
    if (normalized == "virtual-machine") {
        normalized = "vm";
    }
    return normalized;
}

std::string shellQuote(const std::string& value) {
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
}

std::string commandToShellString(const std::vector<std::string>& args) {
    std::ostringstream command;
    bool first = true;
    for (const auto& arg : args) {
        if (!first) {
            command << ' ';
        }
        command << shellQuote(arg);
        first = false;
    }
    return command.str();
}

ExecResult runShellCommand(const std::vector<std::string>& args, size_t outputLimitBytes = 262144) {
    ExecResult result;
    if (args.empty()) {
        result.output = "No command specified.";
        return result;
    }

    const std::string command = commandToShellString(args) + " 2>&1";
    FILE* pipe = ::popen(command.c_str(), "r");
    if (!pipe) {
        result.output = "Unable to execute command.";
        return result;
    }

    std::array<char, 4096> buffer{};
    bool outputTruncated = false;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        if (result.output.size() < outputLimitBytes) {
            const size_t remaining = outputLimitBytes - result.output.size();
            const std::string chunk(buffer.data());
            result.output.append(chunk, 0, std::min(chunk.size(), remaining));
            if (chunk.size() > remaining) {
                outputTruncated = true;
            }
        }
    }

    const int status = ::pclose(pipe);
    if (status != -1 && WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    }

    if (outputTruncated) {
        result.output += "\n...(output truncated)\n";
    }
    return result;
}

bool readTextFile(const std::string& path, size_t maxBytes, std::string& contentOut, std::string& errorOut) {
    contentOut.clear();
    errorOut.clear();

    try {
        const std::filesystem::path fsPath(path);
        if (!std::filesystem::exists(fsPath) || !std::filesystem::is_regular_file(fsPath)) {
            errorOut = "File does not exist: " + path;
            return false;
        }
        const uintmax_t size = std::filesystem::file_size(fsPath);
        if (size > maxBytes) {
            errorOut = "File exceeds size limit (" + std::to_string(maxBytes) + " bytes): " + path;
            return false;
        }
    } catch (const std::exception& e) {
        errorOut = "Unable to inspect file '" + path + "': " + std::string(e.what());
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        errorOut = "Unable to open file: " + path;
        return false;
    }

    std::ostringstream oss;
    oss << input.rdbuf();
    contentOut = oss.str();
    return true;
}

bool writeTempFile(const std::string& content, std::string& pathOut, std::string& errorOut) {
    pathOut.clear();
    errorOut.clear();

    char tmpName[] = "/tmp/nmc-recruit-XXXXXX";
    const int fd = ::mkstemp(tmpName);
    if (fd < 0) {
        errorOut = "Failed to create temporary file.";
        return false;
    }
    ::close(fd);

    try {
        std::ofstream output(tmpName, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            errorOut = "Failed to open temporary file for writing.";
            ::unlink(tmpName);
            return false;
        }
        output << content;
        output.close();
        std::filesystem::permissions(
            tmpName,
            std::filesystem::perms::owner_read |
            std::filesystem::perms::owner_write |
            std::filesystem::perms::owner_exec,
            std::filesystem::perm_options::replace
        );
        pathOut = tmpName;
        return true;
    } catch (const std::exception& e) {
        errorOut = "Failed to write temporary file: " + std::string(e.what());
        ::unlink(tmpName);
        return false;
    }
}

std::string buildDefaultRecruitScript() {
    return R"(#!/usr/bin/env bash
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
if command -v apt-get >/dev/null 2>&1; then
  apt-get update -y
  apt-get install -y curl ca-certificates jq
fi

mkdir -p /opt/continuum/node
cat >/opt/continuum/node/recruitment.env <<EOF
NMC_NODE_TYPE=${NMC_NODE_TYPE:-bare-metal}
NMC_NODE_REGION=${NMC_NODE_REGION:-}
NMC_NODE_NAME=${NMC_NODE_NAME:-}
NMC_CONTINUUM_URL=${NMC_CONTINUUM_URL:-}
NMC_CONTINUUM_AUTH_TOKEN=${NMC_CONTINUUM_AUTH_TOKEN:-}
TRACEY_AGENT_ID=${TRACEY_AGENT_ID:-}
TRACEY_STATUS_ADDR=${TRACEY_STATUS_ADDR:-}
RECRUITED_AT_UTC=$(date -u +%FT%TZ)
EOF

case "${NMC_NODE_TYPE:-bare-metal}" in
  podman)
    if command -v apt-get >/dev/null 2>&1; then
      apt-get install -y podman
    fi
    ;;
  kubernetes)
    if command -v apt-get >/dev/null 2>&1; then
      apt-get install -y apt-transport-https containerd
    fi
    ;;
  vm|app-install|bare-metal)
    true
    ;;
esac

echo "ready" >/opt/continuum/node/state
echo "Continuum recruitment completed for ${NMC_NODE_NAME:-unknown-node} (${NMC_NODE_TYPE:-bare-metal})."
)";
}

std::string optionalFlag(const std::map<std::string, std::string>& flags,
                         const std::string& name,
                         const std::string& fallback = "") {
    auto it = flags.find(name);
    if (it == flags.end() || it->second.empty()) {
        return fallback;
    }
    return it->second;
}

bool hasFlag(const std::map<std::string, std::string>& flags, const std::string& name) {
    auto it = flags.find(name);
    return it != flags.end() && it->second == "1";
}

bool parseIntInRange(const std::string& raw,
                     int fallback,
                     int minValue,
                     int maxValue,
                     int& out,
                     std::string& errorOut) {
    if (raw.empty()) {
        out = fallback;
        return true;
    }

    try {
        const int value = std::stoi(raw);
        if (value < minValue || value > maxValue) {
            errorOut = "value out of range";
            return false;
        }
        out = value;
        return true;
    } catch (const std::exception&) {
        errorOut = "expected an integer";
        return false;
    }
}

std::vector<std::string> splitCommaSeparated(const std::string& raw) {
    std::vector<std::string> values;
    if (raw.empty()) {
        return values;
    }

    std::stringstream ss(raw);
    std::string item;
    while (std::getline(ss, item, ',')) {
        const std::string trimmed = trimCopy(item);
        if (!trimmed.empty()) {
            values.push_back(trimmed);
        }
    }
    return values;
}

std::string normalizeCapability(std::string capability) {
    capability = toLowerCopy(trimCopy(capability));
    if (capability == "app" || capability == "app-install") {
        return "apps";
    }
    if (capability == "k8s") {
        return "kubernetes";
    }
    if (capability == "virtual-machine") {
        return "vm";
    }
    return capability;
}

bool isKnownCapability(const std::string& capability) {
    static const std::set<std::string> allowed = {
        "apps",
        "vm",
        "podman",
        "kubernetes"
    };
    return allowed.find(capability) != allowed.end();
}

std::vector<std::string> defaultCapabilitiesForNodeType(const std::string& nodeType) {
    if (nodeType == "kubernetes") {
        return {"kubernetes"};
    }
    if (nodeType == "podman") {
        return {"podman"};
    }
    if (nodeType == "vm") {
        return {"vm"};
    }
    if (nodeType == "app-install") {
        return {"apps"};
    }
    return {};
}

bool isSafeTenantId(const std::string& tenantId) {
    if (tenantId.empty()) {
        return true;
    }
    if (tenantId.size() > 128) {
        return false;
    }
    return std::all_of(tenantId.begin(), tenantId.end(), [](const unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '_' || ch == '-' || ch == '.';
    });
}

bool isSafeAnsibleVarKey(const std::string& key) {
    if (key.empty() || key.size() > 128) {
        return false;
    }
    const unsigned char first = static_cast<unsigned char>(key.front());
    if (std::isalpha(first) == 0 && first != '_') {
        return false;
    }
    return std::all_of(key.begin() + 1, key.end(), [](const unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '_';
    });
}

std::string resolveRecruitPlaybookPath(const std::string& requestedPath) {
    std::vector<std::filesystem::path> candidates;
    if (!requestedPath.empty()) {
        candidates.emplace_back(requestedPath);
    } else {
        const char* fromEnv = std::getenv("NMC_RECRUIT_ANSIBLE_PLAYBOOK");
        if (fromEnv && *fromEnv) {
            candidates.emplace_back(std::string(fromEnv));
        }
    }

    if (candidates.empty()) {
        try {
            std::filesystem::path cursor = std::filesystem::current_path();
            for (int depth = 0; depth < 7; ++depth) {
                candidates.push_back(cursor / "ansible" / "recruited-node.yml");
                if (!cursor.has_parent_path()) {
                    break;
                }
                cursor = cursor.parent_path();
            }
        } catch (const std::exception&) {
            return "";
        }
    }

    for (const auto& candidate : candidates) {
        try {
            std::filesystem::path absolute = candidate;
            if (absolute.is_relative()) {
                absolute = std::filesystem::current_path() / absolute;
            }
            absolute = std::filesystem::weakly_canonical(absolute);
            if (std::filesystem::exists(absolute) && std::filesystem::is_regular_file(absolute)) {
                return absolute.string();
            }
        } catch (const std::exception&) {
            continue;
        }
    }
    return "";
}

Models::CloudResponse runDirectRecruitment(const nlohmann::json& payload) {
    Models::CloudResponse response;
    response.success = false;
    response.message = "Direct recruitment failed.";
    response.data = nlohmann::json::object();

    try {
        const std::string host = trimCopy(payload.value("host", ""));
        const std::string user = trimCopy(payload.value("user", "ubuntu"));
        const int port = payload.value("port", 22);
        const bool dryRun = payload.value("dry_run", false);
        const bool useSudo = payload.value("sudo", true);
        const std::string nodeType = normalizeNodeType(payload.value("node_type", "bare-metal"));
        const std::string nodeName = trimCopy(payload.value("name", host));
        const std::string region = trimCopy(payload.value("region", ""));
        const std::string sshKeyPath = trimCopy(payload.value("ssh_key_path", ""));
        std::string scriptContent = payload.value("script", "");
        const std::string binaryPath = trimCopy(payload.value("binary_path", ""));
        const std::string rawRemotePath = trimCopy(payload.value("remote_path", ""));
        const std::string continuumUrl = trimCopy(payload.value("continuum_url", ""));
        const std::string continuumAuthToken = trimCopy(payload.value("continuum_auth_token", ""));
        const std::string sudoPassword = trimLineEndCopy(payload.value(
            "sudo_password",
            payload.value(
                "sudoPassword",
                payload.value("become_password", payload.value("becomePassword", ""))
            )
        ));
        const bool autoConfigure = payload.value("auto_configure", false);
        const bool ansibleBecome = payload.value("ansible_become", payload.value("ansibleBecome", payload.value("become", true)));
        const std::string tenantId = trimCopy(payload.value("tenant_id", ""));
        const std::string tenantName = trimCopy(payload.value("tenant_name", ""));
        const std::string tenantEnvironment = trimCopy(payload.value("tenant_environment", ""));
        const std::string ansiblePlaybookRequested = trimCopy(payload.value("ansible_playbook", ""));

        nlohmann::json ansibleExtraVars = nlohmann::json::object();
        if (payload.contains("ansible_extra_vars")) {
            ansibleExtraVars = payload["ansible_extra_vars"];
        }

        std::string traceyAgentId;
        std::string traceyStatusAddr;
        if (payload.contains("tracey") && payload["tracey"].is_object()) {
            traceyAgentId = trimCopy(payload["tracey"].value("agent_id", ""));
            traceyStatusAddr = trimCopy(payload["tracey"].value("status_addr", ""));
        }

        std::vector<std::string> binaryArgs;
        if (payload.contains("binary_args") && payload["binary_args"].is_array()) {
            for (const auto& value : payload["binary_args"]) {
                if (value.is_string()) {
                    binaryArgs.push_back(value.get<std::string>());
                }
            }
        }

        std::vector<std::string> capabilities;
        if (payload.contains("capabilities")) {
            const auto& capValue = payload["capabilities"];
            if (capValue.is_array()) {
                for (const auto& item : capValue) {
                    if (item.is_string()) {
                        const std::string normalized = normalizeCapability(item.get<std::string>());
                        if (!normalized.empty() && isKnownCapability(normalized) &&
                            std::find(capabilities.begin(), capabilities.end(), normalized) == capabilities.end()) {
                            capabilities.push_back(normalized);
                        }
                    }
                }
            } else if (capValue.is_string()) {
                for (const auto& raw : splitCommaSeparated(capValue.get<std::string>())) {
                    const std::string normalized = normalizeCapability(raw);
                    if (!normalized.empty() && isKnownCapability(normalized) &&
                        std::find(capabilities.begin(), capabilities.end(), normalized) == capabilities.end()) {
                        capabilities.push_back(normalized);
                    }
                }
            }
        }
        if (capabilities.empty()) {
            capabilities = defaultCapabilitiesForNodeType(nodeType);
        }

        if (host.empty() || !isSafeHost(host)) {
            response.message = "Invalid host. Use a hostname or IPv4 address.";
            return response;
        }
        if (!isSafeUser(user)) {
            response.message = "Invalid SSH user.";
            return response;
        }
        if (port <= 0 || port > 65535) {
            response.message = "Invalid SSH port. Expected 1-65535.";
            return response;
        }
        if (!isKnownNodeType(nodeType)) {
            response.message = "Invalid node type.";
            return response;
        }
        if (hasControlChars(nodeName) || hasControlChars(region) || hasControlChars(continuumUrl)) {
            response.message = "Invalid control characters in request values.";
            return response;
        }
        if (!isSafeTenantId(tenantId) || hasControlChars(tenantName) || hasControlChars(tenantEnvironment) ||
            tenantName.size() > 128 || tenantEnvironment.size() > 64) {
            response.message = "Invalid tenant metadata.";
            return response;
        }
        if (hasControlChars(ansiblePlaybookRequested)) {
            response.message = "ansible_playbook contains invalid control characters.";
            return response;
        }
        if (sudoPassword.size() > 1024 || hasControlChars(sudoPassword)) {
            response.message = "Invalid sudo/become password value.";
            return response;
        }
        if (!ansibleExtraVars.is_object()) {
            response.message = "ansible_extra_vars must be a JSON object.";
            return response;
        }
        for (const auto& item : ansibleExtraVars.items()) {
            if (!isSafeAnsibleVarKey(item.key())) {
                response.message = "Invalid ansible_extra_vars key: " + item.key();
                return response;
            }
            if (item.value().is_string() && hasControlChars(item.value().get<std::string>())) {
                response.message = "ansible_extra_vars values cannot contain control characters.";
                return response;
            }
        }

        const bool useBinaryArtifact = !binaryPath.empty();
        if (useBinaryArtifact && !scriptContent.empty()) {
            response.message = "Provide either script content or binary path for direct mode, not both.";
            return response;
        }

        if (!binaryPath.empty()) {
            if (!isSafeLocalPath(binaryPath)) {
                response.message = "Invalid binary path. Must be an absolute filesystem path.";
                return response;
            }
            try {
                const std::filesystem::path fsPath(binaryPath);
                if (!std::filesystem::exists(fsPath) || !std::filesystem::is_regular_file(fsPath)) {
                    response.message = "Binary path does not exist or is not a file.";
                    return response;
                }
                if (std::filesystem::file_size(fsPath) > (100ULL * 1024ULL * 1024ULL)) {
                    response.message = "Binary exceeds 100 MiB size limit.";
                    return response;
                }
            } catch (const std::exception& e) {
                response.message = "Unable to inspect binary path: " + std::string(e.what());
                return response;
            }
        }

        if (!sshKeyPath.empty()) {
            if (!isSafeLocalPath(sshKeyPath)) {
                response.message = "Invalid SSH key path. Must be an absolute filesystem path.";
                return response;
            }
            try {
                const std::filesystem::path fsPath(sshKeyPath);
                if (!std::filesystem::exists(fsPath) || !std::filesystem::is_regular_file(fsPath)) {
                    response.message = "SSH key path does not exist or is not a file.";
                    return response;
                }
            } catch (const std::exception& e) {
                response.message = "Unable to inspect SSH key path: " + std::string(e.what());
                return response;
            }
        }

        if (!useBinaryArtifact && scriptContent.empty()) {
            scriptContent = buildDefaultRecruitScript();
        }

        struct CleanupGuard {
            std::vector<std::string> paths;
            ~CleanupGuard() {
                for (const auto& path : paths) {
                    if (!path.empty()) {
                        ::unlink(path.c_str());
                    }
                }
            }
        } cleanup;

        std::string localArtifactPath = binaryPath;
        if (!useBinaryArtifact) {
            std::string tempPath;
            std::string tempError;
            if (!writeTempFile(scriptContent, tempPath, tempError)) {
                response.message = tempError;
                return response;
            }
            localArtifactPath = tempPath;
            cleanup.paths.push_back(tempPath);
        }

        const std::string remotePath = rawRemotePath.empty()
            ? (useBinaryArtifact ? "/tmp/nmc-recruit.bin" : "/tmp/nmc-recruit.sh")
            : rawRemotePath;
        if (!isSafeRemotePath(remotePath)) {
            response.message = "Invalid remote path. Must be absolute and use safe characters.";
            return response;
        }

        for (const auto& arg : binaryArgs) {
            if (hasControlChars(arg)) {
                response.message = "Binary arguments cannot contain control characters.";
                return response;
            }
        }

        const std::filesystem::path remoteFsPath(remotePath);
        const std::string remoteDir = remoteFsPath.parent_path().empty()
            ? std::string("/tmp")
            : remoteFsPath.parent_path().string();

        const std::string sshTarget = user + "@" + host;

        auto appendSshCommonArgs = [&](std::vector<std::string>& args, bool forScp) {
            args.push_back(forScp ? "-P" : "-p");
            args.push_back(std::to_string(port));
            args.push_back("-o");
            args.push_back("BatchMode=yes");
            args.push_back("-o");
            args.push_back("StrictHostKeyChecking=accept-new");
            args.push_back("-o");
            args.push_back("ConnectTimeout=15");
            args.push_back("-o");
            args.push_back("ServerAliveInterval=15");
            args.push_back("-o");
            args.push_back("ServerAliveCountMax=2");
            args.push_back("-o");
            args.push_back("LogLevel=ERROR");
            if (!sshKeyPath.empty()) {
                args.push_back("-i");
                args.push_back(sshKeyPath);
            }
        };

        std::vector<std::string> mkdirArgs{"ssh"};
        appendSshCommonArgs(mkdirArgs, false);
        mkdirArgs.push_back(sshTarget);
        mkdirArgs.push_back("mkdir -p " + shellQuote(remoteDir));

        std::vector<std::string> scpArgs{"scp"};
        appendSshCommonArgs(scpArgs, true);
        scpArgs.push_back(localArtifactPath);
        scpArgs.push_back(sshTarget + ":" + remotePath);

        std::ostringstream remoteExec;
        auto appendEnv = [&](const std::string& key, const std::string& value) {
            if (!value.empty()) {
                remoteExec << key << "=" << shellQuote(value) << " ";
            }
        };
        appendEnv("NMC_NODE_TYPE", nodeType);
        appendEnv("NMC_NODE_REGION", region);
        appendEnv("NMC_NODE_NAME", nodeName);
        appendEnv("NMC_CONTINUUM_URL", continuumUrl);
        appendEnv("NMC_CONTINUUM_AUTH_TOKEN", continuumAuthToken);
        appendEnv("TRACEY_AGENT_ID", traceyAgentId);
        appendEnv("TRACEY_STATUS_ADDR", traceyStatusAddr);

        if (useBinaryArtifact) {
            if (useSudo) {
                if (!sudoPassword.empty()) {
                    remoteExec << "printf %s\\\\n " << shellQuote(sudoPassword)
                               << " | sudo -S -p '' -E " << shellQuote(remotePath);
                } else {
                    remoteExec << "sudo -E " << shellQuote(remotePath);
                }
            } else {
                remoteExec << shellQuote(remotePath);
            }
            for (const auto& arg : binaryArgs) {
                remoteExec << " " << shellQuote(arg);
            }
        } else {
            if (useSudo) {
                if (!sudoPassword.empty()) {
                    remoteExec << "printf %s\\\\n " << shellQuote(sudoPassword)
                               << " | sudo -S -p '' -E bash " << shellQuote(remotePath);
                } else {
                    remoteExec << "sudo -E bash " << shellQuote(remotePath);
                }
            } else {
                remoteExec << "bash " << shellQuote(remotePath);
            }
        }

        std::vector<std::string> execArgs{"ssh"};
        appendSshCommonArgs(execArgs, false);
        execArgs.push_back(sshTarget);
        execArgs.push_back("chmod +x " + shellQuote(remotePath) + " && " + remoteExec.str());

        const bool enableApps = std::find(capabilities.begin(), capabilities.end(), "apps") != capabilities.end();
        const bool enableVm = std::find(capabilities.begin(), capabilities.end(), "vm") != capabilities.end();
        const bool enablePodman = std::find(capabilities.begin(), capabilities.end(), "podman") != capabilities.end();
        const bool enableKubernetes = std::find(capabilities.begin(), capabilities.end(), "kubernetes") != capabilities.end();

        std::vector<std::string> configureArgs;
        std::string ansiblePlaybookPath;
        if (autoConfigure) {
            ansiblePlaybookPath = resolveRecruitPlaybookPath(ansiblePlaybookRequested);
            if (ansiblePlaybookPath.empty()) {
                response.message = "Unable to resolve ansible playbook. Provide --ansible-playbook or set NMC_RECRUIT_ANSIBLE_PLAYBOOK.";
                return response;
            }

            nlohmann::json mergedAnsibleVars = ansibleExtraVars;
            mergedAnsibleVars["ansible_user"] = user;
            mergedAnsibleVars["ansible_port"] = port;
            mergedAnsibleVars["ansible_python_interpreter"] = "/usr/bin/python3";
            mergedAnsibleVars["ansible_ssh_common_args"] = "-o StrictHostKeyChecking=accept-new -o ConnectTimeout=15";
            if (!sshKeyPath.empty()) {
                mergedAnsibleVars["ansible_ssh_private_key_file"] = sshKeyPath;
            }
            mergedAnsibleVars["ansible_become"] = ansibleBecome;
            if (ansibleBecome && !sudoPassword.empty()) {
                mergedAnsibleVars["ansible_become_password"] = sudoPassword;
                mergedAnsibleVars["ansible_become_pass"] = sudoPassword;
            }
            mergedAnsibleVars["nmc_tenant_id"] = tenantId;
            mergedAnsibleVars["nmc_tenant_name"] = tenantName;
            mergedAnsibleVars["nmc_tenant_environment"] = tenantEnvironment;
            mergedAnsibleVars["nmc_requested_capabilities"] = capabilities;
            mergedAnsibleVars["nmc_enable_apps"] = enableApps;
            mergedAnsibleVars["nmc_enable_vm"] = enableVm;
            mergedAnsibleVars["nmc_enable_podman"] = enablePodman;
            mergedAnsibleVars["nmc_enable_kubernetes"] = enableKubernetes;
            mergedAnsibleVars["nmc_node_host"] = host;
            mergedAnsibleVars["nmc_node_name"] = nodeName;
            mergedAnsibleVars["nmc_node_region"] = region;
            mergedAnsibleVars["nmc_node_type"] = nodeType;
            mergedAnsibleVars["nmc_continuum_url"] = continuumUrl;
            if (!continuumAuthToken.empty()) {
                mergedAnsibleVars["nmc_continuum_auth_token"] = continuumAuthToken;
            }
            if (!traceyAgentId.empty()) {
                mergedAnsibleVars["nmc_tracey_agent_id"] = traceyAgentId;
            }
            if (!traceyStatusAddr.empty()) {
                mergedAnsibleVars["nmc_tracey_status_addr"] = traceyStatusAddr;
            }

            configureArgs = {
                "env",
                "ANSIBLE_HOST_KEY_CHECKING=False",
                "ANSIBLE_TIMEOUT=45",
                "ansible-playbook",
                "-i",
                host + ",",
                ansiblePlaybookPath,
                "--limit",
                host,
                "-u",
                user,
                "-e",
                mergedAnsibleVars.dump()
            };
            if (ansibleBecome) {
                configureArgs.push_back("--become");
            }
            if (!sshKeyPath.empty()) {
                configureArgs.push_back("--private-key");
                configureArgs.push_back(sshKeyPath);
            }

            if (!dryRun) {
                const ExecResult ansibleCheck = runShellCommand({"which", "ansible-playbook"});
                if (ansibleCheck.exitCode != 0) {
                    response.message = "ansible-playbook is required for --auto-configure in direct mode but is not available.";
                    return response;
                }
            }
        }

        auto redactSensitive = [&](std::string value) {
            const std::vector<std::string> sensitiveValues = {
                continuumAuthToken,
                sudoPassword
            };
            for (const auto& sensitive : sensitiveValues) {
                if (sensitive.empty()) {
                    continue;
                }
                size_t offset = 0;
                while (true) {
                    const auto found = value.find(sensitive, offset);
                    if (found == std::string::npos) {
                        break;
                    }
                    value.replace(found, sensitive.size(), "[redacted]");
                    offset = found + 10;
                }
            }
            return value;
        };

        response.data = {
            {"target", {{"host", host}, {"port", port}, {"user", user}}},
            {"node", {{"name", nodeName}, {"type", nodeType}, {"region", region}}},
            {"mode", useBinaryArtifact ? "binary" : "script"},
            {"auto_configure", {
                {"enabled", autoConfigure},
                {"become", ansibleBecome},
                {"tenant", {
                    {"id", tenantId},
                    {"name", tenantName},
                    {"environment", tenantEnvironment}
                }},
                {"capabilities", capabilities},
                {"playbook", ansiblePlaybookPath}
            }},
            {"artifact", {
                {"local_path", useBinaryArtifact ? localArtifactPath : "<generated-script>"},
                {"remote_path", remotePath}
            }},
            {"commands", {
                {"prepare", commandToShellString(mkdirArgs)},
                {"transfer", commandToShellString(scpArgs)},
                {"execute", redactSensitive(commandToShellString(execArgs))}
            }}
        };
        if (autoConfigure) {
            response.data["commands"]["configure"] = redactSensitive(commandToShellString(configureArgs));
        }

        if (dryRun) {
            response.success = true;
            response.message = "Node recruitment dry-run generated successfully.";
            return response;
        }

        const ExecResult prepareResult = runShellCommand(mkdirArgs);
        response.data["prepare_output"] = prepareResult.output;
        response.data["prepare_exit_code"] = prepareResult.exitCode;
        if (prepareResult.exitCode != 0) {
            response.message = "Failed to create remote directory for recruitment artifact.";
            response.data["failed_step"] = "prepare";
            return response;
        }

        const ExecResult transferResult = runShellCommand(scpArgs);
        response.data["transfer_output"] = transferResult.output;
        response.data["transfer_exit_code"] = transferResult.exitCode;
        if (transferResult.exitCode != 0) {
            response.message = "Failed to transfer recruitment artifact to remote node.";
            response.data["failed_step"] = "transfer";
            return response;
        }

        const ExecResult execResult = runShellCommand(execArgs);
        response.data["execute_output"] = redactSensitive(execResult.output);
        response.data["execute_exit_code"] = execResult.exitCode;
        if (execResult.exitCode != 0) {
            response.message = "Recruitment artifact executed with errors on remote node.";
            response.data["failed_step"] = "execute";
            return response;
        }

        if (autoConfigure) {
            const ExecResult configureResult = runShellCommand(configureArgs);
            response.data["configure_output"] = redactSensitive(configureResult.output);
            response.data["configure_exit_code"] = configureResult.exitCode;
            if (configureResult.exitCode != 0) {
                response.message = "Node recruited, but Ansible auto-configuration failed.";
                response.data["failed_step"] = "configure";
                return response;
            }
        }

        response.success = true;
        response.message = autoConfigure
            ? "Node recruitment and auto-configuration completed successfully."
            : "Node recruitment completed successfully.";
        return response;

    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Direct recruitment failed: " + std::string(e.what());
        return response;
    }
}

} // namespace

NodeCommand::NodeCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("node", "Manage Continuum node recruitment", std::move(client)) {
    usage = "nmc node [recruit]";
    addAlias("nodes");
}

int NodeCommand::execute(const std::map<std::string, std::string>&,
                         const std::vector<std::string>&,
                         const CLI::GlobalFlags&) {
    printHelp();
    return 0;
}

NodeRecruitCommand::NodeRecruitCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("recruit", "Recruit a remote Ubuntu host as a Continuum node", std::move(client)) {
    usage = "nmc node recruit --host 192.168.1.60 [--user ubuntu] [--script /abs/path/setup.sh|--binary /abs/path/agent] [--sudo-password-file /abs/path/pass.txt] [--auto-configure] [--direct]";
    examples = "nmc node recruit --host 192.168.1.60 --user pbisaacs --ssh-key /home/me/.ssh/id_ed25519 --sudo-password-file /home/me/.nmc/sudo.pass --auto-configure --capability apps --capability podman --tenant-id acme";

    addFlag(CLI::Flag("H", "host", "Remote host/IP to recruit", CLI::FlagType::String, true));
    addFlag(CLI::Flag("u", "user", "SSH user", CLI::FlagType::String, false, "ubuntu"));
    addFlag(CLI::Flag("p", "port", "SSH port", CLI::FlagType::Int, false, "22"));
    addFlag(CLI::Flag("t", "node-type", "Node type: bare-metal|app-install|vm|podman|kubernetes", CLI::FlagType::String, false, "bare-metal"));
    addFlag(CLI::Flag("k", "ssh-key", "SSH private key path on recruiting host", CLI::FlagType::String, false));
    addFlag(CLI::Flag("s", "script", "Recruitment script path (local file, uploaded in API mode)", CLI::FlagType::String, false));
    addFlag(CLI::Flag("b", "binary", "Recruitment binary path (path on recruiter host; server-side in API mode)", CLI::FlagType::String, false));
    addFlag(CLI::Flag("a", "binary-arg", "Argument passed to recruited binary (repeatable)", CLI::FlagType::String, false));
    addFlag(CLI::Flag("r", "remote-path", "Target path on remote host", CLI::FlagType::String, false));
    addFlag(CLI::Flag("n", "name", "Logical node name", CLI::FlagType::String, false));
    addFlag(CLI::Flag("g", "region", "Node region/tag", CLI::FlagType::String, false));
    addFlag(CLI::Flag("A", "tracey-agent-id", "Tracey agent ID requirement", CLI::FlagType::String, false));
    addFlag(CLI::Flag("S", "tracey-status-addr", "Tracey status endpoint URL", CLI::FlagType::String, false));
    addFlag(CLI::Flag("R", "tracey-require-status-addr", "Set tracey.auto_discovery=false", CLI::FlagType::Bool, false));
    addFlag(CLI::Flag("U", "continuum-url", "Continuum base URL to write into bootstrap env", CLI::FlagType::String, false));
    addFlag(CLI::Flag("C", "continuum-auth-token", "Continuum token for bootstrap env", CLI::FlagType::String, false));
    addFlag(CLI::Flag("Z", "sudo-password", "Password used for remote sudo and Ansible become (prefer file/env)", CLI::FlagType::String, false));
    addFlag(CLI::Flag("F", "sudo-password-file", "Absolute path to file containing sudo/become password", CLI::FlagType::String, false));
    addFlag(CLI::Flag("B", "become-password", "Alias for --sudo-password", CLI::FlagType::String, false));
    addFlag(CLI::Flag("f", "become-password-file", "Alias for --sudo-password-file", CLI::FlagType::String, false));
    addFlag(CLI::Flag("q", "recruit-token", "Additional server recruit token (NMC_RECRUIT_TOKEN)", CLI::FlagType::String, false));
    addFlag(CLI::Flag("X", "auto-configure", "Run Ansible post-configuration for tenant/workload requirements", CLI::FlagType::Bool, false));
    addFlag(CLI::Flag("I", "tenant-id", "Tenant/customer identifier", CLI::FlagType::String, false));
    addFlag(CLI::Flag("m", "tenant-name", "Tenant/customer display name", CLI::FlagType::String, false));
    addFlag(CLI::Flag("e", "tenant-environment", "Tenant environment label (e.g. prod/dev)", CLI::FlagType::String, false));
    addFlag(CLI::Flag("w", "capability", "Requested host capability (apps|vm|podman|kubernetes, repeatable)", CLI::FlagType::String, false));
    addFlag(CLI::Flag("y", "ansible-playbook", "Override Ansible playbook path for auto-configure", CLI::FlagType::String, false));
    addFlag(CLI::Flag("v", "ansible-extra-var", "Additional Ansible var key=value (repeatable)", CLI::FlagType::String, false));
    addFlag(CLI::Flag("d", "dry-run", "Print commands/payload without executing remote steps", CLI::FlagType::Bool, false));
    addFlag(CLI::Flag("N", "no-sudo", "Do not use sudo when executing artifact remotely", CLI::FlagType::Bool, false));
    addFlag(CLI::Flag("M", "no-become", "Disable Ansible privilege escalation during --auto-configure", CLI::FlagType::Bool, false));
    addFlag(CLI::Flag("D", "direct", "Execute recruitment directly from this CLI host", CLI::FlagType::Bool, false));
    addFlag(CLI::Flag("P", "no-propagate-server-token", "Disable server auth-token propagation in API mode", CLI::FlagType::Bool, false));
}

int NodeRecruitCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                const std::vector<std::string>& parsedArgs,
                                const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    Models::CloudResponse response;
    response.success = false;
    response.data = nlohmann::json::object();

    const std::string host = optionalFlag(parsedFlags, "host", "");
    const std::string user = optionalFlag(parsedFlags, "user", "ubuntu");
    const std::string nodeTypeRaw = optionalFlag(parsedFlags, "node-type", "bare-metal");
    const std::string sshKey = optionalFlag(parsedFlags, "ssh-key", "");
    const std::string scriptPath = optionalFlag(parsedFlags, "script", "");
    const std::string binaryPath = optionalFlag(parsedFlags, "binary", "");
    const std::string remotePath = optionalFlag(parsedFlags, "remote-path", "");
    const std::string nodeName = optionalFlag(parsedFlags, "name", host);
    const std::string region = optionalFlag(parsedFlags, "region", "");
    const std::string traceyAgentId = optionalFlag(parsedFlags, "tracey-agent-id", "");
    const std::string traceyStatusAddr = optionalFlag(parsedFlags, "tracey-status-addr", "");
    const std::string continuumUrl = optionalFlag(parsedFlags, "continuum-url", "");
    const std::string continuumAuthToken = optionalFlag(parsedFlags, "continuum-auth-token", "");
    const std::string sudoPasswordRaw = optionalFlag(parsedFlags, "sudo-password", "");
    const std::string becomePasswordRaw = optionalFlag(parsedFlags, "become-password", "");
    const std::string sudoPasswordFileRaw = optionalFlag(parsedFlags, "sudo-password-file", "");
    const std::string becomePasswordFileRaw = optionalFlag(parsedFlags, "become-password-file", "");
    const std::string recruitToken = optionalFlag(parsedFlags, "recruit-token", "");
    const std::string tenantId = optionalFlag(parsedFlags, "tenant-id", "");
    const std::string tenantName = optionalFlag(parsedFlags, "tenant-name", "");
    const std::string tenantEnvironment = optionalFlag(parsedFlags, "tenant-environment", "");
    const std::string capabilityFlagsRaw = optionalFlag(parsedFlags, "capability", "");
    const std::string ansiblePlaybook = optionalFlag(parsedFlags, "ansible-playbook", "");
    const std::string ansibleExtraVarRaw = optionalFlag(parsedFlags, "ansible-extra-var", "");
    const bool dryRun = hasFlag(parsedFlags, "dry-run");
    const bool directMode = hasFlag(parsedFlags, "direct");
    const bool useSudo = !hasFlag(parsedFlags, "no-sudo");
    const bool ansibleBecome = !hasFlag(parsedFlags, "no-become");
    const bool noPropagateServerToken = hasFlag(parsedFlags, "no-propagate-server-token");
    const bool requireStatusAddr = hasFlag(parsedFlags, "tracey-require-status-addr");
    const bool autoConfigure = hasFlag(parsedFlags, "auto-configure");

    std::string parseError;
    int port = 22;
    if (!parseIntInRange(optionalFlag(parsedFlags, "port", "22"), 22, 1, 65535, port, parseError)) {
        response.message = "Invalid --port: " + parseError;
        printOutput(response, globalFlags);
        return 1;
    }

    const std::string nodeType = normalizeNodeType(nodeTypeRaw);
    if (host.empty() || !isSafeHost(host)) {
        response.message = "Invalid --host value.";
        printOutput(response, globalFlags);
        return 1;
    }
    if (!isSafeUser(user)) {
        response.message = "Invalid --user value.";
        printOutput(response, globalFlags);
        return 1;
    }
    if (!isKnownNodeType(nodeType)) {
        response.message = "Invalid --node-type. Supported: bare-metal, app-install, vm, podman, kubernetes.";
        printOutput(response, globalFlags);
        return 1;
    }
    if (!scriptPath.empty() && !binaryPath.empty()) {
        response.message = "Provide either --script or --binary, not both.";
        printOutput(response, globalFlags);
        return 1;
    }
    if (hasControlChars(nodeName) || hasControlChars(region) || hasControlChars(continuumUrl)) {
        response.message = "Name/region/continuum-url contains invalid control characters.";
        printOutput(response, globalFlags);
        return 1;
    }
    if (!isSafeTenantId(tenantId) || hasControlChars(tenantName) || hasControlChars(tenantEnvironment) ||
        tenantName.size() > 128 || tenantEnvironment.size() > 64) {
        response.message = "Invalid tenant metadata.";
        printOutput(response, globalFlags);
        return 1;
    }
    if (hasControlChars(ansiblePlaybook)) {
        response.message = "--ansible-playbook contains invalid control characters.";
        printOutput(response, globalFlags);
        return 1;
    }

    std::string sudoPassword = sudoPasswordRaw;
    if (!becomePasswordRaw.empty()) {
        if (!sudoPassword.empty() && sudoPassword != becomePasswordRaw) {
            response.message = "--sudo-password and --become-password do not match.";
            printOutput(response, globalFlags);
            return 1;
        }
        sudoPassword = becomePasswordRaw;
    }

    std::string sudoPasswordFile = sudoPasswordFileRaw;
    if (!becomePasswordFileRaw.empty()) {
        if (!sudoPasswordFile.empty() && sudoPasswordFile != becomePasswordFileRaw) {
            response.message = "--sudo-password-file and --become-password-file do not match.";
            printOutput(response, globalFlags);
            return 1;
        }
        sudoPasswordFile = becomePasswordFileRaw;
    }

    if (!sudoPasswordFile.empty()) {
        if (!isSafeLocalPath(sudoPasswordFile)) {
            response.message = "--sudo-password-file must be an absolute filesystem path.";
            printOutput(response, globalFlags);
            return 1;
        }
        std::string passwordFromFile;
        std::string passwordReadError;
        if (!readTextFile(sudoPasswordFile, 8192, passwordFromFile, passwordReadError)) {
            response.message = passwordReadError;
            printOutput(response, globalFlags);
            return 1;
        }
        passwordFromFile = trimLineEndCopy(passwordFromFile);
        if (passwordFromFile.empty()) {
            response.message = "--sudo-password-file is empty after trimming newline.";
            printOutput(response, globalFlags);
            return 1;
        }
        if (!sudoPassword.empty() && sudoPassword != passwordFromFile) {
            response.message = "--sudo-password and --sudo-password-file do not match.";
            printOutput(response, globalFlags);
            return 1;
        }
        sudoPassword = passwordFromFile;
    }

    if (sudoPassword.empty()) {
        const char* sudoEnv = std::getenv("NMC_SUDO_PASSWORD");
        if (sudoEnv && *sudoEnv) {
            sudoPassword = sudoEnv;
        }
    }
    if (sudoPassword.empty()) {
        const char* becomeEnv = std::getenv("NMC_BECOME_PASSWORD");
        if (becomeEnv && *becomeEnv) {
            sudoPassword = becomeEnv;
        }
    }
    sudoPassword = trimLineEndCopy(sudoPassword);
    if (sudoPassword.size() > 1024 || hasControlChars(sudoPassword)) {
        response.message = "Invalid sudo/become password value.";
        printOutput(response, globalFlags);
        return 1;
    }

    std::vector<std::string> binaryArgs = splitCommaSeparated(optionalFlag(parsedFlags, "binary-arg", ""));
    for (const auto& arg : binaryArgs) {
        if (hasControlChars(arg)) {
            response.message = "--binary-arg contains invalid control characters.";
            printOutput(response, globalFlags);
            return 1;
        }
    }

    std::vector<std::string> capabilities;
    for (const auto& rawCapability : splitCommaSeparated(capabilityFlagsRaw)) {
        const std::string normalized = normalizeCapability(rawCapability);
        if (normalized.empty()) {
            continue;
        }
        if (!isKnownCapability(normalized)) {
            response.message = "Unsupported --capability value: " + rawCapability;
            printOutput(response, globalFlags);
            return 1;
        }
        if (std::find(capabilities.begin(), capabilities.end(), normalized) == capabilities.end()) {
            capabilities.push_back(normalized);
        }
    }
    if (capabilities.empty()) {
        capabilities = defaultCapabilitiesForNodeType(nodeType);
    }

    nlohmann::json ansibleExtraVars = nlohmann::json::object();
    for (const auto& pair : splitCommaSeparated(ansibleExtraVarRaw)) {
        const auto eq = pair.find('=');
        if (eq == std::string::npos || eq == 0 || eq == pair.size() - 1) {
            response.message = "Invalid --ansible-extra-var format (expected key=value): " + pair;
            printOutput(response, globalFlags);
            return 1;
        }
        const std::string key = trimCopy(pair.substr(0, eq));
        const std::string value = trimCopy(pair.substr(eq + 1));
        if (!isSafeAnsibleVarKey(key)) {
            response.message = "Invalid --ansible-extra-var key: " + key;
            printOutput(response, globalFlags);
            return 1;
        }
        if (hasControlChars(value)) {
            response.message = "Invalid control characters in --ansible-extra-var value.";
            printOutput(response, globalFlags);
            return 1;
        }
        ansibleExtraVars[key] = value;
    }

    nlohmann::json payload;
    payload["host"] = host;
    payload["user"] = user;
    payload["port"] = port;
    payload["node_type"] = nodeType;
    payload["name"] = nodeName;
    payload["region"] = region;
    payload["sudo"] = useSudo;
    payload["dry_run"] = dryRun;
    payload["ansible_become"] = ansibleBecome;

    if (!sshKey.empty()) {
        payload["ssh_key_path"] = sshKey;
    }
    if (!remotePath.empty()) {
        payload["remote_path"] = remotePath;
    }
    if (!continuumUrl.empty()) {
        payload["continuum_url"] = continuumUrl;
    }
    if (!continuumAuthToken.empty()) {
        payload["continuum_auth_token"] = continuumAuthToken;
    }
    if (!sudoPassword.empty()) {
        payload["sudo_password"] = sudoPassword;
    }
    if (!recruitToken.empty()) {
        payload["recruit_token"] = recruitToken;
    }
    if (noPropagateServerToken) {
        payload["propagate_server_auth_token"] = false;
    }

    if (!traceyAgentId.empty() || !traceyStatusAddr.empty() || requireStatusAddr) {
        payload["tracey"] = nlohmann::json::object();
        if (!traceyAgentId.empty()) {
            payload["tracey"]["agent_id"] = traceyAgentId;
        }
        if (!traceyStatusAddr.empty()) {
            payload["tracey"]["status_addr"] = traceyStatusAddr;
        }
        payload["tracey"]["auto_discovery"] = !requireStatusAddr;
    }

    if (!scriptPath.empty()) {
        if (!isSafeLocalPath(scriptPath)) {
            response.message = "--script must be an absolute filesystem path.";
            printOutput(response, globalFlags);
            return 1;
        }

        std::string scriptBody;
        std::string readError;
        if (!readTextFile(scriptPath, 1024 * 1024, scriptBody, readError)) {
            response.message = readError;
            printOutput(response, globalFlags);
            return 1;
        }
        payload["script"] = scriptBody;
    }

    if (!binaryPath.empty()) {
        payload["binary_path"] = binaryPath;
    }

    if (!binaryArgs.empty()) {
        payload["binary_args"] = binaryArgs;
    }
    if (autoConfigure) {
        payload["auto_configure"] = true;
    }
    if (!tenantId.empty()) {
        payload["tenant_id"] = tenantId;
    }
    if (!tenantName.empty()) {
        payload["tenant_name"] = tenantName;
    }
    if (!tenantEnvironment.empty()) {
        payload["tenant_environment"] = tenantEnvironment;
    }
    if (!capabilities.empty()) {
        payload["capabilities"] = capabilities;
    }
    if (!ansiblePlaybook.empty()) {
        payload["ansible_playbook"] = ansiblePlaybook;
    }
    if (!ansibleExtraVars.empty()) {
        payload["ansible_extra_vars"] = ansibleExtraVars;
    }

    if (directMode) {
        response = runDirectRecruitment(payload);
    } else {
        response = apiClient->recruitNode(payload);
    }

    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

} // namespace NMC::Commands
