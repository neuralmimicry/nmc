// Provider-aware compute lifecycle operations using the AWS, Google and Azure CLIs.
#include "ProviderCompute.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <regex>
#include <set>
#include <sstream>
#include <vector>
#include <signal.h>
#include <spawn.h>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

namespace NMC::Server {
namespace {

using Json = nlohmann::json;

struct ProcessResult {
    int exitCode{1};
    bool timedOut{false};
    bool outputTooLarge{false};
    std::string output;
};

ProviderComputeResult failure(int status, const std::string& message) {
    return {false, status, message, Json::object()};
}

ProviderComputeResult success(const std::string& message, Json data) {
    return {true, 200, message, std::move(data)};
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool matches(const std::string& value, const char* expression) {
    return std::regex_match(value, std::regex(expression));
}

bool enabled(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) return false;
    const std::string normal = upper(trim(value));
    return normal == "1" || normal == "TRUE" || normal == "YES" || normal == "ON";
}

const char* allowedScopesVariable(const std::string& provider) {
    if (provider == "aws") return "NMC_AWS_ALLOWED_SCOPES";
    if (provider == "gcp") return "NMC_GCP_ALLOWED_SCOPES";
    return "NMC_AZURE_ALLOWED_SCOPES";
}

bool scopeAllowed(const std::string& provider, const std::string& scope) {
    const char* raw = std::getenv(allowedScopesVariable(provider));
    if (raw == nullptr) return false;
    std::stringstream stream(raw);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (trim(item) == scope) return true;
    }
    return false;
}

std::string executableFor(const std::string& provider) {
    const std::string variable = "NMC_PROVIDER_" + upper(provider) + "_CLI_PATH";
    const char* configured = std::getenv(variable.c_str());
    if (configured != nullptr && *configured != '\0') {
        const std::string path(configured);
        if (!path.empty() && path.front() == '/' && ::access(path.c_str(), X_OK) == 0) return path;
        return {};
    }
    const std::vector<std::string> candidates = provider == "aws"
            ? std::vector<std::string>{"/usr/bin/aws", "/usr/local/bin/aws"}
            : provider == "gcp"
                    ? std::vector<std::string>{"/usr/bin/gcloud", "/usr/local/bin/gcloud"}
                    : std::vector<std::string>{"/usr/bin/az", "/usr/local/bin/az"};
    for (const auto& candidate : candidates) {
        if (::access(candidate.c_str(), X_OK) == 0) return candidate;
    }
    return {};
}

bool supportedProvider(const std::string& provider) {
    return provider == "aws" || provider == "gcp" || provider == "azure";
}

bool validScope(const std::string& provider, const std::string& scope) {
    if (provider == "aws") return matches(scope, "[0-9]{12}");
    if (provider == "gcp") return matches(scope, "[a-z][a-z0-9-]{3,28}[a-z0-9]");
    return matches(scope, "[A-Fa-f0-9]{8}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{12}");
}

bool validRegion(const std::string& region) {
    return matches(region, "[A-Za-z0-9][A-Za-z0-9._-]{0,63}");
}

// Provider SDKs need their normal workload identity variables, but NMC's own
// API tokens and unrelated process environment must never be forwarded.
std::vector<std::string> childEnvironment() {
    std::vector<std::string> environment{
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
        "LC_ALL=C"
    };
    static const std::array<const char*, 25> names{
        "HOME", "AWS_PROFILE", "AWS_REGION", "AWS_DEFAULT_REGION", "AWS_CONFIG_FILE",
        "AWS_SHARED_CREDENTIALS_FILE", "AWS_ROLE_ARN", "AWS_ROLE_SESSION_NAME",
        "AWS_WEB_IDENTITY_TOKEN_FILE", "AWS_CONTAINER_CREDENTIALS_FULL_URI",
        "AWS_CONTAINER_CREDENTIALS_RELATIVE_URI", "AWS_ACCESS_KEY_ID", "AWS_SECRET_ACCESS_KEY",
        "AWS_SESSION_TOKEN", "CLOUDSDK_CONFIG", "GOOGLE_APPLICATION_CREDENTIALS",
        "GOOGLE_CLOUD_PROJECT", "GOOGLE_PROJECT", "GCLOUD_PROJECT", "AZURE_CONFIG_DIR",
        "AZURE_CLIENT_ID", "AZURE_TENANT_ID", "AZURE_SUBSCRIPTION_ID", "AZURE_CLIENT_SECRET",
        "AZURE_FEDERATED_TOKEN_FILE"
    };
    for (const char* name : names) {
        const char* value = std::getenv(name);
        if (value != nullptr) environment.emplace_back(std::string(name) + "=" + value);
    }
    return environment;
}

ProcessResult runProviderCli(const std::string& provider,
                             const std::vector<std::string>& arguments,
                             int timeoutMs = 12000) {
    ProcessResult result;
    const std::string executable = executableFor(provider);
    if (executable.empty() || arguments.empty()) return result;

    int pipeFds[2];
    if (::pipe(pipeFds) != 0) return result;
    std::vector<std::string> argumentStorage{executable};
    argumentStorage.insert(argumentStorage.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    argv.reserve(argumentStorage.size() + 1);
    for (auto& item : argumentStorage) argv.push_back(item.data());
    argv.push_back(nullptr);
    auto environmentStorage = childEnvironment();
    std::vector<char*> environment;
    environment.reserve(environmentStorage.size() + 1);
    for (auto& item : environmentStorage) environment.push_back(item.data());
    environment.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    if (::posix_spawn_file_actions_init(&actions) != 0) {
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        return result;
    }
    ::posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDOUT_FILENO);
    ::posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    ::posix_spawn_file_actions_addclose(&actions, pipeFds[0]);
    ::posix_spawn_file_actions_addclose(&actions, pipeFds[1]);
    pid_t child = -1;
    const int spawnError = ::posix_spawn(&child, executable.c_str(), &actions, nullptr, argv.data(), environment.data());
    ::posix_spawn_file_actions_destroy(&actions);
    ::close(pipeFds[1]);
    if (spawnError != 0) {
        ::close(pipeFds[0]);
        return result;
    }

    const int oldFlags = ::fcntl(pipeFds[0], F_GETFL, 0);
    if (oldFlags >= 0) ::fcntl(pipeFds[0], F_SETFL, oldFlags | O_NONBLOCK);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    bool pipeOpen = true;
    bool childExited = false;
    int waitStatus = 0;
    std::array<char, 8192> buffer{};
    while (pipeOpen || !childExited) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) {
            result.timedOut = true;
            ::kill(child, SIGKILL);
            break;
        }
        struct pollfd descriptor{pipeFds[0], static_cast<short>(POLLIN | POLLHUP), 0};
        const int pollResult = ::poll(&descriptor, 1, static_cast<int>(std::min<int64_t>(remaining, 200)));
        if (pollResult > 0 && pipeOpen) {
            while (true) {
                const ssize_t count = ::read(pipeFds[0], buffer.data(), buffer.size());
                if (count > 0) {
                    const size_t room = result.output.size() < 1024 * 1024
                            ? 1024 * 1024 - result.output.size() : 0;
                    const size_t accepted = std::min<size_t>(static_cast<size_t>(count), room);
                    result.output.append(buffer.data(), accepted);
                    if (accepted < static_cast<size_t>(count) || result.output.size() >= 1024 * 1024) {
                        result.outputTooLarge = true;
                        ::kill(child, SIGKILL);
                        break;
                    }
                    continue;
                }
                if (count == 0 || (count < 0 && errno != EAGAIN && errno != EINTR)) pipeOpen = false;
                break;
            }
        }
        const pid_t waited = ::waitpid(child, &waitStatus, WNOHANG);
        if (waited == child) childExited = true;
    }
    if (!childExited) ::waitpid(child, &waitStatus, 0);
    ::close(pipeFds[0]);
    if (!result.timedOut && !result.outputTooLarge && WIFEXITED(waitStatus)) {
        result.exitCode = WEXITSTATUS(waitStatus);
    }
    return result;
}

