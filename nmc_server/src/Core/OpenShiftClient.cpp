// nmc_server/src/Core/OpenShiftClient.cpp
#include "OpenShiftClient.h"
#include <iostream>

namespace NMC::Server {

    OpenShiftClient::OpenShiftClient(const std::string& baseUrl)
            : url(parseBaseUrl(baseUrl)) {
        // httplib supports SSL clients when OpenSSL is available.
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        if (url.https) {
            httpsClient = std::make_unique<httplib::SSLClient>(url.host, url.port);
        } else {
            httpClient = std::make_unique<httplib::Client>(url.host, url.port);
        }
#else
        if (url.https) {
            // If SSL isn't compiled in, keep client null and surface an error at call time.
            httpClient.reset();
        } else {
            httpClient = std::make_unique<httplib::Client>(url.host, url.port);
        }
#endif
        if (httpClient) {
            httpClient->set_connection_timeout(5);
            httpClient->set_read_timeout(30);
            httpClient->set_write_timeout(30);
        }
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        if (httpsClient) {
            httpsClient->set_connection_timeout(5);
            httpsClient->set_read_timeout(30);
            httpsClient->set_write_timeout(30);
        }
#endif
    }

    OpenShiftClient::ParsedUrl OpenShiftClient::parseBaseUrl(const std::string& baseUrl) const {
        ParsedUrl parsed{"127.0.0.1", 8000, "", false};
        if (baseUrl.empty()) {
            return parsed;
        }

        std::string s = baseUrl;
        if (s.rfind("https://", 0) == 0) {
            parsed.https = true;
            s.erase(0, 8);
        } else if (s.rfind("http://", 0) == 0) {
            s.erase(0, 7);
        }

        auto slash = s.find('/');
        std::string hostPort = slash == std::string::npos ? s : s.substr(0, slash);
        parsed.basePath = slash == std::string::npos ? "" : s.substr(slash);
        if (!parsed.basePath.empty() && parsed.basePath.back() == '/') {
            parsed.basePath.pop_back();
        }

        auto colon = hostPort.find(':');
        parsed.host = hostPort.substr(0, colon);
        if (parsed.host.empty()) {
            parsed.host = "127.0.0.1";
        }
        if (colon != std::string::npos) {
            try {
                parsed.port = std::stoi(hostPort.substr(colon + 1));
            } catch (const std::exception&) {
                parsed.port = 8000;
            }
        }
        return parsed;
    }

    std::string OpenShiftClient::buildPath(const std::string& suffix) const {
        std::string normalized = url.basePath;
        std::string tail = suffix;
        if (tail.empty() || tail.front() != '/') {
            tail = "/" + tail;
        }
        return normalized + tail;
    }

    Models::CloudResponse OpenShiftClient::processHttpResponse(httplib::Result& res,
                                                               const std::string& successMessage) const {
        Models::CloudResponse apiResponse;
        if (res) {
            apiResponse.success = (res->status >= 200 && res->status < 300);
            apiResponse.message = successMessage;
            apiResponse.statusCode = res->status;
            if (!res->body.empty()) {
                try {
                    apiResponse.data = nlohmann::json::parse(res->body);
                } catch (const nlohmann::json::parse_error& e) {
                    apiResponse.success = false;
                    apiResponse.message = "Failed to parse OpenShift response: " + std::string(e.what());
                    apiResponse.data = res->body;
                }
            } else {
                apiResponse.data = nlohmann::json::object();
            }

            if (!apiResponse.success && apiResponse.data.is_object() && apiResponse.data.contains("message")) {
                apiResponse.message = apiResponse.data["message"].get<std::string>();
            }
        } else {
            apiResponse.success = false;
            apiResponse.message = "OpenShift API call failed: " + httplib::to_string(res.error());
            apiResponse.data = nlohmann::json::object();
            apiResponse.statusCode = 502;
        }
        return apiResponse;
    }

    Models::CloudResponse OpenShiftClient::getResources() {
        const bool hasClient =
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                (url.https && httpsClient != nullptr) || (!url.https && httpClient != nullptr);
#else
                (!url.https && httpClient != nullptr);
#endif
        if (!hasClient) {
            Models::CloudResponse apiResponse;
            apiResponse.success = false;
            apiResponse.message = "OpenShift API client is not configured for HTTPS support.";
            apiResponse.data = nlohmann::json::object();
            apiResponse.statusCode = 500;
            return apiResponse;
        }
        std::lock_guard<std::mutex> lock(clientMutex);
        httplib::Result res;
        if (url.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            res = httpsClient->Get(buildPath("/resources"));
#endif
        } else {
            res = httpClient->Get(buildPath("/resources"));
        }
        return processHttpResponse(res, "OpenShift resources retrieved.");
    }

    Models::CloudResponse OpenShiftClient::listClusters() {
        const bool hasClient =
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                (url.https && httpsClient != nullptr) || (!url.https && httpClient != nullptr);
#else
                (!url.https && httpClient != nullptr);
#endif
        if (!hasClient) {
            Models::CloudResponse apiResponse;
            apiResponse.success = false;
            apiResponse.message = "OpenShift API client is not configured for HTTPS support.";
            apiResponse.data = nlohmann::json::object();
            apiResponse.statusCode = 500;
            return apiResponse;
        }
        std::lock_guard<std::mutex> lock(clientMutex);
        httplib::Result res;
        if (url.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            res = httpsClient->Get(buildPath("/clusters"));
#endif
        } else {
            res = httpClient->Get(buildPath("/clusters"));
        }
        return processHttpResponse(res, "OpenShift clusters listed.");
    }

    Models::CloudResponse OpenShiftClient::requestCluster(const nlohmann::json& requestBody) {
        const bool hasClient =
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                (url.https && httpsClient != nullptr) || (!url.https && httpClient != nullptr);
#else
                (!url.https && httpClient != nullptr);
#endif
        if (!hasClient) {
            Models::CloudResponse apiResponse;
            apiResponse.success = false;
            apiResponse.message = "OpenShift API client is not configured for HTTPS support.";
            apiResponse.data = nlohmann::json::object();
            apiResponse.statusCode = 500;
            return apiResponse;
        }
        std::lock_guard<std::mutex> lock(clientMutex);
        httplib::Result res;
        if (url.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            res = httpsClient->Post(buildPath("/clusters/request"), requestBody.dump(), "application/json");
#endif
        } else {
            res = httpClient->Post(buildPath("/clusters/request"), requestBody.dump(), "application/json");
        }
        return processHttpResponse(res, "OpenShift cluster request submitted.");
    }

} // namespace NMC::Server
