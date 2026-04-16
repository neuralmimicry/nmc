#include "GailClient.h"

#include "Utils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <curl/curl.h>

namespace NMC::Core {

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

std::string getenvOrEmpty(const char* name) {
    const char* value = std::getenv(name);
    if (!value) {
        return "";
    }
    return trimCopy(value);
}

bool startsWithHttpScheme(const std::string& value) {
    return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
}

bool responseLooksJson(const std::string& contentType, const std::string& body) {
    const std::string normalizedContentType = trimCopy(contentType);
    if (normalizedContentType.find("json") != std::string::npos) {
        return true;
    }
    const std::string trimmedBody = trimCopy(body);
    return !trimmedBody.empty() && (trimmedBody.front() == '{' || trimmedBody.front() == '[');
}

std::string normalizeBaseUrl(const std::string& rawBaseUrl) {
    std::string normalized = trimCopy(rawBaseUrl);
    while (!normalized.empty() && normalized.back() == '/') {
        normalized.pop_back();
    }
    return normalized;
}

std::string normalizePath(const std::string& rawPath) {
    std::string normalized = trimCopy(rawPath);
    if (normalized.empty()) {
        return "/";
    }
    if (normalized.front() != '/') {
        normalized.insert(normalized.begin(), '/');
    }
    return normalized;
}

void loadGailDefaultsFromConfig(std::string& baseUrlOut, std::string& apiTokenOut) {
    baseUrlOut.clear();
    apiTokenOut.clear();

    std::ifstream input(Utils::getConfigPath());
    if (!input.is_open()) {
        return;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const auto payload = nlohmann::json::parse(buffer.str(), nullptr, false);
    if (!payload.is_object()) {
        return;
    }

    if (payload.contains("gail") && payload["gail"].is_object()) {
        const auto& gail = payload["gail"];
        if (gail.contains("base_url") && gail["base_url"].is_string()) {
            baseUrlOut = normalizeBaseUrl(gail["base_url"].get<std::string>());
        }
        if (gail.contains("api_token") && gail["api_token"].is_string()) {
            apiTokenOut = trimCopy(gail["api_token"].get<std::string>());
        }
        return;
    }

    if (payload.contains("gail_base_url") && payload["gail_base_url"].is_string()) {
        baseUrlOut = normalizeBaseUrl(payload["gail_base_url"].get<std::string>());
    }
    if (payload.contains("gail_api_token") && payload["gail_api_token"].is_string()) {
        apiTokenOut = trimCopy(payload["gail_api_token"].get<std::string>());
    }
}

std::string guessErrorMessage(const nlohmann::json& payload,
                              long statusCode,
                              const std::string& fallback) {
    if (payload.is_object()) {
        const auto messageIt = payload.find("message");
        if (messageIt != payload.end() && messageIt->is_string()) {
            return messageIt->get<std::string>();
        }
        const auto errorIt = payload.find("error");
        if (errorIt != payload.end() && errorIt->is_string()) {
            return errorIt->get<std::string>();
        }
    }
    if (!fallback.empty()) {
        return fallback;
    }
    return "Gail request failed with status " + std::to_string(statusCode) + ".";
}

std::string defaultSuccessMessage(const nlohmann::json& payload,
                                  const std::string& fallback) {
    if (!fallback.empty()) {
        return fallback;
    }
    if (payload.is_object()) {
        const auto messageIt = payload.find("message");
        if (messageIt != payload.end() && messageIt->is_string()) {
            return messageIt->get<std::string>();
        }
    }
    return "Gail request succeeded.";
}

std::string defaultUserAgent() {
#ifdef NMC_CLIENT_VERSION
    return std::string("nmc-gail-client/") + NMC_CLIENT_VERSION;
#else
    return "nmc-gail-client";
#endif
}

size_t writeBodyCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    if (!userdata || !ptr) {
        return 0;
    }
    const size_t total = size * nmemb;
    auto* output = static_cast<std::string*>(userdata);
    output->append(ptr, total);
    return total;
}

void ensureCurlGlobalInit() {
    static std::once_flag initFlag;
    static CURLcode initResult = CURLE_OK;
    std::call_once(initFlag, []() {
        initResult = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (initResult == CURLE_OK) {
            std::atexit([]() { curl_global_cleanup(); });
        }
    });
    if (initResult != CURLE_OK) {
        throw std::runtime_error("curl_global_init failed");
    }
}

class CurlHandle {
public:
    CurlHandle() : handle(curl_easy_init()) {}
    ~CurlHandle() {
        if (handle) {
            curl_easy_cleanup(handle);
        }
    }

    CurlHandle(const CurlHandle&) = delete;
    CurlHandle& operator=(const CurlHandle&) = delete;

