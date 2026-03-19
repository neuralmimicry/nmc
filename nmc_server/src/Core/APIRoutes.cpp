// server/APIRoutes.cpp
#include "APIRoutes.h"
#include "K8sHandlers.h"
#include "ConnectionHandlers.h"
#include "Utils.h"
#include <cstdlib>
#include <cctype>
#include <sstream>
#include <iostream>
#include <algorithm> // For std::remove_if, std::find_if
#include <set>       // For unique locations/types


namespace NMC::Server {

    namespace {
        std::string trim(const std::string& value) {
            const auto start = value.find_first_not_of(" \t");
            if (start == std::string::npos) {
                return "";
            }
            const auto end = value.find_last_not_of(" \t");
            return value.substr(start, end - start + 1);
        }

        std::string toLower(std::string value) {
            for (auto& ch : value) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return value;
        }

        std::string normalizeOpenShiftStatus(const std::string& rawStatus) {
            const std::string status = toLower(rawStatus);
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

        void normalizeClusterStatus(nlohmann::json& cluster) {
            if (cluster.is_object() && cluster.contains("status") && cluster["status"].is_string()) {
                cluster["status"] = normalizeOpenShiftStatus(cluster["status"].get<std::string>());
            }
        }
    }

    APIRoutes::APIRoutes(httplib::Server& svr) {
        auto envOr = [](const char* primary, const char* fallback) -> const char* {
            const char* val = std::getenv(primary);
            if (val && *val) {
                return val;
            }
            val = std::getenv(fallback);
            if (val && *val) {
                return val;
            }
            return nullptr;
        };

        const char* authModeEnv = envOr("NMC_AUTH_MODE", "NM_AUTH_MODE");
        authMode = authModeEnv ? toLower(std::string(authModeEnv)) : "";
        if (authMode.empty()) {
            authMode = "token";
        }

        const char* authTokenEnv = envOr("NMC_AUTH_TOKEN", "NM_AUTH_TOKEN");
        authToken = authTokenEnv ? authTokenEnv : "";

        if (authMode == "off") {
            authRequired = false;
        } else if (authMode == "token") {
            authRequired = !authToken.empty();
        } else {
            authRequired = true;
        }

        const char* maxBodyEnv = std::getenv("NMC_MAX_BODY_BYTES");
        maxBodyBytes = 1024 * 1024; // 1 MiB default
        if (maxBodyEnv) {
            try {
                maxBodyBytes = static_cast<size_t>(std::stoul(maxBodyEnv));
            } catch (const std::exception&) {
                maxBodyBytes = 1024 * 1024;
            }
        }

        const char* maxLogBodyEnv = std::getenv("NMC_LOG_BODY_BYTES");
        maxLogBodyBytes = 2048;
        if (maxLogBodyEnv) {
            try {
                maxLogBodyBytes = static_cast<size_t>(std::stoul(maxLogBodyEnv));
            } catch (const std::exception&) {
                maxLogBodyBytes = 2048;
            }
        }

        const char* docsEnv = std::getenv("NMC_DOCS_ENABLED");
        docsEnabled = true;
        if (docsEnv) {
            const std::string val = toLower(std::string(docsEnv));
            docsEnabled = !(val == "0" || val == "false" || val == "no");
        }

        if (authMode == "oidc") {
            OIDCConfig cfg;
            const char* introspection = envOr("NMC_OIDC_INTROSPECTION_URL", "NM_OIDC_INTROSPECTION_URL");
            cfg.introspectionUrl = introspection ? introspection : "";
            const char* issuer = envOr("NMC_OIDC_ISSUER", "NM_OIDC_ISSUER");
            cfg.issuer = issuer ? issuer : "";
            const char* clientId = envOr("NMC_OIDC_CLIENT_ID", "NM_OIDC_CLIENT_ID");
            cfg.clientId = clientId ? clientId : "";
            const char* clientSecret = envOr("NMC_OIDC_CLIENT_SECRET", "NM_OIDC_CLIENT_SECRET");
            cfg.clientSecret = clientSecret ? clientSecret : "";

            auto appendCsv = [&cfg](const char* raw, const auto& trimFn) {
                if (!raw) return;
                std::stringstream ss(raw);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    const std::string trimmed = trimFn(item);
                    if (!trimmed.empty()) {
                        cfg.audiences.push_back(trimmed);
                    }
                }
            };

            appendCsv(envOr("NMC_OIDC_AUDIENCE", "NM_OIDC_AUDIENCE"), trim);
            appendCsv(envOr("NMC_OIDC_ALLOWED_AUDIENCES", "NM_OIDC_ALLOWED_AUDIENCES"), trim);
            appendCsv(envOr("NMC_OIDC_AUDIENCES", "NM_OIDC_AUDIENCES"), trim);

            const char* requiredScope = envOr("NMC_OIDC_REQUIRED_SCOPE", "NM_OIDC_REQUIRED_SCOPE");
            if (requiredScope) {
                std::stringstream ss(requiredScope);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    const std::string trimmed = trim(item);
                    if (!trimmed.empty()) {
                        cfg.requiredScopes.push_back(trimmed);
                    }
                }
            }

