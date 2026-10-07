// Continuum provider-scoped compute commands.
#include "ProviderCommands.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {

constexpr std::uintmax_t MAX_PROVIDER_SPEC_BYTES = 1024 * 1024;

// Read bounded JSON input without echoing user data into errors or logs.
bool loadProviderSpec(const std::string& path, nlohmann::json& spec, std::string& error) {
    std::error_code fileError;
    const auto fileStatus = std::filesystem::symlink_status(path, fileError);
    if (fileError || !std::filesystem::is_regular_file(fileStatus)) {
        error = "--spec-file must name a regular file and cannot be a symbolic link.";
        return false;
    }
    const auto fileSize = std::filesystem::file_size(path, fileError);
    if (fileError || fileSize > MAX_PROVIDER_SPEC_BYTES) {
        error = "--spec-file must be readable and no larger than 1 MiB.";
        return false;
    }
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input) {
        error = "Could not open --spec-file.";
        return false;
    }
    std::string contents(static_cast<std::size_t>(MAX_PROVIDER_SPEC_BYTES + 1), '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    const auto bytesRead = input.gcount();
    if (bytesRead < 0 || static_cast<std::uintmax_t>(bytesRead) > MAX_PROVIDER_SPEC_BYTES ||
        input.bad() || (input.fail() && !input.eof())) {
        error = "Could not read a valid bounded --spec-file.";
        return false;
    }
    contents.resize(static_cast<std::size_t>(bytesRead));
    try {
        spec = nlohmann::json::parse(contents);
    } catch (const nlohmann::json::exception&) {
        error = "--spec-file must contain valid JSON.";
        return false;
    }
    if (!spec.is_object()) {
        error = "--spec-file must contain a JSON object.";
        return false;
    }
    return true;
}

} // namespace

namespace NMC::Commands {

ProviderCommand::ProviderCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("provider", "Inspect and manage explicitly scoped cloud provider resources", std::move(client)) {
    usage = "nmc provider compute [command]";
}

int ProviderCommand::execute(const std::map<std::string, std::string>&,
                             const std::vector<std::string>&,
                             const CLI::GlobalFlags&) {
    printHelp();
    return 0;
}

ProviderComputeCommand::ProviderComputeCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("compute", "Manage AWS, GCP and Azure compute instances", std::move(client)) {
    usage = "nmc provider compute [command]";
}

int ProviderComputeCommand::execute(const std::map<std::string, std::string>&,
                                    const std::vector<std::string>&,
                                    const CLI::GlobalFlags&) {
    printHelp();
    return 0;
}

ProviderComputeStatusCommand::ProviderComputeStatusCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("status", "Show configured provider adapter readiness", std::move(client)) {
    usage = "nmc provider compute status";
    examples = "nmc provider compute status --output json";
}

int ProviderComputeStatusCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                          const std::vector<std::string>& parsedArgs,
                                          const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) return 1;
    const auto response = apiClient->getProviderComputeStatus();
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

ProviderComputeInstancesCommand::ProviderComputeInstancesCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("instances", "List provider instances within an allowlisted scope", std::move(client)) {
    usage = "nmc provider compute instances --provider aws --scope 123456789012 --region eu-west-2";
    examples = "nmc provider compute instances --provider gcp --scope project-id --region europe-west2 --output json";
    addFlag(CLI::Flag("p", "provider", "Provider: aws, gcp or azure", CLI::FlagType::String, true));
    addFlag(CLI::Flag("s", "scope", "AWS account ID, GCP project ID or Azure subscription ID", CLI::FlagType::String, true));
    addFlag(CLI::Flag("r", "region", "Provider region (required for AWS and Azure)", CLI::FlagType::String, false));
}

int ProviderComputeInstancesCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                             const std::vector<std::string>& parsedArgs,
                                             const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) return 1;
    const auto response = apiClient->listProviderInstances(
            parsedFlags.at("provider"), parsedFlags.at("scope"),
            parsedFlags.count("region") ? parsedFlags.at("region") : "");
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

ProviderComputeCreateCommand::ProviderComputeCreateCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("create", "Create a scoped compute instance from a provider-specific JSON spec", std::move(client)) {
    usage = "nmc provider compute create --provider aws --scope 123456789012 --spec-file aws-instance.json --idempotency-key request-identifier";
    examples = "nmc provider compute create --provider azure --scope 00000000-0000-0000-0000-000000000000 --spec-file azure-instance.json --idempotency-key request-identifier";
    addFlag(CLI::Flag("p", "provider", "Provider: aws, gcp or azure", CLI::FlagType::String, true));
    addFlag(CLI::Flag("s", "scope", "Allowlisted provider account, project or subscription", CLI::FlagType::String, true));
    addFlag(CLI::Flag("f", "spec-file", "Bounded JSON file with explicit network, image and size settings", CLI::FlagType::String, true));
    addFlag(CLI::Flag("i", "idempotency-key", "Stable request identifier for retries", CLI::FlagType::String, true));
}

int ProviderComputeCreateCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                          const std::vector<std::string>& parsedArgs,
                                          const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) return 1;
    nlohmann::json spec;
    std::string error;
    if (!loadProviderSpec(parsedFlags.at("spec-file"), spec, error)) {
        std::cerr << "Error: " << error << std::endl;
        return 1;
    }
    const nlohmann::json request{
        {"provider", parsedFlags.at("provider")},
        {"scope", parsedFlags.at("scope")},
        {"spec", std::move(spec)},
        {"idempotency_key", parsedFlags.at("idempotency-key")}
    };
    const auto response = apiClient->createProviderInstance(request);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

ProviderComputeActionCommand::ProviderComputeActionCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("action", "Restart, start, stop or delete one exact provider instance", std::move(client)) {
    usage = "nmc provider compute action --provider aws --scope 123456789012 --region eu-west-2 --instance-id i-0123456789abcdef0 --action restart";
    examples = "nmc provider compute action --provider gcp --scope project-id --zone europe-west2-a --instance-id worker-01 --action stop";
    addFlag(CLI::Flag("p", "provider", "Provider: aws, gcp or azure", CLI::FlagType::String, true));
    addFlag(CLI::Flag("s", "scope", "Allowlisted provider account, project or subscription", CLI::FlagType::String, true));
    addFlag(CLI::Flag("r", "region", "Provider region (required for AWS and Azure)", CLI::FlagType::String, false));
    addFlag(CLI::Flag("z", "zone", "GCP zone (required for GCP)", CLI::FlagType::String, false));
    addFlag(CLI::Flag("i", "instance-id", "Exact provider instance ID; Azure requires the canonical resource ID", CLI::FlagType::String, true));
    addFlag(CLI::Flag("a", "action", "Action: restart, start, stop or delete", CLI::FlagType::String, true));
}

int ProviderComputeActionCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                          const std::vector<std::string>& parsedArgs,
                                          const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) return 1;
    nlohmann::json request{
        {"provider", parsedFlags.at("provider")},
        {"scope", parsedFlags.at("scope")},
        {"instance_id", parsedFlags.at("instance-id")},
        {"action", parsedFlags.at("action")}
    };
    if (parsedFlags.count("region")) request["region"] = parsedFlags.at("region");
    if (parsedFlags.count("zone")) request["zone"] = parsedFlags.at("zone");
    const auto response = apiClient->actOnProviderInstance(request);
    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

} // namespace NMC::Commands
