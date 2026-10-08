// Fail-closed provider dependency and health checks owned by Continuum.
#include "ProviderDependencyPreflight.h"

#include <httplib.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <future>
#include <iomanip>
#include <openssl/sha.h>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace NMC::Server {
namespace {

using Json = nlohmann::json;
constexpr std::size_t MaximumPolicyBytes = 1024 * 1024;
constexpr std::size_t MaximumProbesPerGroup = 16;
constexpr std::size_t MaximumHealthChecksPerBinding = 16;
constexpr std::size_t MaximumHealthResponseBytes = 4096;

ProviderComputeResult failure(int status, const std::string& message, Json data = Json::object()) {
    return {false, status, message, std::move(data)};
}

ProviderComputeResult success(const std::string& message, Json data = Json::object()) {
    return {true, 200, message, std::move(data)};
}

bool matches(const std::string& value, const char* expression) {
    return std::regex_match(value, std::regex(expression));
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool allowedKeys(const Json& object, std::initializer_list<const char*> allowed) {
    if (!object.is_object()) return false;
    for (auto item = object.begin(); item != object.end(); ++item) {
        const bool known = std::any_of(allowed.begin(), allowed.end(), [&](const char* key) {
            return item.key() == key;
        });
        if (!known) return false;
    }
    return true;
}

bool validScope(const std::string& provider, const std::string& scope) {
    if (provider == "aws") return matches(scope, "[0-9]{12}");
    if (provider == "gcp") return matches(scope, "[a-z][a-z0-9-]{3,28}[a-z0-9]");
    if (provider == "azure") return matches(scope, "[A-Fa-f0-9]{8}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{12}");
    return false;
}

bool validResourceId(const std::string& provider, const std::string& id) {
    if (provider == "aws") return matches(id, "i-[A-Fa-f0-9]{8,17}");
    if (provider == "gcp") return matches(id, "[A-Za-z0-9][A-Za-z0-9._-]{0,62}");
    if (provider == "azure") {
        return matches(id, "/subscriptions/[A-Fa-f0-9]{8}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{12}/resourceGroups/[A-Za-z0-9._()-]{1,90}/providers/Microsoft\\.Compute/virtualMachines/[A-Za-z0-9._-]{1,64}");
    }
    return false;
}

bool validLocation(const std::string& location) {
    return matches(location, "[A-Za-z0-9][A-Za-z0-9._-]{0,63}");
}

bool safeHealthUrl(const std::string& url) {
    if (url.empty() || url.size() > 2048 || url.find_first_of(" \t\r\n\\@?#") != std::string::npos) return false;
    const auto separator = url.find("://");
    if (separator == std::string::npos) return false;
    const std::string scheme = lower(url.substr(0, separator));
    if (scheme != "https" && scheme != "http") return false;
    const std::size_t authorityStart = separator + 3;
    const std::size_t pathStart = url.find('/', authorityStart);
    if (pathStart == std::string::npos) return false;
    const std::string authority = url.substr(authorityStart, pathStart - authorityStart);
    const std::string path = url.substr(pathStart);
    if (authority.empty() || authority.size() > 255 || path.size() > 1024 || path.find("..") != std::string::npos
        || !matches(path, "/[A-Za-z0-9._~/-]*")) return false;

    std::string host = authority;
    const auto portSeparator = authority.rfind(':');
    if (portSeparator != std::string::npos && authority.find(']') == std::string::npos) {
        const std::string port = authority.substr(portSeparator + 1);
        if (!matches(port, "[0-9]{1,5}")) return false;
        const long number = std::strtol(port.c_str(), nullptr, 10);
        if (number < 1 || number > 65535) return false;
        host = authority.substr(0, portSeparator);
    } else if (authority.front() == '[') {
        const auto closing = authority.find(']');
        if (closing == std::string::npos) return false;
        host = authority.substr(0, closing + 1);
        if (closing + 1 < authority.size()) {
            const std::string port = authority.substr(closing + 2);
            if (authority[closing + 1] != ':' || !matches(port, "[0-9]{1,5}")) return false;
            const long number = std::strtol(port.c_str(), nullptr, 10);
            if (number < 1 || number > 65535) return false;
        }
    }
    if (host.empty()) return false;
    if (host.front() == '[') {
        if (!matches(host, "\\[[0-9A-Fa-f:]{2,45}\\]")) return false;
    } else if (!matches(host, "[A-Za-z0-9][A-Za-z0-9.-]{0,252}")) {
        return false;
    }

    const std::string normalHost = lower(host);
    if (normalHost == "metadata.google.internal" || normalHost == "metadata" ||
        normalHost == "instance-data.ec2.internal" || normalHost == "169.254.169.254" ||
        normalHost == "168.63.129.16") return false;
    // Plain HTTP is permitted only for deterministic local probes. Production
    // health endpoints must use HTTPS with certificate verification enabled.
    if (scheme == "http" && normalHost != "127.0.0.1" && normalHost != "[::1]") return false;
    return true;
}

bool validProbe(const Json& probe) {
    return allowedKeys(probe, {"id", "url", "expected_status"})
        && probe.size() == 3
        && probe.contains("id") && probe["id"].is_string()
        && matches(probe["id"].get<std::string>(), "[A-Za-z0-9][A-Za-z0-9._:-]{0,63}")
        && probe.contains("url") && probe["url"].is_string()
        && safeHealthUrl(probe["url"].get<std::string>())
        && probe.contains("expected_status") && probe["expected_status"].is_number_integer()
        && probe["expected_status"].get<int>() == 200;
}

bool validProbeList(const Json& probes) {
    if (!probes.is_array() || probes.size() > MaximumProbesPerGroup) return false;
    std::set<std::string> ids;
    for (const auto& probe : probes) {
        if (!validProbe(probe) || !ids.insert(probe["id"].get<std::string>()).second) return false;
    }
    return true;
}

bool validBindingShape(const Json& binding, bool createProfile) {
    const auto fields = createProfile
        ? std::initializer_list<const char*>{"provider", "scope", "profile_id", "location", "host_health", "dependencies", "dependent_services"}
        : std::initializer_list<const char*>{"provider", "scope", "instance_id", "location", "host_health", "dependencies", "dependent_services", "allowed_actions"};
    if (!allowedKeys(binding, fields)) return false;
    for (const char* key : {"provider", "scope", "location"}) {
        if (!binding.contains(key) || !binding[key].is_string()) return false;
    }
    const std::string provider = binding["provider"].get<std::string>();
    if (provider != "aws" && provider != "gcp" && provider != "azure") return false;
    if (!validScope(provider, binding["scope"].get<std::string>()) || !validLocation(binding["location"].get<std::string>())) return false;
    const char* identityField = createProfile ? "profile_id" : "instance_id";
    if (!binding.contains(identityField) || !binding[identityField].is_string()) return false;
    const std::string identity = binding[identityField].get<std::string>();
    if (createProfile ? !matches(identity, "[A-Za-z0-9][A-Za-z0-9._-]{0,63}") : !validResourceId(provider, identity)) return false;
    if (!binding.contains("host_health") || !validProbe(binding["host_health"])) return false;
    if (!binding.contains("dependencies") || !validProbeList(binding["dependencies"])) return false;
    if (!binding.contains("dependent_services") || !validProbeList(binding["dependent_services"])) return false;
    if (1 + binding["dependencies"].size() + binding["dependent_services"].size() > MaximumHealthChecksPerBinding) return false;
    if (!createProfile) {
        if (!binding.contains("allowed_actions") || !binding["allowed_actions"].is_array() || binding["allowed_actions"].empty() || binding["allowed_actions"].size() > 4) return false;
        std::set<std::string> actions;
        for (const auto& action : binding["allowed_actions"]) {
            if (!action.is_string()) return false;
            const std::string value = action.get<std::string>();
            if ((value != "start" && value != "restart" && value != "stop" && value != "delete") || !actions.insert(value).second) return false;
        }
    }
    return true;
}

bool validatePolicyDocument(const Json& document) {
    if (!allowedKeys(document, {"schema_version", "dependency_graph_revision", "resources", "create_profiles"})
        || document.size() != 4 || !document.contains("schema_version")
        || !document["schema_version"].is_number_integer() || document["schema_version"].get<int>() != 1
        || !document.contains("dependency_graph_revision") || !document["dependency_graph_revision"].is_string()
        || !matches(document["dependency_graph_revision"].get<std::string>(), "[A-Za-z0-9][A-Za-z0-9._:-]{0,127}")
        || !document.contains("resources") || !document["resources"].is_array() || document["resources"].size() > 512
        || !document.contains("create_profiles") || !document["create_profiles"].is_array() || document["create_profiles"].size() > 128) return false;

    std::set<std::string> identities;
    for (const auto& item : document["resources"]) {
        if (!validBindingShape(item, false)) return false;
        const std::string key = item["provider"].get<std::string>() + "\n" + item["scope"].get<std::string>()
            + "\n" + item["instance_id"].get<std::string>() + "\n" + item["location"].get<std::string>();
        if (!identities.insert(key).second) return false;
    }
    std::set<std::string> profiles;
    for (const auto& item : document["create_profiles"]) {
        if (!validBindingShape(item, true)) return false;
        const std::string key = item["provider"].get<std::string>() + "\n" + item["scope"].get<std::string>()
            + "\n" + item["profile_id"].get<std::string>() + "\n" + item["location"].get<std::string>();
        if (!profiles.insert(key).second) return false;
    }
    return true;
}

std::string sha256(const std::string& input) {
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest.data());
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const unsigned char byte : digest) output << std::setw(2) << static_cast<unsigned int>(byte);
    return output.str();
}