ProviderComputeResult runFailure(const std::string& provider, const ProcessResult& result) {
    if (executableFor(provider).empty()) {
        return failure(503, "The configured " + provider + " provider CLI is unavailable.");
    }
    if (result.timedOut) return failure(504, "The " + provider + " provider request exceeded its time limit.");
    if (result.outputTooLarge) return failure(502, "The " + provider + " provider response exceeded the safety limit.");
    return failure(502, "The " + provider + " provider rejected or could not complete the request.");
}

bool parseOutput(const std::string& provider, const ProcessResult& process, Json& parsed) {
    if (process.exitCode != 0 || process.timedOut || process.outputTooLarge) return false;
    try {
        parsed = Json::parse(process.output);
        return true;
    } catch (const Json::exception&) {
        return false;
    }
}

bool jsonString(const Json& value, const char* key, std::string& result) {
    const auto it = value.find(key);
    if (it == value.end() || !it->is_string()) return false;
    result = it->get<std::string>();
    return !result.empty();
}

ProviderComputeResult requireContext(const std::string& provider, const std::string& scope) {
    if (!supportedProvider(provider)) return failure(400, "provider must be one of: aws, gcp, azure.");
    if (!validScope(provider, scope)) return failure(400, "scope is not a valid provider account, project, or subscription identifier.");
    if (!scopeAllowed(provider, scope)) return failure(403, "The requested provider scope is not in the server allowlist.");
    if (executableFor(provider).empty()) return failure(503, "The configured " + provider + " provider CLI is unavailable.");
    return success("Provider scope accepted.", Json::object());
}

