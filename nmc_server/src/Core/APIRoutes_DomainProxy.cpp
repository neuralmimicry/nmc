// APIRoutes_DomainProxy.cpp
// Cross-domain proxy route registration and handlers for AARNN, provider portals, and Gail trading.

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

    void APIRoutes::registerDomainProxyRoutes(httplib::Server& svr, const APIRoutes::RouteGuard& guard) {
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

        // --- OpenShift Routes ---
        svr.Get("/openshift/resources", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenShiftResources(req, res);
        });
        svr.Get("/openshift/clusters", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenShiftClusters(req, res);
        });
        svr.Get(R"(/openshift/details/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenShiftClusterDetails(req, res);
        });
        svr.Post("/openshift/clusters/request", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenShiftRequestCluster(req, res);
        });

        // --- OpenStack Routes ---
        svr.Get("/openstack/resources", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenStackResources(req, res);
        });
        svr.Get("/openstack/clusters", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenStackClusters(req, res);
        });
        svr.Get(R"(/openstack/details/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenStackClusterDetails(req, res);
        });
        svr.Post("/openstack/clusters/request", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleOpenStackRequestCluster(req, res);
        });

        // --- Proxmox Routes ---
        svr.Get("/proxmox/resources", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleProxmoxResources(req, res);
        });
        svr.Get("/proxmox/clusters", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleProxmoxClusters(req, res);
        });
        svr.Get(R"(/proxmox/details/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleProxmoxClusterDetails(req, res);
        });
        svr.Post("/proxmox/clusters/request", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleProxmoxRequestCluster(req, res);
        });

    }

    nlohmann::json APIRoutes::buildAarnnEndpointsPayload(std::string& errorOut) {
        errorOut.clear();

        const std::string namespaceName = getenvTrimmedOr(
                "NMC_AARNN_NAMESPACE",
                "NM_AARNN_NAMESPACE",
                "aarnn"
        );
        const std::string appName = getenvTrimmedOr(
                "NMC_AARNN_APP_NAME",
                "NM_AARNN_APP_NAME",
                "aarnn"
        );

        nlohmann::json payload = {
                {"namespace", namespaceName},
                {"app_name", appName},
                {"runtime", {
                        {"found", false},
                        {"component", "web-ui"}
                }},
                {"control", {
                        {"found", false},
                        {"component", "control-api"}
                }},
                {"orchestrator", {
                        {"found", false},
                        {"component", "orchestrator"}
                }},
                {"discovery", {
                        {"kubernetes_available", k8sHandlers != nullptr},
                        {"errors", nlohmann::json::array()}
                }}
        };

        std::optional<nlohmann::json> services;
        std::optional<nlohmann::json> deployments;
        if (k8sHandlers) {
            services = k8sHandlers->listNamespacedResources("", "v1", "services", namespaceName);
            if (!services) {
                payload["discovery"]["errors"].push_back(
                        "Unable to list Service resources in namespace '" + namespaceName + "'."
                );
            }
            deployments = k8sHandlers->listNamespacedResources("apps", "v1", "deployments", namespaceName);
            if (!deployments) {
                payload["discovery"]["errors"].push_back(
                        "Unable to list Deployment resources in namespace '" + namespaceName + "'."
                );
            }
        }

        auto attachEndpoint = [&](const std::string& fieldName,
                                  const std::string& component,
                                  const std::string& serviceName,
                                  const std::string& workloadName,
                                  const std::string& overrideUrl,
                                  const std::string& urlField,
                                  const std::string& preferredPortName,
                                  int fallbackPort) {
            nlohmann::json summary = {
                    {"found", false},
                    {"component", component}
            };

            if (services) {
                const auto serviceMatch = findK8sItemByNameOrComponent(*services, serviceName, appName, component);
                if (serviceMatch) {
                    summary = buildAarnnServiceSummary(
                            *serviceMatch,
                            component,
                            "http",
                            urlField,
                            preferredPortName,
                            fallbackPort
                    );
                }
            }

            if (deployments) {
                const auto workloadMatch = findK8sItemByNameOrComponent(*deployments, workloadName, appName, component);
                if (workloadMatch) {
                    const auto metadata = workloadMatch->value("metadata", nlohmann::json::object());
                    summary["workload"] = buildAarnnDeploymentSummary(
                            *workloadMatch,
                            metadata.value("namespace", namespaceName),
                            metadata.value("name", workloadName)
                    );
                }
            }

            if (!overrideUrl.empty()) {
                TraceyEndpoint parsed{};
                if (parseTraceyEndpoint(overrideUrl, parsed)) {
                    summary["found"] = true;
                    summary["source"] = summary.value("source", "") == "kubernetes" ? "environment+kubernetes" : "environment";
                    summary["host"] = parsed.host;
                    summary["port"] = parsed.port;
                    summary[urlField] = parsed.normalized;
                } else {
                    payload["discovery"]["errors"].push_back(
                            "Invalid AARNN " + fieldName + " override: " + overrideUrl
                    );
                }
            }

            if (!summary.contains("source")) {
                summary["source"] = summary.value("found", false) ? "kubernetes" : "undiscovered";
            }
            if (!summary.contains("workload")) {
                summary["workload"] = nlohmann::json{
                        {"observed", false},
                        {"healthy", false},
                        {"status", "Unknown"},
                        {"namespace", namespaceName},
                        {"deployment", workloadName}
                };
            }
            payload[fieldName] = summary;
        };

        attachEndpoint(
                "runtime",
                "web-ui",
                getenvTrimmedOr("NMC_AARNN_RUNTIME_SERVICE_NAME", "NM_AARNN_RUNTIME_SERVICE_NAME", "aarnn-web-ui"),
                getenvTrimmedOr("NMC_AARNN_RUNTIME_WORKLOAD_NAME", "NM_AARNN_RUNTIME_WORKLOAD_NAME", "aarnn-web-ui"),
                getenvTrimmedOr("NMC_AARNN_RUNTIME_URL", "NM_AARNN_RUNTIME_URL"),
                "api_base_url",
                "http",
                8080
        );
        attachEndpoint(
                "control",
                "control-api",
                getenvTrimmedOr("NMC_AARNN_CONTROL_SERVICE_NAME", "NM_AARNN_CONTROL_SERVICE_NAME", "aarnn-control-api"),
                getenvTrimmedOr("NMC_AARNN_CONTROL_WORKLOAD_NAME", "NM_AARNN_CONTROL_WORKLOAD_NAME", "aarnn-control-api"),
                getenvTrimmedOr("NMC_AARNN_CONTROL_URL", "NM_AARNN_CONTROL_URL"),
                "api_base_url",
                "http",
                8080
        );
        attachEndpoint(
                "orchestrator",
                "orchestrator",
                getenvTrimmedOr("NMC_AARNN_ORCHESTRATOR_SERVICE_NAME", "NM_AARNN_ORCHESTRATOR_SERVICE_NAME", "aarnn-orchestrator"),
                getenvTrimmedOr("NMC_AARNN_ORCHESTRATOR_WORKLOAD_NAME", "NM_AARNN_ORCHESTRATOR_WORKLOAD_NAME", "aarnn-orchestrator"),
                getenvTrimmedOr("NMC_AARNN_ORCHESTRATOR_URL", "NM_AARNN_ORCHESTRATOR_URL"),
                "grpc_url",
                "grpc",
                50051
        );

        const bool anyFound = payload["runtime"].value("found", false)
                              || payload["control"].value("found", false)
                              || payload["orchestrator"].value("found", false);
        if (!anyFound) {
            errorOut = "AARNN services were not discovered in namespace '" + namespaceName + "'.";
        }

        return payload;
    }

    bool APIRoutes::resolveAarnnEndpoint(const std::string& plane,
                                         std::string& endpointOut,
                                         nlohmann::json& endpointInfoOut,
                                         std::string& errorOut) {
        endpointOut.clear();
        endpointInfoOut = nlohmann::json::object();
        errorOut.clear();

        const std::string normalizedPlane = toLower(trim(plane));
        if (normalizedPlane != "runtime" && normalizedPlane != "control") {
            errorOut = "Unsupported AARNN plane '" + plane + "'. Expected 'runtime' or 'control'.";
            return false;
        }

        std::string discoveryError;
        const nlohmann::json payload = buildAarnnEndpointsPayload(discoveryError);
        endpointInfoOut = payload.value(normalizedPlane, nlohmann::json::object());
        endpointOut = trim(endpointInfoOut.value("api_base_url", ""));
        if (!endpointOut.empty() && endpointInfoOut.value("found", false)) {
            return true;
        }

        errorOut = discoveryError.empty()
                   ? ("AARNN " + normalizedPlane + " endpoint is not available.")
                   : discoveryError;
        return false;
    }

    bool APIRoutes::fetchAarnnStatusViaControlPlane(const std::string& grpcUrl,
                                                    int64_t nowMs,
                                                    nlohmann::json& statusOut,
                                                    std::string& errorOut) {
        statusOut = nlohmann::json::object();
        errorOut.clear();

        const std::string trimmedGrpcUrl = trim(grpcUrl);
        if (trimmedGrpcUrl.empty()) {
            errorOut = "AARNN orchestrator gRPC URL is empty.";
            return false;
        }

        std::string controlEndpointRaw;
        nlohmann::json controlEndpointInfo;
        if (!resolveAarnnEndpoint("control", controlEndpointRaw, controlEndpointInfo, errorOut)) {
            return false;
        }

        TraceyEndpoint controlEndpoint;
        if (!parseTraceyEndpoint(controlEndpointRaw, controlEndpoint)) {
            errorOut = "Resolved AARNN control endpoint is invalid.";
            return false;
        }
        if (!isLocalOrPrivateHost(controlEndpoint.host, aarnnAllowPublicAddr)) {
            errorOut = "AARNN control endpoint is outside allowed network ranges.";
            return false;
        }

        std::string statusPath = buildTraceyPath(controlEndpoint, "/api/status");
        appendEncodedQueryParameter(statusPath, "addr", trimmedGrpcUrl);

        httplib::Headers headers{{"Accept", "application/json"}};
        const int timeoutSec = static_cast<int>(std::max<int64_t>(1, (aarnnStatusTimeoutMs + 999) / 1000));
        httplib::Result result;
        if (controlEndpoint.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            const bool tlsVerify = parseBoolValue(
                    std::getenv("NMC_AARNN_TLS_VERIFY"),
                    parseBoolValue(std::getenv("NM_AARNN_TLS_VERIFY"), true)
            );
            httplib::SSLClient client(controlEndpoint.host, controlEndpoint.port);
            client.enable_server_certificate_verification(tlsVerify);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Get(statusPath.c_str(), headers);
#else
            errorOut = "HTTPS AARNN status polling is unavailable: httplib built without OpenSSL support.";
            return false;
#endif
        } else {
            httplib::Client client(controlEndpoint.host, controlEndpoint.port);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Get(statusPath.c_str(), headers);
        }

        if (!result) {
            errorOut = "AARNN status poll failed: " + httplib::to_string(result.error());
            return false;
        }
        if (result->status < 200 || result->status >= 300) {
            errorOut = "AARNN status endpoint returned HTTP " + std::to_string(result->status) + ".";
            return false;
        }

        statusOut = nlohmann::json::parse(result->body, nullptr, false);
        if (statusOut.is_discarded() || !statusOut.is_object()) {
            errorOut = "AARNN status endpoint returned invalid JSON.";
            return false;
        }
        if (!statusOut.contains("orchestrator")) {
            statusOut["orchestrator"] = trimmedGrpcUrl;
        }
        statusOut["polled_at_epoch_ms"] = nowMs > 0 ? nowMs : nowEpochMs();
        return true;
    }

    nlohmann::json APIRoutes::buildAarnnInventoryPayload(const std::string& clusterId,
                                                         const std::string& orchestratorId,
                                                         std::string& errorOut) {
        errorOut.clear();
        const std::string normalizedClusterId = toLower(trim(clusterId));
        const std::string normalizedOrchestratorId = toLower(trim(orchestratorId));

        std::string endpointsError;
        const nlohmann::json endpointsPayload = buildAarnnEndpointsPayload(endpointsError);

        nlohmann::json payload = {
                {"planes", endpointsPayload},
                {"discovery", {
                        {"enabled", aarnnDiscoveryEnabled},
                        {"bind_addr", aarnnDiscoveryBindAddr},
                        {"port", aarnnDiscoveryPort},
                        {"stale_after_seconds", aarnnStaleAfterSeconds},
                        {"status_poll_ms", aarnnStatusPollMs},
                        {"status_timeout_ms", aarnnStatusTimeoutMs},
                        {"errors", nlohmann::json::array()}
                }},
                {"clusters", nlohmann::json::array()},
                {"orchestrators", nlohmann::json::array()},
                {"nodes", nlohmann::json::array()},
                {"networks", nlohmann::json::array()},
                {"summary", {
                        {"cluster_count", 0},
                        {"orchestrator_count", 0},
                        {"node_count", 0},
                        {"network_count", 0},
                        {"stale_orchestrator_count", 0}
                }}
        };

        if (endpointsPayload.contains("discovery") && endpointsPayload["discovery"].is_object()) {
            const auto endpointErrors = endpointsPayload["discovery"].value("errors", nlohmann::json::array());
            if (endpointErrors.is_array()) {
                for (const auto& entry : endpointErrors) {
                    payload["discovery"]["errors"].push_back(entry);
                }
            }
        }

        const auto matchesTarget = [&](const std::string& candidateClusterId,
                                       const std::string& candidateOrchestratorId,
                                       const std::string& candidateGrpcUrl) {
            const std::string clusterNeedle = toLower(trim(candidateClusterId));
            const std::string orchestratorNeedle = toLower(trim(candidateOrchestratorId));
            const std::string grpcNeedle = toLower(trim(candidateGrpcUrl));
            if (!normalizedClusterId.empty()) {
                return clusterNeedle == normalizedClusterId || grpcNeedle == normalizedClusterId;
            }
            if (!normalizedOrchestratorId.empty()) {
                return orchestratorNeedle == normalizedOrchestratorId || grpcNeedle == normalizedOrchestratorId;
            }
            return true;
        };

        std::vector<AarnnOrchestratorRecord> discoveredSnapshot;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            discoveredSnapshot.reserve(aarnnOrchestrators.size());
            for (const auto& [mapKey, record] : aarnnOrchestrators) {
                (void)mapKey;
                discoveredSnapshot.push_back(record);
            }
        }
        std::sort(discoveredSnapshot.begin(), discoveredSnapshot.end(), [](const AarnnOrchestratorRecord& left,
                                                                           const AarnnOrchestratorRecord& right) {
            if (left.clusterId == right.clusterId) {
                return left.orchestratorId < right.orchestratorId;
            }
            return left.clusterId < right.clusterId;
        });

        std::set<std::string> seenGrpcUrls;
        int staleOrchestratorCount = 0;
        const auto appendInventoryRecord = [&](const AarnnOrchestratorRecord& record,
                                               const nlohmann::json& statusPayload) {
            const auto nodes = statusPayload.contains("nodes") && statusPayload["nodes"].is_array()
                               ? statusPayload["nodes"]
                               : nlohmann::json::array();
            const auto networks = statusPayload.contains("networks") && statusPayload["networks"].is_array()
                                  ? statusPayload["networks"]
                                  : nlohmann::json::array();
            const int64_t statusTsMs = firstInt64Value(statusPayload, {"timestamp_ms"}, 0);

            nlohmann::json orchestratorView = {
                    {"orchestrator_id", record.orchestratorId},
                    {"cluster_id", record.clusterId},
                    {"grpc_url", record.grpcUrl},
                    {"host", record.host},
                    {"port", record.port},
                    {"source", record.source},
                    {"discovery_protocol", record.discoveryProtocol.empty() ? nlohmann::json(nullptr) : nlohmann::json(record.discoveryProtocol)},
                    {"stale", record.stale},
                    {"status_reachable", record.statusReachable},
                    {"first_seen_epoch_ms", record.firstSeenEpochMs > 0 ? nlohmann::json(record.firstSeenEpochMs) : nlohmann::json(nullptr)},
                    {"last_seen_epoch_ms", record.lastSeenEpochMs > 0 ? nlohmann::json(record.lastSeenEpochMs) : nlohmann::json(nullptr)},
                    {"last_polled_epoch_ms", record.lastPolledEpochMs > 0 ? nlohmann::json(record.lastPolledEpochMs) : nlohmann::json(nullptr)},
                    {"status_timestamp_ms", statusTsMs > 0 ? nlohmann::json(statusTsMs) : nlohmann::json(nullptr)},
                    {"last_error", record.lastError.empty() ? nlohmann::json(nullptr) : nlohmann::json(record.lastError)},
                    {"node_count", nodes.size()},
                    {"network_count", networks.size()}
            };
            payload["orchestrators"].push_back(orchestratorView);

            nlohmann::json clusterView = orchestratorView;
            clusterView["name"] = record.clusterId;
            payload["clusters"].push_back(clusterView);

            for (const auto& node : nodes) {
                if (!node.is_object()) {
                    continue;
                }
                nlohmann::json enriched = node;
                enriched["cluster_id"] = record.clusterId;
                enriched["orchestrator_id"] = record.orchestratorId;
                enriched["orchestrator_grpc_url"] = record.grpcUrl;
                enriched["source"] = record.source;
                payload["nodes"].push_back(std::move(enriched));
            }
            for (const auto& network : networks) {
                if (!network.is_object()) {
                    continue;
                }
                nlohmann::json enriched = network;
                enriched["cluster_id"] = record.clusterId;
                enriched["orchestrator_id"] = record.orchestratorId;
                enriched["orchestrator_grpc_url"] = record.grpcUrl;
                enriched["source"] = record.source;
                payload["networks"].push_back(std::move(enriched));
            }
        };

        for (const auto& record : discoveredSnapshot) {
            if (!matchesTarget(record.clusterId, record.orchestratorId, record.grpcUrl)) {
                continue;
            }
            if (!trim(record.grpcUrl).empty()) {
                seenGrpcUrls.insert(trim(record.grpcUrl));
            }
            if (record.stale) {
                staleOrchestratorCount += 1;
            }
            appendInventoryRecord(record, record.statusCache.is_object() ? record.statusCache : nlohmann::json::object());
        }

        const nlohmann::json staticOrchestrator = endpointsPayload.value("orchestrator", nlohmann::json::object());
        const std::string staticGrpcUrl = trim(staticOrchestrator.value("grpc_url", ""));
        if (staticOrchestrator.value("found", false) && !staticGrpcUrl.empty() && seenGrpcUrls.find(staticGrpcUrl) == seenGrpcUrls.end()) {
            TraceyEndpoint staticEndpoint;
            if (parseTraceyEndpoint(staticGrpcUrl, staticEndpoint)) {
                AarnnOrchestratorRecord staticRecord;
                staticRecord.grpcUrl = staticEndpoint.normalized;
                staticRecord.host = staticEndpoint.host;
                staticRecord.port = staticEndpoint.port;
                staticRecord.source = trim(staticOrchestrator.value("source", "static"));
                if (staticRecord.source.empty()) {
                    staticRecord.source = "static";
                }
                staticRecord.discoveryProtocol = staticRecord.source == "environment" ? "configured" : "continuum";
                staticRecord.clusterId = "continuum-default";
                staticRecord.orchestratorId = buildAarnnSyntheticId("aarnn-orchestrator", staticRecord.host, staticRecord.port);
                staticRecord.stale = false;

                if (matchesTarget(staticRecord.clusterId, staticRecord.orchestratorId, staticRecord.grpcUrl)) {
                    nlohmann::json statusPayload = nlohmann::json::object();
                    std::string statusError;
                    staticRecord.statusReachable = fetchAarnnStatusViaControlPlane(staticRecord.grpcUrl, nowEpochMs(), statusPayload, statusError);
                    staticRecord.statusCache = staticRecord.statusReachable ? statusPayload : nlohmann::json::object();
                    staticRecord.lastPolledEpochMs = nowEpochMs();
                    staticRecord.lastError = staticRecord.statusReachable ? "" : statusError;
                    appendInventoryRecord(staticRecord, staticRecord.statusCache);
                }
            }
        }

        payload["summary"]["cluster_count"] = payload["clusters"].size();
        payload["summary"]["orchestrator_count"] = payload["orchestrators"].size();
        payload["summary"]["node_count"] = payload["nodes"].size();
        payload["summary"]["network_count"] = payload["networks"].size();
        payload["summary"]["stale_orchestrator_count"] = staleOrchestratorCount;
        payload["discovery"]["known_orchestrators"] = payload["orchestrators"].size();

        if (payload["orchestrators"].empty()) {
            errorOut = !endpointsError.empty()
                       ? endpointsError
                       : "No AARNN orchestrators are currently known to Continuum.";
        }
        return payload;
    }

    bool APIRoutes::resolveAarnnOrchestratorTarget(const std::string& clusterId,
                                                   const std::string& orchestratorId,
                                                   nlohmann::json& targetOut,
                                                   std::string& errorOut) {
        targetOut = nlohmann::json::object();
        errorOut.clear();

        if (trim(clusterId).empty() && trim(orchestratorId).empty()) {
            errorOut = "Specify either cluster_id or orchestrator_id.";
            return false;
        }

        std::string inventoryError;
        const nlohmann::json inventory = buildAarnnInventoryPayload(clusterId, orchestratorId, inventoryError);
        const auto orchestrators = inventory.value("orchestrators", nlohmann::json::array());
        if (orchestrators.is_array() && !orchestrators.empty() && orchestrators.front().is_object()) {
            targetOut = orchestrators.front();
            return true;
        }

        errorOut = inventoryError.empty()
                   ? "AARNN orchestrator target was not found."
                   : inventoryError;
        return false;
    }

    void APIRoutes::handleAarnnInventory(const httplib::Request& req, httplib::Response& res) {
        const std::string clusterId = req.has_param("cluster_id") ? trim(req.get_param_value("cluster_id")) : "";
        const std::string orchestratorId = req.has_param("orchestrator_id") ? trim(req.get_param_value("orchestrator_id")) : "";
        if (!clusterId.empty() && !orchestratorId.empty()) {
            sendErrorResponse(res, 400, "Use either cluster_id or orchestrator_id, not both.");
            return;
        }

        std::string error;
        const nlohmann::json payload = buildAarnnInventoryPayload(clusterId, orchestratorId, error);
        const bool anyKnown = payload["summary"].value("orchestrator_count", 0) > 0;

        Models::CloudResponse apiResponse;
        apiResponse.success = anyKnown;
        apiResponse.message = anyKnown
                              ? "AARNN inventory generated."
                              : (error.empty() ? "AARNN inventory is empty." : error);
        apiResponse.data = payload;
        sendJsonResponse(res, apiResponse);
        if (!anyKnown) {
            res.status = 404;
        }
    }

    void APIRoutes::handleAarnnEndpoints(const httplib::Request& req, httplib::Response& res) {
        (void)req;
        std::string error;
        const nlohmann::json payload = buildAarnnEndpointsPayload(error);
        const bool anyFound = payload["runtime"].value("found", false)
                              || payload["control"].value("found", false)
                              || payload["orchestrator"].value("found", false);

        Models::CloudResponse apiResponse;
        apiResponse.success = anyFound;
        apiResponse.message = anyFound
                              ? "AARNN endpoints discovered."
                              : (error.empty() ? "AARNN endpoints were not found." : error);
        apiResponse.data = payload;
        sendJsonResponse(res, apiResponse);
        if (!anyFound) {
            res.status = 404;
        }
    }

    void APIRoutes::handleAarnnProxy(const httplib::Request& req, httplib::Response& res, const std::string& plane) {
        const nlohmann::json payload = nlohmann::json::parse(req.body, nullptr, false);
        if (payload.is_discarded() || !payload.is_object()) {
            sendErrorResponse(res, 400, "Invalid JSON payload.");
            return;
        }

        const std::string normalizedPlane = toLower(trim(plane));
        if (normalizedPlane != "runtime" && normalizedPlane != "control") {
            sendErrorResponse(res, 400, "Unsupported AARNN plane. Use 'runtime' or 'control'.");
            return;
        }

        const std::string method = toUpper(trim(payload.value("method", "GET")));
        if (method != "GET" && method != "POST" && method != "PUT" && method != "PATCH" && method != "DELETE") {
            sendErrorResponse(res, 400, "Unsupported HTTP method for AARNN proxy.");
            return;
        }

        const std::string requestPath = trim(payload.value("path", ""));
        if (requestPath.empty() || requestPath.front() != '/') {
            sendErrorResponse(res, 400, "AARNN proxy path must start with '/'.");
            return;
        }

        const std::string clusterId = trim(payload.value("cluster_id", ""));
        const std::string orchestratorId = trim(payload.value("orchestrator_id", ""));
        if (!clusterId.empty() && !orchestratorId.empty()) {
            sendErrorResponse(res, 400, "Use either cluster_id or orchestrator_id, not both.");
            return;
        }

        if (payload.contains("json") && payload.contains("body")) {
            sendErrorResponse(res, 400, "Provide either 'json' or 'body', not both.");
            return;
        }

        std::string endpointRaw;
        nlohmann::json endpointInfo;
        std::string resolutionError;
        if (!resolveAarnnEndpoint(normalizedPlane, endpointRaw, endpointInfo, resolutionError)) {
            sendErrorResponse(res, 503, resolutionError);
            return;
        }

        TraceyEndpoint endpoint;
        if (!parseTraceyEndpoint(endpointRaw, endpoint)) {
            sendErrorResponse(res, 503, "Resolved AARNN endpoint is invalid.");
            return;
        }

        const bool allowPublicAddr = parseBoolValue(
                std::getenv("NMC_AARNN_ALLOW_PUBLIC_ADDR"),
                parseBoolValue(std::getenv("NM_AARNN_ALLOW_PUBLIC_ADDR"), false)
        );
        if (!isLocalOrPrivateHost(endpoint.host, allowPublicAddr)) {
            sendErrorResponse(res, 403, "AARNN endpoint is outside allowed network ranges.");
            return;
        }

        std::string resolvedRequestPath = requestPath;
        if (!clusterId.empty() || !orchestratorId.empty()) {
            if (normalizedPlane != "control") {
                sendErrorResponse(res, 400, "cluster_id/orchestrator_id targeting is only supported on the AARNN control plane.");
                return;
            }
            if (pathHasQueryParameter(resolvedRequestPath, "addr")) {
                sendErrorResponse(res, 400, "Do not combine cluster_id/orchestrator_id with an explicit addr query parameter.");
                return;
            }

            nlohmann::json targetInfo;
            std::string targetError;
            if (!resolveAarnnOrchestratorTarget(clusterId, orchestratorId, targetInfo, targetError)) {
                sendErrorResponse(res, 404, targetError);
                return;
            }

            const std::string targetGrpcUrl = trim(targetInfo.value("grpc_url", ""));
            if (targetGrpcUrl.empty()) {
                sendErrorResponse(res, 404, "Resolved AARNN orchestrator target has no grpc_url.");
                return;
            }
            appendEncodedQueryParameter(resolvedRequestPath, "addr", targetGrpcUrl);
        }

        std::string requestBody;
        std::string contentType = trim(payload.value("content_type", ""));
        if (payload.contains("json")) {
            requestBody = payload["json"].dump();
            if (contentType.empty()) {
                contentType = "application/json";
            }
        } else if (payload.contains("body")) {
            if (!payload["body"].is_string()) {
                sendErrorResponse(res, 400, "AARNN proxy 'body' must be a string.");
                return;
            }
            requestBody = payload["body"].get<std::string>();
        }
        if (contentType.empty() && !requestBody.empty()) {
            contentType = "text/plain";
        }

        httplib::Headers headers{
                {"Accept", "application/json, application/octet-stream;q=0.9, text/plain;q=0.8, */*;q=0.7"}
        };
        const std::string authToken = extractAuthToken(req);
        if (!authToken.empty()) {
            headers.emplace("Authorization", "Bearer " + authToken);
        }

        const auto maybeAuthenticateRuntimeLocally = [&](auto& client) {
            if (normalizedPlane != "runtime" || !authToken.empty()) {
                return;
            }
            if (resolvedRequestPath == "/api/login" || resolvedRequestPath == "/login") {
                return;
            }

            const std::string runtimeUser = getenvTrimmedOr(
                    "NMC_AARNN_RUNTIME_LOCAL_USER",
                    "NM_AARNN_RUNTIME_LOCAL_USER",
                    getenvTrimmedOr("NMC_AARNN_LOCAL_USER", "NM_AARNN_LOCAL_USER")
            );
            const std::string runtimePass = getenvTrimmedOr(
                    "NMC_AARNN_RUNTIME_LOCAL_PASS",
                    "NM_AARNN_RUNTIME_LOCAL_PASS",
                    getenvTrimmedOr("NMC_AARNN_LOCAL_PASS", "NM_AARNN_LOCAL_PASS")
            );
            if (runtimeUser.empty() || runtimePass.empty()) {
                return;
            }

            nlohmann::json loginPayload = {
                    {"username", runtimeUser},
                    {"password", runtimePass}
            };
            httplib::Headers loginHeaders{
                    {"Accept", "application/json"},
                    {"Content-Type", "application/json"}
            };
            const std::string loginPath = buildTraceyPath(endpoint, "/api/login");
            httplib::Result loginResult = client.Post(
                    loginPath.c_str(),
                    loginHeaders,
                    loginPayload.dump(),
                    "application/json"
            );
            if (!loginResult || loginResult->status < 200 || loginResult->status >= 300) {
                return;
            }
            const std::string setCookie = trim(loginResult->get_header_value("Set-Cookie"));
            if (setCookie.empty()) {
                return;
            }
            const auto semiPos = setCookie.find(';');
            headers.emplace("Cookie", semiPos == std::string::npos ? setCookie : setCookie.substr(0, semiPos));
        };

        const auto executeRequest = [&](auto& client) -> httplib::Result {
            client.set_connection_timeout(10);
            client.set_read_timeout(180);
            client.set_write_timeout(180);
            maybeAuthenticateRuntimeLocally(client);

            const std::string upstreamPath = buildTraceyPath(endpoint, resolvedRequestPath);
            if (method == "GET") {
                return client.Get(upstreamPath.c_str(), headers);
            }
            if (method == "DELETE") {
                return client.Delete(upstreamPath.c_str(), headers, requestBody, contentType.c_str());
            }
            if (method == "POST") {
                return client.Post(upstreamPath.c_str(), headers, requestBody, contentType.c_str());
            }
            if (method == "PUT") {
                return client.Put(upstreamPath.c_str(), headers, requestBody, contentType.c_str());
            }
            return client.Patch(upstreamPath.c_str(), headers, requestBody, contentType.c_str());
        };

        httplib::Result result;
        if (endpoint.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient client(endpoint.host, endpoint.port);
            client.enable_server_certificate_verification(true);
            result = executeRequest(client);
#else
            sendErrorResponse(res, 503, "HTTPS AARNN proxy unavailable: httplib built without OpenSSL support.");
            return;
#endif
        } else {
            httplib::Client client(endpoint.host, endpoint.port);
            result = executeRequest(client);
        }

        if (!result) {
            sendErrorResponse(res, 502, "AARNN proxy request failed: " + httplib::to_string(result.error()));
            return;
        }

        const int upstreamStatus = result->status;
        const std::string upstreamContentType = trim(result->get_header_value("Content-Type"));
        nlohmann::json upstreamData = {
                {"plane", normalizedPlane},
                {"endpoint", endpointInfo},
                {"path", requestPath},
                {"resolved_path", resolvedRequestPath},
                {"method", method},
                {"upstream_status", upstreamStatus},
                {"content_type", upstreamContentType.empty() ? nlohmann::json(nullptr) : nlohmann::json(upstreamContentType)}
        };
        if (!clusterId.empty()) {
            upstreamData["cluster_id"] = clusterId;
        }
        if (!orchestratorId.empty()) {
            upstreamData["orchestrator_id"] = orchestratorId;
        }
        const std::string disposition = trim(result->get_header_value("Content-Disposition"));
        if (!disposition.empty()) {
            upstreamData["content_disposition"] = disposition;
        }

        nlohmann::json parsedBody = nlohmann::json::parse(result->body, nullptr, false);
        if (!parsedBody.is_discarded()) {
            upstreamData["payload"] = parsedBody;
        } else if (!result->body.empty()) {
            if (looksLikeTextPayload(result->body)) {
                upstreamData["text"] = result->body;
            } else {
                upstreamData["body_base64"] = base64Encode(result->body);
            }
        } else {
            upstreamData["payload"] = nlohmann::json::object();
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = upstreamStatus >= 200 && upstreamStatus < 300;
        apiResponse.message = apiResponse.success
                              ? "AARNN " + normalizedPlane + " proxy request completed."
                              : ("AARNN " + normalizedPlane + " proxy request failed with HTTP " + std::to_string(upstreamStatus) + ".");
        apiResponse.data = upstreamData;
        sendJsonResponse(res, apiResponse);
        res.status = upstreamStatus;
    }

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

    void APIRoutes::handleOpenShiftClusterDetails(const httplib::Request& req, httplib::Response& res) {
        const std::string identifier = req.matches.size() > 1 ? trim(req.matches[1].str()) : "";
        if (identifier.empty()) {
            return sendErrorResponse(res, 400, "Missing OpenShift cluster identifier.");
        }

        Models::CloudResponse listResponse = openShiftClient->listClusters();
        if (!listResponse.success) {
            sendJsonResponse(res, listResponse);
            res.status = listResponse.statusCode;
            return;
        }

        const std::vector<nlohmann::json> clusters = collectClusterObjects(listResponse.data);
        if (clusters.empty()) {
            Models::CloudResponse notFound;
            notFound.success = false;
            notFound.statusCode = 404;
            notFound.message = "OpenShift cluster '" + identifier + "' not found.";
            notFound.data = nlohmann::json::object();
            sendJsonResponse(res, notFound);
            res.status = 404;
            return;
        }

        nlohmann::json selectedCluster = findClusterByIdentifier(clusters, identifier);

        if (selectedCluster.is_null()) {
            Models::CloudResponse notFound;
            notFound.success = false;
            notFound.statusCode = 404;
            notFound.message = "OpenShift cluster '" + identifier + "' not found.";
            notFound.data = nlohmann::json::object();
            sendJsonResponse(res, notFound);
            res.status = 404;
            return;
        }

        normalizeClusterStatus(selectedCluster);
        nlohmann::json details = buildClusterDetailsWithNetworking(selectedCluster);

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.statusCode = 200;
        apiResponse.message = "OpenShift cluster details retrieved.";
        apiResponse.data = details;
        sendJsonResponse(res, apiResponse);
        res.status = 200;
    }

    void APIRoutes::handleOpenShiftRequestCluster(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string traceyAgentId;
            std::string traceyStatusAddr;
            std::string traceyReason;

            // Validate minimum required fields before forwarding to oshift.
            const std::string name = json_body.value("name", "");
            const std::string organization = json_body.value("organization", "");
            const int gpuCount = json_body.value("gpu_count", 0);
            const std::string architecture = json_body.value("architecture", "");
            const std::string region = json_body.value("region", "");
            const std::string provider = json_body.value("provider", "on-prem");
            const std::set<std::string> allowedProviders = {"on-prem", "rosa", "aro", "gcp", "hybrid-burst", "openstack"};

            if (name.empty() || organization.empty() || architecture.empty() || region.empty() || gpuCount <= 0) {
                return sendErrorResponse(res, 400, "Missing required parameters: name, organization, gpu_count, architecture, or region.");
            }
            if (allowedProviders.find(provider) == allowedProviders.end()) {
                return sendErrorResponse(res, 400, "Invalid provider. Supported: on-prem, rosa, aro, gcp, hybrid-burst, openstack.");
            }
            if (provider == "hybrid-burst" && (!json_body.contains("burst_targets") || json_body["burst_targets"].empty())) {
                return sendErrorResponse(res, 400, "burst_targets is required when provider is hybrid-burst.");
            }
            if (!extractTraceyProvisioningRequest(json_body, traceyAgentId, traceyStatusAddr, traceyReason)) {
                return sendErrorResponse(res, 400, traceyReason);
            }

            Models::CloudResponse apiResponse = openShiftClient->requestCluster(json_body);
            if (apiResponse.success && apiResponse.data.is_object()) {
                if (apiResponse.data.contains("cluster")) {
                    normalizeClusterStatus(apiResponse.data["cluster"]);
                }
            }
            if (apiResponse.success && !traceyAgentId.empty()) {
                std::lock_guard<std::mutex> lock(dataMutex);
                upsertTraceyRequirementLocked("openshift_cluster", name, region, traceyAgentId, traceyStatusAddr, nowEpochMs());
                if (!apiResponse.data.is_object()) {
                    apiResponse.data = nlohmann::json::object();
                }
                apiResponse.data["tracey"] = {
                        {"required", true},
                        {"agent_id", traceyAgentId},
                        {"status_addr", traceyStatusAddr},
                        {"compliance_state", "pending"},
                        {"reason", "Waiting for Tracey agent heartbeat/discovery after provisioning."}
                };
            }
            sendJsonResponse(res, apiResponse);
            res.status = apiResponse.statusCode;
        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

    void APIRoutes::handleOpenStackResources(const httplib::Request& req, httplib::Response& res) {
        (void)req;
        Models::CloudResponse apiResponse = openStackClient->getResources();
        sendJsonResponse(res, apiResponse);
        res.status = apiResponse.statusCode;
    }

    void APIRoutes::handleOpenStackClusters(const httplib::Request& req, httplib::Response& res) {
        (void)req;
        Models::CloudResponse apiResponse = openStackClient->listClusters();
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

    void APIRoutes::handleOpenStackClusterDetails(const httplib::Request& req, httplib::Response& res) {
        const std::string identifier = req.matches.size() > 1 ? trim(req.matches[1].str()) : "";
        if (identifier.empty()) {
            return sendErrorResponse(res, 400, "Missing OpenStack cluster identifier.");
        }

        Models::CloudResponse listResponse = openStackClient->listClusters();
        if (!listResponse.success) {
            sendJsonResponse(res, listResponse);
            res.status = listResponse.statusCode;
            return;
        }

        const std::vector<nlohmann::json> clusters = collectClusterObjects(listResponse.data);
        if (clusters.empty()) {
            Models::CloudResponse notFound;
            notFound.success = false;
            notFound.statusCode = 404;
            notFound.message = "OpenStack cluster '" + identifier + "' not found.";
            notFound.data = nlohmann::json::object();
            sendJsonResponse(res, notFound);
            res.status = 404;
            return;
        }

        nlohmann::json selectedCluster = findClusterByIdentifier(clusters, identifier);
        if (selectedCluster.is_null()) {
            Models::CloudResponse notFound;
            notFound.success = false;
            notFound.statusCode = 404;
            notFound.message = "OpenStack cluster '" + identifier + "' not found.";
            notFound.data = nlohmann::json::object();
            sendJsonResponse(res, notFound);
            res.status = 404;
            return;
        }

        normalizeClusterStatus(selectedCluster);
        const nlohmann::json details = buildClusterDetailsWithNetworking(selectedCluster);

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.statusCode = 200;
        apiResponse.message = "OpenStack cluster details retrieved.";
        apiResponse.data = details;
        sendJsonResponse(res, apiResponse);
        res.status = 200;
    }

    void APIRoutes::handleOpenStackRequestCluster(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string traceyAgentId;
            std::string traceyStatusAddr;
            std::string traceyReason;

            const std::string name = json_body.value("name", "");
            const std::string organization = json_body.value("organization", "");
            const int gpuCount = json_body.value("gpu_count", 0);
            const std::string architecture = json_body.value("architecture", "");
            const std::string region = json_body.value("region", "");
            const std::string provider = json_body.value("provider", "openstack");
            const std::set<std::string> allowedProviders = {"openstack", "openstack-heat", "openstack-magnum", "hybrid-burst"};

            if (name.empty() || organization.empty() || architecture.empty() || region.empty() || gpuCount <= 0) {
                return sendErrorResponse(res, 400, "Missing required parameters: name, organization, gpu_count, architecture, or region.");
            }
            if (allowedProviders.find(provider) == allowedProviders.end()) {
                return sendErrorResponse(res, 400, "Invalid provider. Supported: openstack, openstack-heat, openstack-magnum, hybrid-burst.");
            }
            if (provider == "hybrid-burst" && (!json_body.contains("burst_targets") || json_body["burst_targets"].empty())) {
                return sendErrorResponse(res, 400, "burst_targets is required when provider is hybrid-burst.");
            }
            if (!extractTraceyProvisioningRequest(json_body, traceyAgentId, traceyStatusAddr, traceyReason)) {
                return sendErrorResponse(res, 400, traceyReason);
            }

            Models::CloudResponse apiResponse = openStackClient->requestCluster(json_body);
            if (apiResponse.success && apiResponse.data.is_object() && apiResponse.data.contains("cluster")) {
                normalizeClusterStatus(apiResponse.data["cluster"]);
            }
            if (apiResponse.success && !traceyAgentId.empty()) {
                std::lock_guard<std::mutex> lock(dataMutex);
                upsertTraceyRequirementLocked("openstack_cluster", name, region, traceyAgentId, traceyStatusAddr, nowEpochMs());
                if (!apiResponse.data.is_object()) {
                    apiResponse.data = nlohmann::json::object();
                }
                apiResponse.data["tracey"] = {
                        {"required", true},
                        {"agent_id", traceyAgentId},
                        {"status_addr", traceyStatusAddr},
                        {"compliance_state", "pending"},
                        {"reason", "Waiting for Tracey agent heartbeat/discovery after provisioning."}
                };
            }
            sendJsonResponse(res, apiResponse);
            res.status = apiResponse.statusCode;
        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

    void APIRoutes::handleProxmoxResources(const httplib::Request& req, httplib::Response& res) {
        (void)req;
        Models::CloudResponse apiResponse = proxmoxClient->getResources();
        sendJsonResponse(res, apiResponse);
        res.status = apiResponse.statusCode;
    }

    void APIRoutes::handleProxmoxClusters(const httplib::Request& req, httplib::Response& res) {
        (void)req;
        Models::CloudResponse apiResponse = proxmoxClient->listClusters();
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

    void APIRoutes::handleProxmoxClusterDetails(const httplib::Request& req, httplib::Response& res) {
        const std::string identifier = req.matches.size() > 1 ? trim(req.matches[1].str()) : "";
        if (identifier.empty()) {
            return sendErrorResponse(res, 400, "Missing Proxmox cluster identifier.");
        }

        Models::CloudResponse listResponse = proxmoxClient->listClusters();
        if (!listResponse.success) {
            sendJsonResponse(res, listResponse);
            res.status = listResponse.statusCode;
            return;
        }

        const std::vector<nlohmann::json> clusters = collectClusterObjects(listResponse.data);
        if (clusters.empty()) {
            Models::CloudResponse notFound;
            notFound.success = false;
            notFound.statusCode = 404;
            notFound.message = "Proxmox cluster '" + identifier + "' not found.";
            notFound.data = nlohmann::json::object();
            sendJsonResponse(res, notFound);
            res.status = 404;
            return;
        }

        nlohmann::json selectedCluster = findClusterByIdentifier(clusters, identifier);
        if (selectedCluster.is_null()) {
            Models::CloudResponse notFound;
            notFound.success = false;
            notFound.statusCode = 404;
            notFound.message = "Proxmox cluster '" + identifier + "' not found.";
            notFound.data = nlohmann::json::object();
            sendJsonResponse(res, notFound);
            res.status = 404;
            return;
        }

        normalizeClusterStatus(selectedCluster);
        const nlohmann::json details = buildClusterDetailsWithNetworking(selectedCluster);

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.statusCode = 200;
        apiResponse.message = "Proxmox cluster details retrieved.";
        apiResponse.data = details;
        sendJsonResponse(res, apiResponse);
        res.status = 200;
    }

    void APIRoutes::handleProxmoxRequestCluster(const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = nlohmann::json::parse(req.body);
            std::string traceyAgentId;
            std::string traceyStatusAddr;
            std::string traceyReason;

            const std::string name = json_body.value("name", "");
            const std::string organization = json_body.value("organization", "");
            const int gpuCount = json_body.value("gpu_count", 0);
            const std::string architecture = json_body.value("architecture", "");
            const std::string region = json_body.value("region", "");
            const std::string provider = json_body.value("provider", "proxmox");
            const std::set<std::string> allowedProviders = {"proxmox", "proxmox-ve", "hybrid-burst"};

            if (name.empty() || organization.empty() || architecture.empty() || region.empty() || gpuCount <= 0) {
                return sendErrorResponse(res, 400, "Missing required parameters: name, organization, gpu_count, architecture, or region.");
            }
            if (allowedProviders.find(provider) == allowedProviders.end()) {
                return sendErrorResponse(res, 400, "Invalid provider. Supported: proxmox, proxmox-ve, hybrid-burst.");
            }
            if (provider == "hybrid-burst" && (!json_body.contains("burst_targets") || json_body["burst_targets"].empty())) {
                return sendErrorResponse(res, 400, "burst_targets is required when provider is hybrid-burst.");
            }
            if (!extractTraceyProvisioningRequest(json_body, traceyAgentId, traceyStatusAddr, traceyReason)) {
                return sendErrorResponse(res, 400, traceyReason);
            }

            Models::CloudResponse apiResponse = proxmoxClient->requestCluster(json_body);
            if (apiResponse.success && apiResponse.data.is_object() && apiResponse.data.contains("cluster")) {
                normalizeClusterStatus(apiResponse.data["cluster"]);
            }
            if (apiResponse.success && !traceyAgentId.empty()) {
                std::lock_guard<std::mutex> lock(dataMutex);
                upsertTraceyRequirementLocked("proxmox_cluster", name, region, traceyAgentId, traceyStatusAddr, nowEpochMs());
                if (!apiResponse.data.is_object()) {
                    apiResponse.data = nlohmann::json::object();
                }
                apiResponse.data["tracey"] = {
                        {"required", true},
                        {"agent_id", traceyAgentId},
                        {"status_addr", traceyStatusAddr},
                        {"compliance_state", "pending"},
                        {"reason", "Waiting for Tracey agent heartbeat/discovery after provisioning."}
                };
            }
            sendJsonResponse(res, apiResponse);
            res.status = apiResponse.statusCode;
        } catch (const nlohmann::json::parse_error& e) {
            sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
        } catch (const std::exception& e) {
            sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
        }
    }

// --- Tracey Agent Handlers ---
    void APIRoutes::runAarnnDiscoveryLoop() {
        constexpr size_t maxDatagramBytes = 2048;
        std::vector<char> buffer(maxDatagramBytes);
        int sockFd = -1;
        int64_t nextBindAttemptMs = 0;
        int64_t lastMaintenanceMs = 0;

        while (!stopAarnnDiscovery.load()) {
            const int64_t nowMs = nowEpochMs();

            if (sockFd < 0 && nowMs >= nextBindAttemptMs) {
                sockFd = ::socket(AF_INET, SOCK_DGRAM, 0);
                if (sockFd < 0) {
                    nextBindAttemptMs = nowMs + 3000;
                    std::cerr << "[WARN] AARNN discovery socket() failed: " << std::strerror(errno) << std::endl;
                } else {
                    int enable = 1;
                    (void)::setsockopt(sockFd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
#ifdef SO_REUSEPORT
                    (void)::setsockopt(sockFd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
#endif
                    sockaddr_in bindAddr{};
                    bindAddr.sin_family = AF_INET;
                    bindAddr.sin_port = htons(static_cast<uint16_t>(aarnnDiscoveryPort));
                    if (aarnnDiscoveryBindAddr == "*" || aarnnDiscoveryBindAddr.empty() || aarnnDiscoveryBindAddr == "0.0.0.0") {
                        bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
                    } else if (::inet_pton(AF_INET, aarnnDiscoveryBindAddr.c_str(), &bindAddr.sin_addr) != 1) {
                        std::cerr << "[WARN] Invalid NMC_AARNN_DISCOVERY_BIND_ADDR '" << aarnnDiscoveryBindAddr
                                  << "': expected IPv4 address" << std::endl;
                        ::close(sockFd);
                        sockFd = -1;
                        nextBindAttemptMs = nowMs + 10000;
                    }

                    if (sockFd >= 0 && ::bind(sockFd, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) != 0) {
                        std::cerr << "[WARN] AARNN discovery bind(" << aarnnDiscoveryBindAddr << ":" << aarnnDiscoveryPort
                                  << ") failed: " << std::strerror(errno) << std::endl;
                        ::close(sockFd);
                        sockFd = -1;
                        nextBindAttemptMs = nowMs + 5000;
                    }
                }
            }

            if (sockFd >= 0) {
                pollfd pfd{};
                pfd.fd = sockFd;
                pfd.events = POLLIN;
                const int pollRc = ::poll(&pfd, 1, 200);
                if (pollRc > 0 && (pfd.revents & POLLIN)) {
                    sockaddr_storage sender{};
                    socklen_t senderLen = sizeof(sender);
                    const ssize_t bytes = ::recvfrom(sockFd, buffer.data(), buffer.size(), 0,
                                                     reinterpret_cast<sockaddr*>(&sender), &senderLen);
                    if (bytes > 0) {
                        std::string senderAddress = "unknown";
                        char addrBuf[INET6_ADDRSTRLEN] = {0};
                        if (sender.ss_family == AF_INET) {
                            const auto* sin = reinterpret_cast<sockaddr_in*>(&sender);
                            if (::inet_ntop(AF_INET, &sin->sin_addr, addrBuf, sizeof(addrBuf)) != nullptr) {
                                senderAddress = addrBuf;
                            }
                        } else if (sender.ss_family == AF_INET6) {
                            const auto* sin6 = reinterpret_cast<sockaddr_in6*>(&sender);
                            if (::inet_ntop(AF_INET6, &sin6->sin6_addr, addrBuf, sizeof(addrBuf)) != nullptr) {
                                senderAddress = addrBuf;
                            }
                        }

                        const std::string payload(buffer.data(), static_cast<size_t>(bytes));
                        ingestAarnnDiscoveryAnnouncement(trimLineEnd(payload), senderAddress, nowEpochMs());
                    } else if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                        std::cerr << "[WARN] AARNN discovery recvfrom() failed: " << std::strerror(errno) << std::endl;
                        ::close(sockFd);
                        sockFd = -1;
                        nextBindAttemptMs = nowEpochMs() + 3000;
                    }
                } else if (pollRc < 0 && errno != EINTR) {
                    std::cerr << "[WARN] AARNN discovery poll() failed: " << std::strerror(errno) << std::endl;
                    ::close(sockFd);
                    sockFd = -1;
                    nextBindAttemptMs = nowEpochMs() + 3000;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }

            const int64_t maintenanceNowMs = nowEpochMs();
            if (maintenanceNowMs - lastMaintenanceMs >= 1000) {
                markAarnnStaleOrchestrators(maintenanceNowMs);

                std::vector<std::pair<std::string, std::string>> duePolls;
                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    duePolls.reserve(aarnnOrchestrators.size());
                    for (auto& [mapKey, record] : aarnnOrchestrators) {
                        if (record.grpcUrl.empty() || record.stale) {
                            continue;
                        }
                        if (record.nextPollEpochMs <= 0 || maintenanceNowMs >= record.nextPollEpochMs) {
                            duePolls.emplace_back(mapKey, record.grpcUrl);
                            record.nextPollEpochMs = maintenanceNowMs + aarnnStatusPollMs;
                        }
                    }
                }

                for (const auto& pendingPoll : duePolls) {
                    pollAarnnOrchestratorStatus(pendingPoll.first, pendingPoll.second, maintenanceNowMs);
                }

                lastMaintenanceMs = maintenanceNowMs;
            }
        }

        if (sockFd >= 0) {
            ::close(sockFd);
        }
    }

    void APIRoutes::ingestAarnnDiscoveryAnnouncement(const std::string& payload,
                                                     const std::string& senderAddress,
                                                     int64_t receivedAtMs) {
        constexpr const char* kBeaconPrefix = "NEUROMORPHIC_ORCHESTRATOR:";
        const std::string trimmedPayload = trim(payload);
        if (trimmedPayload.rfind(kBeaconPrefix, 0) != 0) {
            return;
        }

        std::string rawEndpoint = trim(trimmedPayload.substr(std::strlen(kBeaconPrefix)));
        if (rawEndpoint.empty()) {
            return;
        }
        if (senderAddress != "unknown") {
            if (rawEndpoint.rfind("http://0.0.0.0", 0) == 0) {
                rawEndpoint.replace(7, 7, senderAddress);
            } else if (rawEndpoint.rfind("https://0.0.0.0", 0) == 0) {
                rawEndpoint.replace(8, 7, senderAddress);
            } else if (rawEndpoint.rfind("0.0.0.0", 0) == 0) {
                rawEndpoint.replace(0, 7, senderAddress);
            }
        }

        TraceyEndpoint endpoint;
        if (!parseTraceyEndpoint(rawEndpoint, endpoint)) {
            return;
        }
        if ((endpoint.host == "0.0.0.0" || endpoint.host == "localhost") && senderAddress != "unknown") {
            endpoint.host = senderAddress;
            endpoint.normalized = normalizedUrlWithHost(endpoint, endpoint.host);
        }
        if (!isLocalOrPrivateHost(endpoint.host, aarnnAllowPublicAddr)) {
            return;
        }

        std::lock_guard<std::mutex> lock(dataMutex);
        auto& record = aarnnOrchestrators[endpoint.normalized];
        if (record.firstSeenEpochMs <= 0) {
            record.firstSeenEpochMs = receivedAtMs;
            record.nextPollEpochMs = receivedAtMs;
        }
        record.orchestratorId = buildAarnnSyntheticId("aarnn-orchestrator", endpoint.host, endpoint.port);
        record.clusterId = buildAarnnSyntheticId("aarnn-cluster", endpoint.host, endpoint.port);
        record.grpcUrl = endpoint.normalized;
        record.host = endpoint.host;
        record.port = endpoint.port;
        record.source = "discovery";
        record.discoveryProtocol = "udp-beacon";
        record.lastSeenEpochMs = std::max(record.lastSeenEpochMs, receivedAtMs);
        record.stale = false;
        if (record.lastError == "AARNN orchestrator beacon is stale.") {
            record.lastError.clear();
        }
    }

    void APIRoutes::pollAarnnOrchestratorStatus(const std::string& mapKey, const std::string& grpcUrl, int64_t nowMs) {
        nlohmann::json statusPayload;
        std::string error;
        const bool ok = fetchAarnnStatusViaControlPlane(grpcUrl, nowMs, statusPayload, error);

        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = aarnnOrchestrators.find(mapKey);
        if (it == aarnnOrchestrators.end()) {
            return;
        }

        auto& record = it->second;
        record.lastPolledEpochMs = nowMs;
        record.nextPollEpochMs = nowMs + aarnnStatusPollMs;
        if (ok) {
            record.statusReachable = true;
            record.statusCache = statusPayload;
            record.lastError.clear();
            return;
        }
        record.statusReachable = false;
        record.lastError = error;
    }

    void APIRoutes::markAarnnStaleOrchestrators(int64_t nowMs) {
        std::lock_guard<std::mutex> lock(dataMutex);
        const int64_t staleAfterMs = std::max<int64_t>(4, aarnnStaleAfterSeconds) * 1000;
        for (auto& [mapKey, record] : aarnnOrchestrators) {
            (void)mapKey;
            if (record.lastSeenEpochMs <= 0) {
                continue;
            }
            const bool staleNow = nowMs > record.lastSeenEpochMs && (nowMs - record.lastSeenEpochMs) > staleAfterMs;
            record.stale = staleNow;
            if (staleNow) {
                record.statusReachable = false;
                if (record.lastError.empty()) {
                    record.lastError = "AARNN orchestrator beacon is stale.";
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Gail Trading Bridge — proxy helpers and handlers
    // -----------------------------------------------------------------------

    std::string APIRoutes::resolveGailBaseUrl() const {
        std::string url = normalizeHttpBaseUrl(getenvTrimmedOr("NMC_GAIL_BASE_URL", "GAIL_BASE_URL"));
        if (url.empty()) {
            url = normalizeHttpBaseUrl(getenvTrimmedOr("NMC_GAIL_URL", "GAIL_URL"));
        }
        return url;
    }

    std::string APIRoutes::resolveGailApiToken() const {
        static constexpr std::array<const char*, 6> keys = {
            "NMC_GAIL_API_TOKEN",
            "GAIL_API_TOKEN",
            "NMC_GAIL_BEARER_TOKEN",
            "GAIL_BEARER_TOKEN",
            "NMC_GAIL_AUTH_TOKEN",
            "GAIL_AUTH_TOKEN"
        };
        for (const char* key : keys) {
            std::string val = getenvTrimmed(key);
            if (!val.empty()) {
                return val;
            }
        }
        return {};
    }

    bool APIRoutes::proxyGailTradingRequest(
        const httplib::Request& req,
        httplib::Response& res,
        const std::string& gailPath,
        const std::string& method,
        const std::string& body
    ) {
        const std::string baseUrl = resolveGailBaseUrl();
        if (baseUrl.empty()) {
            sendErrorResponse(res, 503, "Gail service is not configured (NMC_GAIL_BASE_URL not set).");
            return false;
        }

        TraceyEndpoint endpoint;
        if (!parseTraceyEndpoint(baseUrl, endpoint)) {
            sendErrorResponse(res, 503, "Gail base URL is invalid: " + baseUrl);
            return false;
        }

        const std::string apiToken = resolveGailApiToken();
        httplib::Headers headers{
            {"Accept", "application/json"},
        };
        if (!apiToken.empty()) {
            headers.emplace("Authorization", "Bearer " + apiToken);
        }

        // Forward query string from incoming request to upstream.
        std::string upstreamPath = buildTraceyPath(endpoint, gailPath);
        if (!req.params.empty()) {
            bool first = (upstreamPath.find('?') == std::string::npos);
            for (const auto& [k, v] : req.params) {
                upstreamPath += (first ? '?' : '&');
                upstreamPath += k + '=' + v;
                first = false;
            }
        }

        const auto doRequest = [&](auto& client) -> httplib::Result {
            client.set_connection_timeout(10);
            client.set_read_timeout(60);
            client.set_write_timeout(30);
            if (method == "GET") {
                return client.Get(upstreamPath.c_str(), headers);
            }
            if (method == "POST") {
                return client.Post(upstreamPath.c_str(), headers,
                                   body.empty() ? "{}" : body,
                                   "application/json");
            }
            // Fallback: treat as GET.
            return client.Get(upstreamPath.c_str(), headers);
        };

        httplib::Result result;
        if (endpoint.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient client(endpoint.host, endpoint.port);
            client.enable_server_certificate_verification(true);
            result = doRequest(client);
#else
            sendErrorResponse(res, 503, "HTTPS Gail proxy unavailable: httplib built without OpenSSL support.");
            return false;
#endif
        } else {
            httplib::Client client(endpoint.host, endpoint.port);
            result = doRequest(client);
        }

        if (!result) {
            sendErrorResponse(res, 502, "Gail proxy request failed: " + httplib::to_string(result.error()));
            return false;
        }

        const int upstreamStatus = result->status;
        nlohmann::json upstreamData = {
            {"gail_path", gailPath},
            {"upstream_status", upstreamStatus}
        };

        nlohmann::json parsedBody = nlohmann::json::parse(result->body, nullptr, false);
        if (!parsedBody.is_discarded()) {
            upstreamData["payload"] = parsedBody;
        } else if (!result->body.empty()) {
            upstreamData["text"] = result->body;
        } else {
            upstreamData["payload"] = nlohmann::json::object();
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = upstreamStatus >= 200 && upstreamStatus < 300;
        apiResponse.message = apiResponse.success
            ? "Gail trading request completed."
            : ("Gail trading request failed with HTTP " + std::to_string(upstreamStatus) + ".");
        apiResponse.data = upstreamData;
        sendJsonResponse(res, apiResponse);
        res.status = upstreamStatus;
        return true;
    }

    void APIRoutes::handleGailTradingStatus(const httplib::Request& req, httplib::Response& res) {
        proxyGailTradingRequest(req, res, "/v1/trading/status", "GET");
    }

    void APIRoutes::handleGailTradingPortfolio(const httplib::Request& req, httplib::Response& res) {
        proxyGailTradingRequest(req, res, "/v1/trading/portfolio", "GET");
    }

    void APIRoutes::handleGailTradingPositions(const httplib::Request& req, httplib::Response& res) {
        proxyGailTradingRequest(req, res, "/v1/trading/positions", "GET");
    }

    void APIRoutes::handleGailTradingHistory(const httplib::Request& req, httplib::Response& res) {
        proxyGailTradingRequest(req, res, "/v1/trading/history", "GET");
    }

    void APIRoutes::handleGailTradingLogs(const httplib::Request& req, httplib::Response& res) {
        proxyGailTradingRequest(req, res, "/v1/trading/logs", "GET");
    }

    void APIRoutes::handleGailTradingExchanges(const httplib::Request& req, httplib::Response& res) {
        proxyGailTradingRequest(req, res, "/v1/trading/exchanges", "GET");
    }

    void APIRoutes::handleGailTradingCurrencies(const httplib::Request& req, httplib::Response& res) {
        proxyGailTradingRequest(req, res, "/v1/trading/currencies", "GET");
    }

    void APIRoutes::handleGailTradingGetConfig(const httplib::Request& req, httplib::Response& res) {
        proxyGailTradingRequest(req, res, "/v1/trading/config", "GET");
    }

    void APIRoutes::handleGailTradingSetConfig(const httplib::Request& req, httplib::Response& res) {
        proxyGailTradingRequest(req, res, "/v1/trading/config", "POST", req.body);
    }

    void APIRoutes::handleGailTradingPause(const httplib::Request& req, httplib::Response& res) {
        proxyGailTradingRequest(req, res, "/v1/trading/pause", "POST", req.body);
    }

    void APIRoutes::handleGailTradingResume(const httplib::Request& req, httplib::Response& res) {
        proxyGailTradingRequest(req, res, "/v1/trading/resume", "POST", req.body);
    }

    void APIRoutes::handleGailTradingOverride(const httplib::Request& req, httplib::Response& res) {
        proxyGailTradingRequest(req, res, "/v1/trading/override", "POST", req.body);
    }

    void APIRoutes::handleGailTradingEvaluate(const httplib::Request& req, httplib::Response& res) {
        proxyGailTradingRequest(req, res, "/v1/trading/evaluate", "POST", req.body);
    }

} // namespace NMC::Server
