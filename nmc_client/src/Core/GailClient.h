#ifndef NMC_CORE_GAIL_CLIENT_H
#define NMC_CORE_GAIL_CLIENT_H

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Models/CloudResponse.h"

namespace NMC::Core {

struct GailMultipartField {
    std::string name;
    std::string value;
};

// Thin Gail-specific transport facade for the CLI.
// This intentionally uses libcurl so Gail can be consumed over HTTPS without
// changing the legacy nmc_server/httplib transport assumptions.
class GailClient {
public:
    GailClient(std::string baseUrl, std::string apiToken = "", long timeoutSeconds = 180L);

    // Resolve explicit flags first, then NMC/Gail environment variables, then
    // secure per-user defaults in ~/.nmc/config.json.
    static bool resolveConfiguration(const std::string& explicitBaseUrl,
                                     const std::string& explicitApiToken,
                                     std::string& baseUrlOut,
                                     std::string& apiTokenOut,
                                     std::string& errorOut);

    Models::CloudResponse health() const;
    Models::CloudResponse orchestrationStatus(int limit,
                                              bool probeEngines,
                                              bool probeProviders) const;
    Models::CloudResponse postJson(const std::string& path, const nlohmann::json& payload) const;
    Models::CloudResponse request(const std::string& method,
                                  const std::string& path,
                                  const std::string& body = "",
                                  const std::string& contentType = "") const;
    Models::CloudResponse transcribe(const std::string& filePath,
                                     const std::string& provider,
                                     const std::string& mimeType,
                                     const std::string& model = "",
                                     const std::string& providerApiKey = "",
                                     const std::string& providerAccessToken = "",
                                     const std::string& providerBaseUrl = "") const;

    const std::string& baseUrl() const { return baseUrlValue; }

private:
    struct HttpResponse {
        bool transportOk{false};
        long statusCode{0};
        std::string body;
        std::string contentType;
        std::string errorMessage;
    };

    Models::CloudResponse decodeResponse(const HttpResponse& response,
                                         const std::string& successMessage) const;
    HttpResponse performRequest(const std::string& method,
                                const std::string& path,
                                const std::string& body,
                                const std::string& contentType) const;
    HttpResponse performMultipartRequest(const std::string& path,
                                         const std::vector<GailMultipartField>& fields,
                                         const std::string& fileFieldName,
                                         const std::string& filePath,
                                         const std::string& mimeType) const;
    std::string absoluteUrl(const std::string& path) const;

    std::string baseUrlValue;
    std::string apiToken;
    long timeoutSeconds;
};

} // namespace NMC::Core

#endif // NMC_CORE_GAIL_CLIENT_H
