#include "GailCommands.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "BaseCommand.h"
#include "Core/GailClient.h"

namespace NMC::Commands {

namespace {

std::string trimCopy(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string toUpperCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

std::string optionalFlag(const std::map<std::string, std::string>& parsedFlags,
                         const std::string& flagName,
                         const std::string& fallback = "") {
    const auto it = parsedFlags.find(flagName);
    if (it == parsedFlags.end()) {
        return fallback;
    }
    return it->second;
}

bool hasFlag(const std::map<std::string, std::string>& parsedFlags, const std::string& flagName) {
    return parsedFlags.find(flagName) != parsedFlags.end();
}

NMC::Models::CloudResponse errorResponse(const std::string& message, int statusCode = 400) {
    return {false, message, nlohmann::json::object(), statusCode};
}

bool readTextFile(const std::string& path, std::string& contentOut, std::string& errorOut) {
    contentOut.clear();
    errorOut.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        errorOut = "Unable to open file: " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    contentOut = buffer.str();
    return true;
}

bool parseJsonFlagOrFile(const std::map<std::string, std::string>& parsedFlags,
                         const std::string& jsonFlag,
                         const std::string& fileFlag,
                         nlohmann::json& valueOut,
                         std::string& errorOut,
                         bool requireObject) {
    valueOut = nlohmann::json();
    errorOut.clear();

    const bool hasJson = hasFlag(parsedFlags, jsonFlag);
    const bool hasFile = hasFlag(parsedFlags, fileFlag);
    if (hasJson && hasFile) {
        errorOut = "Use either --" + jsonFlag + " or --" + fileFlag + ", not both.";
        return false;
    }
    if (!hasJson && !hasFile) {
        errorOut = "One of --" + jsonFlag + " or --" + fileFlag + " is required.";
        return false;
    }

    std::string raw = hasJson ? optionalFlag(parsedFlags, jsonFlag) : "";
    if (hasFile && !readTextFile(optionalFlag(parsedFlags, fileFlag), raw, errorOut)) {
        return false;
    }

    const auto parsed = nlohmann::json::parse(raw, nullptr, false);
    if (parsed.is_discarded()) {
        errorOut = "Invalid JSON in --" + (hasJson ? jsonFlag : fileFlag) + ".";
        return false;
    }
    if (requireObject && !parsed.is_object()) {
        errorOut = "--" + (hasJson ? jsonFlag : fileFlag) + " must contain a JSON object.";
        return false;
    }
    valueOut = parsed;
    return true;
}

bool loadRawValue(const std::map<std::string, std::string>& parsedFlags,
                  const std::string& valueFlag,
                  const std::string& fileFlag,
                  std::string& valueOut,
                  std::string& errorOut) {
    valueOut.clear();
    errorOut.clear();
    const bool hasValue = hasFlag(parsedFlags, valueFlag);
    const bool hasFile = hasFlag(parsedFlags, fileFlag);
    if (hasValue && hasFile) {
        errorOut = "Use either --" + valueFlag + " or --" + fileFlag + ", not both.";
        return false;
    }
    if (!hasValue && !hasFile) {
        return false;
    }
    if (hasValue) {
        valueOut = optionalFlag(parsedFlags, valueFlag);
        return true;
    }
    return readTextFile(optionalFlag(parsedFlags, fileFlag), valueOut, errorOut);
}

bool parsePositiveInt(const std::map<std::string, std::string>& parsedFlags,
                      const std::string& flagName,
                      int fallback,
                      int& valueOut,
                      std::string& errorOut) {
    errorOut.clear();
    valueOut = fallback;
    const auto it = parsedFlags.find(flagName);
    if (it == parsedFlags.end() || trimCopy(it->second).empty()) {
        return true;
    }
    try {
        valueOut = std::stoi(it->second);
    } catch (const std::exception&) {
        errorOut = "--" + flagName + " must be an integer.";
        return false;
    }
    if (valueOut <= 0) {
        errorOut = "--" + flagName + " must be greater than zero.";
        return false;
    }
    return true;
}

bool isSupportedMethod(const std::string& method) {
    return method == "GET"
           || method == "POST"
           || method == "PUT"
           || method == "PATCH"
           || method == "DELETE";
}

std::string guessMimeType(const std::string& filePath) {
    const std::string ext = std::filesystem::path(filePath).extension().string();
    if (ext == ".wav") {
        return "audio/wav";
    }
    if (ext == ".mp3") {
        return "audio/mpeg";
    }
    if (ext == ".m4a") {
        return "audio/mp4";
    }
    if (ext == ".flac") {
        return "audio/flac";
    }
    if (ext == ".ogg") {
        return "audio/ogg";
    }
    if (ext == ".webm") {
        return "audio/webm";
    }
    return "application/octet-stream";
}

class GailCommandBase : public CLI::Command {
public:
    GailCommandBase(const std::string& name, const std::string& description)
        : CLI::Command(name, description) {}

protected:
    void addClientFlags() {
        addFlag(CLI::Flag(
            "u",
            "base-url",
            "Gail base URL. Overrides NMC_GAIL_BASE_URL/GAIL_BASE_URL.",
            CLI::FlagType::String,
            false));
        addFlag(CLI::Flag(
            "t",
            "api-token",
            "Gail bearer token. Overrides NMC_GAIL_API_TOKEN/GAIL_API_TOKEN.",
            CLI::FlagType::String,
            false));
    }

    std::unique_ptr<Core::GailClient> createClient(const std::map<std::string, std::string>& parsedFlags,
                                                   std::string& errorOut) const {
        std::string baseUrl;
        std::string apiToken;
        if (!Core::GailClient::resolveConfiguration(
                optionalFlag(parsedFlags, "base-url"),
                optionalFlag(parsedFlags, "api-token"),
                baseUrl,
                apiToken,
                errorOut)) {
            return nullptr;
        }
        return std::make_unique<Core::GailClient>(baseUrl, apiToken);
    }

    int printAndReturn(const Models::CloudResponse& response,
                       const CLI::GlobalFlags& globalFlags) const {
        BaseCommand::printOutput(response, globalFlags);
        return response.success ? 0 : 1;
    }
};

class GailRootCommand final : public GailCommandBase {
public:
    GailRootCommand() : GailCommandBase("gail", "Call the Gail shared AI middleware service") {
        usage = "nmc gail [command]";
        examples = "nmc gail health --base-url https://gail.internal.example\n"
                   "nmc gail status --probe-engines";
    }

    int execute(const std::map<std::string, std::string>&,
                const std::vector<std::string>&,
                const CLI::GlobalFlags&) override {
        printHelp();
        return 0;
    }
};

class GailHealthCommand final : public GailCommandBase {
public:
    GailHealthCommand() : GailCommandBase("health", "Check Gail service health") {
        usage = "nmc gail health [--base-url URL] [--api-token TOKEN]";
        examples = "nmc gail health --base-url https://gail.internal.example";
        addClientFlags();
    }

    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        std::string error;
        auto client = createClient(parsedFlags, error);
        if (!client) {
            return printAndReturn(errorResponse(error), globalFlags);
        }
        Models::CloudResponse response = client->health();
        if (response.success) {
            response.message = "Gail service is healthy.";
        }
        return printAndReturn(response, globalFlags);
    }
};

class GailStatusCommand final : public GailCommandBase {
public:
    GailStatusCommand() : GailCommandBase("status", "Inspect Gail provider, specialist, and metrics status") {
        usage = "nmc gail status [--limit N] [--probe-engines] [--probe-providers] [--base-url URL] [--api-token TOKEN]";
        examples = "nmc gail status --limit 10 --probe-engines --probe-providers";
        addClientFlags();
        addFlag(CLI::Flag("l", "limit", "Maximum candidates to include in status output", CLI::FlagType::Int, false));
        addFlag(CLI::Flag("e", "probe-engines", "Probe neuromorphic engine reachability", CLI::FlagType::Bool, false));
        addFlag(CLI::Flag("p", "probe-providers", "Probe configured provider reachability", CLI::FlagType::Bool, false));
    }

    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        int limit = 20;
        std::string parseError;
        if (!parsePositiveInt(parsedFlags, "limit", 20, limit, parseError)) {
            return printAndReturn(errorResponse(parseError), globalFlags);
        }

