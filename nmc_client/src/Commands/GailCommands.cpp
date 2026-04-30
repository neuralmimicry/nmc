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
// Gail Trading Bridge commands
// ---------------------------------------------------------------------------

class GailTradingRootCommand final : public GailCommandBase {
public:
    GailTradingRootCommand() : GailCommandBase("trading", "Manage the Gail crypto trading bridge") {
        usage = "nmc gail trading [command]";
        examples = "nmc gail trading status\nnmc gail trading pause\nnmc gail trading resume";
    }

    int execute(const std::map<std::string, std::string>&,
                const std::vector<std::string>&,
                const CLI::GlobalFlags&) override {
        printHelp();
        return 0;
    }
};

// Simple GET command that proxies to a Gail trading endpoint.
class GailTradingGetCommand final : public GailCommandBase {
public:
    GailTradingGetCommand(const std::string& name,
                          const std::string& description,
                          const std::string& gailPath,
                          const std::string& usageStr,
                          const std::string& examplesStr,
                          const std::string& successMessage,
                          bool hasLimitFlag = false)
        : GailCommandBase(name, description)
        , gailPath_(gailPath)
        , successMessage_(successMessage)
        , hasLimitFlag_(hasLimitFlag) {
        usage = usageStr;
        examples = examplesStr;
        addClientFlags();
        if (hasLimitFlag_) {
            addFlag(CLI::Flag("n", "limit", "Maximum number of items to return", CLI::FlagType::Int, false));
        }
    }

    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        std::string path = gailPath_;
        if (hasLimitFlag_ && hasFlag(parsedFlags, "limit")) {
            int limit = 50;
            std::string parseError;
            if (!parsePositiveInt(parsedFlags, "limit", 50, limit, parseError)) {
                return printAndReturn(errorResponse(parseError), globalFlags);
            }
            path += "?limit=" + std::to_string(limit);
        }

        std::string error;
        auto client = createClient(parsedFlags, error);
        if (!client) {
            return printAndReturn(errorResponse(error), globalFlags);
        }

        auto response = client->request("GET", path);
        if (response.success) {
            response.message = successMessage_;
        }
        return printAndReturn(response, globalFlags);
    }

private:
    std::string gailPath_;
    std::string successMessage_;
    bool hasLimitFlag_;
};

// Simple POST command that sends an optional JSON body to a Gail trading endpoint.
class GailTradingPostCommand final : public GailCommandBase {
public:
    GailTradingPostCommand(const std::string& name,
                           const std::string& description,
                           const std::string& gailPath,
                           const std::string& usageStr,
                           const std::string& examplesStr,
                           const std::string& successMessage,
                           bool acceptsJsonBody = false)
        : GailCommandBase(name, description)
        , gailPath_(gailPath)
        , successMessage_(successMessage)
        , acceptsJsonBody_(acceptsJsonBody) {
        usage = usageStr;
        examples = examplesStr;
        addClientFlags();
        if (acceptsJsonBody_) {
            addFlag(CLI::Flag("j", "json", "JSON payload to send", CLI::FlagType::String, false));
            addFlag(CLI::Flag("f", "json-file", "Path to JSON file to send", CLI::FlagType::String, false));
        }
    }

    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        nlohmann::json body = nlohmann::json::object();
        if (acceptsJsonBody_ && (hasFlag(parsedFlags, "json") || hasFlag(parsedFlags, "json-file"))) {
            std::string parseError;
            if (!parseJsonFlagOrFile(parsedFlags, "json", "json-file", body, parseError, true)) {
                return printAndReturn(errorResponse(parseError), globalFlags);
            }
        }

        std::string error;
        auto client = createClient(parsedFlags, error);
        if (!client) {
            return printAndReturn(errorResponse(error), globalFlags);
        }

        auto response = client->postJson(gailPath_, body);
        if (response.success) {
            response.message = successMessage_;
        }
        return printAndReturn(response, globalFlags);
    }

private:
    std::string gailPath_;
    std::string successMessage_;
    bool acceptsJsonBody_;
};

// Override command: accepts --action, --symbol, --amount flags.
class GailTradingOverrideCommand final : public GailCommandBase {
public:
    GailTradingOverrideCommand() : GailCommandBase("override", "Inject a one-shot trade override into the trading bridge") {
        usage = "nmc gail trading override --action ACTION --symbol SYMBOL [--amount USD] [--exchange EXCHANGE]";
        examples = "nmc gail trading override --action buy --symbol BTC/USDT --amount 10.0 --exchange binance";
        addClientFlags();
        addFlag(CLI::Flag("a", "action", "Trade action: buy, sell, strong_buy, strong_sell, cancel, hold", CLI::FlagType::String, true));
        addFlag(CLI::Flag("s", "symbol", "Trading pair symbol, e.g. BTC/USDT", CLI::FlagType::String, true));
        addFlag(CLI::Flag("m", "amount", "USD amount to trade", CLI::FlagType::String, false));
        addFlag(CLI::Flag("e", "exchange", "Exchange name", CLI::FlagType::String, false));
    }

    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override {
        if (!validateArguments(parsedArgs) || !validateFlags(parsedFlags)) {
            return 1;
        }

        const std::string action = trimCopy(optionalFlag(parsedFlags, "action"));
        const std::string symbol = trimCopy(optionalFlag(parsedFlags, "symbol"));
        if (action.empty()) {
            return printAndReturn(errorResponse("--action is required (buy, sell, strong_buy, strong_sell, cancel, hold)"), globalFlags);
        }
        if (symbol.empty()) {
            return printAndReturn(errorResponse("--symbol is required (e.g. BTC/USDT)"), globalFlags);
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
                return printAndReturn(errorResponse("--amount must be a valid number"), globalFlags);
            }
        }
        const std::string exchange = trimCopy(optionalFlag(parsedFlags, "exchange"));
        if (!exchange.empty()) {
            body["exchange"] = exchange;
        }