    CURL* get() const { return handle; }

private:
    CURL* handle;
};

class CurlHeaderList {
public:
    CurlHeaderList() = default;
    ~CurlHeaderList() {
        if (headers) {
            curl_slist_free_all(headers);
        }
    }

    CurlHeaderList(const CurlHeaderList&) = delete;
    CurlHeaderList& operator=(const CurlHeaderList&) = delete;

    bool append(const std::string& value) {
        curl_slist* next = curl_slist_append(headers, value.c_str());
        if (!next) {
            return false;
        }
        headers = next;
        return true;
    }

    curl_slist* get() const { return headers; }

private:
    curl_slist* headers{nullptr};
};

class CurlMimeHandle {
public:
    explicit CurlMimeHandle(CURL* curl) : mime(curl ? curl_mime_init(curl) : nullptr) {}
    ~CurlMimeHandle() {
        if (mime) {
            curl_mime_free(mime);
        }
    }

    CurlMimeHandle(const CurlMimeHandle&) = delete;
    CurlMimeHandle& operator=(const CurlMimeHandle&) = delete;

    curl_mime* get() const { return mime; }

private:
    curl_mime* mime;
};

bool configureCommonOptions(CURL* curl,
                            const std::string& url,
                            const std::string& apiToken,
                            long timeoutSeconds,
                            CurlHeaderList& headers,
                            std::string& errorOut) {
    errorOut.clear();
    if (!curl) {
        errorOut = "Unable to initialise curl handle.";
        return false;
    }

    const std::string userAgent = defaultUserAgent();
    if (curl_easy_setopt(curl, CURLOPT_URL, url.c_str()) != CURLE_OK
        || curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) != CURLE_OK
        || curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L) != CURLE_OK
        || curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L) != CURLE_OK
        || curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds) != CURLE_OK
        || curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBodyCallback) != CURLE_OK
        || curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str()) != CURLE_OK) {
        errorOut = "Unable to configure curl options.";
        return false;
    }

    if (!headers.append("Accept: application/json")) {
        errorOut = "Unable to prepare curl headers.";
        return false;
    }
    if (!apiToken.empty()) {
        if (!headers.append("Authorization: Bearer " + apiToken)) {
            errorOut = "Unable to prepare Authorization header.";
            return false;
        }
    }
    if (curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get()) != CURLE_OK) {
        errorOut = "Unable to configure curl headers.";
        return false;
    }
    return true;
}

} // namespace

GailClient::GailClient(std::string baseUrl, std::string apiToken, long timeoutSeconds)
    : baseUrlValue(normalizeBaseUrl(baseUrl)),
      apiToken(trimCopy(apiToken)),
      timeoutSeconds(timeoutSeconds > 0 ? timeoutSeconds : 180L) {}

bool GailClient::resolveConfiguration(const std::string& explicitBaseUrl,
                                      const std::string& explicitApiToken,
                                      std::string& baseUrlOut,
                                      std::string& apiTokenOut,
                                      std::string& errorOut) {
    errorOut.clear();
    std::string configBaseUrl;
    std::string configApiToken;
    loadGailDefaultsFromConfig(configBaseUrl, configApiToken);

    baseUrlOut = normalizeBaseUrl(explicitBaseUrl);
    if (baseUrlOut.empty()) {
        baseUrlOut = normalizeBaseUrl(getenvOrEmpty("NMC_GAIL_BASE_URL"));
    }
    if (baseUrlOut.empty()) {
        baseUrlOut = normalizeBaseUrl(getenvOrEmpty("GAIL_BASE_URL"));
    }
    if (baseUrlOut.empty()) {
        baseUrlOut = normalizeBaseUrl(getenvOrEmpty("NMC_GAIL_URL"));
    }
    if (baseUrlOut.empty()) {
        baseUrlOut = normalizeBaseUrl(getenvOrEmpty("GAIL_URL"));
    }
    if (baseUrlOut.empty()) {
        baseUrlOut = configBaseUrl;
    }
    if (baseUrlOut.empty()) {
        errorOut = "Gail base URL is not configured. Use --base-url, set NMC_GAIL_BASE_URL/GAIL_BASE_URL, or add gail.base_url to ~/.nmc/config.json.";
        return false;
    }
    if (!startsWithHttpScheme(baseUrlOut)) {
        errorOut = "Gail base URL must begin with http:// or https://.";
        return false;
    }

    apiTokenOut = trimCopy(explicitApiToken);
    if (apiTokenOut.empty()) {
        static constexpr std::array<const char*, 6> tokenKeys = {
            "NMC_GAIL_API_TOKEN",
            "GAIL_API_TOKEN",
            "NMC_GAIL_BEARER_TOKEN",
            "GAIL_BEARER_TOKEN",
            "NMC_GAIL_AUTH_TOKEN",
            "GAIL_AUTH_TOKEN"
        };
        for (const char* key : tokenKeys) {
            apiTokenOut = getenvOrEmpty(key);
            if (!apiTokenOut.empty()) {
                break;
            }
        }
    }
    if (apiTokenOut.empty()) {
        apiTokenOut = configApiToken;
    }
    return true;
}