        std::string error;
        auto client = createClient(parsedFlags, error);
        if (!client) {
            return printAndReturn(errorResponse(error), globalFlags);
        }

        const bool probeEngines = hasFlag(parsedFlags, "probe-engines");
        const bool probeProviders = hasFlag(parsedFlags, "probe-providers");
        Models::CloudResponse response = client->orchestrationStatus(limit, probeEngines, probeProviders);
        if (response.success) {
            response.message = "Gail orchestration status retrieved.";
        }
        return printAndReturn(response, globalFlags);
    }
};

class GailJsonEndpointCommand final : public GailCommandBase {
public:
    GailJsonEndpointCommand(std::string name,
                            std::string description,
                            std::string endpoint,
                            std::string usage,
                            std::string examples,
                            std::string successMessage)
        : GailCommandBase(std::move(name), std::move(description)),
          endpoint(std::move(endpoint)),
          successMessage(std::move(successMessage)) {
        this->usage = std::move(usage);
        this->examples = std::move(examples);
        addClientFlags();
        addFlag(CLI::Flag("j", "json", "Inline JSON request payload", CLI::FlagType::String, false));
        addFlag(CLI::Flag("f", "json-file", "Path to a JSON request payload file", CLI::FlagType::String, false));
    }

    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        nlohmann::json payload;
        std::string parseError;
        if (!parseJsonFlagOrFile(parsedFlags, "json", "json-file", payload, parseError, true)) {
            return printAndReturn(errorResponse(parseError), globalFlags);
        }