bool loadPolicy(Json& document, std::string& revision, std::string& policyFingerprint, std::string& error) {
    const char* configuredPath = std::getenv("NMC_PROVIDER_DEPENDENCY_POLICY_PATH");
    if (configuredPath == nullptr || *configuredPath == '\0') {
        error = "not-configured";
        return false;
    }
    const std::string path(configuredPath);
    if (path.empty() || path.front() != '/' || path.size() > 4096 || path.find("..") != std::string::npos) {
        error = "invalid-path";
        return false;
    }
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        error = "unavailable";
        return false;
    }
    struct stat metadata{};
    const bool trusted = ::fstat(fd, &metadata) == 0 && S_ISREG(metadata.st_mode)
        && metadata.st_nlink == 1 && metadata.st_uid == ::geteuid()
        && (metadata.st_mode & 0777) == 0600 && metadata.st_size > 0
        && static_cast<uint64_t>(metadata.st_size) <= MaximumPolicyBytes;
    if (!trusted) {
        ::close(fd);
        error = "insecure-file";
        return false;
    }
    std::string contents;
    contents.reserve(static_cast<std::size_t>(metadata.st_size));
    std::array<char, 8192> buffer{};
    while (true) {
        const ssize_t count = ::read(fd, buffer.data(), buffer.size());
        if (count == 0) break;
        if (count < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            error = "read-failed";
            return false;
        }
        if (contents.size() + static_cast<std::size_t>(count) > MaximumPolicyBytes) {
            ::close(fd);
            error = "oversized-file";
            return false;
        }
        contents.append(buffer.data(), static_cast<std::size_t>(count));
    }
    ::close(fd);
    try {
        document = Json::parse(contents);
    } catch (const Json::exception&) {
        error = "invalid-json";
        return false;
    }
    if (!validatePolicyDocument(document)) {
        error = "invalid-policy";
        return false;
    }
    revision = document["dependency_graph_revision"].get<std::string>();
    // Pin the canonical full policy as well as its operator-managed revision.
    // This detects edits that accidentally retain the same revision label.
    policyFingerprint = sha256(document.dump());
    return true;
}