            const char* requiredScopesCsv = envOr("NMC_OIDC_REQUIRED_SCOPES", "NM_OIDC_REQUIRED_SCOPES");
            if (requiredScopesCsv) {
                std::stringstream ss(requiredScopesCsv);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    const std::string trimmed = trim(item);
                    if (!trimmed.empty()) {
                        cfg.requiredScopes.push_back(trimmed);
                    }
                }
            }
            oidcValidator = std::make_unique<OIDCValidator>(cfg);
        }

        // Initialize K8sHandlers, passing necessary references and callbacks
        const std::string api_server_url_value = "http://localhost:8080"; // Replace with your actual Kubernetes API server URL

        k8sHandlers = std::make_unique<K8sHandlers>(
                api_server_url_value,
                "", // Placeholder for kubeconfig path, can be set later
                dataMutex,
                [this](httplib::Response& res, const Models::CloudResponse& apiResponse) {
                    sendJsonResponse(res, apiResponse);
                },
                [this](httplib::Response& res, int status, const std::string& message) {
                    sendErrorResponse(res, status, message);
                }
        );

        // OpenShift portal API base URL (FastAPI in oshift). Defaults to localhost:8000.
        const char* osUrlEnv = std::getenv("NMC_OSHIFT_API_URL");
        std::string osUrl = osUrlEnv ? osUrlEnv : "http://127.0.0.1:8000";
        openShiftClient = std::make_unique<OpenShiftClient>(osUrl);

        // Enable logging of all requests
        svr.set_logger([this](const httplib::Request& req, const httplib::Response& res) {
            logRequest(req, res);
        });

        if (docsEnabled) {
            // Set up static file serving for documentation
            // Assumes a 'docs' directory exists relative to the executable
            // or where the server is run from.
            // This is a robust way to serve static content like documentation.
            // All requests to /docs/* will look for files in the ./docs/ directory.
            // E.g., /docs/index.html will serve ./docs/index.html
            // /docs/bucket.html will serve ./docs/bucket.html
            svr.set_base_dir("./docs"); // Set the base directory for static files

            // --- Documentation Route ---
            // This route will serve the index.html from the docs directory
            // when /docs is accessed directly.
            // Other files like /docs/bucket.html will be served automatically
            // by set_base_dir.
            svr.Get("/index.html", [](const httplib::Request& req, httplib::Response& res) {
                res.set_content(Utils::readFile("./docs/index.html"), "text/html");
            });
            svr.Get("/docs", [](const httplib::Request& req, httplib::Response& res) {
                res.set_content(Utils::readFile("./docs/index.html"), "text/html");
            });
        }

        auto guard = [this](const httplib::Request& req, httplib::Response& res) -> bool {
            ensureRequestId(req, res);
            if (!enforceBodyLimit(req, res)) {
                return false;
            }
            if (!authorizeOrReject(req, res)) {
                return false;
            }
            return true;
        };

        // --- Bucket Routes ---
        svr.Post("/bucket/create", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleCreateBucket(req, res);
        });
        svr.Delete(R"(/bucket/delete/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleDeleteBucket(req, res);
        });
        svr.Get(R"(/bucket/get/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGetBucket(req, res);
        });
        svr.Get("/bucket/list", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListBuckets(req, res);
        });
        svr.Get("/bucket/list-locations", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListBucketLocations(req, res);
        });
        svr.Get("/bucket/list-types", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListBucketTypes(req, res);
        });
        svr.Post(R"(/bucket/reset-key/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleResetBucketKey(req, res);
        });

        // --- Connection Routes ---
        // GET /connections/status
        svr.Get("/connections/status", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            ConnectionHandlers::handleGetConnectionStatus(req, res);
        });

        // POST /connections/make
        svr.Post("/connections/make", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            ConnectionHandlers::handleMakeConnection(req, res);
        });

        // DELETE /connections/:name
        // svr.Delete(R"(/connections/(?P<name>[^/]+))", ConnectionHandlers::handleDropConnection);
        svr.Delete(R"(^/connections/([^/]+)$)",  [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            ConnectionHandlers::handleDropConnection(req, res);
        });

        // GET /connections
        svr.Get("/connections", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            ConnectionHandlers::handleListConnections(req, res);
        });

        // POST /connections/select
        svr.Post("/connections/select", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            ConnectionHandlers::handleSelectConnection(req, res);
        });

        std::cout << "Connection API routes registered." << std::endl;

        // --- K8s Routes ---
        svr.Post("/k8s/create", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleCreateK8sCluster(req, res);
        });
        svr.Delete(R"(/k8s/delete/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleDeleteK8sCluster(req, res);
        });
        svr.Get(R"(/k8s/get/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetK8sCluster(req, res);
        });
        svr.Get(R"(/k8s/get-config/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetKubeConfig(req, res);
        });
        svr.Get("/k8s/list", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleListK8sClusters(req, res);
        });
        svr.Get("/k8s/list-locations", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleListK8sLocations(req, res);
        });
        svr.Get("/k8s/healthz", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleK8sHealthCheck(req, res);
        });
        svr.Post(R"(/k8s/resume/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleResumeK8sCluster(req, res);
        });
        svr.Post(R"(/k8s/suspend/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleSuspendK8sCluster(req, res);
        });

        // --- Model Routes ---
        svr.Post("/model/upload", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleUploadModel(req, res);
        });

        // --- SSH Routes ---
        svr.Post("/ssh/create", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleCreateSSHKey(req, res);
        });
        svr.Delete(R"(/ssh/delete/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleDeleteSSHKey(req, res);
        });
        svr.Get("/ssh/list", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListSSHKeys(req, res);
        });

        // --- VM Routes ---
        svr.Post("/vm/create", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleCreateVM(req, res);
        });
        svr.Delete(R"(/vm/delete/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleDeleteVM(req, res);
        });
        svr.Get(R"(/vm/get/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGetVM(req, res);
        });
        svr.Get("/vm/list", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListVMs(req, res);
        });
        svr.Get("/vm/list-locations", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListVMLocations(req, res);
        });
        svr.Get("/vm/list-os", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListVMOSImages(req, res);
        });
        svr.Get("/vm/list-sku", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListVMSKUs(req, res);
        });
        svr.Post(R"(/vm/restart/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleRestartVM(req, res);
        });
        svr.Post(R"(/vm/resume/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleResumeVM(req, res);
        });
        svr.Post(R"(/vm/suspend/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleSuspendVM(req, res);
        });

        // --- OpenShift Routes ---
        svr.Get("/openshift/resources", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenShiftResources(req, res);
        });
        svr.Get("/openshift/clusters", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenShiftClusters(req, res);
        });
        svr.Post("/openshift/clusters/request", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenShiftRequestCluster(req, res);
        });
    }

    // Explicit definition of the destructor for APIRoutes
    // This is crucial because APIRoutes contains a std::unique_ptr to K8sHandlers,
    // and K8sHandlers is only forward-declared in APIRoutes.h.
    // The default destructor for unique_ptr needs the full definition of the type
    // it manages when it's instantiated, which happens when the APIRoutes destructor
    // is defined.
    APIRoutes::~APIRoutes() = default;

    /**
     * @brief Logs incoming HTTP requests to standard output.
     *
     * This method provides basic logging for each request received by the server,
     * including the HTTP method, path, and request body if present.
     *
     * @param req The httplib::Request object representing the incoming request.
     */
    void APIRoutes::logRequest(const httplib::Request& req, const httplib::Response& res) const {
        const std::string requestId = res.get_header_value("X-Request-ID").empty()
                ? req.get_header_value("X-Request-ID")
                : res.get_header_value("X-Request-ID");

        std::cout << "[" << req.method << "] " << req.path;
        if (!requestId.empty()) {
            std::cout << " request_id=" << requestId;
        }
        if (!req.body.empty() && shouldLogBody(req.path)) {
            std::cout << " Body: " << redactBody(req.body);
        } else if (!req.body.empty()) {
            std::cout << " Body: [redacted]";
        }
        std::cout << std::endl;
    }

    /**
     * @brief Sends a JSON response based on a Models::CloudResponse object.
     *
     * Sets the Content-Type header to application/json, determines the HTTP status
     * (200 for success, 400 for client error based on apiResponse.success), and
     * sets the response body to a pretty-printed JSON string.
     *
     * @param res The httplib::Response object to send the response through.
     * @param apiResponse The Models::CloudResponse object containing success status,
     * message, and data to be converted to JSON.
     */
    void APIRoutes::sendJsonResponse(httplib::Response& res, const Models::CloudResponse& apiResponse) const {
        res.set_header("Content-Type", "application/json");
        res.status = apiResponse.success ? 200 : 400; // 200 for success, 400 for client error
        res.body = apiResponse.toJsonString().dump(4); // Pretty print JSON
    }

    /**
     * @brief Sends an error JSON response.
     *
     * Creates a Models::CloudResponse object with success set to false,
     * the provided message, and an empty JSON data object. It then calls
     * sendJsonResponse to format and send this error response, finally
     * setting the HTTP status code explicitly.
     *
     * @param res The httplib::Response object to send the error response through.
     * @param status The HTTP status code for the error (e.g., 400, 404, 500).
     * @param message A string describing the error.
     */
    void APIRoutes::sendErrorResponse(httplib::Response& res, int status, const std::string& message) const {
        Models::CloudResponse apiResponse;
        apiResponse.success = false;
        apiResponse.message = message;
        apiResponse.data = nlohmann::json({});
        sendJsonResponse(res, apiResponse);
        res.status = status; // Set HTTP status code for error
    }

    void APIRoutes::ensureRequestId(const httplib::Request& req, httplib::Response& res) const {
        const std::string existing = req.get_header_value("X-Request-ID");
        if (!existing.empty()) {
            res.set_header("X-Request-ID", existing);
            return;
        }
        res.set_header("X-Request-ID", Utils::generateUniqueId("req"));
    }

    bool APIRoutes::enforceBodyLimit(const httplib::Request& req, httplib::Response& res) const {
        if (req.body.size() > maxBodyBytes) {
            sendErrorResponse(res, 413, "Request body exceeds maximum allowed size.");
            return false;
        }
        return true;
    }

    bool APIRoutes::authorizeOrReject(const httplib::Request& req, httplib::Response& res) const {
        if (!authRequired) {
            return true;
        }
        if (authMode == "token") {
            const std::string bearer = req.get_header_value("Authorization");
            if (!bearer.empty()) {
                const std::string prefix = "Bearer ";
                if (bearer.rfind(prefix, 0) == 0) {
                    const std::string token = bearer.substr(prefix.size());
                    if (!authToken.empty() && token == authToken) {
                        return true;
                    }
                }
            }
            const std::string tokenHeader = req.get_header_value("X-NMC-Token");
            if (!tokenHeader.empty() && !authToken.empty() && tokenHeader == authToken) {
                return true;
            }
            res.set_header("WWW-Authenticate", "Bearer");
            sendErrorResponse(res, 401, "Unauthorized.");
            return false;
        }

        if (authMode == "oidc") {
            if (!oidcValidator || !oidcValidator->isConfigured()) {
                sendErrorResponse(res, 500, "OIDC authentication is misconfigured.");
                return false;
            }
            const std::string bearer = req.get_header_value("Authorization");
            std::string token;
            if (!bearer.empty()) {
                const std::string prefix = "Bearer ";
                if (bearer.rfind(prefix, 0) == 0) {
                    token = bearer.substr(prefix.size());
                }
            }
            if (token.empty()) {
                res.set_header("WWW-Authenticate", "Bearer");
                sendErrorResponse(res, 401, "Unauthorized.");
                return false;
            }

            std::string errorMessage;
            nlohmann::json claims;
            if (!oidcValidator->validateToken(token, errorMessage, &claims)) {
                res.set_header("WWW-Authenticate", "Bearer");
                sendErrorResponse(res, 401, "Unauthorized.");
                return false;
            }
            return true;
        }

        sendErrorResponse(res, 500, "Unsupported authentication mode.");
        return false;
    }

    bool APIRoutes::shouldLogBody(const std::string& path) const {
        // Redact known sensitive endpoints (keys, scripts, credentials, provisioning).
        const std::vector<std::string> redactedPrefixes = {
                "/ssh/create",
                "/vm/create",
                "/model/upload",
                "/connections/make",
                "/openshift/clusters/request"
        };
        for (const auto& prefix : redactedPrefixes) {
            if (path.rfind(prefix, 0) == 0) {
                return false;
            }
        }
        return true;
    }

    std::string APIRoutes::redactBody(const std::string& body) const {
        if (body.size() <= maxLogBodyBytes) {
            return body;
        }
        return body.substr(0, maxLogBodyBytes) + "...(truncated)";
    }