        std::string error;
        auto client = createClient(parsedFlags, error);
        if (!client) {
            return printAndReturn(errorResponse(error), globalFlags);
        }

        Models::CloudResponse response = client->postJson(endpoint, payload);
        if (response.success && !successMessage.empty()) {
            response.message = successMessage;
        }
        return printAndReturn(response, globalFlags);
    }

private:
    std::string endpoint;
    std::string successMessage;
};

class GailTranscribeCommand final : public GailCommandBase {
public:
    GailTranscribeCommand() : GailCommandBase("transcribe", "Proxy audio transcription through Gail") {
        usage = "nmc gail transcribe --provider PROVIDER --file PATH [--model MODEL] [--mime-type TYPE] [--provider-api-key KEY] [--provider-access-token TOKEN] [--provider-base-url URL] [--base-url URL] [--api-token TOKEN]";
        examples = "nmc gail transcribe --provider openai --file ./sample.wav --model gpt-4o-mini-transcribe";
        addClientFlags();
        addFlag(CLI::Flag("p", "provider", "Transcription provider name", CLI::FlagType::String, true));
        addFlag(CLI::Flag("f", "file", "Path to the audio file to upload", CLI::FlagType::String, true));
        addFlag(CLI::Flag("m", "model", "Optional provider model override", CLI::FlagType::String, false));
        addFlag(CLI::Flag("c", "mime-type", "Explicit MIME type for the upload", CLI::FlagType::String, false));
        addFlag(CLI::Flag("k", "provider-api-key", "Optional upstream provider API key", CLI::FlagType::String, false));
        addFlag(CLI::Flag("a", "provider-access-token", "Optional upstream provider access token", CLI::FlagType::String, false));
        addFlag(CLI::Flag("b", "provider-base-url", "Optional upstream provider base URL", CLI::FlagType::String, false));
    }

    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        const std::string provider = trimCopy(optionalFlag(parsedFlags, "provider"));
        const std::string filePath = optionalFlag(parsedFlags, "file");
        if (provider.empty()) {
            return printAndReturn(errorResponse("--provider is required."), globalFlags);
        }
        if (trimCopy(filePath).empty()) {
            return printAndReturn(errorResponse("--file is required."), globalFlags);
        }
        if (!std::filesystem::exists(filePath)) {
            return printAndReturn(errorResponse("Unable to open file: " + filePath), globalFlags);
        }

        const std::string mimeType = trimCopy(optionalFlag(parsedFlags, "mime-type", guessMimeType(filePath)));
        std::string error;
        auto client = createClient(parsedFlags, error);
        if (!client) {
            return printAndReturn(errorResponse(error), globalFlags);
        }

        Models::CloudResponse response = client->transcribe(
            filePath,
            provider,
            mimeType,
            optionalFlag(parsedFlags, "model"),
            optionalFlag(parsedFlags, "provider-api-key"),
            optionalFlag(parsedFlags, "provider-access-token"),
            optionalFlag(parsedFlags, "provider-base-url"));
        return printAndReturn(response, globalFlags);
    }
};

class GailRequestCommand final : public GailCommandBase {
public:
    GailRequestCommand() : GailCommandBase("request", "Send a raw HTTP request to Gail") {
        usage = "nmc gail request PATH [--method METHOD] [--json JSON|--json-file FILE] [--body TEXT|--body-file FILE] [--content-type TYPE] [--base-url URL] [--api-token TOKEN]";
        examples = "nmc gail request /v1/llm/complete --method POST --json '{\"workflow\":\"assistant\",\"role\":\"assistant\",\"messages\":[{\"role\":\"user\",\"content\":\"Hello\"}]}'";
        addClientFlags();
        addArgument(CLI::Argument("path", "Gail request path", true, 0));
        addFlag(CLI::Flag("m", "method", "HTTP method: GET, POST, PUT, PATCH, DELETE", CLI::FlagType::String, false));
        addFlag(CLI::Flag("j", "json", "Inline JSON request payload", CLI::FlagType::String, false));
        addFlag(CLI::Flag("f", "json-file", "Path to a JSON request payload file", CLI::FlagType::String, false));
        addFlag(CLI::Flag("b", "body", "Inline raw request body", CLI::FlagType::String, false));
        addFlag(CLI::Flag("i", "body-file", "Path to a raw request body file", CLI::FlagType::String, false));
        addFlag(CLI::Flag("c", "content-type", "Explicit request Content-Type", CLI::FlagType::String, false));
    }

    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        const std::string method = toUpperCopy(trimCopy(optionalFlag(parsedFlags, "method", "GET")));
        if (!isSupportedMethod(method)) {
            return printAndReturn(
                errorResponse("--method must be one of GET, POST, PUT, PATCH, or DELETE."),
                globalFlags);
        }