ProviderComputeResult verifyIdentity(const std::string& provider, const std::string& scope) {
    Json identity;
    ProcessResult process;
    if (provider == "aws") {
        process = runProviderCli(provider, {"sts", "get-caller-identity", "--output", "json", "--no-cli-pager"});
        if (!parseOutput(provider, process, identity)) return runFailure(provider, process);
        std::string account;
        if (!jsonString(identity, "Account", account) || account != scope) {
            return failure(403, "The active AWS identity does not match the allowlisted account scope.");
        }
    } else if (provider == "gcp") {
        process = runProviderCli(provider, {"projects", "describe", scope, "--project", scope, "--format=json", "--quiet"});
        if (!parseOutput(provider, process, identity)) return runFailure(provider, process);
        std::string project;
        if (!jsonString(identity, "projectId", project) || project != scope) {
            return failure(403, "The active GCP identity cannot verify the allowlisted project scope.");
        }
    } else {
        process = runProviderCli(provider, {"account", "show", "--subscription", scope, "--output", "json", "--only-show-errors"});
        if (!parseOutput(provider, process, identity)) return runFailure(provider, process);
        std::string subscription;
        if (!jsonString(identity, "id", subscription) || upper(subscription) != upper(scope)) {
            return failure(403, "The active Azure identity does not match the allowlisted subscription scope.");
        }
    }
    return success("Provider identity and scope verified.", Json::object());
}

Json awsInstance(const Json& source, const std::string& scope, const std::string& region) {
    Json item = Json::object();
    item["provider"] = "aws";
    item["scope"] = scope;
    item["region"] = region;
    item["id"] = source.value("InstanceId", std::string{});
    item["name"] = item["id"];
    item["machine_type"] = source.value("InstanceType", std::string{});
    item["state"] = source.value("State", Json::object()).value("Name", std::string("unknown"));
    if (source.contains("PrivateIpAddress")) item["private_ip"] = source["PrivateIpAddress"];
    if (source.contains("PublicIpAddress")) item["public_ip"] = source["PublicIpAddress"];
    if (source.contains("Placement")) item["zone"] = source["Placement"].value("AvailabilityZone", std::string{});
    if (source.contains("Tags") && source["Tags"].is_array()) {
        for (const auto& tag : source["Tags"]) {
            if (tag.value("Key", std::string{}) == "Name") item["name"] = tag.value("Value", std::string{});
        }
    }
    return item;
}

Json gcpInstance(const Json& source, const std::string& scope) {
    Json item = Json::object();
    item["provider"] = "gcp";
    item["scope"] = scope;
    item["id"] = source.value("name", std::string{});
    item["name"] = item["id"];
    item["machine_type"] = source.value("machineType", std::string{});
    item["state"] = source.value("status", std::string("UNKNOWN"));
    const std::string zonePath = source.value("zone", std::string{});
    const auto slash = zonePath.find_last_of('/');
    item["zone"] = slash == std::string::npos ? zonePath : zonePath.substr(slash + 1);
    const std::string& zone = item["zone"].get_ref<const std::string&>();
    const auto zoneSeparator = zone.find_last_of('-');
    item["region"] = zoneSeparator == std::string::npos ? zone : zone.substr(0, zoneSeparator);
    if (source.contains("networkInterfaces") && source["networkInterfaces"].is_array() && !source["networkInterfaces"].empty()) {
        const auto& network = source["networkInterfaces"][0];
        if (network.contains("networkIP")) item["private_ip"] = network["networkIP"];
        if (network.contains("accessConfigs") && network["accessConfigs"].is_array() && !network["accessConfigs"].empty()) {
            const auto& access = network["accessConfigs"][0];
            if (access.contains("natIP")) item["public_ip"] = access["natIP"];
        }
    }
    return item;
}

Json azureInstance(const Json& source, const std::string& scope) {
    Json item = Json::object();
    item["provider"] = "azure";
    item["scope"] = scope;
    item["id"] = source.value("id", std::string{});
    item["name"] = source.value("name", std::string{});
    item["region"] = source.value("location", std::string{});
    item["machine_type"] = source.value("hardwareProfile", Json::object()).value("vmSize", std::string{});
    item["state"] = source.value("powerState", std::string("unknown"));
    item["resource_group"] = source.value("resourceGroup", std::string{});
    if (source.contains("privateIps")) item["private_ip"] = source["privateIps"];
    if (source.contains("publicIps")) item["public_ip"] = source["publicIps"];
    return item;
}