ProviderComputeResult policyFailure(const std::string& error) {
    if (error == "not-configured") {
        return failure(423, "Provider dependency preflight is not configured; no provider mutation was accepted.",
                       {{"dependency_preflight", "not-configured"}});
    }
    return failure(503, "The private provider dependency policy is unavailable, insecure or invalid; no provider mutation was accepted.",
                   {{"dependency_preflight", "blocked"}, {"policy_error", error}});
}

bool probeHealth(const Json& probe) {
    if (!validProbe(probe)) return false;
    const std::string url = probe["url"].get<std::string>();
    const std::size_t separator = url.find("://");
    const std::string scheme = lower(url.substr(0, separator));
    const std::size_t authorityStart = separator + 3;
    const std::size_t pathStart = url.find('/', authorityStart);
    const std::string authority = url.substr(authorityStart, pathStart - authorityStart);
    const std::string baseUrl = scheme + "://" + authority;
    const std::string path = url.substr(pathStart);
    httplib::Client client(baseUrl);
    if (!client.is_valid()) return false;
    client.set_connection_timeout(1, 0);
    client.set_read_timeout(1, 0);
    client.set_write_timeout(1, 0);
    client.set_keep_alive(false);
    client.set_follow_location(false);
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    if (scheme == "https") {
        client.enable_server_certificate_verification(true);
        client.enable_server_hostname_verification(true);
    }
#endif
    std::size_t received = 0;
    bool oversized = false;
    const auto response = client.Get(path, [&](const char*, std::size_t length) {
        if (received + length > MaximumHealthResponseBytes) {
            oversized = true;
            return false;
        }
        received += length;
        return true;
    });
    return response && !oversized && response->status == probe["expected_status"].get<int>();
}