        const bool hasJsonPayload = hasFlag(parsedFlags, "json") || hasFlag(parsedFlags, "json-file");
        const bool hasRawBody = hasFlag(parsedFlags, "body") || hasFlag(parsedFlags, "body-file");
        if (hasJsonPayload && hasRawBody) {
            return printAndReturn(
                errorResponse("Use either JSON payload flags or raw body flags, not both."),
                globalFlags);
        }

        std::string body;
        std::string contentType = trimCopy(optionalFlag(parsedFlags, "content-type"));
        if (hasJsonPayload) {
            nlohmann::json payload;
            std::string parseError;
            if (!parseJsonFlagOrFile(parsedFlags, "json", "json-file", payload, parseError, false)) {
                return printAndReturn(errorResponse(parseError), globalFlags);
            }
            body = payload.dump();
            if (contentType.empty()) {
                contentType = "application/json";
            }
        } else if (hasRawBody) {
            std::string loadError;
            if (!loadRawValue(parsedFlags, "body", "body-file", body, loadError)) {
                return printAndReturn(errorResponse(loadError), globalFlags);
            }
            if (contentType.empty() && !body.empty()) {
                contentType = "text/plain; charset=utf-8";
            }
        }

        std::string error;
        auto client = createClient(parsedFlags, error);
        if (!client) {
            return printAndReturn(errorResponse(error), globalFlags);
        }

        return printAndReturn(client->request(method, parsedArgs[0], body, contentType), globalFlags);
    }
};

// ---------------------------------------------------------------------------
// Gail Trading Bridge helpers
// ---------------------------------------------------------------------------

void addDirectTradingFlags(CLI::Command& command, bool includeLimit = false, bool includeJsonBody = false) {
    command.addFlag(CLI::Flag(
        "d",
        "direct",
        "Call Gail directly instead of routing through the configured NMC server.",
        CLI::FlagType::Bool,
        false));
    command.addFlag(CLI::Flag(
        "u",
        "base-url",
        "Direct Gail base URL. Overrides NMC_GAIL_BASE_URL/GAIL_BASE_URL.",
        CLI::FlagType::String,
        false));
    command.addFlag(CLI::Flag(
        "t",
        "api-token",
        "Direct Gail bearer token. Overrides NMC_GAIL_API_TOKEN/GAIL_API_TOKEN.",
        CLI::FlagType::String,
        false));
    if (includeLimit) {
        command.addFlag(CLI::Flag("n", "limit", "Maximum number of items to return", CLI::FlagType::Int, false));
    }
    if (includeJsonBody) {
        command.addFlag(CLI::Flag("j", "json", "JSON payload to send", CLI::FlagType::String, false));
        command.addFlag(CLI::Flag("f", "json-file", "Path to JSON file to send", CLI::FlagType::String, false));
    }
}

std::unique_ptr<Core::GailClient> createDirectTradingClient(
    const std::map<std::string, std::string>& parsedFlags,
    std::string& errorOut
) {
    std::string baseUrl;
    std::string apiToken;
    if (!Core::GailClient::resolveConfiguration(
            optionalFlag(parsedFlags, "base-url"),
            optionalFlag(parsedFlags, "api-token"),
            baseUrl,
            apiToken,
            errorOut)) {
        return nullptr;
    }
    return std::make_unique<Core::GailClient>(baseUrl, apiToken);
}

bool shouldUseDirectGailTrading(const std::shared_ptr<Core::CloudAPIClient>& apiClient,
                                const std::map<std::string, std::string>& parsedFlags) {
    if (hasFlag(parsedFlags, "direct") || hasFlag(parsedFlags, "base-url") || hasFlag(parsedFlags, "api-token")) {
        return true;
    }
    if (apiClient && apiClient->hasDefaultConnection()) {
        return false;
    }

    std::string baseUrl;
    std::string apiToken;
    std::string error;
    return Core::GailClient::resolveConfiguration("", "", baseUrl, apiToken, error);
}

int printAndReturnResponse(const Models::CloudResponse& response, const CLI::GlobalFlags& globalFlags) {
    BaseCommand::printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

bool parseTradingLimitFlag(const std::map<std::string, std::string>& parsedFlags,
                           int fallback,
                           int& limitOut,
                           std::string& errorOut) {
    return parsePositiveInt(parsedFlags, "limit", fallback, limitOut, errorOut);
}

} // namespace