ProviderComputeResult listUnchecked(const std::string& provider,
                                    const std::string& scope,
                                    const std::string& region,
                                    Json& instances) {
    Json output;
    ProcessResult process;
    instances = Json::array();
    if (provider == "aws") {
        process = runProviderCli(provider, {"ec2", "describe-instances", "--region", region,
                "--filters", "Name=instance-state-name,Values=pending,running,stopping,stopped,shutting-down",
                "--output", "json", "--no-cli-pager"});
        if (!parseOutput(provider, process, output)) return runFailure(provider, process);
        if (!output.contains("Reservations") || !output["Reservations"].is_array()) {
            return failure(502, "The AWS provider returned an invalid instance inventory.");
        }
        for (const auto& reservation : output["Reservations"]) {
            if (!reservation.contains("Instances") || !reservation["Instances"].is_array()) continue;
            for (const auto& instance : reservation["Instances"]) instances.push_back(awsInstance(instance, scope, region));
        }
    } else if (provider == "gcp") {
        process = runProviderCli(provider, {"compute", "instances", "list", "--project", scope, "--format=json", "--quiet"});
        if (!parseOutput(provider, process, output) || !output.is_array()) return runFailure(provider, process);
        for (const auto& instance : output) {
            Json normalized = gcpInstance(instance, scope);
            if (region.empty() || normalized.value("region", std::string{}) == region) instances.push_back(std::move(normalized));
        }
    } else {
        process = runProviderCli(provider, {"vm", "list", "--show-details", "--subscription", scope,
                "--output", "json", "--only-show-errors"});
        if (!parseOutput(provider, process, output) || !output.is_array()) return runFailure(provider, process);
        for (const auto& vm : output) {
            Json normalized = azureInstance(vm, scope);
            if (region.empty() || normalized.value("region", std::string{}) == region) instances.push_back(std::move(normalized));
        }
    }
    // The caller owns and consumes this array for the response or the exact
    // instance precheck. Moving it into this status result would invalidate
    // that output parameter and silently turn a valid inventory into null.
    return success("Provider instances listed from the live provider API.", Json::object());
}

ProviderComputeResult listInternal(const std::string& provider,
                                   const std::string& scope,
                                   const std::string& region,
                                   Json& instances) {
    ProviderComputeResult context = requireContext(provider, scope);
    if (!context.success) return context;
    if ((!region.empty() && !validRegion(region)) || (provider != "gcp" && region.empty())) {
        return failure(400, "region is required for AWS and Azure and must be a valid provider region when supplied.");
    }
    ProviderComputeResult identity = verifyIdentity(provider, scope);
    if (!identity.success) return identity;
    return listUnchecked(provider, scope, region, instances);
}

std::string normalizedState(const std::string& provider, const Json& instance) {
    std::string state = upper(instance.value("state", std::string("UNKNOWN")));
    if (provider == "azure") {
        if (state.find("RUNNING") != std::string::npos) return "RUNNING";
        if (state.find("DEALLOCATED") != std::string::npos || state.find("STOPPED") != std::string::npos) return "STOPPED";
        if (state.find("DELETING") != std::string::npos || state.find("DELETED") != std::string::npos) return "DELETED";
    }
    if (provider == "aws") {
        if (state == "RUNNING") return "RUNNING";
        if (state == "STOPPED") return "STOPPED";
        if (state == "TERMINATED" || state == "SHUTTING-DOWN") return "DELETED";
    }
    if (provider == "gcp") {
        if (state == "RUNNING") return "RUNNING";
        if (state == "TERMINATED" || state == "SUSPENDED") return "STOPPED";
        if (state == "DELETED") return "DELETED";
    }
    return state;
}

std::string targetState(const std::string& action) {
    if (action == "start" || action == "restart") return "RUNNING";
    if (action == "stop") return "STOPPED";
    return "DELETED";
}

std::vector<std::string> actionArguments(const std::string& provider,
                                         const std::string& scope,
                                         const Json& request,
                                         const std::string& action,
                                         const std::string& id,
                                         const std::string& region) {
    if (provider == "aws") {
        std::vector<std::string> args{"ec2"};
        const std::string operation = action == "restart" ? "reboot-instances"
                : action == "start" ? "start-instances"
                : action == "stop" ? "stop-instances" : "terminate-instances";
        args.insert(args.end(), {operation, "--instance-ids", id, "--region", region, "--output", "json", "--no-cli-pager"});
        return args;
    }
    if (provider == "gcp") {
        const std::string zone = request.value("zone", std::string{});
        std::vector<std::string> args{"compute", "instances"};
        const std::string operation = action == "restart" ? "reset"
                : action == "start" ? "start" : action == "stop" ? "stop" : "delete";
        args.push_back(operation);
        args.push_back(id);
        args.insert(args.end(), {"--project", scope, "--zone", zone, "--quiet", "--format=json"});
        return args;
    }
    const std::string operation = action == "restart" ? "restart"
            : action == "start" ? "start" : action == "stop" ? "deallocate" : "delete";
    std::vector<std::string> args{"vm", operation, "--ids", id, "--subscription", scope,
                                  "--only-show-errors", "--output", "json"};
    if (action == "delete") args.push_back("--yes");
    return args;
}