Json checkProbeGroup(const Json& probes) {
    Json checks = Json::array();
    if (probes.empty()) return checks;
    std::vector<std::future<bool>> futures;
    futures.reserve(probes.size());
    for (const auto& probe : probes) {
        futures.push_back(std::async(std::launch::async, [probe]() { return probeHealth(probe); }));
    }
    for (std::size_t index = 0; index < probes.size(); ++index) {
        bool healthy = false;
        try {
            healthy = futures[index].get();
        } catch (const std::exception&) {
            healthy = false;
        }
        checks.push_back({{"id", probes[index]["id"]}, {"healthy", healthy}});
    }
    return checks;
}

Json failedProbeGroup(const Json& probes) {
    Json checks = Json::array();
    for (const auto& probe : probes) checks.push_back({{"id", probe.value("id", std::string{})}, {"healthy", false}});
    return checks;
}

Json collectProbeGroup(std::future<Json>& future, bool started, const Json& probes) {
    if (probes.empty()) return Json::array();
    if (!started) return failedProbeGroup(probes);
    try {
        return future.get();
    } catch (...) {
        return failedProbeGroup(probes);
    }
}

bool allHealthy(const Json& checks) {
    return std::all_of(checks.begin(), checks.end(), [](const Json& check) {
        return check.value("healthy", false);
    });
}

Json baseEvidence(const Json& binding) {
    return {{"dependency_graph_revision", binding.value("_dependency_graph_revision", std::string{})},
            {"dependency_preflight", "passed"}};
}

ProviderComputeResult runChecks(const Json& binding, bool checkHost, bool checkServices, bool checkDependencies) {
    Json evidence = baseEvidence(binding);
    const Json dependencies = checkDependencies ? binding.value("dependencies", Json::array()) : Json::array();
    const Json hostProbes = checkHost ? Json::array({binding.at("host_health")}) : Json::array();
    const Json services = checkServices ? binding.value("dependent_services", Json::array()) : Json::array();
    std::future<Json> dependencyFuture;
    std::future<Json> hostFuture;
    std::future<Json> serviceFuture;
    bool dependencyStarted = false;
    bool hostStarted = false;
    bool serviceStarted = false;
    try {
        if (!dependencies.empty()) {
            dependencyFuture = std::async(std::launch::async, [dependencies]() { return checkProbeGroup(dependencies); });
            dependencyStarted = true;
        }
    } catch (...) {
        dependencyStarted = false;
    }
    try {
        if (!hostProbes.empty()) {
            hostFuture = std::async(std::launch::async, [hostProbes]() { return checkProbeGroup(hostProbes); });
            hostStarted = true;
        }
    } catch (...) {
        hostStarted = false;
    }
    try {
        if (!services.empty()) {
            serviceFuture = std::async(std::launch::async, [services]() { return checkProbeGroup(services); });
            serviceStarted = true;
        }
    } catch (...) {
        serviceStarted = false;
    }
    const Json dependencyChecks = collectProbeGroup(dependencyFuture, dependencyStarted, dependencies);
    const Json hostChecks = collectProbeGroup(hostFuture, hostStarted, hostProbes);
    const Json serviceChecks = collectProbeGroup(serviceFuture, serviceStarted, services);
    const bool dependenciesHealthy = !checkDependencies || allHealthy(dependencyChecks);
    const bool hostHealthy = !checkHost || allHealthy(hostChecks);
    const bool servicesHealthy = !checkServices || allHealthy(serviceChecks);
    evidence["dependency_health"] = !checkDependencies ? "not-checked" : (binding.value("dependencies", Json::array()).empty() ? "not-applicable" : (dependenciesHealthy ? "healthy" : "failed"));
    evidence["host_health"] = !checkHost ? "not-checked" : (hostHealthy ? "healthy" : "failed");
    evidence["dependent_service_health"] = !checkServices ? "not-checked" : (binding.value("dependent_services", Json::array()).empty() ? "not-applicable" : (servicesHealthy ? "healthy" : "failed"));
    evidence["checks"] = {{"dependencies", dependencyChecks}, {"host", hostChecks}, {"dependent_services", serviceChecks}};
    if (!dependenciesHealthy || !hostHealthy || !servicesHealthy) {
        evidence["dependency_preflight"] = "blocked";
        return failure(503, "A required live dependency, host or dependent-service health check failed; no further provider mutation was launched.", std::move(evidence));
    }
    return success("Configured dependency and health checks passed.", std::move(evidence));
}