void attachDirectGailSubcommands(const std::shared_ptr<NMC::CLI::Command>& root) {
    root->addSubcommand(std::make_shared<GailHealthCommand>());
    root->addSubcommand(std::make_shared<GailStatusCommand>());
    root->addSubcommand(std::make_shared<GailJsonEndpointCommand>(
        "complete",
        "Submit a workflow-aware LLM completion request to Gail",
        "/v1/llm/complete",
        "nmc gail complete --json JSON|--json-file FILE [--base-url URL] [--api-token TOKEN]",
        "nmc gail complete --json '{\"workflow\":\"assistant_requirements\",\"role\":\"assistant\",\"messages\":[{\"role\":\"user\",\"content\":\"Summarise cluster readiness.\"}]}'\n"
        "nmc gail complete --json-file ./complete.json",
        "Gail completion completed."));
    root->addSubcommand(std::make_shared<GailJsonEndpointCommand>(
        "direct-complete",
        "Submit a direct provider completion request to Gail",
        "/v1/llm/direct-complete",
        "nmc gail direct-complete --json JSON|--json-file FILE [--base-url URL] [--api-token TOKEN]",
        "nmc gail direct-complete --json '{\"provider\":\"openai\",\"model\":\"gpt-4o-mini\",\"messages\":[{\"role\":\"user\",\"content\":\"Hello\"}]}'\n"
        "nmc gail direct-complete --json-file ./direct-complete.json",
        "Gail direct completion completed."));
    root->addSubcommand(std::make_shared<GailTranscribeCommand>());
    root->addSubcommand(std::make_shared<GailJsonEndpointCommand>(
        "analyze",
        "Run Gail neuromorphic specialist analysis",
        "/v1/neuromorphic/analyze",
        "nmc gail analyze --json JSON|--json-file FILE [--base-url URL] [--api-token TOKEN]",
        "nmc gail analyze --json '{\"text\":\"Implement an AER-backed SNN planner.\",\"workflow\":\"topic_research\",\"role\":\"researcher\"}'\n"
        "nmc gail analyze --json-file ./analyze.json",
        "Gail neuromorphic analysis completed."));
    root->addSubcommand(std::make_shared<GailJsonEndpointCommand>(
        "predict",
        "Run Gail neuromorphic prediction",
        "/v1/neuromorphic/predict",
        "nmc gail predict --json JSON|--json-file FILE [--base-url URL] [--api-token TOKEN]",
        "nmc gail predict --json '{\"inputs\":[0.1,0.6,0.9]}'\n"
        "nmc gail predict --json-file ./predict.json",
        "Gail neuromorphic prediction completed."));
    root->addSubcommand(std::make_shared<GailJsonEndpointCommand>(
        "aer-encode",
        "Encode neuromorphic spikes/events with Gail",
        "/v1/aer/encode",
        "nmc gail aer-encode --json JSON|--json-file FILE [--base-url URL] [--api-token TOKEN]",
        "nmc gail aer-encode --json '{\"base_addr\":4096,\"spikes\":[1,0,1,1]}'\n"
        "nmc gail aer-encode --json-file ./aer-encode.json",
        "Gail AER encode completed."));
    root->addSubcommand(std::make_shared<GailJsonEndpointCommand>(
        "aer-decode",
        "Decode AER payloads with Gail",
        "/v1/aer/decode",
        "nmc gail aer-decode --json JSON|--json-file FILE [--base-url URL] [--api-token TOKEN]",
        "nmc gail aer-decode --json '{\"payload_hex\":\"AER10000\"}'\n"
        "nmc gail aer-decode --json-file ./aer-decode.json",
        "Gail AER decode completed."));
    root->addSubcommand(std::make_shared<GailRequestCommand>());
}

GailCommand::GailCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("gail", "Call the Gail shared AI middleware service", std::move(client)) {
    usage = "nmc gail [command]";
    examples = "nmc gail health --base-url https://gail.internal.example\n"
               "nmc gail status --probe-engines\n"
               "nmc gail trading status";
}

int GailCommand::execute(const std::map<std::string, std::string>&,
                         const std::vector<std::string>&,
                         const CLI::GlobalFlags&) {
    printHelp();
    return 0;
}

GailTradingCommand::GailTradingCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("trading", "Manage the Gail crypto trading bridge", std::move(client)) {
    usage = "nmc gail trading [command]";
    examples = "nmc gail trading status\nnmc gail trading pause\nnmc gail trading resume";
}

int GailTradingCommand::execute(const std::map<std::string, std::string>&,
                                const std::vector<std::string>&,
                                const CLI::GlobalFlags&) {
    printHelp();
    return 0;
}

GailTradingStatusCommand::GailTradingStatusCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("status", "Show Gail trading bridge status", std::move(client)) {
    usage = "nmc gail trading status [--direct] [--base-url URL] [--api-token TOKEN]";
    examples = "nmc gail trading status";
    addDirectTradingFlags(*this);
}

int GailTradingStatusCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                      const std::vector<std::string>& parsedArgs,
                                      const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    Models::CloudResponse response;
    if (shouldUseDirectGailTrading(apiClient, parsedFlags)) {
        std::string error;
        auto client = createDirectTradingClient(parsedFlags, error);
        if (!client) {
            return printAndReturnResponse(errorResponse(error), globalFlags);
        }
        response = client->request("GET", "/v1/trading/status");
    } else {
        response = apiClient->getGailTradingStatus();
    }
    if (response.success) {
        response.message = "Trading bridge status retrieved.";
    }
    return printAndReturnResponse(response, globalFlags);
}