ProviderComputeResult verifyExpectedState(const std::string& provider,
                                          const std::string& scope,
                                          const std::string& region,
                                          const Json& request,
                                          const std::string& id,
                                          const std::string& action,
                                          Json& instance) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (std::chrono::steady_clock::now() < deadline) {
        if (provider == "aws") {
            ProcessResult response = runProviderCli(provider, {"ec2", "describe-instances", "--instance-ids", id,
                    "--region", region, "--output", "json", "--no-cli-pager"}, 5000);
            Json output;
            if (response.exitCode == 0 && parseOutput(provider, response, output)
                    && output.contains("Reservations") && output["Reservations"].is_array()
                    && !output["Reservations"].empty() && output["Reservations"][0].contains("Instances")
                    && output["Reservations"][0]["Instances"].is_array()
                    && !output["Reservations"][0]["Instances"].empty()) {
                instance = awsInstance(output["Reservations"][0]["Instances"][0], scope, region);
            } else if (action == "delete" && response.exitCode == 0) {
                return success("Provider accepted deletion and inventory no longer contains the instance.",
                               {{"id", id}, {"state", "DELETED"}});
            }
        } else if (provider == "gcp") {
            const std::string zone = request.value("zone", std::string{});
            ProcessResult response = runProviderCli(provider, {"compute", "instances", "describe", id, "--project", scope,
                    "--zone", zone, "--format=json", "--quiet"}, 5000);
            Json output;
            if (parseOutput(provider, response, output)) instance = gcpInstance(output, scope);
            else if (action == "delete" && response.exitCode != 0) {
                Json remaining;
                ProviderComputeResult listed = listUnchecked(provider, scope, "", remaining);
                if (!listed.success) return listed;
                const bool found = std::any_of(remaining.begin(), remaining.end(), [&](const Json& item) {
                    return item.value("id", std::string{}) == id;
                });
                if (!found) return success("Provider accepted deletion and inventory no longer contains the instance.",
                                           {{"id", id}, {"state", "DELETED"}});
            }
        } else {
            ProcessResult response = runProviderCli(provider, {"vm", "list", "--show-details", "--subscription", scope,
                    "--output", "json", "--only-show-errors"}, 5000);
            Json output;
            if (parseOutput(provider, response, output) && output.is_array()) {
                auto found = std::find_if(output.begin(), output.end(), [&](const Json& item) {
                    return item.value("id", std::string{}) == id;
                });
                if (found != output.end()) instance = azureInstance(*found, scope);
                else if (action == "delete") return success("Provider accepted deletion and inventory no longer contains the instance.",
                                                               {{"id", id}, {"state", "DELETED"}});
            }
        }
        if (!instance.is_null() && normalizedState(provider, instance) == targetState(action)) {
            return success("Provider reports the requested instance state.", instance);
        }
        instance = nullptr;
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
    return failure(504, "The provider accepted the operation but the requested provider state was not verified before timeout.");
}

ProviderComputeResult validateMutation(const std::string& action) {
    if (!enabled("NMC_PROVIDER_MUTATIONS_ENABLED")) {
        return failure(423, "Provider mutations are disabled; set NMC_PROVIDER_MUTATIONS_ENABLED=true after configuring scoped credentials and dependency checks.");
    }
    if (action == "delete" && !enabled("NMC_PROVIDER_ALLOW_DELETE")) {
        return failure(423, "Provider deletion is disabled; set NMC_PROVIDER_ALLOW_DELETE=true only for an approved teardown scope.");
    }
    return success("Provider mutation policy allows this operation.", Json::object());
}

bool safeName(const std::string& value) {
    return matches(value, "[A-Za-z0-9][A-Za-z0-9._-]{0,62}");
}

bool safeResourceId(const std::string& provider, const std::string& id) {
    if (provider == "aws") return matches(id, "i-[A-Fa-f0-9]{8,17}");
    if (provider == "gcp") return safeName(id);
    return matches(id,
            "/subscriptions/[A-Fa-f0-9]{8}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{12}/resourceGroups/[A-Za-z0-9._()-]{1,90}/providers/Microsoft\\.Compute/virtualMachines/[A-Za-z0-9._-]{1,64}");
}

bool expectedAzureResourceId(const std::string& id, const std::string& scope) {
    std::string prefix = "/subscriptions/" + scope + "/";
    std::string lowerId = id;
    std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    std::string lowerPrefix = prefix;
    std::transform(lowerPrefix.begin(), lowerPrefix.end(), lowerPrefix.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowerId.rfind(lowerPrefix, 0) == 0;
}

} // namespace

ProviderComputeResult ProviderCompute::status() {
    Json providers = Json::array();
    for (const std::string& provider : {"aws", "gcp", "azure"}) {
        const std::string variable = allowedScopesVariable(provider);
        const char* scopes = std::getenv(variable.c_str());
        providers.push_back({
            {"provider", provider},
            {"cli_available", !executableFor(provider).empty()},
            {"scope_allowlist_configured", scopes != nullptr && *scopes != '\0'},
            {"mutations_enabled", enabled("NMC_PROVIDER_MUTATIONS_ENABLED")},
            {"delete_enabled", enabled("NMC_PROVIDER_MUTATIONS_ENABLED") && enabled("NMC_PROVIDER_ALLOW_DELETE")}
        });
    }
    return success("Provider compute adapter readiness.", std::move(providers));
}

