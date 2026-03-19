#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>
#include <httplib.h>
#include "APIRoutes.h"   // Our route handler class
#include "K8sHandlers.h"
#include <string>
#include <vector>        // Required for std::vector
#include <algorithm>     // Required for std::find
#include <exception>
#include <cstdlib>
#include <regex>         // For catching std::regex_error
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <kubernetes/include/apiClient.h>      // for apiClient_t, apiClient_create_with_base_path()
#include <kubernetes/api/CoreV1API.h>          // for CoreV1API_listNode, v1_node_list_free()
#include <kubernetes/config/kube_config.h>

static std::atomic<bool> k8sHealthy{false};

struct K8sClientConfig {
    char* basePath = nullptr;
    sslConfig_t* sslConfig = nullptr;
    list_t* apiKeys = nullptr;
};

static void freeK8sClientConfig(K8sClientConfig& cfg) {
    if (cfg.basePath || cfg.sslConfig || cfg.apiKeys) {
        free_client_config(cfg.basePath, cfg.sslConfig, cfg.apiKeys);
        cfg.basePath = nullptr;
        cfg.sslConfig = nullptr;
        cfg.apiKeys = nullptr;
    }
}

static apiClient_t* createK8sClientFromKubeconfig(K8sClientConfig& cfg) {
    const int rc = load_kube_config(&cfg.basePath, &cfg.sslConfig, &cfg.apiKeys, nullptr);
    if (rc != 0) {
        spdlog::warn("Unable to load Kubernetes kubeconfig (rc={}); disabling health monitor", rc);
        return nullptr;
    }

    apiClient_t* client = apiClient_create_with_base_path(cfg.basePath, cfg.sslConfig, cfg.apiKeys);
    if (!client) {
        spdlog::warn("Unable to create Kubernetes client from kubeconfig; disabling health monitor");
        freeK8sClientConfig(cfg);
    }
    return client;
}