GailTradingPortfolioCommand::GailTradingPortfolioCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("portfolio", "Show OctoBot portfolio holdings", std::move(client)) {
    usage = "nmc gail trading portfolio [--direct] [--base-url URL] [--api-token TOKEN]";
    examples = "nmc gail trading portfolio";
    addDirectTradingFlags(*this);
}

int GailTradingPortfolioCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                         const std::vector<std::string>& parsedArgs,
                                         const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    Models::CloudResponse response;
    if (shouldUseDirectGailTrading(apiClient, parsedFlags)) {
        std::string error;
        auto client = createDirectTradingClient(parsedFlags, error);
        if (!client) {
            return printAndReturnResponse(errorResponse(error), globalFlags);
        }
        response = client->request("GET", "/v1/trading/portfolio");
    } else {
        response = apiClient->getGailTradingPortfolio();
    }
    if (response.success) {
        response.message = "Portfolio retrieved.";
    }
    return printAndReturnResponse(response, globalFlags);
}

GailTradingPositionsCommand::GailTradingPositionsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("positions", "Show open OctoBot positions", std::move(client)) {
    usage = "nmc gail trading positions [--direct] [--base-url URL] [--api-token TOKEN]";
    examples = "nmc gail trading positions";
    addDirectTradingFlags(*this);
}

int GailTradingPositionsCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                         const std::vector<std::string>& parsedArgs,
                                         const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    Models::CloudResponse response;
    if (shouldUseDirectGailTrading(apiClient, parsedFlags)) {
        std::string error;
        auto client = createDirectTradingClient(parsedFlags, error);
        if (!client) {
            return printAndReturnResponse(errorResponse(error), globalFlags);
        }
        response = client->request("GET", "/v1/trading/positions");
    } else {
        response = apiClient->getGailTradingPositions();
    }
    if (response.success) {
        response.message = "Open positions retrieved.";
    }
    return printAndReturnResponse(response, globalFlags);
}

GailTradingHistoryCommand::GailTradingHistoryCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("history", "Show recent executed trades", std::move(client)) {
    usage = "nmc gail trading history [--limit N] [--direct] [--base-url URL] [--api-token TOKEN]";
    examples = "nmc gail trading history --limit 20";
    addDirectTradingFlags(*this, true);
}

int GailTradingHistoryCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                       const std::vector<std::string>& parsedArgs,
                                       const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    int limit = 50;
    std::string parseError;
    if (!parseTradingLimitFlag(parsedFlags, 50, limit, parseError)) {
        return printAndReturnResponse(errorResponse(parseError), globalFlags);
    }

    Models::CloudResponse response;
    if (shouldUseDirectGailTrading(apiClient, parsedFlags)) {
        std::string error;
        auto client = createDirectTradingClient(parsedFlags, error);
        if (!client) {
            return printAndReturnResponse(errorResponse(error), globalFlags);
        }
        std::string path = "/v1/trading/history";
        if (hasFlag(parsedFlags, "limit")) {
            path += "?limit=" + std::to_string(limit);
        }
        response = client->request("GET", path);
    } else {
        response = apiClient->getGailTradingHistory(hasFlag(parsedFlags, "limit") ? limit : -1);
    }
    if (response.success) {
        response.message = "Trade history retrieved.";
    }
    return printAndReturnResponse(response, globalFlags);
}

GailTradingLogsCommand::GailTradingLogsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("logs", "Show trading bridge activity log", std::move(client)) {
    usage = "nmc gail trading logs [--limit N] [--direct] [--base-url URL] [--api-token TOKEN]";
    examples = "nmc gail trading logs --limit 100";
    addDirectTradingFlags(*this, true);
}

int GailTradingLogsCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                    const std::vector<std::string>& parsedArgs,
                                    const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    int limit = 50;
    std::string parseError;
    if (!parseTradingLimitFlag(parsedFlags, 50, limit, parseError)) {
        return printAndReturnResponse(errorResponse(parseError), globalFlags);
    }

    Models::CloudResponse response;
    if (shouldUseDirectGailTrading(apiClient, parsedFlags)) {
        std::string error;
        auto client = createDirectTradingClient(parsedFlags, error);
        if (!client) {
            return printAndReturnResponse(errorResponse(error), globalFlags);
        }
        std::string path = "/v1/trading/logs";
        if (hasFlag(parsedFlags, "limit")) {
            path += "?limit=" + std::to_string(limit);
        }
        response = client->request("GET", path);
    } else {
        response = apiClient->getGailTradingLogs(hasFlag(parsedFlags, "limit") ? limit : -1);
    }
    if (response.success) {
        response.message = "Activity log retrieved.";
    }
    return printAndReturnResponse(response, globalFlags);
}

GailTradingExchangesCommand::GailTradingExchangesCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("exchanges", "List available OctoBot exchanges", std::move(client)) {
    usage = "nmc gail trading exchanges [--direct] [--base-url URL] [--api-token TOKEN]";
    examples = "nmc gail trading exchanges";
    addDirectTradingFlags(*this);
}

int GailTradingExchangesCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                         const std::vector<std::string>& parsedArgs,
                                         const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    Models::CloudResponse response;
    if (shouldUseDirectGailTrading(apiClient, parsedFlags)) {
        std::string error;
        auto client = createDirectTradingClient(parsedFlags, error);
        if (!client) {
            return printAndReturnResponse(errorResponse(error), globalFlags);
        }
        response = client->request("GET", "/v1/trading/exchanges");
    } else {
        response = apiClient->getGailTradingExchanges();
    }
    if (response.success) {
        response.message = "Exchanges retrieved.";
    }
    return printAndReturnResponse(response, globalFlags);
}

GailTradingCurrenciesCommand::GailTradingCurrenciesCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("currencies", "List available trading currencies/pairs", std::move(client)) {
    usage = "nmc gail trading currencies [--direct] [--base-url URL] [--api-token TOKEN]";
    examples = "nmc gail trading currencies";
    addDirectTradingFlags(*this);
}

int GailTradingCurrenciesCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                          const std::vector<std::string>& parsedArgs,
                                          const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    Models::CloudResponse response;
    if (shouldUseDirectGailTrading(apiClient, parsedFlags)) {
        std::string error;
        auto client = createDirectTradingClient(parsedFlags, error);
        if (!client) {
            return printAndReturnResponse(errorResponse(error), globalFlags);
        }
        response = client->request("GET", "/v1/trading/currencies");
    } else {
        response = apiClient->getGailTradingCurrencies();
    }
    if (response.success) {
        response.message = "Currencies retrieved.";
    }
    return printAndReturnResponse(response, globalFlags);
}

GailTradingConfigCommand::GailTradingConfigCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("config", "Show current trading bridge configuration", std::move(client)) {
    usage = "nmc gail trading config [--direct] [--base-url URL] [--api-token TOKEN]";
    examples = "nmc gail trading config";
    addDirectTradingFlags(*this);
}

int GailTradingConfigCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                      const std::vector<std::string>& parsedArgs,
                                      const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    Models::CloudResponse response;
    if (shouldUseDirectGailTrading(apiClient, parsedFlags)) {
        std::string error;
        auto client = createDirectTradingClient(parsedFlags, error);
        if (!client) {
            return printAndReturnResponse(errorResponse(error), globalFlags);
        }
        response = client->request("GET", "/v1/trading/config");
    } else {
        response = apiClient->getGailTradingConfig();
    }
    if (response.success) {
        response.message = "Trading config retrieved.";
    }
    return printAndReturnResponse(response, globalFlags);
}

GailTradingConfigSetCommand::GailTradingConfigSetCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("config-set", "Update trading bridge configuration at runtime", std::move(client)) {
    usage = "nmc gail trading config-set --json JSON|--json-file FILE [--direct] [--base-url URL] [--api-token TOKEN]";
    examples = "nmc gail trading config-set --json '{\"micro_trade_max_usd\":15.0}'";
    addDirectTradingFlags(*this, false, true);
}

int GailTradingConfigSetCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                         const std::vector<std::string>& parsedArgs,
                                         const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    nlohmann::json body;
    std::string parseError;
    if (!parseJsonFlagOrFile(parsedFlags, "json", "json-file", body, parseError, true)) {
        return printAndReturnResponse(errorResponse(parseError), globalFlags);
    }

    Models::CloudResponse response;
    if (shouldUseDirectGailTrading(apiClient, parsedFlags)) {
        std::string error;
        auto client = createDirectTradingClient(parsedFlags, error);
        if (!client) {
            return printAndReturnResponse(errorResponse(error), globalFlags);
        }
        response = client->postJson("/v1/trading/config", body);
    } else {
        response = apiClient->setGailTradingConfig(body);
    }
    if (response.success) {
        response.message = "Trading config updated.";
    }
    return printAndReturnResponse(response, globalFlags);
}

GailTradingPauseCommand::GailTradingPauseCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("pause", "Pause the trading bridge evaluation loop", std::move(client)) {
    usage = "nmc gail trading pause [--direct] [--base-url URL] [--api-token TOKEN]";
    examples = "nmc gail trading pause";
    addDirectTradingFlags(*this);
}

int GailTradingPauseCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                     const std::vector<std::string>& parsedArgs,
                                     const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const nlohmann::json body = nlohmann::json::object();
    Models::CloudResponse response;
    if (shouldUseDirectGailTrading(apiClient, parsedFlags)) {
        std::string error;
        auto client = createDirectTradingClient(parsedFlags, error);
        if (!client) {
            return printAndReturnResponse(errorResponse(error), globalFlags);
        }
        response = client->postJson("/v1/trading/pause", body);
    } else {
        response = apiClient->pauseGailTrading(body);
    }
    if (response.success) {
        response.message = "Trading bridge paused.";
    }
    return printAndReturnResponse(response, globalFlags);
}