ProviderComputeResult resolve(const std::string& providerInput,
                              const std::string& scope,
                              const std::string& identity,
                              const std::string& location,
                              const std::string& action,
                              bool createProfile,
                              Json& binding) {
    Json document;
    std::string revision;
    std::string policyFingerprint;
    std::string error;
    if (!loadPolicy(document, revision, policyFingerprint, error)) return policyFailure(error);
    const Json& list = document[createProfile ? "create_profiles" : "resources"];
    const std::string provider = lower(providerInput);
    const char* identityField = createProfile ? "profile_id" : "instance_id";
    const auto found = std::find_if(list.begin(), list.end(), [&](const Json& item) {
        return item.value("provider", std::string{}) == provider
            && item.value("scope", std::string{}) == scope
            && item.value(identityField, std::string{}) == identity
            && item.value("location", std::string{}) == location;
    });
    if (found == list.end()) {
        return failure(409, "No exact provider dependency policy is registered for this resource, scope and location.",
                       {{"dependency_preflight", "blocked"}, {"host_health", "not-checked"}, {"dependent_service_health", "not-checked"}});
    }
    binding = *found;
    binding["_dependency_graph_revision"] = revision;
    binding["_dependency_policy_fingerprint"] = policyFingerprint;
    if (!createProfile) {
        const Json actions = binding["allowed_actions"];
        if (std::find(actions.begin(), actions.end(), action) == actions.end()) {
            return failure(423, "The provider dependency policy does not authorise this lifecycle action.",
                           {{"dependency_preflight", "blocked"}});
        }
        if ((action == "stop" || action == "delete") && !binding["dependent_services"].empty()) {
            return failure(409, "Stop and delete are blocked while the resource has registered dependent services; use an approved maintenance or teardown plan.",
                           {{"dependency_preflight", "blocked"}, {"dependent_service_health", "not-checked"}});
        }
    }
    return success("An exact provider dependency policy is configured.",
                   {{"dependency_graph_revision", revision}, {"dependency_policy_fingerprint", policyFingerprint},
                    {"dependency_preflight", "configured"}});
}

} // namespace

nlohmann::json ProviderDependencyPreflight::status() {
    Json document;
    std::string revision;
    std::string policyFingerprint;
    std::string error;
    if (!loadPolicy(document, revision, policyFingerprint, error)) {
        return {{"dependency_preflight_configured", false}, {"dependency_policy_error", error}};
    }
    Json resourceBindings{{"aws", 0}, {"gcp", 0}, {"azure", 0}};
    Json createProfiles{{"aws", 0}, {"gcp", 0}, {"azure", 0}};
    for (const auto& binding : document["resources"]) {
        const std::string provider = binding["provider"].get<std::string>();
        resourceBindings[provider] = resourceBindings[provider].get<int>() + 1;
    }
    for (const auto& profile : document["create_profiles"]) {
        const std::string provider = profile["provider"].get<std::string>();
        createProfiles[provider] = createProfiles[provider].get<int>() + 1;
    }
    return {{"dependency_preflight_configured", true}, {"dependency_graph_revision", revision},
            {"resource_bindings", std::move(resourceBindings)}, {"create_profiles", std::move(createProfiles)}};
}

ProviderComputeResult ProviderDependencyPreflight::resolveAction(const std::string& provider,
                                                                 const std::string& scope,
                                                                 const std::string& instanceId,
                                                                 const std::string& location,
                                                                 const std::string& action,
                                                                 nlohmann::json& binding) {
    return resolve(provider, scope, instanceId, location, action, false, binding);
}

