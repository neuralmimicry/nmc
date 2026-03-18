// nmc_client/src/Commands/OpenShiftCommands.cpp
#include "OpenShiftCommands.h"
#include <iostream>
#include <chrono>
#include <cctype>
#include <set>
#include <sstream>
#include <thread>
#include <nlohmann/json.hpp>

namespace NMC::Commands {

    namespace {
        std::string toLower(std::string value) {
            for (auto& ch : value) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return value;
        }

        std::string trim(const std::string& value) {
            const auto start = value.find_first_not_of(" \t");
            if (start == std::string::npos) {
                return "";
            }
            const auto end = value.find_last_not_of(" \t");
            return value.substr(start, end - start + 1);
        }

        std::vector<std::string> splitCommaList(const std::string& input) {
            std::vector<std::string> out;
            std::stringstream ss(input);
            std::string item;
            while (std::getline(ss, item, ',')) {
                const std::string trimmed = trim(item);
                if (!trimmed.empty()) {
                    out.push_back(trimmed);
                }
            }
            return out;
        }

        std::string normalizeStatus(const std::string& rawStatus) {
            const std::string status = toLower(trim(rawStatus));
            if (status == "ready" || status == "running") {
                return "Ready";
            }
            if (status == "failed" || status == "error" || status == "unhealthy") {
                return "Failed";
            }
            if (status == "pending" || status == "accepted" || status == "queued" || status == "requested") {
                return "Pending";
            }
            if (status == "provisioning" || status == "gitops-syncing" || status == "gitops_syncing"
                || status == "syncing" || status == "installing" || status == "creating") {
                return "Provisioning";
            }
            return "Unknown";
        }

        std::string normalizeTarget(const std::string& rawTarget) {
            const std::string target = toLower(trim(rawTarget));
            if (target == "ready") {
                return "Ready";
            }
            if (target == "failed") {
                return "Failed";
            }
            if (target == "pending") {
                return "Pending";
            }
            if (target == "provisioning") {
                return "Provisioning";
            }
            if (target == "unknown") {
                return "Unknown";
            }
            return "";
        }
    }

// --- OpenShiftCommand (Parent) ---
    OpenShiftCommand::OpenShiftCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
            : BaseCommand("openshift", "Manage OpenShift resources via NeuralMimicry Continuum", std::move(client)) {
        usage = "nmc openshift [command]";
        addAlias("ocp");
        addAlias("oshift");
    }

    int OpenShiftCommand::execute(const std::map<std::string, std::string>&,
                                  const std::vector<std::string>&,
                                  const CLI::GlobalFlags&) {
        printHelp();
        return 0;
    }

// --- OpenShiftResourcesCommand ---
    OpenShiftResourcesCommand::OpenShiftResourcesCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
            : BaseCommand("resources", "Show available OpenShift capacity", std::move(client)) {
        usage = "nmc openshift resources";
        examples = "nmc openshift resources";
    }