Models::CloudResponse GailClient::health() const {
    return request("GET", "/healthz");
}

Models::CloudResponse GailClient::orchestrationStatus(int limit,
                                                      bool probeEngines,
                                                      bool probeProviders) const {
    const int safeLimit = limit > 0 ? limit : 20;
    std::ostringstream path;
    path << "/v1/status/orchestration"
         << "?limit=" << safeLimit
         << "&probe_engines=" << (probeEngines ? "true" : "false")
         << "&probe_providers=" << (probeProviders ? "true" : "false");
    return request("GET", path.str());
}

Models::CloudResponse GailClient::postJson(const std::string& path,
                                           const nlohmann::json& payload) const {
    return request("POST", path, payload.dump(), "application/json");
}

Models::CloudResponse GailClient::request(const std::string& method,
                                          const std::string& path,
                                          const std::string& body,
                                          const std::string& contentType) const {
    const HttpResponse response = performRequest(toUpperCopy(method), path, body, contentType);
    return decodeResponse(response, "");
}

Models::CloudResponse GailClient::transcribe(const std::string& filePath,
                                             const std::string& provider,
                                             const std::string& mimeType,
                                             const std::string& model,
                                             const std::string& providerApiKey,
                                             const std::string& providerAccessToken,
                                             const std::string& providerBaseUrl) const {
    std::vector<GailMultipartField> fields;
    fields.push_back({"provider", provider});
    if (!trimCopy(model).empty()) {
        fields.push_back({"model", model});
    }
    if (!trimCopy(providerApiKey).empty()) {
        fields.push_back({"api_key", providerApiKey});
    }
    if (!trimCopy(providerAccessToken).empty()) {
        fields.push_back({"access_token", providerAccessToken});
    }
    if (!trimCopy(providerBaseUrl).empty()) {
        fields.push_back({"base_url", providerBaseUrl});
    }
    const HttpResponse response = performMultipartRequest(
        "/v1/llm/transcribe",
        fields,
        "file",
        filePath,
        mimeType);
    return decodeResponse(response, "Gail transcription completed.");
}

Models::CloudResponse GailClient::decodeResponse(const HttpResponse& response,
                                                 const std::string& successMessage) const {
    Models::CloudResponse cloudResponse{false, "", nlohmann::json(), static_cast<int>(response.statusCode)};
    if (!response.transportOk) {
        cloudResponse.message = response.errorMessage.empty()
                                ? "Gail request failed before an HTTP response was received."
                                : "Gail request failed: " + response.errorMessage;
        return cloudResponse;
    }

    cloudResponse.success = response.statusCode >= 200 && response.statusCode < 300;

    if (response.body.empty()) {
        cloudResponse.data = nlohmann::json();
    } else {
        const auto parsed = nlohmann::json::parse(response.body, nullptr, false);
        if (!parsed.is_discarded()) {
            cloudResponse.data = parsed;
        } else if (cloudResponse.success && responseLooksJson(response.contentType, response.body)) {
            cloudResponse.success = false;
            cloudResponse.message = "Failed to parse Gail JSON response.";
            cloudResponse.data = response.body;
            return cloudResponse;
        } else {
            cloudResponse.data = response.body;
        }
    }

    if (cloudResponse.success) {
        cloudResponse.message = defaultSuccessMessage(cloudResponse.data, successMessage);
        return cloudResponse;
    }

    if (cloudResponse.data.is_string()) {
        cloudResponse.message = guessErrorMessage(
            nlohmann::json(),
            response.statusCode,
            cloudResponse.data.get<std::string>());
    } else {
        cloudResponse.message = guessErrorMessage(cloudResponse.data, response.statusCode, "");
    }
    return cloudResponse;
}