GailTradingResumeCommand::GailTradingResumeCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("resume", "Resume the trading bridge evaluation loop", std::move(client)) {
    usage = "nmc gail trading resume [--direct] [--base-url URL] [--api-token TOKEN]";
    examples = "nmc gail trading resume";
    addDirectTradingFlags(*this);
}

int GailTradingResumeCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                      const std::vector<std::string>& parsedArgs,
                                      const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const nlohmann::json body = nlohmann::json::object();
    Models::CloudResponse response;
    if (shouldUseDirectGailTrading(apiClient, parsedFlags)) {
        std::string error;
        auto client = createDirectTradingClient(parsedFlags, error);
        if (!client) {
            return printAndReturnResponse(errorResponse(error), globalFlags);
        }
        response = client->postJson("/v1/trading/resume", body);
    } else {
        response = apiClient->resumeGailTrading(body);
    }
    if (response.success) {
        response.message = "Trading bridge resumed.";
    }
    return printAndReturnResponse(response, globalFlags);
}

GailTradingOverrideCommand::GailTradingOverrideCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("override", "Inject a one-shot trade override into the trading bridge", std::move(client)) {
    usage = "nmc gail trading override --action ACTION --symbol SYMBOL [--amount USD] [--exchange EXCHANGE] [--direct] [--base-url URL] [--api-token TOKEN]";
    examples = "nmc gail trading override --action buy --symbol BTC/USDT --amount 10.0 --exchange binance";
    addDirectTradingFlags(*this);
    addFlag(CLI::Flag("a", "action", "Trade action: buy, sell, strong_buy, strong_sell, cancel, hold", CLI::FlagType::String, true));
    addFlag(CLI::Flag("s", "symbol", "Trading pair symbol, e.g. BTC/USDT", CLI::FlagType::String, true));
    addFlag(CLI::Flag("m", "amount", "USD amount to trade", CLI::FlagType::String, false));
    addFlag(CLI::Flag("e", "exchange", "Exchange name", CLI::FlagType::String, false));
}

int GailTradingOverrideCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                        const std::vector<std::string>& parsedArgs,
                                        const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const std::string action = trimCopy(optionalFlag(parsedFlags, "action"));
    const std::string symbol = trimCopy(optionalFlag(parsedFlags, "symbol"));
    if (action.empty()) {
        return printAndReturnResponse(
            errorResponse("--action is required (buy, sell, strong_buy, strong_sell, cancel, hold)"),
            globalFlags);
    }
    if (symbol.empty()) {
        return printAndReturnResponse(errorResponse("--symbol is required (e.g. BTC/USDT)"), globalFlags);
    }

    nlohmann::json body = {
        {"action", action},
        {"symbol", symbol}
    };
    const std::string amountStr = trimCopy(optionalFlag(parsedFlags, "amount"));
    if (!amountStr.empty()) {
        try {
            body["amount_usd"] = std::stod(amountStr);
        } catch (const std::exception&) {
            return printAndReturnResponse(errorResponse("--amount must be a valid number"), globalFlags);
        }
    }
    const std::string exchange = trimCopy(optionalFlag(parsedFlags, "exchange"));
    if (!exchange.empty()) {
        body["exchange"] = exchange;
    }

    Models::CloudResponse response;
    if (shouldUseDirectGailTrading(apiClient, parsedFlags)) {
        std::string error;
        auto client = createDirectTradingClient(parsedFlags, error);
        if (!client) {
            return printAndReturnResponse(errorResponse(error), globalFlags);
        }
        response = client->postJson("/v1/trading/override", body);
    } else {
        response = apiClient->overrideGailTrading(body);
    }
    if (response.success) {
        response.message = "Trade override submitted.";
    }
    return printAndReturnResponse(response, globalFlags);
}

GailTradingEvaluateCommand::GailTradingEvaluateCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("evaluate", "Trigger an immediate trading evaluation cycle", std::move(client)) {
    usage = "nmc gail trading evaluate [--direct] [--base-url URL] [--api-token TOKEN]";
    examples = "nmc gail trading evaluate";
    addDirectTradingFlags(*this);
}

int GailTradingEvaluateCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                        const std::vector<std::string>& parsedArgs,
                                        const CLI::GlobalFlags& globalFlags) {
    if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
        return 1;
    }

    const nlohmann::json body = nlohmann::json::object();
    Models::CloudResponse response;
    if (shouldUseDirectGailTrading(apiClient, parsedFlags)) {
        std::string error;
        auto client = createDirectTradingClient(parsedFlags, error);
        if (!client) {
            return printAndReturnResponse(errorResponse(error), globalFlags);
        }
        response = client->postJson("/v1/trading/evaluate", body);
    } else {
        response = apiClient->evaluateGailTrading(body);
    }
    if (response.success) {
        response.message = "Evaluation triggered.";
    }
    return printAndReturnResponse(response, globalFlags);
}

} // namespace NMC::Commands