void monitorK8sHealth(apiClient_t* client) {
    bool last = true;
    while (true) {
        bool ok = true;
        v1_node_list_t* node_list = nullptr;
        // prepare local params
        char* pretty               = nullptr;
        int   allowWatchBookmarks  = 0;
        char* _continue            = nullptr;
        char* fieldSelector        = nullptr;
        char* labelSelector        = nullptr;
        int   limit                = 1;
        char* resourceVersion      = nullptr;
        char* resourceVersionMatch = nullptr;
        int   sendInitialEvents    = 0;
        int   timeoutSeconds       = 0;
        int   watch                = 0;

        try {
            node_list = CoreV1API_listNode(
                client,
                pretty,
                &allowWatchBookmarks,
                _continue,
                fieldSelector,
                labelSelector,
                &limit,
                resourceVersion,
                resourceVersionMatch,
                &sendInitialEvents,
                &timeoutSeconds,
                &watch
            );
        }
        catch (...) {
            ok = false;
        }
        if (node_list) {
           v1_node_list_free(node_list);
        }
        else {
           ok = false;
        }
        if (ok != last) {
            k8sHealthy.store(ok);
            if (ok) spdlog::info("K8s API is healthy again");
            else    spdlog::warn("K8s API is DOWN — marking degraded");
            last = ok;
        }
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

// Load a simple JSON config with keys "server", "port", "token"
static void loadConfigFromFile(const std::string &path) {
    std::ifstream in{path};
    if (!in) {
        spdlog::critical("Unable to open Kubernetes config file '{}'", path);
        std::exit(1);
    }
    nlohmann::json cfg;
    in >> cfg;
    if (cfg.contains("server")) {
        setenv("KUBERNETES_SERVICE_HOST",
            cfg["server"].get<std::string>().c_str(), 1);
    }
    if (cfg.contains("port")) {
        auto p = std::to_string(cfg["port"].get<int>());
        setenv("KUBERNETES_SERVICE_PORT", p.c_str(), 1);
    }
    if (cfg.contains("token")) {
        setenv("KUBERNETES_SERVICEACCOUNT_TOKEN",
            cfg["token"].get<std::string>().c_str(), 1);
    }
}

// Custom terminate handler to catch any uncaught std::regex_error
void myTerminate() {
    if (auto ep = std::current_exception()) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::regex_error& ex) {
            std::cerr << "Fatal regex_error: " << ex.what() << std::endl;
        } catch (const std::exception& ex) {
            std::cerr << "Fatal exception: " << ex.what() << std::endl;
        } catch (...) {
            std::cerr << "Fatal unknown exception" << std::endl;
        }
    } else {
        std::cerr << "Terminated without an active exception" << std::endl;
    }
    std::_Exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    // --- Initialize spdlog rotating file logger ---
    // Rotates at 5 MiB, keeps 3 files
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "logs/nmc_server.log",  // log file path
        1048576 * 5,            // max file size = 5 MiB
        3                       // max rotated files
    );
    auto logger = std::make_shared<spdlog::logger>("nmc", file_sink);
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::debug);    // log debug+ messages
    spdlog::flush_on(spdlog::level::info);      // flush on info or higher

    // Install custom terminate handler
    std::set_terminate(myTerminate);

    try {
        const char* authModeEnv = std::getenv("NMC_AUTH_MODE");
        const std::string authMode = authModeEnv ? std::string(authModeEnv) : "token";
        if (authMode == "off") {
            spdlog::warn("NMC_AUTH_MODE=off. API authentication is disabled.");
        } else if (authMode == "token") {
            const char* authTokenEnv = std::getenv("NMC_AUTH_TOKEN");
            if (!authTokenEnv || std::string(authTokenEnv).empty()) {
                spdlog::warn("NMC_AUTH_TOKEN is not set. API authentication is disabled.");
            }
        } else if (authMode == "oidc") {
            const char* introspection = std::getenv("NMC_OIDC_INTROSPECTION_URL");
            if (!introspection || std::string(introspection).empty()) {
                spdlog::warn("NMC_OIDC_INTROSPECTION_URL is not set. OIDC authentication will fail closed.");
            }
        }
        //
        // 1) Handle '-k' or '-c' overrides, or fall back to env/default config.json
        //
        std::string cfgPath;
        std::string kubeconfigPath;
        bool useConfigFile = false;
        bool useKubeConfig = false;

        // scan for -k and -c before we parse --port below
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if ((a == "-k" || a == "--kubeconfig") && i + 1 < argc) {
                kubeconfigPath = argv[++i];
                useKubeConfig = true;
            }
            else if ((a == "-c" || a == "--config") && i + 1 < argc) {
                cfgPath = argv[++i];
                useConfigFile = true;
            }
        }

        if (useKubeConfig) {
            // Highest precedence: use kubeconfig file
            setenv("KUBECONFIG", kubeconfigPath.c_str(), 1);
            spdlog::info("Using kubeconfig file '{}'", kubeconfigPath);
        }
        else if (useConfigFile) {
            // Next: explicit config file
            spdlog::info("Loading Kubernetes config from '{}'", cfgPath);
            loadConfigFromFile(cfgPath);
        }
        else {
            // No override flags: check env
            if (getenv("KUBECONFIG") ||
                getenv("KUBERNETES_SERVICE_HOST")) {
                spdlog::info("Using Kubernetes env variables");
            } else {
                // fallback to ~/.nmc/config.json
                auto home = getenv("HOME") ? getenv("HOME") : "";
                std::string defaultCfg = std::string(home) + "/.nmc/config.json";
                spdlog::info("No K8s env vars found; loading '{}'", defaultCfg);
                loadConfigFromFile(defaultCfg);
            }
        }
        spdlog::info("Starting NMC server initialization");
        // 1. Kubernetes C Client Global Environment Setup
        K8sClientConfig k8sConfig{};
        apiClient_setupGlobalEnv();
        apiClient_t* k8s = createK8sClientFromKubeconfig(k8sConfig);
        spdlog::info("Kubernetes C client global environment set up");

        // start health-monitor thread
        if (k8s) {
            std::thread monitorThread(monitorK8sHealth, k8s);
            monitorThread.detach();
        } else {
            k8sHealthy.store(false);
            spdlog::warn("K8s health monitor is disabled due to Kubernetes client initialization failure");
        }

        httplib::Server svr; // Create an HTTP server instance

        // Set up API routes
        NMC::Server::APIRoutes apiRoutes(svr);

        // Set server connection timeout in seconds (optional)
        svr.set_read_timeout(5);  // Read timeout for request
        svr.set_write_timeout(5); // Write timeout for response

        // Listen on port 8080 (you can change this)
        const char* host = "0.0.0.0"; // Listen on all available network interfaces
        int port = 8080;

        // --- Parse command-line arguments for port ---
        std::vector<std::string> args(argv + 1, argv + argc); // Convert argv to vector for easier parsing

        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--port" || args[i] == "-p") {
                if (i + 1 < args.size()) {
                    try {
                        port = std::stoi(args[i + 1]);
                        // Basic port validation
                        if (port <= 0 || port > 65535) {
                            spdlog::error("Invalid port number {}. Must be 1-65535.", port);
                            return EXIT_FAILURE;
                        }
                        i++; // Skip the next argument as it's the port value
                    } catch (const std::exception& e) {
                        spdlog::error("Invalid value for --port: {}", e.what());
                        return EXIT_FAILURE;
                    }
                } else {
                    spdlog::error("--port argument requires a value");
                    return EXIT_FAILURE;
                }
            }
            // Add other command-line arguments parsing here if needed in the future
        }

        spdlog::info("NMC Server listening on http://{}:{}", host, port);
        spdlog::info("Press Ctrl+C to stop the server");

        // Start the server (blocking call)
        if (!svr.listen(host, port)) {
            spdlog::critical("Could not start server on {}:{}", host, port);
            if (k8s) {
                apiClient_free(k8s);
            }
            freeK8sClientConfig(k8sConfig);
            apiClient_unsetupGlobalEnv();
            return EXIT_FAILURE;
        }

        // 2. Kubernetes C Client Global Environment Teardown
        // Must be called ONCE after all worker threads (including HTTP server threads) have finished.
        if (k8s) {
            apiClient_free(k8s);
        }
        freeK8sClientConfig(k8sConfig);
        apiClient_unsetupGlobalEnv();
        spdlog::info("Kubernetes C client global environment torn down");

        return EXIT_SUCCESS;

    } catch (const std::regex_error& ex) {
        spdlog::critical("Caught regex_error: {}", ex.what());
        spdlog::critical("Shutting down due to invalid regex usage");
        return EXIT_FAILURE;
    } catch (const std::exception& ex) {
        spdlog::critical("Unhandled exception: {}", ex.what());
        return EXIT_FAILURE;
    } catch (...) {
        spdlog::critical("Unknown fatal error encountered");
        return EXIT_FAILURE;
    }
}
