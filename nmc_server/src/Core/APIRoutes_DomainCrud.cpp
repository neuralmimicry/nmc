// APIRoutes_DomainCrud.cpp
// Domain CRUD route registration and handlers for connections, storage, cluster lifecycle, and server snapshots.

#include "APIRoutes.h"
#include "K8sHandlers.h"
#include "VersionCheck.h"
#include "Utils.h"

#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <ifaddrs.h>
#include <iomanip>
#include <iostream>
#include <map>
#include <netdb.h>
#include <poll.h>
#include <set>
#include <sstream>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <vector>

namespace NMC::Server {

    namespace {
#include "APIRoutes_InternalHelpers.inl"
    }

    void APIRoutes::registerDomainCrudRoutes(httplib::Server& svr, const APIRoutes::RouteGuard& guard) {
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
            handleGetConnectionStatus(req, res);
        });

        // POST /connections/make
        svr.Post("/connections/make", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleMakeConnection(req, res);
        });

        // DELETE /connections/:name
        svr.Delete(R"(^/connections/([^/]+)$)",  [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleDropConnection(req, res);
        });

        // GET /connections
        svr.Get("/connections", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListConnections(req, res);
        });

        // POST /connections/select
        svr.Post("/connections/select", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleSelectConnection(req, res);
        });

        std::cout << "Connection API routes registered." << std::endl;

        // --- K8s Routes ---
        svr.Post("/k8s/create", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            std::string clusterName = "unknown";
            std::string region;
            std::string traceyAgentId;
            std::string traceyStatusAddr;
            try {
                auto jsonBody = nlohmann::json::parse(req.body);
                clusterName = jsonBody.value("name", clusterName);
                region = jsonBody.value("region", "");
                std::string reason;
                if (!extractTraceyProvisioningRequest(jsonBody, traceyAgentId, traceyStatusAddr, reason)) {
                    return sendErrorResponse(res, 400, reason);
                }
            } catch (const nlohmann::json::parse_error& e) {
                return sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
            }
            k8sHandlers->handleCreateK8sCluster(req, res);
            if (res.status >= 200 && res.status < 300 && !traceyAgentId.empty()) {
                std::lock_guard<std::mutex> lock(dataMutex);
                upsertTraceyRequirementLocked("k8s_cluster", clusterName, region, traceyAgentId, traceyStatusAddr, nowEpochMs());
            }
        });
        svr.Delete(R"(/k8s/delete/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            const std::string clusterId = req.matches.size() > 1 ? req.matches[1].str() : "";
            k8sHandlers->handleDeleteK8sCluster(req, res);
            if (res.status >= 200 && res.status < 300 && !clusterId.empty()) {
                std::lock_guard<std::mutex> lock(dataMutex);
                removeTraceyRequirementLocked("k8s_cluster", clusterId);
            }
        });
        svr.Get(R"(/k8s/get/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetK8sCluster(req, res);
        });
        svr.Get(R"(/k8s/details/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetK8sClusterDetails(req, res);
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

        registerReleaseOperateRoutes(svr, guard);

        // --- AARNN Routes ---
        svr.Get("/aarnn/endpoints", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleAarnnEndpoints(req, res);
        });
        svr.Get("/aarnn/inventory", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleAarnnInventory(req, res);
        });
        svr.Post(R"(^/aarnn/proxy/([^/]+)$)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            const std::string plane = req.matches.size() > 1 ? req.matches[1].str() : "";
            handleAarnnProxy(req, res, plane);
        });

        // --- Gail Trading Bridge Routes ---
        // These proxy Gail's /v1/trading/* endpoints so the NMC dashboard and CLI
        // can observe and control the trading bridge without direct Gail connectivity.
        svr.Get("/gail/trading/status", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGailTradingStatus(req, res);
        });
        svr.Get("/gail/trading/portfolio", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGailTradingPortfolio(req, res);
        });
        svr.Get("/gail/trading/positions", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGailTradingPositions(req, res);
        });
        svr.Get("/gail/trading/history", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGailTradingHistory(req, res);
        });
        svr.Get("/gail/trading/logs", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGailTradingLogs(req, res);
        });
        svr.Get("/gail/trading/exchanges", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGailTradingExchanges(req, res);
        });
        svr.Get("/gail/trading/currencies", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGailTradingCurrencies(req, res);
        });
        svr.Get("/gail/trading/config", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGailTradingGetConfig(req, res);
        });
        svr.Post("/gail/trading/config", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGailTradingSetConfig(req, res);
        });
        svr.Post("/gail/trading/pause", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGailTradingPause(req, res);
        });
        svr.Post("/gail/trading/resume", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGailTradingResume(req, res);
        });
        svr.Post("/gail/trading/override", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGailTradingOverride(req, res);
        });
        svr.Post("/gail/trading/evaluate", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGailTradingEvaluate(req, res);
        });

        // --- VCluster Routes ---
        svr.Post("/vcluster/create", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleCreateVCluster(req, res);
        });
        svr.Delete(R"(/vcluster/delete/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleDeleteVCluster(req, res);
        });
        svr.Get(R"(/vcluster/get/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetVCluster(req, res);
        });
        svr.Get("/vcluster/list", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleListVClusters(req, res);
        });
        svr.Get(R"(/vcluster/get-config/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetVClusterKubeConfig(req, res);
        });

        // --- VCluster Lifecycle Routes ---
        svr.Post(R"(/vcluster/pause/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handlePauseVCluster(req, res);
        });
        svr.Post(R"(/vcluster/resume/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleResumeVCluster(req, res);
        });
        svr.Post(R"(/vcluster/backup/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleBackupVCluster(req, res);
        });
        svr.Post("/vcluster/restore", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleRestoreVCluster(req, res);
        });
        svr.Post(R"(/vcluster/upgrade/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleUpgradeVCluster(req, res);
        });

        // --- VCluster Configuration Routes ---
        svr.Get(R"(/vcluster/config/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetVClusterConfig(req, res);
        });
        svr.Put(R"(/vcluster/config/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleUpdateVClusterConfig(req, res);
        });

        // --- VCluster Monitoring Routes ---
        svr.Get(R"(/vcluster/metrics/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetVClusterMetrics(req, res);
        });
        svr.Get(R"(/vcluster/health/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetVClusterHealth(req, res);
        });
        svr.Get(R"(/vcluster/resources/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetVClusterResources(req, res);
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
        svr.Get(R"(/ssh/get/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGetSSHKey(req, res);
        });
        svr.Get("/ssh/list", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListSSHKeys(req, res);
        });

    }

    void APIRoutes::handleGetConnectionStatus(const httplib::Request& req, httplib::Response& res) {
        (void)req;

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Current connection status.";

        nlohmann::json responseData;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            normalizeConnectionRegistry(connections, currentConnectionName);

            auto it = currentConnectionName.empty()
                      ? connections.end()
                      : std::find_if(
                              connections.begin(),
                              connections.end(),
                              [&](const Models::Connection& connection) {
                                  return connection.name == currentConnectionName;
                              }
                      );

            if (it != connections.end()) {
                it->healthStatus = simulateConnectionHealthCheck(it->endpoint);
                responseData = it->toJsonString();
            } else {
                apiResponse.message = "No active cloud connection. Local Kubernetes mode is available.";
                responseData = nlohmann::json{
                        {"name", ""},
                        {"endpoint", ""},
                        {"isActive", false},
                        {"status", "local_only"},
                        {"mode", "local"}
                };
            }
        }

        apiResponse.data = std::move(responseData);
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleMakeConnection(const httplib::Request& req, httplib::Response& res) {
        Models::CloudResponse apiResponse;
        bool created = false;
        int64_t mutationTs = 0;
        nlohmann::json eventPayload = nlohmann::json::object();
        std::string eventKey;

        try {
            const nlohmann::json params = parseJsonBodyStrict(req.body);

            std::string name;
            if (params.contains("name") && params["name"].is_string()) {
                name = params["name"].get<std::string>();
            } else {
                return sendErrorResponse(res, 400, "Connection name is required and must be a string.");
            }

            std::string endpoint;
            if (params.contains("endpoint") && params["endpoint"].is_string()) {
                endpoint = params["endpoint"].get<std::string>();
            } else {
                return sendErrorResponse(res, 400, "Endpoint is required and must be a string.");
            }

            bool setDefault = false;
            if (params.contains("setDefault")) {
                if (params["setDefault"].is_boolean()) {
                    setDefault = params["setDefault"].get<bool>();
                } else if (params["setDefault"].is_string()) {
                    const std::string setDefaultStr = params["setDefault"].get<std::string>();
                    setDefault = (setDefaultStr == "true");
                }
            }

            if (trim(name).empty() || trim(endpoint).empty()) {
                return sendErrorResponse(res, 400, "Connection name and endpoint cannot be empty.");
            }

            nlohmann::json responseData;
            {
                std::lock_guard<std::mutex> lock(dataMutex);
                auto it = std::find_if(
                        connections.begin(),
                        connections.end(),
                        [&](const Models::Connection& connection) { return connection.name == name; }
                );
                if (it != connections.end()) {
                    return sendErrorResponse(res, 409, "Connection with name '" + name + "' already exists.");
                }

                Models::Connection newConnection(
                        name,
                        endpoint,
                        setDefault,
                        simulateConnectionHealthCheck(endpoint)
                );
                connections.push_back(newConnection);
                if (setDefault) {
                    currentConnectionName = name;
                }
                normalizeConnectionRegistry(connections, currentConnectionName);

                auto stored = std::find_if(
                        connections.begin(),
                        connections.end(),
                        [&](const Models::Connection& connection) { return connection.name == name; }
                );
                const nlohmann::json storedConnection = stored != connections.end() ? stored->toJsonString() : newConnection.toJsonString();
                responseData = storedConnection;
                apiResponse.success = true;
                apiResponse.message = setDefault
                                      ? "Connection '" + name + "' created and set as default."
                                      : "Connection '" + name + "' created.";
                apiResponse.data = std::move(responseData);
                created = true;
                mutationTs = nowEpochMs();
                eventKey = name;
                eventPayload = {
                        {"connection", storedConnection},
                        {"current_connection_name", currentConnectionName}
                };
            }
        } catch (const std::runtime_error& error) {
            return sendErrorResponse(res, 400, std::string("Error processing request: ") + error.what());
        } catch (const std::exception& error) {
            return sendErrorResponse(res, 500, std::string("An unexpected error occurred: ") + error.what());
        }

        if (created) {
            scheduleServerStateSnapshot(mutationTs);
            recordServerStateEvent("connection", eventKey, "created", mutationTs, eventPayload);
        }
        sendJsonResponse(res, apiResponse);
        if (created) {
            res.status = 201;
        }
    }

    void APIRoutes::handleDropConnection(const httplib::Request& req, httplib::Response& res) {
        std::string name;
        if (req.matches.size() > 1) {
            name = req.matches[1];
        }
        if (trim(name).empty()) {
            return sendErrorResponse(res, 400, "Connection name missing or empty in URL path.");
        }

        Models::CloudResponse apiResponse;
        bool removed = false;
        int64_t mutationTs = 0;
        nlohmann::json eventPayload = nlohmann::json::object();

        {
            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(
                    connections.begin(),
                    connections.end(),
                    [&](const Models::Connection& connection) { return connection.name == name; }
            );
            if (it == connections.end()) {
                return sendErrorResponse(res, 404, "Connection with name '" + name + "' not found.");
            }

            const bool wasSelected = currentConnectionName == name;
            const nlohmann::json removedConnection = it->toJsonString();
            connections.erase(it);

            if (wasSelected) {
                currentConnectionName.clear();
            }
            normalizeConnectionRegistry(connections, currentConnectionName);

            apiResponse.success = true;
            apiResponse.message = currentConnectionName.empty()
                                  ? "Connection '" + name + "' dropped. No active connection now."
                                  : "Connection '" + name + "' dropped.";
            removed = true;
            mutationTs = nowEpochMs();
            eventPayload = {
                    {"connection", removedConnection},
                    {"was_selected", wasSelected},
                    {"current_connection_name", currentConnectionName}
            };
        }

        if (removed) {
            scheduleServerStateSnapshot(mutationTs);
            recordServerStateEvent("connection", name, "dropped", mutationTs, eventPayload);
        }
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleListConnections(const httplib::Request& req, httplib::Response& res) {
        (void)req;

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Connections listed successfully.";

        nlohmann::json connectionsJson = nlohmann::json::array();
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            normalizeConnectionRegistry(connections, currentConnectionName);
            for (auto& connection : connections) {
                connection.healthStatus = simulateConnectionHealthCheck(connection.endpoint);
                connectionsJson.push_back(connection.toJsonString());
            }
        }

        apiResponse.data = std::move(connectionsJson);
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleSelectConnection(const httplib::Request& req, httplib::Response& res) {
        Models::CloudResponse apiResponse;
        int64_t mutationTs = 0;
        nlohmann::json eventPayload = nlohmann::json::object();
        std::string eventKey;

        try {
            const nlohmann::json params = parseJsonBodyStrict(req.body);
            if (!params.contains("name") || !params["name"].is_string()) {
                return sendErrorResponse(res, 400, "Connection name is required and must be a string.");
            }

            const std::string name = trim(params["name"].get<std::string>());
            if (name.empty()) {
                return sendErrorResponse(res, 400, "Connection name cannot be empty.");
            }

            {
                std::lock_guard<std::mutex> lock(dataMutex);
                const std::string previousConnectionName = currentConnectionName;
                auto it = std::find_if(
                        connections.begin(),
                        connections.end(),
                        [&](const Models::Connection& connection) { return connection.name == name; }
                );
                if (it == connections.end()) {
                    return sendErrorResponse(res, 404, "Connection with name '" + name + "' not found.");
                }

                currentConnectionName = name;
                normalizeConnectionRegistry(connections, currentConnectionName);

                apiResponse.success = true;
                apiResponse.message = "Connection '" + name + "' selected as default.";
                mutationTs = nowEpochMs();
                eventKey = name;
                eventPayload = {
                        {"connection", it->toJsonString()},
                        {"previous_connection_name", previousConnectionName},
                        {"current_connection_name", currentConnectionName}
                };
            }
        } catch (const std::runtime_error& error) {
            return sendErrorResponse(res, 400, std::string("Error processing request: ") + error.what());
        } catch (const std::exception& error) {
            return sendErrorResponse(res, 500, std::string("An unexpected error occurred: ") + error.what());
        }

        scheduleServerStateSnapshot(mutationTs);
        recordServerStateEvent("connection", eventKey, "selected", mutationTs, eventPayload);
        sendJsonResponse(res, apiResponse);
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
            int64_t mutationTs = 0;
            Models::CloudResponse apiResponse;
            nlohmann::json eventPayload = nlohmann::json::object();

            if (name.empty() || location.empty() || type.empty()) {
                return sendErrorResponse(res, 400, "Missing required parameters: name, location, or type.");
            }

            {
                std::lock_guard<std::mutex> lock(dataMutex);
                auto it = std::find_if(buckets.begin(), buckets.end(),
                                       [&](const Models::Bucket& b) { return b.name == name; });
                if (it != buckets.end()) {
                    return sendErrorResponse(res, 409, "Bucket '" + name + "' already exists.");
                }

                Models::Bucket newBucket;
                newBucket.name = name;
                newBucket.location = location;
                newBucket.type = type;
                newBucket.status = "Active";
                buckets.push_back(newBucket);
                mutationTs = nowEpochMs();

                apiResponse.success = true;
                apiResponse.message = "Bucket '" + name + "' created successfully.";
                apiResponse.data = newBucket.toJsonString();
                eventPayload = {
                        {"bucket", newBucket.toJsonString()}
                };
            }

            scheduleServerStateSnapshot(mutationTs);
            recordServerStateEvent("bucket", name, "created", mutationTs, eventPayload);
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
        int64_t mutationTs = 0;
        Models::CloudResponse apiResponse;
        nlohmann::json eventPayload = nlohmann::json::object();

        {
            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(
                    buckets.begin(),
                    buckets.end(),
                    [&](const Models::Bucket& bucket) { return bucket.name == name; }
            );
            if (it == buckets.end()) {
                return sendErrorResponse(res, 404, "Bucket '" + name + "' not found.");
            }
            eventPayload = {
                    {"bucket", it->toJsonString()}
            };
            buckets.erase(it);
            mutationTs = nowEpochMs();
            apiResponse.success = true;
            apiResponse.message = "Bucket '" + name + "' deleted successfully.";
        }
        scheduleServerStateSnapshot(mutationTs);
        recordServerStateEvent("bucket", name, "deleted", mutationTs, eventPayload);
        sendJsonResponse(res, apiResponse);
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
            int64_t mutationTs = 0;
            Models::CloudResponse apiResponse;

            if (name.empty() || publicKey.empty()) {
                return sendErrorResponse(res, 400, "Missing required parameters: name or publicKey.");
            }

            {
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
                mutationTs = nowEpochMs();

                apiResponse.success = true;
                apiResponse.message = "SSH key '" + name + "' created successfully.";
                apiResponse.data = newKey.toJsonString();
            }

            scheduleServerStateSnapshot(mutationTs);
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
        int64_t mutationTs = 0;
        Models::CloudResponse apiResponse;
        bool removed = false;

        {
            std::lock_guard<std::mutex> lock(dataMutex);
            auto initial_size = sshKeys.size();
            sshKeys.erase(std::remove_if(sshKeys.begin(), sshKeys.end(),
                                             [&](const Models::SSHKey& k) { return k.id == id; }),
                              sshKeys.end());

            if (sshKeys.size() < initial_size) {
                mutationTs = nowEpochMs();
                apiResponse.success = true;
                apiResponse.message = "SSH key '" + id + "' deleted successfully.";
                removed = true;
            }
        }

        if (!removed) {
            return sendErrorResponse(res, 404, "SSH key '" + id + "' not found.");
        }
        scheduleServerStateSnapshot(mutationTs);
        sendJsonResponse(res, apiResponse);
    }

    /**
     * @brief Handles retrieval of a single SSH key by ID.
     *
     * Returns redacted key metadata when found.
     *
     * @param req The httplib::Request object.
     * @param res The httplib::Response object.
     */
    void APIRoutes::handleGetSSHKey(const httplib::Request& req, httplib::Response& res) {
        const std::string id = req.matches[1];
        if (id.empty()) {
            return sendErrorResponse(res, 400, "SSH key ID is required.");
        }

        std::lock_guard<std::mutex> lock(dataMutex);
        const auto it = std::find_if(sshKeys.begin(), sshKeys.end(),
                                     [&](const Models::SSHKey& k) { return k.id == id; });
        if (it == sshKeys.end()) {
            return sendErrorResponse(res, 404, "SSH key '" + id + "' not found.");
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "SSH key '" + id + "' retrieved successfully.";
        apiResponse.data = it->toJsonString();
        sendJsonResponse(res, apiResponse);
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

    nlohmann::json APIRoutes::buildServerStateSnapshotLocked(int64_t nowMs) const {
        std::vector<Models::Bucket> sortedBuckets = buckets;
        std::sort(
                sortedBuckets.begin(),
                sortedBuckets.end(),
                [](const Models::Bucket& left, const Models::Bucket& right) {
                    return std::tie(left.name, left.location, left.type, left.status) <
                           std::tie(right.name, right.location, right.type, right.status);
                }
        );

        std::vector<Models::Connection> sortedConnections = connections;
        std::sort(
                sortedConnections.begin(),
                sortedConnections.end(),
                [](const Models::Connection& left, const Models::Connection& right) {
                    return std::tie(left.name, left.endpoint, left.isActive, left.healthStatus) <
                           std::tie(right.name, right.endpoint, right.isActive, right.healthStatus);
                }
        );

        std::vector<Models::VM> sortedVms = vms;
        std::sort(
                sortedVms.begin(),
                sortedVms.end(),
                [](const Models::VM& left, const Models::VM& right) {
                    return std::tie(left.id, left.name, left.region) <
                           std::tie(right.id, right.name, right.region);
                }
        );

        std::vector<Models::SSHKey> sortedSshKeys = sshKeys;
        std::sort(
                sortedSshKeys.begin(),
                sortedSshKeys.end(),
                [](const Models::SSHKey& left, const Models::SSHKey& right) {
                    return std::tie(left.name, left.id, left.description) <
                           std::tie(right.name, right.id, right.description);
                }
        );

        std::vector<Models::K8sCluster> sortedK8sClusters = k8sClusters;
        std::sort(
                sortedK8sClusters.begin(),
                sortedK8sClusters.end(),
                [](const Models::K8sCluster& left, const Models::K8sCluster& right) {
                    return std::tie(left.name, left.id, left.region, left.status, left.is_vcluster) <
                           std::tie(right.name, right.id, right.region, right.status, right.is_vcluster);
                }
        );

        std::vector<std::string> vclusterIds;
        vclusterIds.reserve(vclusterConfigs.size());
        for (const auto& entry : vclusterConfigs) {
            vclusterIds.push_back(entry.first);
        }
        std::sort(vclusterIds.begin(), vclusterIds.end());

        nlohmann::json bucketPayload = nlohmann::json::array();
        for (const auto& bucket : sortedBuckets) {
            bucketPayload.push_back(bucket.toJsonString());
        }

        nlohmann::json connectionPayload = nlohmann::json::array();
        for (const auto& connection : sortedConnections) {
            connectionPayload.push_back(connectionSnapshotToJson(connection));
        }

        nlohmann::json vmPayload = nlohmann::json::array();
        for (const auto& vm : sortedVms) {
            vmPayload.push_back(vmSnapshotToJson(vm));
        }

        nlohmann::json sshKeyPayload = nlohmann::json::array();
        for (const auto& sshKey : sortedSshKeys) {
            sshKeyPayload.push_back(sshKeySnapshotToJson(sshKey));
        }

        nlohmann::json k8sClusterPayload = nlohmann::json::array();
        for (const auto& cluster : sortedK8sClusters) {
            k8sClusterPayload.push_back(k8sClusterSnapshotToJson(cluster));
        }

        nlohmann::json vclusterPayload = nlohmann::json::array();
        for (const auto& configId : vclusterIds) {
            const auto it = vclusterConfigs.find(configId);
            if (it == vclusterConfigs.end()) {
                continue;
            }
            nlohmann::json configPayload = vclusterConfigSnapshotToJson(it->second);
            configPayload["id"] = it->second.id.empty() ? configId : it->second.id;
            vclusterPayload.push_back(std::move(configPayload));
        }

        return {
                {"schema_version", 1},
                {"generated_epoch_ms", nowMs > 0 ? nowMs : nowEpochMs()},
                {"current_connection_name", currentConnectionName},
                {"buckets", std::move(bucketPayload)},
                {"connections", std::move(connectionPayload)},
                {"k8s_clusters", std::move(k8sClusterPayload)},
                {"ssh_keys", std::move(sshKeyPayload)},
                {"vms", std::move(vmPayload)},
                {"vcluster_configs", std::move(vclusterPayload)}
        };
    }

    void APIRoutes::restoreServerStateSnapshotLocked(const nlohmann::json& snapshot) {
        if (!snapshot.is_object()) {
            return;
        }

        buckets.clear();
        connections.clear();
        k8sClusters.clear();
        sshKeys.clear();
        vms.clear();
        vclusterConfigs.clear();
        currentConnectionName.clear();

        for (const auto& bucketPayload : snapshot.value("buckets", nlohmann::json::array())) {
            if (!bucketPayload.is_object()) {
                continue;
            }
            Models::Bucket bucket = Models::Bucket::fromJson(bucketPayload);
            if (!trim(bucket.name).empty()) {
                buckets.push_back(std::move(bucket));
            }
        }

        for (const auto& connectionPayload : snapshot.value("connections", nlohmann::json::array())) {
            Models::Connection connection = connectionSnapshotFromJson(connectionPayload);
            if (!trim(connection.name).empty()) {
                connections.push_back(std::move(connection));
            }
        }
        currentConnectionName = snapshot.value("current_connection_name", "");
        normalizeConnectionRegistry(connections, currentConnectionName);

        for (const auto& clusterPayload : snapshot.value("k8s_clusters", nlohmann::json::array())) {
            Models::K8sCluster cluster = k8sClusterSnapshotFromJson(clusterPayload);
            if (!trim(cluster.id).empty() || !trim(cluster.name).empty()) {
                k8sClusters.push_back(std::move(cluster));
            }
        }

        for (const auto& sshKeyPayload : snapshot.value("ssh_keys", nlohmann::json::array())) {
            Models::SSHKey key = sshKeySnapshotFromJson(sshKeyPayload);
            if (!trim(key.id).empty() || !trim(key.name).empty()) {
                sshKeys.push_back(std::move(key));
            }
        }

        for (const auto& vmPayload : snapshot.value("vms", nlohmann::json::array())) {
            Models::VM vm = vmSnapshotFromJson(vmPayload);
            if (!trim(vm.id).empty() || !trim(vm.name).empty()) {
                vms.push_back(std::move(vm));
            }
        }

        for (const auto& configPayload : snapshot.value("vcluster_configs", nlohmann::json::array())) {
            if (!configPayload.is_object()) {
                continue;
            }
            Models::VClusterConfig config = Models::VClusterConfig::fromJson(configPayload);
            if (config.id.empty()) {
                config.id = configPayload.value("id", "");
            }
            if (!config.id.empty()) {
                vclusterConfigs[config.id] = std::move(config);
            }
        }
    }

    void APIRoutes::scheduleServerStateSnapshot(int64_t nowMs) {
        if (!serverStateStore) {
            return;
        }
        const int64_t snapshotTs = nowMs > 0 ? nowMs : nowEpochMs();
        serverStateStore->scheduleSnapshot(snapshotTs);
    }

    void APIRoutes::recordServerStateEvent(const std::string& entityType,
                                           const std::string& entityKey,
                                           const std::string& action,
                                           int64_t tsMs,
                                           nlohmann::json payload) {
        if (!serverStateStore) {
            return;
        }
        serverStateStore->recordEvent(entityType, entityKey, action, tsMs, std::move(payload));
    }

} // namespace NMC::Server