GailClient::HttpResponse GailClient::performRequest(const std::string& method,
                                                    const std::string& path,
                                                    const std::string& body,
                                                    const std::string& contentType) const {
    HttpResponse response;
    try {
        ensureCurlGlobalInit();
    } catch (const std::exception& error) {
        response.errorMessage = error.what();
        return response;
    }

    CurlHandle curl;
    CurlHeaderList headers;
    std::string configError;
    if (!configureCommonOptions(curl.get(), absoluteUrl(path), apiToken, timeoutSeconds, headers, configError)) {
        response.errorMessage = configError;
        return response;
    }

    if (curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response.body) != CURLE_OK) {
        response.errorMessage = "Unable to capture Gail response body.";
        return response;
    }

    if (!contentType.empty()) {
        if (!headers.append("Content-Type: " + contentType)) {
            response.errorMessage = "Unable to prepare Content-Type header.";
            return response;
        }
        if (curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get()) != CURLE_OK) {
            response.errorMessage = "Unable to configure Content-Type header.";
            return response;
        }
    }

    // Keep verb handling explicit so raw requests behave predictably.
    if (method == "HEAD") {
        if (curl_easy_setopt(curl.get(), CURLOPT_NOBODY, 1L) != CURLE_OK) {
            response.errorMessage = "Unable to configure HEAD request.";
            return response;
        }
    } else if (method == "GET") {
        if (curl_easy_setopt(curl.get(), CURLOPT_HTTPGET, 1L) != CURLE_OK) {
            response.errorMessage = "Unable to configure GET request.";
            return response;
        }
    } else {
        if (curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, method.c_str()) != CURLE_OK) {
            response.errorMessage = "Unable to configure request method.";
            return response;
        }
    }

    if (!body.empty()) {
        if (curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str()) != CURLE_OK
            || curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                                static_cast<curl_off_t>(body.size())) != CURLE_OK) {
            response.errorMessage = "Unable to attach Gail request body.";
            return response;
        }
    }

    const CURLcode code = curl_easy_perform(curl.get());
    if (code != CURLE_OK) {
        response.errorMessage = curl_easy_strerror(code);
        return response;
    }

    response.transportOk = true;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.statusCode);
    char* contentTypeValue = nullptr;
    if (curl_easy_getinfo(curl.get(), CURLINFO_CONTENT_TYPE, &contentTypeValue) == CURLE_OK
        && contentTypeValue != nullptr) {
        response.contentType = contentTypeValue;
    }
    return response;
}

GailClient::HttpResponse GailClient::performMultipartRequest(const std::string& path,
                                                             const std::vector<GailMultipartField>& fields,
                                                             const std::string& fileFieldName,
                                                             const std::string& filePath,
                                                             const std::string& mimeType) const {
    HttpResponse response;
    if (!std::filesystem::exists(filePath)) {
        response.errorMessage = "Unable to open file: " + filePath;
        return response;
    }

    try {
        ensureCurlGlobalInit();
    } catch (const std::exception& error) {
        response.errorMessage = error.what();
        return response;
    }

    CurlHandle curl;
    CurlHeaderList headers;
    std::string configError;
    if (!configureCommonOptions(curl.get(), absoluteUrl(path), apiToken, timeoutSeconds, headers, configError)) {
        response.errorMessage = configError;
        return response;
    }

    if (curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response.body) != CURLE_OK) {
        response.errorMessage = "Unable to capture Gail multipart response body.";
        return response;
    }
    if (curl_easy_setopt(curl.get(), CURLOPT_POST, 1L) != CURLE_OK) {
        response.errorMessage = "Unable to configure Gail multipart POST request.";
        return response;
    }

    CurlMimeHandle mime(curl.get());
    if (!mime.get()) {
        response.errorMessage = "Unable to create multipart request.";
        return response;
    }

    for (const auto& field : fields) {
        curl_mimepart* part = curl_mime_addpart(mime.get());
        if (!part
            || curl_mime_name(part, field.name.c_str()) != CURLE_OK
            || curl_mime_data(part, field.value.c_str(), CURL_ZERO_TERMINATED) != CURLE_OK) {
            response.errorMessage = "Unable to add multipart field '" + field.name + "'.";
            return response;
        }
    }

    curl_mimepart* filePart = curl_mime_addpart(mime.get());
    const std::filesystem::path inputPath(filePath);
    const std::string filename = inputPath.filename().string();
    if (!filePart
        || curl_mime_name(filePart, fileFieldName.c_str()) != CURLE_OK
        || curl_mime_filedata(filePart, filePath.c_str()) != CURLE_OK
        || (!filename.empty() && curl_mime_filename(filePart, filename.c_str()) != CURLE_OK)
        || (!mimeType.empty() && curl_mime_type(filePart, mimeType.c_str()) != CURLE_OK)) {
        response.errorMessage = "Unable to add multipart file field.";
        return response;
    }

    if (curl_easy_setopt(curl.get(), CURLOPT_MIMEPOST, mime.get()) != CURLE_OK) {
        response.errorMessage = "Unable to attach multipart payload.";
        return response;
    }

    const CURLcode code = curl_easy_perform(curl.get());
    if (code != CURLE_OK) {
        response.errorMessage = curl_easy_strerror(code);
        return response;
    }

    response.transportOk = true;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.statusCode);
    char* contentTypeValue = nullptr;
    if (curl_easy_getinfo(curl.get(), CURLINFO_CONTENT_TYPE, &contentTypeValue) == CURLE_OK
        && contentTypeValue != nullptr) {
        response.contentType = contentTypeValue;
    }
    return response;
}

std::string GailClient::absoluteUrl(const std::string& path) const {
    return baseUrlValue + normalizePath(path);
}

} // namespace NMC::Core
