// APIRoutes_DocsAuth.cpp
// Docs, auth/session, and lightweight server metadata route registration.

#include "APIRoutes.h"
#include "Utils.h"

#include <cctype>
#include <cstdlib>
#include <string>

namespace NMC::Server {

    namespace {
        std::string trimDocsRouteValue(const std::string& value) {
            size_t start = 0;
            while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
                ++start;
            }
            size_t end = value.size();
            while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
                --end;
            }
            return value.substr(start, end - start);
        }

        std::string resolveDocsDirForRoutes() {
            const char* docsDirEnv = std::getenv("NMC_DOCS_DIR");
            if (docsDirEnv) {
                const std::string value = trimDocsRouteValue(docsDirEnv);
                if (!value.empty()) {
                    return value;
                }
            }
            return "./docs";
        }
    }

    void APIRoutes::registerDocsAndAuthRoutes(httplib::Server& svr, const APIRoutes::RouteGuard& guard) {
        if (!docsEnabled) {
            return;
        }

        const std::string docsDir = resolveDocsDirForRoutes();
        const std::string docsIndexPath = docsDir + "/index.html";
        const std::string docsLoginPath = docsDir + "/login.html";

        svr.set_base_dir(docsDir);

        svr.Get("/", [docsIndexPath](const httplib::Request& req, httplib::Response& res) {
            res.set_content(Utils::readFile(docsIndexPath), "text/html");
        });
        svr.Get("/index.html", [docsIndexPath](const httplib::Request& req, httplib::Response& res) {
            res.set_content(Utils::readFile(docsIndexPath), "text/html");
        });
        svr.Get("/docs", [docsIndexPath](const httplib::Request& req, httplib::Response& res) {
            res.set_content(Utils::readFile(docsIndexPath), "text/html");
        });
        svr.Get("/login", [docsLoginPath](const httplib::Request& req, httplib::Response& res) {
            res.set_content(Utils::readFile(docsLoginPath), "text/html");
        });
        svr.Post("/auth/login", [this](const httplib::Request& req, httplib::Response& res) {
            handleAuthLogin(req, res);
        });
        svr.Get("/auth/session", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleAuthSession(req, res);
        });

        // Legacy launch path used by neuralmimicry.ai control-panel links.
        svr.Get(R"(^/services/health/monitoring/?$)", [docsIndexPath](const httplib::Request& req, httplib::Response& res) {
            res.set_content(Utils::readFile(docsIndexPath), "text/html");
        });
        svr.Get(R"(^/services/health/monitoring/index\.html$)", [docsIndexPath](const httplib::Request& req, httplib::Response& res) {
            res.set_content(Utils::readFile(docsIndexPath), "text/html");
        });
        svr.Get(R"(^/services/health/monitoring/login/?$)", [docsLoginPath](const httplib::Request& req, httplib::Response& res) {
            res.set_content(Utils::readFile(docsLoginPath), "text/html");
        });
        svr.Post(R"(^/services/health/monitoring/auth/login/?$)", [this](const httplib::Request& req, httplib::Response& res) {
            handleAuthLogin(req, res);
        });
        svr.Get(R"(^/services/health/monitoring/auth/session/?$)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleAuthSession(req, res);
        });
        svr.Get("/logout", [](const httplib::Request& req, httplib::Response& res) {
            res.set_redirect("/login");
        });
    }

    void APIRoutes::registerControlMetadataRoutes(httplib::Server& svr, const APIRoutes::RouteGuard& guard) {
        // Lightweight probe for CLI connection checks and external monitors.
        svr.Get("/health", [this](const httplib::Request& req, httplib::Response& res) {
            ensureRequestId(req, res);
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "nmc_server is healthy.";
            apiResponse.data = {
                    {"status", "ok"},
                    {"service", "nmc_server"}
            };
            sendJsonResponse(res, apiResponse);
        });

        svr.Get("/server/version", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleServerVersion(req, res);
        });
    }

} // namespace NMC::Server