ProviderComputeResult ProviderCompute::preflight(const std::string& operation,
                                                 const Json& request) {
    if (!request.is_object()) return failure(400, "Provider job request must be a JSON object.");
    auto field = [&](const char* name, std::string& value, bool required = true) {
        const auto found = request.find(name);
        if (found == request.end()) return !required;
        if (!found->is_string()) return false;
        value = found->get<std::string>();
        return !required || !value.empty();
    };

    std::string provider;
    std::string scope;
    if (!field("provider", provider) || !field("scope", scope)) {
        return failure(400, "provider and scope must be non-empty strings.");
    }
    provider = lower(provider);

    if (operation == "list") {
        ProviderComputeResult context = requireContext(provider, scope);
        if (!context.success) return context;
        std::string region;
        if (!field("region", region, false)) return failure(400, "region must be a string when supplied.");
        if ((provider == "aws" || provider == "azure") && !validRegion(region)) {
            return failure(400, "region is required and must be a valid provider region.");
        }
        if (!region.empty() && !validRegion(region)) return failure(400, "region is not valid.");
        return success("Provider inventory request passed local scope preflight.", Json::object());
    }

    if (operation == "create") {
        const auto specification = request.find("spec");
        if (specification == request.end() || !specification->is_object()) {
            return failure(400, "spec must be a JSON object.");
        }
        std::string idempotencyKey;
        if (!field("idempotency_key", idempotencyKey) ||
            !matches(idempotencyKey, "[A-Za-z0-9][A-Za-z0-9-]{14,62}[A-Za-z0-9]")) {
            return failure(400, "idempotency_key must contain 16 to 64 letters, digits or hyphens and start and end with a letter or digit.");
        }
        ProviderComputeResult policy = validateMutation("create");
        if (!policy.success) return policy;
        return requireContext(provider, scope);
    }

    if (operation == "action") {
        std::string actionName;
        std::string id;
        std::string requestId;
        if (!field("action", actionName) || !field("instance_id", id) || !field("request_id", requestId)) {
            return failure(400, "action, instance_id and request_id must be non-empty strings.");
        }
        actionName = lower(actionName);
        if (!matches(requestId, "[A-Za-z0-9][A-Za-z0-9-]{14,62}[A-Za-z0-9]")) {
            return failure(400, "request_id must contain 16 to 64 letters, digits or hyphens and start and end with a letter or digit.");
        }
        if (actionName != "restart" && actionName != "start" && actionName != "stop" && actionName != "delete") {
            return failure(400, "action must be one of: restart, start, stop, delete.");
        }
        if (!safeResourceId(provider, id)) return failure(400, "instance_id is not a valid canonical resource identifier for the provider.");
        if (provider == "azure" && !expectedAzureResourceId(id, scope)) {
            return failure(403, "Azure instance ID does not belong to the requested subscription scope.");
        }
        if (provider == "gcp") {
            std::string zone;
            if (!field("zone", zone) || !safeName(zone)) return failure(400, "GCP actions require a valid zone.");
        } else {
            std::string region;
            if (!field("region", region) || !validRegion(region)) {
                return failure(400, "region is required and must be a valid provider region.");
            }
        }
        ProviderComputeResult policy = validateMutation(actionName);
        if (!policy.success) return policy;
        return requireContext(provider, scope);
    }

    return failure(400, "operation must be one of: list, create, action.");
}

ProviderComputeResult ProviderCompute::list(const std::string& providerInput,
                                            const std::string& scope,
                                            const std::string& region) {
    const std::string provider = lower(providerInput);
    Json instances;
    ProviderComputeResult result = listInternal(provider, scope, region, instances);
    if (!result.success) return result;
    return success("Live " + provider + " compute inventory.",
                   {{"provider", provider}, {"scope", scope}, {"instances", std::move(instances)},
                    {"source", "provider-api"}, {"service_health", "not-checked"}});
}