ProviderComputeResult ProviderDependencyPreflight::resolveCreate(const std::string& provider,
                                                                 const std::string& scope,
                                                                 const nlohmann::json& specification,
                                                                 nlohmann::json& binding) {
    if (!specification.is_object() || !specification.contains("health_profile") || !specification["health_profile"].is_string()) {
        return failure(400, "spec.health_profile must select a server-configured provider health profile.",
                       {{"dependency_preflight", "not-configured"}});
    }
    const std::string profile = specification["health_profile"].get<std::string>();
    const std::string location = provider == "gcp"
        ? specification.value("zone", std::string{}) : specification.value("region", std::string{});
    if (!matches(profile, "[A-Za-z0-9][A-Za-z0-9._-]{0,63}")) {
        return failure(400, "spec.health_profile is invalid.", {{"dependency_preflight", "blocked"}});
    }
    return resolve(provider, scope, profile, location, {}, true, binding);
}

ProviderComputeResult ProviderDependencyPreflight::checkBeforeMutation(const nlohmann::json& binding,
                                                                       bool checkHost,
                                                                       bool checkDependentServices) {
    ProviderComputeResult current = verifyCurrentPolicy(binding);
    if (!current.success) return current;
    return runChecks(binding, checkHost, checkDependentServices, true);
}

ProviderComputeResult ProviderDependencyPreflight::checkAfterMutation(const nlohmann::json& binding,
                                                                      bool checkHost,
                                                                      bool checkDependentServices) {
    const char* configured = std::getenv("NMC_PROVIDER_HEALTH_TIMEOUT_SECONDS");
    long timeout = 180;
    if (configured != nullptr && *configured != '\0') {
        char* end = nullptr;
        errno = 0;
        timeout = std::strtol(configured, &end, 10);
        if (errno != 0 || end == configured || *end != '\0' || timeout < 1 || timeout > 300) {
            return failure(503, "NMC_PROVIDER_HEALTH_TIMEOUT_SECONDS must be an integer from 1 to 300.",
                           {{"dependency_preflight", "blocked"}});
        }
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
    ProviderComputeResult result;
    do {
        ProviderComputeResult current = verifyCurrentPolicy(binding);
        if (!current.success) return current;
        result = runChecks(binding, checkHost, checkDependentServices, true);
        if (result.success) {
            current = verifyCurrentPolicy(binding);
            return current.success ? result : current;
        }
        if (std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    result.message = "The provider resource exists, but dependency, host or dependent-service health did not recover before the configured timeout; reconcile the resource before retrying.";
    result.httpStatus = 503;
    return result;
}

ProviderComputeResult ProviderDependencyPreflight::verifyRevision(const nlohmann::json& binding,
                                                                  const std::string& expectedRevision,
                                                                  const std::string& expectedPolicyFingerprint) {
    if (expectedRevision.empty() || expectedPolicyFingerprint.empty()
        || binding.value("_dependency_graph_revision", std::string{}) != expectedRevision
        || binding.value("_dependency_policy_fingerprint", std::string{}) != expectedPolicyFingerprint) {
        return failure(409, "The provider dependency graph changed after job submission; no provider mutation was launched.",
                       {{"dependency_preflight", "stale"}});
    }
    return success("The provider dependency graph still matches the queued job.",
                   {{"dependency_graph_revision", expectedRevision},
                    {"dependency_policy_fingerprint", expectedPolicyFingerprint}});
}

ProviderComputeResult ProviderDependencyPreflight::verifyCurrentPolicy(const nlohmann::json& binding) {
    Json document;
    std::string revision;
    std::string policyFingerprint;
    std::string error;
    if (!loadPolicy(document, revision, policyFingerprint, error)) {
        return failure(503, "The provider dependency policy became unavailable or invalid; the operation is not treated as successful and requires reconciliation.",
                       {{"dependency_preflight", "blocked"}, {"policy_error", error}});
    }
    if (binding.value("_dependency_graph_revision", std::string{}) != revision
        || binding.value("_dependency_policy_fingerprint", std::string{}) != policyFingerprint) {
        return failure(409, "The provider dependency policy changed; the operation is not treated as successful and requires reconciliation.",
                       {{"dependency_preflight", "stale"}});
    }
    return success("The provider dependency policy still matches the validated binding.",
                   {{"dependency_graph_revision", revision}});
}

} // namespace NMC::Server