// --- Bucket Handlers ---

    /**
     * @brief Handles the creation of a new bucket.
     *
     * Expects a JSON body with 'name', 'location', and 'type'.
     * Checks for missing parameters, duplicate bucket names, and creates a bucket.
     * Sends a success or error JSON response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleCreateBucket(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string name = json_body.value("name", "");
            std::string location = json_body.value("location", "");
            std::string type = json_body.value("type", "");

            if (name.empty() || location.empty() || type.empty()) {
                return sendErrorResponse(res, 400, "Missing required parameters: name, location, or type.");
            }

            std::lock_guard<std::mutex> lock(dataMutex);
            // Check if bucket already exists
            auto it = std::find_if(buckets.begin(), buckets.end(),
                                   [&](const Models::Bucket& b) { return b.name == name; });
            if (it != buckets.end()) {
                return sendErrorResponse(res, 409, "Bucket '" + name + "' already exists."); // Conflict
            }

            Models::Bucket newBucket;
            newBucket.name = name;
            newBucket.location = location;
            newBucket.type = type;
            newBucket.status = "Active";
            buckets.push_back(newBucket);

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Bucket '" + name + "' created successfully.";
            apiResponse.data = newBucket.toJsonString();
            sendJsonResponse(res, apiResponse);

        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

    /**
     * @brief Handles the deletion of a bucket.
     *
     * Extracts the bucket name from the URL path.
     * Deletes the corresponding bucket if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleDeleteBucket(const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1]; // Get name from regex capture

        std::lock_guard<std::mutex> lock(dataMutex);
        auto initial_size = buckets.size();
        buckets.erase(std::remove_if(buckets.begin(), buckets.end(),
                                         [&](const Models::Bucket& b) { return b.name == name; }),
                          buckets.end());

        if (buckets.size() < initial_size) {
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Bucket '" + name + "' deleted successfully.";
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "Bucket '" + name + "' not found.");
        }
    }

    /**
     * @brief Handles retrieving details of a specific bucket.
     *
     * Extracts the bucket name from the URL path.
     * Finds and returns the bucket's details if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleGetBucket(const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = std::find_if(buckets.begin(), buckets.end(),
                               [&](const Models::Bucket& b) { return b.name == name; });

        if (it != buckets.end()) {
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Bucket details retrieved.";
            apiResponse.data = it->toJsonString();
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "Bucket '" + name + "' not found.");
        }
    }

    /**
     * @brief Handles listing all buckets, with optional name filtering.
     *
     * Supports a 'filter-name' query parameter to filter buckets by name.
     * Returns a JSON array of bucket objects.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListBuckets(const httplib::Request& req, httplib::Response& res) {
        std::string filterName = req.get_param_value("filter-name");

        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json bucketList = nlohmann::json::array();
        for (const auto& bucket : buckets) {
            if (filterName.empty() || bucket.name.find(filterName) != std::string::npos) {
                bucketList.push_back(bucket.toJsonString());
            }
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Buckets listed successfully.";
        apiResponse.data = bucketList;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles listing available bucket locations.
     *
     * Returns a predefined list of bucket locations.
     * In a real system, these would be fetched from a configuration or cloud provider API.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListBucketLocations(const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        // In a real system, these would come from a configuration or backend service
        nlohmann::json locations = {
                {"name", "eu-central"},
                {"name", "us-east"},
                {"name", "asia-southeast"}
        };

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Bucket locations listed successfully.";
        apiResponse.data = locations;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles listing available bucket types.
     *
     * Returns a predefined list of bucket types.
     * In a real system, these would be fetched from a configuration or cloud provider API.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListBucketTypes(const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json types = {
                {"name", "standard"},
                {"name", "archive"},
                {"name", "coldline"}
        };

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Bucket types listed successfully.";
        apiResponse.data = types;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles resetting access credentials for a bucket.
     *
     * Extracts the bucket name from the URL path.
     * Simulates resetting credentials for the bucket if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleResetBucketKey(const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = std::find_if(buckets.begin(), buckets.end(),
                               [&](const Models::Bucket& b) { return b.name == name; });

        if (it != buckets.end()) {
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Bucket '" + name + "' access credentials reset successfully.";
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "Bucket '" + name + "' not found.");
        }
    }

// --- Model Handlers ---
    /**
     * @brief Handles the upload of a model.
     *
     * Expects a JSON body with 'filePath', 'sku', 'registryName', and 'tag'.
     * This is a implementation that only acknowledges the upload without
     * actual file handling.
     * Sends a success or error JSON response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleUploadModel(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string filePath = json_body.value("filePath", "");
            std::string sku = json_body.value("sku", "");
            std::string registryName = json_body.value("registryName", "");
            std::string tag = json_body.value("tag", "");

            if (filePath.empty() || sku.empty() || registryName.empty() || tag.empty()) {
                return sendErrorResponse(res, 400, "Missing required parameters: filePath, sku, registryName, or tag.");
            }

            // In a real scenario, you'd handle file upload logic here
            // For mock, just acknowledge the upload
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Model '" + sku + "' version '" + tag + "' uploaded to registry '" + registryName + "' successfully.";
            apiResponse.data = {
                    {"filePath", filePath},
                    {"sku", sku},
                    {"registryName", registryName},
                    {"tag", tag}
            };
            sendJsonResponse(res, apiResponse);

        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

// --- SSH Key Handlers ---
    /**
     * @brief Handles the creation of a new SSH key.
     *
     * Expects a JSON body with 'name' and 'publicKey', and optional 'description'.
     * Checks for missing parameters, duplicate key names, and creates a SSH key.
     * Sends a success or error JSON response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleCreateSSHKey(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string name = json_body.value("name", "");
            std::string publicKey = json_body.value("publicKey", "");
            std::string description = json_body.value("description", "");

            if (name.empty() || publicKey.empty()) {
                return sendErrorResponse(res, 400, "Missing required parameters: name or publicKey.");
            }

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(sshKeys.begin(), sshKeys.end(),
                                   [&](const Models::SSHKey& k) { return k.name == name; });
            if (it != sshKeys.end()) {
                return sendErrorResponse(res, 409, "SSH key '" + name + "' already exists.");
            }

            Models::SSHKey newKey;
            newKey.id = Utils::generateUniqueId("sshkey");
            newKey.name = name;
            newKey.publicKey = publicKey;
            newKey.description = description;
            sshKeys.push_back(newKey);

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "SSH key '" + name + "' created successfully.";
            apiResponse.data = newKey.toJsonString();
            sendJsonResponse(res, apiResponse);

        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

    /**
     * @brief Handles the deletion of an SSH key.
     *
     * Extracts the key ID from the URL path.
     * Deletes the corresponding SSH key if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleDeleteSSHKey(const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto initial_size = sshKeys.size();
        sshKeys.erase(std::remove_if(sshKeys.begin(), sshKeys.end(),
                                         [&](const Models::SSHKey& k) { return k.id == id; }),
                          sshKeys.end());

        if (sshKeys.size() < initial_size) {
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "SSH key '" + id + "' deleted successfully.";
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "SSH key '" + id + "' not found.");
        }
    }

    /**
     * @brief Handles listing all SSH keys, with optional name filtering.
     *
     * Supports a 'filter-name' query parameter to filter keys by name.
     * Returns a JSON array of SSH key objects.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListSSHKeys(const httplib::Request& req, httplib::Response& res) {
        std::string filterName = req.get_param_value("filter-name");

        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json keyList = nlohmann::json::array();
        for (const auto& key : sshKeys) {
            if (filterName.empty() || key.name.find(filterName) != std::string::npos) {
                keyList.push_back(key.toJsonString());
            }
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "SSH keys listed successfully.";
        apiResponse.data = keyList;
        sendJsonResponse(res, apiResponse);
    }

// --- VM Handlers ---
    /**
     * @brief Handles the creation of a new Virtual Machine.
     *
     * Expects a JSON body with 'name', 'sku', 'region', 'osImage', and 'publicKeyId',
     * and optional 'initScript'.
     * Checks for missing parameters, duplicate VM names, valid publicKeyId,
     * and creates a VM.
     * Sends a success or error JSON response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleCreateVM(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string name = json_body.value("name", "");
            std::string sku = json_body.value("sku", "");
            std::string region = json_body.value("region", "");
            std::string osImage = json_body.value("osImage", "");
            std::string publicKeyId = json_body.value("publicKeyId", "");
            std::string initScript = json_body.value("initScript", "");

            if (name.empty() || sku.empty() || region.empty() || osImage.empty() || publicKeyId.empty()) {
                return sendErrorResponse(res, 400, "Missing required parameters: name, sku, region, osImage, or publicKeyId.");
            }

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(vms.begin(), vms.end(),
                                   [&](const Models::VM& vm) { return vm.name == name; });
            if (it != vms.end()) {
                return sendErrorResponse(res, 409, "VM '" + name + "' already exists.");
            }

            // Check if publicKeyId exists in sshKeys
            auto sshKeyIt = std::find_if(sshKeys.begin(), sshKeys.end(),
                                         [&](const Models::SSHKey& k) { return k.id == publicKeyId; });
            if (sshKeyIt == sshKeys.end()) {
                return sendErrorResponse(res, 400, "Invalid publicKeyId: '" + publicKeyId + "' not found.");
            }


            Models::VM newVM;
            newVM.id = Utils::generateUniqueId("vm");
            newVM.name = name;
            newVM.sku = sku;
            newVM.region = region;
            newVM.osImage = osImage;
            newVM.publicKeyId = publicKeyId;
            newVM.initScript = initScript;
            newVM.status = "Creating";
            vms.push_back(newVM);

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM '" + name + "' created successfully.";
            apiResponse.data = newVM.toJsonString();
            sendJsonResponse(res, apiResponse);

        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

    /**
     * @brief Handles the deletion of a Virtual Machine.
     *
     * Extracts the VM ID from the URL path.
     * Deletes the corresponding VM if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleDeleteVM(const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto initial_size = vms.size();
        vms.erase(std::remove_if(vms.begin(), vms.end(),
                                     [&](const Models::VM& vm) { return vm.id == id; }),
                      vms.end());

        if (vms.size() < initial_size) {
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM '" + id + "' deleted successfully.";
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "VM '" + id + "' not found.");
        }
    }

    /**
     * @brief Handles retrieving details of a specific Virtual Machine.
     *
     * Extracts the VM ID from the URL path.
     * Finds and returns the VM's details if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleGetVM(const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = std::find_if(vms.begin(), vms.end(),
                               [&](const Models::VM& vm) { return vm.id == id; });

        if (it != vms.end()) {
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM details retrieved.";
            apiResponse.data = it->toJsonString();
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "VM '" + id + "' not found.");
        }
    }

    /**
     * @brief Handles listing all Virtual Machines, with optional name filtering.
     *
     * Supports a 'filter-name' query parameter to filter VMs by name.
     * Returns a JSON array of VM objects.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListVMs(const httplib::Request& req, httplib::Response& res) {
        std::string filterName = req.get_param_value("filter-name");

        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json vmList = nlohmann::json::array();
        for (const auto& vm : vms) {
            if (filterName.empty() || vm.name.find(filterName) != std::string::npos) {
                vmList.push_back(vm.toJsonString());
            }
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "VMs listed successfully.";
        apiResponse.data = vmList;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles listing available VM locations (regions).
     *
     * Returns a predefined list of VM locations.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListVMLocations(const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json locations = {
                {"name", "gb-mids"},
                {"name", "gb-mids"},
                {"name", "us-central"}
        };

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "VM locations listed successfully.";
        apiResponse.data = locations;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles listing available VM OS images.
     *
     * Returns a predefined list of VM OS images (SKUs).
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListVMOSImages(const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json osImages = {
                {"sku", "ubuntu-22.04"},
                {"sku", "windows-server-2022"},
                {"sku", "debian-11"}
        };

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "VM OS images listed successfully.";
        apiResponse.data = osImages;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles listing available VM SKUs (machine types).
     *
     * Returns a predefined list of VM SKUs.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleListVMSKUs(const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dataMutex);
        nlohmann::json skus = {
                {"sku", "nmx-h100-80"},
                {"sku", "standard-a2"},
                {"sku", "premium-gpu-p100"}
        };

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "VM SKUs listed successfully.";
        apiResponse.data = skus;
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles restarting a Virtual Machine.
     *
     * Extracts the VM ID from the URL path.
     * Changes the status of the VM to "Restarting" if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleRestartVM(const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = std::find_if(vms.begin(), vms.end(),
                               [&](const Models::VM& vm) { return vm.id == id; });

        if (it != vms.end()) {
            it->status = "Restarting"; // Change status
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM '" + id + "' is restarting.";
            apiResponse.data = it->toJsonString();
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "VM '" + id + "' not found.");
        }
    }

    /**
     * @brief Handles resuming a suspended Virtual Machine.
     *
     * Extracts the VM ID from the URL path.
     * Changes the status of the VM to "Running" if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleResumeVM(const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = std::find_if(vms.begin(), vms.end(),
                               [&](const Models::VM& vm) { return vm.id == id; });

        if (it != vms.end()) {
            it->status = "Running"; // Change status
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM '" + id + "' resumed successfully.";
            apiResponse.data = it->toJsonString();
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "VM '" + id + "' not found.");
        }
    }

    /**
     * @brief Handles suspending a running Virtual Machine.
     *
     * Extracts the VM ID from the URL path.
     * Changes the status of the VM to "Suspended" if found.
     * Sends a success or 404 Not Found error response.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleSuspendVM(const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = std::find_if(vms.begin(), vms.end(),
                               [&](const Models::VM& vm) { return vm.id == id; });

        if (it != vms.end()) {
            it->status = "Suspended"; // Change status
            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM '" + id + "' suspended successfully.";
            apiResponse.data = it->toJsonString();
            sendJsonResponse(res, apiResponse);
        } else {
            sendErrorResponse(res, 404, "VM '" + id + "' not found.");
        }
    }

// --- OpenShift Handlers ---
    void APIRoutes::handleOpenShiftResources(const httplib::Request& req, httplib::Response& res) {
        Models::CloudResponse apiResponse = openShiftClient->getResources();
        sendJsonResponse(res, apiResponse);
        res.status = apiResponse.statusCode;
    }

    void APIRoutes::handleOpenShiftClusters(const httplib::Request& req, httplib::Response& res) {
        Models::CloudResponse apiResponse = openShiftClient->listClusters();
        if (apiResponse.success) {
            if (apiResponse.data.is_array()) {
                for (auto& cluster : apiResponse.data) {
                    normalizeClusterStatus(cluster);
                }
            } else if (apiResponse.data.is_object()) {
                if (apiResponse.data.contains("data") && apiResponse.data["data"].is_array()) {
                    for (auto& cluster : apiResponse.data["data"]) {
                        normalizeClusterStatus(cluster);
                    }
                }
                if (apiResponse.data.contains("clusters") && apiResponse.data["clusters"].is_array()) {
                    for (auto& cluster : apiResponse.data["clusters"]) {
                        normalizeClusterStatus(cluster);
                    }
                }
            }
        }
        sendJsonResponse(res, apiResponse);
        res.status = apiResponse.statusCode;
    }

    void APIRoutes::handleOpenShiftRequestCluster(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);

            // Validate minimum required fields before forwarding to oshift.
            const std::string name = json_body.value("name", "");
            const std::string organization = json_body.value("organization", "");
            const int gpuCount = json_body.value("gpu_count", 0);
            const std::string architecture = json_body.value("architecture", "");
            const std::string region = json_body.value("region", "");
            const std::string provider = json_body.value("provider", "on-prem");

            if (name.empty() || organization.empty() || architecture.empty() || region.empty() || gpuCount <= 0) {
                return sendErrorResponse(res, 400, "Missing required parameters: name, organization, gpu_count, architecture, or region.");
            }
            if (provider == "hybrid-burst" && (!json_body.contains("burst_targets") || json_body["burst_targets"].empty())) {
                return sendErrorResponse(res, 400, "burst_targets is required when provider is hybrid-burst.");
            }

            Models::CloudResponse apiResponse = openShiftClient->requestCluster(json_body);
            if (apiResponse.success && apiResponse.data.is_object()) {
                if (apiResponse.data.contains("cluster")) {
                    normalizeClusterStatus(apiResponse.data["cluster"]);
                }
            }
            sendJsonResponse(res, apiResponse);
            res.status = apiResponse.statusCode;
        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

} // namespace NMC::Server