ProviderComputeResult ProviderCompute::create(const Json& request) {
    if (!request.is_object()) return failure(400, "Request body must be a JSON object.");
    const std::string provider = lower(request.value("provider", std::string{}));
    const std::string scope = request.value("scope", std::string{});
    const Json specification = request.value("spec", Json::object());
    if (!specification.is_object()) return failure(400, "spec must be a JSON object.");
    ProviderComputeResult policy = validateMutation("create");
    if (!policy.success) return policy;
    ProviderComputeResult context = requireContext(provider, scope);
    if (!context.success) return context;
    ProviderComputeResult identity = verifyIdentity(provider, scope);
    if (!identity.success) return identity;

    std::vector<std::string> args;
    std::string name;
    std::string region;
    if (provider == "aws") {
        const std::string image = specification.value("image_id", std::string{});
        const std::string type = specification.value("instance_type", std::string{});
        const std::string subnet = specification.value("subnet_id", std::string{});
        const std::string keyName = specification.value("key_name", std::string{});
        const std::string instanceProfile = specification.value("instance_profile", std::string{});
        region = specification.value("region", std::string{});
        name = specification.value("name", std::string{});
        if (!safeName(name) || !validRegion(region) || !matches(image, "ami-[A-Fa-f0-9]{8,17}")
                || !matches(type, "[A-Za-z0-9.-]{1,64}") || !matches(subnet, "subnet-[A-Fa-f0-9]{8,17}")
                || !specification.contains("security_group_ids") || !specification["security_group_ids"].is_array()
                || specification["security_group_ids"].empty()) {
            return failure(400, "AWS spec requires name, region, AMI, instance type, subnet and security group IDs.");
        }
        Json groups = Json::array();
        for (const auto& group : specification["security_group_ids"]) {
            if (!group.is_string() || !matches(group.get<std::string>(), "sg-[A-Fa-f0-9]{8,17}")) {
                return failure(400, "Every AWS security group ID must be a valid sg- identifier.");
            }
            groups.push_back(group);
        }
        const std::string token = request.value("idempotency_key", std::string{});
        if (!matches(token, "[A-Za-z0-9][A-Za-z0-9-]{14,62}[A-Za-z0-9]")) return failure(400, "idempotency_key must contain 16 to 64 letters, digits or hyphens and start and end with a letter or digit.");
        const Json networkInterface = {
            {"DeviceIndex", 0}, {"SubnetId", subnet}, {"Groups", groups}, {"AssociatePublicIpAddress", false}
        };
        const Json networkInterfaces = Json::array({networkInterface});
        const Json tagSpecifications = Json::array({{
            {"ResourceType", "instance"},
            {"Tags", Json::array({{{"Key", "Name"}, {"Value", name}}})}
        }});
        args = {"ec2", "run-instances", "--image-id", image, "--instance-type", type,
                "--network-interfaces", networkInterfaces.dump(),
                "--tag-specifications", tagSpecifications.dump(),
                "--client-token", token, "--region", region, "--output", "json", "--no-cli-pager"};
        if (!keyName.empty()) {
            if (!safeName(keyName)) return failure(400, "key_name contains unsupported characters.");
            args.insert(args.end(), {"--key-name", keyName});
        }
        if (!instanceProfile.empty()) {
            if (!safeName(instanceProfile)) return failure(400, "instance_profile contains unsupported characters.");
            args.insert(args.end(), {"--iam-instance-profile", "Name=" + instanceProfile});
        }
    } else if (provider == "gcp") {
        name = specification.value("name", std::string{});
        region = specification.value("region", std::string{});
        const std::string zone = specification.value("zone", std::string{});
        const std::string machine = specification.value("machine_type", std::string{});
        const std::string image = specification.value("image", std::string{});
        const std::string imageProject = specification.value("image_project", std::string{});
        const std::string subnet = specification.value("subnet", std::string{});
        const std::string serviceAccount = specification.value("service_account", std::string{});
        if (!specification.contains("scopes") || !specification["scopes"].is_array() || specification["scopes"].empty()) {
            return failure(400, "GCP spec requires one or more explicit service account access scopes.");
        }
        std::set<std::string> uniqueScopes;
        std::string accessScopes;
        for (const auto& item : specification["scopes"]) {
            if (!item.is_string() || !matches(item.get<std::string>(), "https://www\\.googleapis\\.com/auth/[A-Za-z0-9._/-]{1,160}")) {
                return failure(400, "GCP access scopes must be explicit Google API OAuth scope URLs.");
            }
            const std::string scopeValue = item.get<std::string>();
            if (!uniqueScopes.insert(scopeValue).second) continue;
            if (!accessScopes.empty()) accessScopes += ",";
            accessScopes += scopeValue;
        }
        const std::string token = request.value("idempotency_key", std::string{});
        if (!safeName(name) || !validRegion(region) || !safeName(zone) || !safeName(machine)
                || !safeName(image) || !safeName(imageProject) || !safeName(subnet)
                || !matches(serviceAccount, "[A-Za-z0-9._@-]{3,256}")
                || !matches(token, "[A-Za-z0-9][A-Za-z0-9-]{14,62}[A-Za-z0-9]")) {
            return failure(400, "GCP spec requires name, region, zone, machine type, image, image project, subnet, service account and idempotency key.");
        }
        std::string labels = "continuum-request=" + lower(token.substr(0, 63));
        args = {"compute", "instances", "create", name, "--project", scope, "--zone", zone,
                "--machine-type", machine, "--image", image, "--image-project", imageProject,
                "--subnet", subnet, "--service-account", serviceAccount, "--scopes", accessScopes, "--no-address",
                "--labels", labels, "--format=json", "--quiet"};
    } else if (provider == "azure") {
        name = specification.value("name", std::string{});
        region = specification.value("region", std::string{});
        const std::string resourceGroup = specification.value("resource_group", std::string{});
        const std::string image = specification.value("image", std::string{});
        const std::string size = specification.value("size", std::string{});
        const std::string admin = specification.value("admin_username", std::string{});
        const std::string key = specification.value("ssh_public_key", std::string{});
        const std::string vnet = specification.value("vnet_name", std::string{});
        const std::string subnet = specification.value("subnet", std::string{});
        const std::string nsg = specification.value("network_security_group", std::string{});
        if (!safeName(name) || !validRegion(region) || !safeName(resourceGroup) || !safeName(image)
                || !safeName(size) || !matches(admin, "[A-Za-z_][A-Za-z0-9_-]{0,31}")
                || key.size() < 32 || key.size() > 16384 || key.find('\n') != std::string::npos
                || !safeName(vnet) || !safeName(subnet) || !safeName(nsg)) {
            return failure(400, "Azure spec requires name, region, resource group, image, size, admin username, SSH public key, VNet, subnet and network security group.");
        }
        args = {"vm", "create", "--subscription", scope, "--resource-group", resourceGroup,
                "--name", name, "--location", region, "--image", image, "--size", size,
                "--admin-username", admin, "--authentication-type", "ssh", "--ssh-key-values", key,
                "--vnet-name", vnet, "--subnet", subnet, "--nsg", nsg,
                "--public-ip-address", "", "--only-show-errors", "--output", "json"};
    }

    Json existing;
    ProviderComputeResult listResult = listUnchecked(provider, scope, region, existing);
    if (!listResult.success) return listResult;
    const bool duplicate = std::any_of(existing.begin(), existing.end(), [&](const Json& item) {
        return item.value("name", std::string{}) == name;
    });
    if (duplicate) return failure(409, "An instance with this name already exists in the requested provider scope and region.");

    ProcessResult process = runProviderCli(provider, args, 60000);
    Json providerResponse;
    if (!parseOutput(provider, process, providerResponse)) return runFailure(provider, process);
    return success("Provider accepted instance creation. Provider state and post-boot host/service health require verification.",
                   {{"provider", provider}, {"scope", scope}, {"region", region}, {"name", name},
                    {"provider_response", providerResponse}, {"service_health", "not-checked"},
                    {"dependency_preflight", "not-configured"}});
}