        std::string error;
        auto client = createClient(parsedFlags, error);
        if (!client) {
            return printAndReturn(errorResponse(error), globalFlags);
        }

        auto response = client->postJson("/v1/trading/override", body);
        if (response.success) {
            response.message = "Trade override submitted.";
        }
        return printAndReturn(response, globalFlags);
    }
};

} // namespace

std::shared_ptr<NMC::CLI::Command> buildGailCommandTree() {
    // Keep the Gail surface thin: small convenience commands plus a raw escape hatch.
    auto root = std::make_shared<GailRootCommand>();
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

    // Trading bridge subcommand tree
    auto trading = std::make_shared<GailTradingRootCommand>();
    trading->addSubcommand(std::make_shared<GailTradingGetCommand>(
        "status",
        "Show Gail trading bridge status",
        "/v1/trading/status",
        "nmc gail trading status [--base-url URL] [--api-token TOKEN]",
        "nmc gail trading status",
        "Trading bridge status retrieved."));
    trading->addSubcommand(std::make_shared<GailTradingGetCommand>(
        "portfolio",
        "Show OctoBot portfolio holdings",
        "/v1/trading/portfolio",
        "nmc gail trading portfolio [--base-url URL] [--api-token TOKEN]",
        "nmc gail trading portfolio",
        "Portfolio retrieved."));
    trading->addSubcommand(std::make_shared<GailTradingGetCommand>(
        "positions",
        "Show open OctoBot positions",
        "/v1/trading/positions",
        "nmc gail trading positions [--base-url URL] [--api-token TOKEN]",
        "nmc gail trading positions",
        "Open positions retrieved."));
    trading->addSubcommand(std::make_shared<GailTradingGetCommand>(
        "history",
        "Show recent executed trades",
        "/v1/trading/history",
        "nmc gail trading history [--limit N] [--base-url URL] [--api-token TOKEN]",
        "nmc gail trading history --limit 20",
        "Trade history retrieved.",
        /*hasLimitFlag=*/true));
    trading->addSubcommand(std::make_shared<GailTradingGetCommand>(
        "logs",
        "Show trading bridge activity log",
        "/v1/trading/logs",
        "nmc gail trading logs [--limit N] [--base-url URL] [--api-token TOKEN]",
        "nmc gail trading logs --limit 100",
        "Activity log retrieved.",
        /*hasLimitFlag=*/true));
    trading->addSubcommand(std::make_shared<GailTradingGetCommand>(
        "exchanges",
        "List available OctoBot exchanges",
        "/v1/trading/exchanges",
        "nmc gail trading exchanges [--base-url URL] [--api-token TOKEN]",
        "nmc gail trading exchanges",
        "Exchanges retrieved."));
    trading->addSubcommand(std::make_shared<GailTradingGetCommand>(
        "currencies",
        "List available trading currencies/pairs",
        "/v1/trading/currencies",
        "nmc gail trading currencies [--base-url URL] [--api-token TOKEN]",
        "nmc gail trading currencies",
        "Currencies retrieved."));
    trading->addSubcommand(std::make_shared<GailTradingGetCommand>(
        "config",
        "Show current trading bridge configuration",
        "/v1/trading/config",
        "nmc gail trading config [--base-url URL] [--api-token TOKEN]",
        "nmc gail trading config",
        "Trading config retrieved."));
    trading->addSubcommand(std::make_shared<GailTradingPostCommand>(
        "config-set",
        "Update trading bridge configuration at runtime",
        "/v1/trading/config",
        "nmc gail trading config-set --json JSON|--json-file FILE [--base-url URL] [--api-token TOKEN]",
        "nmc gail trading config-set --json '{\"micro_trade_max_usd\":15.0}'",
        "Trading config updated.",
        /*acceptsJsonBody=*/true));
    trading->addSubcommand(std::make_shared<GailTradingPostCommand>(
        "pause",
        "Pause the trading bridge evaluation loop",
        "/v1/trading/pause",
        "nmc gail trading pause [--base-url URL] [--api-token TOKEN]",
        "nmc gail trading pause",
        "Trading bridge paused."));
    trading->addSubcommand(std::make_shared<GailTradingPostCommand>(
        "resume",
        "Resume the trading bridge evaluation loop",
        "/v1/trading/resume",
        "nmc gail trading resume [--base-url URL] [--api-token TOKEN]",
        "nmc gail trading resume",
        "Trading bridge resumed."));
    trading->addSubcommand(std::make_shared<GailTradingOverrideCommand>());
    trading->addSubcommand(std::make_shared<GailTradingPostCommand>(
        "evaluate",
        "Trigger an immediate trading evaluation cycle",
        "/v1/trading/evaluate",
        "nmc gail trading evaluate [--base-url URL] [--api-token TOKEN]",
        "nmc gail trading evaluate",
        "Evaluation triggered."));
    root->addSubcommand(trading);

    return root;
}

} // namespace NMC::Commands