    int OpenShiftResourcesCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                           const std::vector<std::string>& parsedArgs,
                                           const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }
        Models::CloudResponse response = apiClient->listOpenShiftResources();
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- OpenShiftClustersCommand ---
    OpenShiftClustersCommand::OpenShiftClustersCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
            : BaseCommand("clusters", "List OpenShift clusters", std::move(client)) {
        usage = "nmc openshift clusters";
        examples = "nmc openshift clusters";
    }

    int OpenShiftClustersCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                          const std::vector<std::string>& parsedArgs,
                                          const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }
        Models::CloudResponse response = apiClient->listOpenShiftClusters();
        if (response.success && response.data.is_array()) {
            nlohmann::json normalized = nlohmann::json::array();
            for (auto& cluster : response.data) {
                if (cluster.is_object() && cluster.contains("status") && cluster["status"].is_string()) {
                    cluster["status"] = normalizeStatus(cluster["status"].get<std::string>());
                }
                normalized.push_back(cluster);
            }
            response.data = normalized;
        }
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- OpenShiftRequestCommand ---
    OpenShiftRequestCommand::OpenShiftRequestCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
            : BaseCommand("request", "Request a new OpenShift cluster", std::move(client)) {
        usage = "nmc openshift request NAME --org ORG --gpu-count 8 --arch amd64 --region us-east-1 --provider rosa";
        examples = "nmc openshift request hpc-example --org neuralmimicry --gpu-count 8 --arch amd64 --region us-east-1 --provider rosa";
        addArgument(CLI::Argument("NAME", "Cluster name", true, 0));
        addFlag(CLI::Flag("o", "org", "Organization name", CLI::FlagType::String, true));
        addFlag(CLI::Flag("g", "gpu-count", "Requested GPU count", CLI::FlagType::Int, true));
        addFlag(CLI::Flag("a", "arch", "Architecture: amd64 or arm64", CLI::FlagType::String, true));
        addFlag(CLI::Flag("r", "region", "Region or location", CLI::FlagType::String, true));
        addFlag(CLI::Flag("p", "provider", "Provider: on-prem, rosa, aro, gcp, hybrid-burst", CLI::FlagType::String, false));
        addFlag(CLI::Flag("b", "burst-targets", "Comma-separated burst targets for hybrid-burst (deprecated)", CLI::FlagType::String, false));
        addFlag(CLI::Flag("t", "burst-target", "Repeatable burst target for hybrid-burst", CLI::FlagType::String, false));
    }

    int OpenShiftRequestCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                         const std::vector<std::string>& parsedArgs,
                                         const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        const std::string& name = parsedArgs[0];
        const std::string organization = parsedFlags.count("org") ? parsedFlags.at("org") : "";
        const std::string gpuCountStr = parsedFlags.count("gpu-count") ? parsedFlags.at("gpu-count") : "";
        const std::string architecture = parsedFlags.count("arch") ? parsedFlags.at("arch") : "";
        const std::string region = parsedFlags.count("region") ? parsedFlags.at("region") : "";
        const std::string provider = parsedFlags.count("provider") ? parsedFlags.at("provider") : "on-prem";
        const std::string burstTargetsRaw = parsedFlags.count("burst-targets") ? parsedFlags.at("burst-targets") : "";
        const std::string burstTargetRaw = parsedFlags.count("burst-target") ? parsedFlags.at("burst-target") : "";

        if (name.empty() || organization.empty() || gpuCountStr.empty() || architecture.empty() || region.empty()) {
            std::cerr << "Error: name, org, gpu-count, arch, and region are required." << std::endl;
            printHelp();
            return 1;
        }

        int gpuCount = 0;
        try {
            gpuCount = std::stoi(gpuCountStr);
        } catch (const std::exception&) {
            std::cerr << "Error: gpu-count must be an integer." << std::endl;
            printHelp();
            return 1;
        }
        if (gpuCount <= 0) {
            std::cerr << "Error: gpu-count must be greater than zero." << std::endl;
            printHelp();
            return 1;
        }

        std::vector<std::string> burstTargets = splitCommaList(burstTargetsRaw);
        const std::vector<std::string> burstTargetsExtra = splitCommaList(burstTargetRaw);
        burstTargets.insert(burstTargets.end(), burstTargetsExtra.begin(), burstTargetsExtra.end());
        const std::set<std::string> allowedProviders = {"on-prem", "rosa", "aro", "gcp", "hybrid-burst"};
        if (!allowedProviders.count(provider)) {
            std::cerr << "Error: provider must be one of: on-prem, rosa, aro, gcp, hybrid-burst." << std::endl;
            printHelp();
            return 1;
        }
        if (provider == "hybrid-burst" && burstTargets.empty()) {
            std::cerr << "Error: burst-targets is required when provider is hybrid-burst." << std::endl;
            printHelp();
            return 1;
        }

        Models::CloudResponse response = apiClient->requestOpenShiftCluster(
                name,
                organization,
                gpuCount,
                architecture,
                region,
                provider,
                burstTargets
        );
        printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }

// --- OpenShiftStatusCommand ---
    OpenShiftStatusCommand::OpenShiftStatusCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
            : BaseCommand("status", "Get OpenShift cluster status (optionally poll)", std::move(client)) {
        usage = "nmc openshift status NAME [--watch] [--until Ready] [--interval 10] [--timeout 600]";
        examples = "nmc openshift status hpc-example --watch --until Ready --interval 10 --timeout 900";
        addArgument(CLI::Argument("NAME", "Cluster name", true, 0));
        addFlag(CLI::Flag("w", "watch", "Poll until status matches --until", CLI::FlagType::Bool, false));
        addFlag(CLI::Flag("u", "until", "Stop polling when status matches target (Pending, Provisioning, Ready, Failed, Unknown)", CLI::FlagType::String, false));
        addFlag(CLI::Flag("i", "interval", "Polling interval in seconds", CLI::FlagType::Int, false));
        addFlag(CLI::Flag("T", "timeout", "Polling timeout in seconds", CLI::FlagType::Int, false));
    }

    int OpenShiftStatusCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                        const std::vector<std::string>& parsedArgs,
                                        const CLI::GlobalFlags& globalFlags) {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        const std::string& name = parsedArgs[0];
        const bool watch = parsedFlags.count("watch") > 0;
        const std::string untilRaw = parsedFlags.count("until") ? parsedFlags.at("until") : "Ready";
        const std::string untilStatus = normalizeTarget(untilRaw);
        if (untilStatus.empty()) {
            std::cerr << "Error: until must be one of: Pending, Provisioning, Ready, Failed, Unknown." << std::endl;
            printHelp();
            return 1;
        }

        int intervalSeconds = 10;
        if (parsedFlags.count("interval")) {
            try {
                intervalSeconds = std::stoi(parsedFlags.at("interval"));
            } catch (const std::exception&) {
                std::cerr << "Error: interval must be an integer." << std::endl;
                printHelp();
                return 1;
            }
        }
        if (intervalSeconds <= 0) {
            std::cerr << "Error: interval must be greater than zero." << std::endl;
            printHelp();
            return 1;
        }

        int timeoutSeconds = 600;
        if (parsedFlags.count("timeout")) {
            try {
                timeoutSeconds = std::stoi(parsedFlags.at("timeout"));
            } catch (const std::exception&) {
                std::cerr << "Error: timeout must be an integer." << std::endl;
                printHelp();
                return 1;
            }
        }
        if (timeoutSeconds <= 0) {
            std::cerr << "Error: timeout must be greater than zero." << std::endl;
            printHelp();
            return 1;
        }

        const auto start = std::chrono::steady_clock::now();
        std::string lastStatus;
        Models::CloudResponse finalResponse;
        finalResponse.success = false;
        finalResponse.message = "";
        finalResponse.data = nlohmann::json::object();

        while (true) {
            Models::CloudResponse response = apiClient->listOpenShiftClusters();
            if (!response.success) {
                if (!watch) {
                    printOutput(response, globalFlags);
                    return 1;
                }
                if (globalFlags.outputFormat.empty()) {
                    std::cerr << "Warning: failed to fetch clusters, retrying..." << std::endl;
                }
            } else {
                nlohmann::json clusters = nlohmann::json::array();
                if (response.data.is_array()) {
                    clusters = response.data;
                } else if (response.data.is_object() && response.data.contains("data") && response.data["data"].is_array()) {
                    clusters = response.data["data"];
                }

                nlohmann::json found;
                for (const auto& cluster : clusters) {
                    if (cluster.contains("name") && cluster["name"].is_string() && cluster["name"].get<std::string>() == name) {
                        found = cluster;
                        break;
                    }
                }

                if (!found.is_null()) {
                    const std::string rawStatus = found.value("status", "Unknown");
                    const std::string status = normalizeStatus(rawStatus);
                    found["status"] = status;
                    if (globalFlags.outputFormat.empty() && watch && status != lastStatus) {
                        std::cout << "Cluster " << name << " status: " << status << std::endl;
                        lastStatus = status;
                    }

                    finalResponse.success = (status == untilStatus);
                    finalResponse.message = "Cluster '" + name + "' status: " + status;
                    finalResponse.data = found;

                    if (!watch) {
                        printOutput(finalResponse, globalFlags);
                        return finalResponse.success ? 0 : 1;
                    }

                    if (status == untilStatus) {
                        printOutput(finalResponse, globalFlags);
                        return 0;
                    }
                    if (status == "Failed") {
                        printOutput(finalResponse, globalFlags);
                        return 1;
                    }
                } else {
                    if (!watch) {
                        finalResponse.success = false;
                        finalResponse.message = "Cluster '" + name + "' not found.";
                        finalResponse.data = nlohmann::json::object();
                        printOutput(finalResponse, globalFlags);
                        return 1;
                    }
                    if (globalFlags.outputFormat.empty()) {
                        std::cout << "Cluster '" << name << "' not found yet. Retrying..." << std::endl;
                    }
                }
            }

            if (!watch) {
                break;
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start).count();
            if (elapsed >= timeoutSeconds) {
                finalResponse.success = false;
                finalResponse.message = "Timed out waiting for cluster '" + name + "' to reach " + untilStatus + " state.";
                if (finalResponse.data.is_null()) {
                    finalResponse.data = nlohmann::json::object();
                }
                printOutput(finalResponse, globalFlags);
                return 1;
            }
            std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
        }

        finalResponse.success = false;
        finalResponse.message = "Cluster '" + name + "' status unavailable.";
        finalResponse.data = nlohmann::json::object();
        printOutput(finalResponse, globalFlags);
        return 1;
    }

} // namespace NMC::Commands