ProviderComputeResult ProviderCompute::action(const Json& request) {
    if (!request.is_object()) return failure(400, "Request body must be a JSON object.");
    const std::string provider = lower(request.value("provider", std::string{}));
    const std::string scope = request.value("scope", std::string{});
    const std::string region = request.value("region", std::string{});
    const std::string id = request.value("instance_id", std::string{});
    const std::string actionName = lower(request.value("action", std::string{}));
    const std::string requestId = request.value("request_id", std::string{});
    if (!matches(requestId, "[A-Za-z0-9][A-Za-z0-9-]{14,62}[A-Za-z0-9]")) {
        return failure(400, "request_id must contain 16 to 64 letters, digits or hyphens and start and end with a letter or digit.");
    }
    if (actionName != "restart" && actionName != "start" && actionName != "stop" && actionName != "delete") {
        return failure(400, "action must be one of: restart, start, stop, delete.");
    }
    if (provider == "gcp") {
        const std::string zone = request.value("zone", std::string{});
        if (!safeName(zone)) return failure(400, "GCP actions require a valid zone.");
    } else if (!validRegion(region)) {
        return failure(400, "region is required and must be a valid provider region.");
    }
    if (!safeResourceId(provider, id)) return failure(400, "instance_id is not a valid canonical resource identifier for the provider.");
    if (provider == "azure" && !expectedAzureResourceId(id, scope)) {
        return failure(403, "Azure instance ID does not belong to the requested subscription scope.");
    }
    ProviderComputeResult policy = validateMutation(actionName);
    if (!policy.success) return policy;
    ProviderComputeResult context = requireContext(provider, scope);
    if (!context.success) return context;
    ProviderComputeResult identity = verifyIdentity(provider, scope);
    if (!identity.success) return identity;

    Json instances;
    ProviderComputeResult inventory = listUnchecked(provider, scope, region, instances);
    if (!inventory.success) return inventory;
    auto target = std::find_if(instances.begin(), instances.end(), [&](const Json& item) {
        return item.value("id", std::string{}) == id;
    });
    if (target == instances.end()) return failure(404, "The exact provider instance was not found in the verified scope and region.");
    if (actionName == "restart" && normalizedState(provider, *target) != "RUNNING") {
        return failure(409, "Restart is only valid for an instance currently reported as running.");
    }
    if (actionName == "start" && normalizedState(provider, *target) != "STOPPED") {
        return failure(409, "Start is only valid for an instance currently reported as stopped.");
    }
    if (actionName == "stop" && normalizedState(provider, *target) != "RUNNING") {
        return failure(409, "Stop is only valid for an instance currently reported as running.");
    }

    const std::vector<std::string> args = actionArguments(provider, scope, request, actionName, id, region);
    ProcessResult process = runProviderCli(provider, args, 60000);
    if (process.exitCode != 0 || process.timedOut || process.outputTooLarge) return runFailure(provider, process);

    Json verification = nullptr;
    ProviderComputeResult verified = verifyExpectedState(provider, scope, region, request, id, actionName, verification);
    if (!verified.success) return verified;
    return success("Provider action completed and provider state was verified; dependent service health remains a separate gate.",
                   {{"provider", provider}, {"scope", scope}, {"instance", std::move(verification)},
                    {"action", actionName}, {"provider_state_verified", true}, {"host_health", "not-checked"},
                    {"dependent_service_health", "not-checked"}, {"dependency_preflight", "not-configured"}});
}

} // namespace NMC::Server
