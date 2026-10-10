#include "APIRoutes.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace NMC::Server {
    namespace {
        using Json = nlohmann::json;
        constexpr std::size_t MAX_GITHUB_JSON_BYTES = 2 * 1024 * 1024;
        constexpr std::size_t MAX_RESULT_ARCHIVE_BYTES = 64 * 1024 * 1024;
        constexpr std::size_t MAX_TASK_RECORDS = 5000;
        constexpr int64_t TASK_RETENTION_MS = 30LL * 24 * 60 * 60 * 1000;

        struct GithubConfig {
            std::string repository;
            std::string workflow{"nmide-remote-task.yml"};
            std::string trustedRef{"main"};
            std::string token;
            std::filesystem::path statePath;
        };

        struct GithubResponse {
            int status{0};
            std::string body;
            httplib::Headers headers;
            bool tooLarge{false};
        };

        std::mutex stateMutex;
        std::atomic<unsigned long long> temporaryFileSequence{0};

        int64_t nowEpochMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
        }

        std::string envValue(const char* name, const std::string& fallback = {}) {
            const char* value = std::getenv(name);
            if (!value || !*value) return fallback;
            return value;
        }

        GithubConfig githubConfig() {
            GithubConfig config;
            config.repository = envValue("NMC_GITHUB_ACTIONS_REPOSITORY");
            config.workflow = envValue("NMC_GITHUB_ACTIONS_WORKFLOW", config.workflow);
            config.trustedRef = envValue("NMC_GITHUB_ACTIONS_TRUSTED_REF", config.trustedRef);
            config.token = envValue("NMC_GITHUB_ACTIONS_TOKEN");
            const std::string stateRoot = envValue("NMC_GITHUB_ACTIONS_STATE_ROOT",
                    (std::filesystem::path(envValue("NMC_STATE_ROOT", "/var/lib/nmc")) / "github-actions").string());
            config.statePath = envValue("NMC_GITHUB_ACTIONS_STATE_FILE").empty()
                    ? std::filesystem::path(stateRoot) / "tasks.json"
                    : std::filesystem::path(envValue("NMC_GITHUB_ACTIONS_STATE_FILE"));
            return config;
        }

        bool validConfig(const GithubConfig& config) {
            static const std::regex repositoryPattern(R"(^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$)");
            static const std::regex workflowPattern(R"(^[A-Za-z0-9_.-]{1,120}$)");
            static const std::regex refPattern(R"(^[A-Za-z0-9._/-]{1,160}$)");
            return !config.token.empty()
                    && std::regex_match(config.repository, repositoryPattern)
                    && std::regex_match(config.workflow, workflowPattern)
                    && std::regex_match(config.trustedRef, refPattern)
                    && config.trustedRef.find("..") == std::string::npos
                    && config.trustedRef.find("//") == std::string::npos;
        }

        std::string workflowPath(const GithubConfig& config) {
            return "/repos/" + config.repository + "/actions/workflows/" + config.workflow;
        }

        httplib::Headers githubHeaders(const GithubConfig& config) {
            return {
                    {"Authorization", "Bearer " + config.token},
                    {"Accept", "application/vnd.github+json"},
                    {"X-GitHub-Api-Version", "2022-11-28"},
                    {"User-Agent", "nmc-continuum-github-actions"}
            };
        }

        GithubResponse githubRequest(const GithubConfig& config,
                                     const std::string& method,
                                     const std::string& path,
                                     const std::string& body = {},
                                     std::size_t maxBytes = MAX_GITHUB_JSON_BYTES) {
            httplib::SSLClient client("api.github.com", 443);
            client.set_connection_timeout(5, 0);
            client.set_read_timeout(30, 0);
            client.set_write_timeout(10, 0);
            client.set_follow_location(false);

            GithubResponse output;
            auto receive = [&](const char* data, std::size_t length) {
                if (output.body.size() + length > maxBytes) {
                    output.tooLarge = true;
                    return false;
                }
                output.body.append(data, length);
                return true;
            };
            httplib::Result response;
            const auto headers = githubHeaders(config);
            if (method == "GET") {
                response = client.Get(path, headers, receive);
            } else if (method == "POST") {
                response = client.Post(path, headers, body, "application/json");
            } else {
                return output;
            }
            if (!response) return output;
            output.status = response->status;
            output.headers = response->headers;
            if (method == "POST" && response->body.size() <= maxBytes) {
                output.body = response->body;
            } else if (method == "POST" && !response->body.empty()) {
                output.tooLarge = true;
            }
            return output;
        }

        GithubResponse artifactStorageGet(const std::string& location) {
            static const std::regex locationPattern(R"(^https://([A-Za-z0-9.-]+)(/[^\r\n ]*)$)");
            std::smatch match;
            if (!std::regex_match(location, match, locationPattern)) return {};
            std::string host = match[1].str();
            std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const bool allowedHost = (host.size() > 22 && host.compare(host.size() - 22, 22, ".blob.core.windows.net") == 0)
                    || (host.size() > 14 && host.compare(host.size() - 14, 14, ".amazonaws.com") == 0)
                    || host == "release-assets.githubusercontent.com"
                    || host == "objects.githubusercontent.com";
            if (!allowedHost) return {};

            httplib::SSLClient client(host, 443);
            client.set_connection_timeout(5, 0);
            client.set_read_timeout(60, 0);
            client.set_write_timeout(10, 0);
            client.set_follow_location(false);
            GithubResponse output;
            auto receive = [&](const char* data, std::size_t length) {
                if (output.body.size() + length > MAX_RESULT_ARCHIVE_BYTES) {
                    output.tooLarge = true;
                    return false;
                }
                output.body.append(data, length);
                return true;
            };
            auto response = client.Get(match[2].str(), receive);
            if (!response) return output;
            output.status = response->status;
            output.headers = response->headers;
            return output;
        }

        std::filesystem::path statePath(const GithubConfig& config) {
            return config.statePath;
        }

        Json readTaskStore(const GithubConfig& config) {
            const auto path = statePath(config);
            std::error_code error;
            const auto type = std::filesystem::symlink_status(path, error).type();
            if (!error && type == std::filesystem::file_type::symlink) {
                throw std::runtime_error("GitHub Actions task state must not be a symbolic link.");
            }
            std::ifstream input(path);
            if (!input.is_open()) {
                if (std::filesystem::exists(path)) throw std::runtime_error("GitHub Actions task state could not be read.");
                return {{"version", 1}, {"tasks", Json::object()}};
            }
            if (std::filesystem::file_size(path) > 8 * 1024 * 1024)
                throw std::runtime_error("GitHub Actions task state exceeds its bounded size.");
            Json value = Json::parse(input, nullptr, false);
            if (value.is_discarded() || !value.is_object() || !value.value("tasks", Json::object()).is_object()) {
                throw std::runtime_error("GitHub Actions task state is invalid; failing closed.");
            }
            return value;
        }

        void writeAll(int fd, const std::string& value) {
            std::size_t offset = 0;
            while (offset < value.size()) {
                const ssize_t written = ::write(fd, value.data() + offset, value.size() - offset);
                if (written < 0 && errno == EINTR) continue;
                if (written <= 0) throw std::runtime_error("Could not persist GitHub Actions task state.");
                offset += static_cast<std::size_t>(written);
            }
        }

        void writeTaskStore(const GithubConfig& config, const Json& value) {
            const auto path = statePath(config);
            std::filesystem::create_directories(path.parent_path());
            ::chmod(path.parent_path().c_str(), 0700);
            std::error_code error;
            const auto type = std::filesystem::symlink_status(path, error).type();
            if (!error && type == std::filesystem::file_type::symlink) {
                throw std::runtime_error("GitHub Actions task state must not be a symbolic link.");
            }
            const auto temporary = path.string() + ".tmp." + std::to_string(::getpid()) + "."
                    + std::to_string(temporaryFileSequence.fetch_add(1));
            int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
            if (fd < 0) throw std::runtime_error("Could not create protected GitHub Actions task state.");
            try {
                const std::string encoded = value.dump();
                writeAll(fd, encoded);
                if (::fsync(fd) != 0) throw std::runtime_error("Could not sync GitHub Actions task state.");
                ::close(fd);
                fd = -1;
                if (::rename(temporary.c_str(), path.c_str()) != 0) throw std::runtime_error("Could not replace GitHub Actions task state.");
                const int directoryFd = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
                if (directoryFd >= 0) {
                    ::fsync(directoryFd);
                    ::close(directoryFd);
                }
            } catch (...) {
                if (fd >= 0) ::close(fd);
                std::filesystem::remove(temporary, error);
                throw;
            }
        }

        bool validTaskId(const std::string& value) {
            static const std::regex pattern(R"(^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$)");
            return std::regex_match(value, pattern);
        }

        bool validRunId(const std::string& value) {
            static const std::regex pattern(R"(^[0-9]{1,20}$)");
            return std::regex_match(value, pattern);
        }

        Json safeRun(Json run) {
            Json value = Json::object();
            for (const char* key : {"id", "run_number", "run_attempt", "status", "conclusion", "created_at", "updated_at", "html_url", "workflow_id", "head_branch", "head_sha", "event", "display_title"}) {
                if (run.is_object() && run.contains(key)) value[key] = run[key];
            }
            return value;
        }

        std::string jsonText(const GithubResponse& response) {
            return response.body;
        }

        Json parseJson(const GithubResponse& response) {
            Json value = Json::parse(jsonText(response), nullptr, false);
            return value.is_discarded() ? Json::object() : value;
        }

        void sendGithubError(httplib::Response& res, int status, const std::string& message) {
            res.status = status;
            res.set_content(Json{{"success", false}, {"message", message}, {"data", Json::object()}}.dump(), "application/json");
        }

        std::optional<Json> findTask(const GithubConfig& config, const std::string& taskId) {
            std::lock_guard<std::mutex> lock(stateMutex);
            Json store = readTaskStore(config);
            auto it = store["tasks"].find(taskId);
            if (it == store["tasks"].end()) return std::nullopt;
            return *it;
        }

        void updateTask(const GithubConfig& config, const std::string& taskId, const Json& task) {
            std::lock_guard<std::mutex> lock(stateMutex);
            Json store = readTaskStore(config);
            store["tasks"][taskId] = task;
            store["tasks"][taskId]["updated_epoch_ms"] = nowEpochMs();
            writeTaskStore(config, store);
        }

        void pruneOldTaskRecords(Json& store) {
            const int64_t cutoff = nowEpochMs() - TASK_RETENTION_MS;
            auto& tasks = store["tasks"];
            for (auto it = tasks.begin(); it != tasks.end();) {
                const std::string state = it.value().value("state", std::string());
                const bool terminal = state == "completed" || state == "rejected" || state == "cancelled";
                const int64_t updated = it.value().value("updated_epoch_ms", 0LL);
                if (terminal && updated > 0 && updated < cutoff) it = tasks.erase(it);
                else ++it;
            }
        }

        std::optional<Json> reconcileTaskRun(const GithubConfig& config, const std::string& taskId) {
            GithubResponse response = githubRequest(config, "GET", workflowPath(config) + "/runs?per_page=100");
            if (response.status < 200 || response.status >= 300) return std::nullopt;
            Json body = parseJson(response);
            if (!body.contains("workflow_runs") || !body["workflow_runs"].is_array()) return std::nullopt;
            const std::string expectedTitle = "NeuralMimicry task " + taskId;
            for (const auto& run : body["workflow_runs"]) {
                if (run.value("display_title", std::string()) != expectedTitle
                    || run.value("event", std::string()) != "workflow_dispatch"
                    || run.value("head_branch", std::string()) != config.trustedRef
                    || !run.contains("id")) continue;
                return safeRun(run);
            }
            return std::nullopt;
        }

        std::optional<Json> ensureTaskRun(const GithubConfig& config, const std::string& taskId) {
            auto task = findTask(config, taskId);
            if (!task) return std::nullopt;
            if (task->contains("run_id")) return task;
            if (task->value("state", std::string()) != "dispatch_unknown"
                && task->value("state", std::string()) != "dispatching") return task;
            const auto run = reconcileTaskRun(config, taskId);
            if (!run) return task;
            (*task)["run_id"] = (*run).value("id", Json());
            (*task)["run"] = *run;
            (*task)["state"] = "queued";
            updateTask(config, taskId, *task);
            return task;
        }

        bool taskOwnsRun(const Json& task, const std::string& runId) {
            if (!task.is_object() || !task.contains("run_id")) return false;
            if (!task["run_id"].is_number_unsigned() && !task["run_id"].is_number_integer()) return false;
            try { return std::to_string(task["run_id"].get<unsigned long long>()) == runId; }
            catch (const std::exception&) { return false; }
        }

        std::string pathRunId(const httplib::Request& req) {
            static const std::regex pattern(R"(^/github/actions/runs/([0-9]{1,20})(?:/(cancel|result))?$)");
            std::smatch match;
            return std::regex_match(req.path, match, pattern) ? match[1].str() : std::string();
        }

        Json runForTask(const GithubConfig& config, const httplib::Request& req, const std::string& runId) {
            const std::string taskId = req.get_param_value("task_id");
            if (!validTaskId(taskId)) throw std::invalid_argument("A valid task_id is required.");
            auto task = ensureTaskRun(config, taskId);
            if (!task || !taskOwnsRun(*task, runId)) throw std::out_of_range("The workflow run is not registered to this task.");
            return *task;
        }
    }

    void APIRoutes::registerGitHubActionsRoutes(httplib::Server& svr, const RouteGuard& guard) {
        svr.Get("/github/actions/workflow", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGitHubActionsWorkflow(req, res);
        });
        svr.Post("/github/actions/dispatch", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGitHubActionsDispatch(req, res);
        });
        svr.Get(R"(/github/actions/tasks/([0-9a-fA-F-]{36}))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGitHubActionsTask(req, res);
        });
        svr.Get(R"(/github/actions/runs/([0-9]{1,20}))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGitHubActionsRun(req, res);
        });
        svr.Post(R"(/github/actions/runs/([0-9]{1,20})/cancel)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGitHubActionsCancel(req, res);
        });
        svr.Get(R"(/github/actions/runs/([0-9]{1,20})/result)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGitHubActionsResult(req, res);
        });
    }

    std::string APIRoutes::githubActionsActor(const httplib::Request& req) const {
        auto cleanActor = [](std::string actor) {
            if (actor.size() > 160) actor.resize(160);
            for (char& character : actor) {
                const auto value = static_cast<unsigned char>(character);
                if (value < 0x20 || value == 0x7f) character = '_';
            }
            return actor.empty() ? std::string("authenticated-client") : actor;
        };
        const std::string token = extractAuthToken(req);
        Json claims;
        if (validateCentralAuthToken(token, &claims) == CentralAuthValidationStatus::authenticated) {
            return cleanActor(claims.value("user", claims.value("sub", std::string("authenticated-user"))));
        }
        if (!authToken.empty() && token == authToken) return "nmc-static-admin-service-token";
        if (oidcValidator) {
            std::string error;
            if (oidcValidator->validateToken(token, error, &claims)) {
                return cleanActor(claims.value("preferred_username", claims.value("sub", std::string("authenticated-oidc-client"))));
            }
        }
        return "authenticated-client";
    }

    void APIRoutes::handleGitHubActionsWorkflow(const httplib::Request&, httplib::Response& res) {
        const GithubConfig config = githubConfig();
        if (!validConfig(config)) return sendGithubError(res, 503, "GitHub Actions control is not configured with a repository-scoped credential.");
        const GithubResponse response = githubRequest(config, "GET", workflowPath(config));
        if (response.status < 200 || response.status >= 300) return sendGithubError(res, 503, "The trusted GitHub workflow could not be inspected.");
        const Json body = parseJson(response);
        if (!body.contains("id") || !body.contains("state")) return sendGithubError(res, 502, "GitHub returned invalid workflow metadata.");
        res.set_content(Json{{"success", true}, {"message", "Trusted GitHub workflow resolved."},
                             {"data", {{"id", body["id"]}, {"path", body.value("path", std::string())},
                                       {"state", body["state"]}, {"trusted_ref", config.trustedRef}}}}.dump(), "application/json");
    }

    void APIRoutes::handleGitHubActionsDispatch(const httplib::Request& req, httplib::Response& res) {
        const GithubConfig config = githubConfig();
        if (!validConfig(config)) return sendGithubError(res, 503, "GitHub Actions control is not configured with a repository-scoped credential.");
        if (req.body.size() > 16 * 1024) return sendGithubError(res, 413, "Task dispatch body exceeds the 16 KiB limit.");
        Json body = Json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) return sendGithubError(res, 400, "A JSON task dispatch body is required.");
        static const std::set<std::string> allowedFields = {"task_id", "workspace_id", "source_sha", "source_tree", "task_kind"};
        if (body.size() != allowedFields.size()) return sendGithubError(res, 400, "Task dispatch must contain only its five required fields.");
        for (auto it = body.begin(); it != body.end(); ++it) {
            if (allowedFields.find(it.key()) == allowedFields.end()) return sendGithubError(res, 400, "Task dispatch includes an unsupported field.");
        }
        const std::string taskId = body.value("task_id", std::string());
        const std::string workspaceId = body.value("workspace_id", std::string());
        const std::string sourceSha = body.value("source_sha", std::string());
        const std::string sourceTree = body.value("source_tree", std::string());
        const std::string taskKind = body.value("task_kind", std::string());
        static const std::regex workspacePattern(R"(^[a-z0-9-]{1,48}$)");
        static const std::regex shaPattern(R"(^[0-9a-f]{40}$)");
        if (!validTaskId(taskId) || !std::regex_match(workspaceId, workspacePattern)
            || !std::regex_match(sourceSha, shaPattern) || !std::regex_match(sourceTree, shaPattern)
            || (taskKind != "check" && taskKind != "test" && taskKind != "python-test")) {
            return sendGithubError(res, 400, "Task ID, workspace, checkpoint SHA/tree or fixed task profile is invalid.");
        }

        Json record = {{"task_id", taskId}, {"workspace_id", workspaceId}, {"source_sha", sourceSha},
                       {"source_tree", sourceTree}, {"task_kind", taskKind},
                       {"state", "dispatching"}, {"request_id", req.get_header_value("X-Request-ID")},
                       {"actor", githubActionsActor(req)}, {"created_epoch_ms", nowEpochMs()},
                       {"updated_epoch_ms", nowEpochMs()}};
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            Json store;
            try { store = readTaskStore(config); }
            catch (const std::exception&) { return sendGithubError(res, 503, "GitHub Actions task state is unavailable; dispatch was not attempted."); }
            pruneOldTaskRecords(store);
            auto prior = store["tasks"].find(taskId);
            if (prior != store["tasks"].end()) {
                for (const char* key : {"workspace_id", "source_sha", "source_tree", "task_kind"}) {
                    if (prior->value(key, std::string()) != record.value(key, std::string()))
                        return sendGithubError(res, 409, "Task ID was previously bound to a different source snapshot.");
                }
                res.status = prior->value("run_id", Json()).is_null() ? 202 : 200;
                res.set_content(Json{{"success", true}, {"message", "Task dispatch is already recorded."}, {"data", *prior}}.dump(), "application/json");
                return;
            }
            if (store["tasks"].size() >= MAX_TASK_RECORDS)
                return sendGithubError(res, 503, "Continuum task correlation storage is full; dispatch was not attempted.");
            store["tasks"][taskId] = record;
            try { writeTaskStore(config, store); }
            catch (const std::exception&) { return sendGithubError(res, 503, "GitHub Actions task state could not be persisted; dispatch was not attempted."); }
        }

        Json dispatchBody = {{"ref", config.trustedRef}, {"inputs", {
                {"task_id", taskId}, {"workspace_id", workspaceId}, {"source_sha", sourceSha},
                {"source_tree", sourceTree}, {"task_kind", taskKind}}}};
        const GithubResponse response = githubRequest(config, "POST", workflowPath(config) + "/dispatches?return_run_details=true", dispatchBody.dump());
        Json updated = record;
        updated["state"] = "dispatch_unknown";
        if (response.status >= 200 && response.status < 300) {
            const Json result = parseJson(response);
            Json run = result.contains("workflow_run") && result["workflow_run"].is_object()
                    ? safeRun(result["workflow_run"]) : safeRun(result);
            if (run.contains("id")) {
                updated["run_id"] = run["id"];
                updated["run"] = run;
                updated["state"] = "queued";
            }
        } else if (response.status >= 400 && response.status < 500) {
            updated["state"] = "rejected";
            updateTask(config, taskId, updated);
            return sendGithubError(res, 502, "GitHub rejected the trusted workflow dispatch.");
        }
        updateTask(config, taskId, updated);
        res.status = 202;
        res.set_content(Json{{"success", true}, {"message", updated.contains("run_id")
                ? "Trusted workflow queued." : "Dispatch outcome is being reconciled; it will not be submitted twice."},
                {"data", updated}}.dump(), "application/json");
    }

    void APIRoutes::handleGitHubActionsRun(const httplib::Request& req, httplib::Response& res) {
        const GithubConfig config = githubConfig();
        if (!validConfig(config)) return sendGithubError(res, 503, "GitHub Actions control is not configured.");
        const std::string runId = pathRunId(req);
        try {
            const Json task = runForTask(config, req, runId);
            const GithubResponse response = githubRequest(config, "GET", "/repos/" + config.repository + "/actions/runs/" + runId);
            if (response.status < 200 || response.status >= 300) return sendGithubError(res, 503, "The registered GitHub workflow run could not be read.");
            const Json run = safeRun(parseJson(response));
            Json merged = task;
            merged["run"] = run;
            merged["state"] = run.value("status", std::string("unknown"));
            updateTask(config, task.value("task_id", std::string()), merged);
            res.set_content(Json{{"success", true}, {"message", "Registered workflow status retrieved."}, {"data", run}}.dump(), "application/json");
        } catch (const std::invalid_argument&) { sendGithubError(res, 400, "A valid task_id is required."); }
        catch (const std::out_of_range&) { sendGithubError(res, 404, "Workflow run is not registered to this task."); }
        catch (const std::exception&) { sendGithubError(res, 503, "GitHub Actions task state is unavailable."); }
    }

    void APIRoutes::handleGitHubActionsTask(const httplib::Request& req, httplib::Response& res) {
        const GithubConfig config = githubConfig();
        if (!validConfig(config)) return sendGithubError(res, 503, "GitHub Actions control is not configured.");
        static const std::regex pathPattern(R"(^/github/actions/tasks/([0-9a-fA-F-]{36})$)");
        std::smatch match;
        if (!std::regex_match(req.path, match, pathPattern) || !validTaskId(match[1].str()))
            return sendGithubError(res, 400, "A valid task ID is required.");
        const std::string taskId = match[1].str();
        try {
            auto task = ensureTaskRun(config, taskId);
            if (!task) return sendGithubError(res, 404, "GitHub task was not found.");
            if (!task->contains("run_id")) {
                res.status = 202;
                res.set_content(Json{{"success", true}, {"message", "Dispatch result is still being reconciled."}, {"data", *task}}.dump(), "application/json");
                return;
            }
            const std::string runId = std::to_string((*task)["run_id"].get<unsigned long long>());
            const GithubResponse response = githubRequest(config, "GET", "/repos/" + config.repository + "/actions/runs/" + runId);
            if (response.status < 200 || response.status >= 300) return sendGithubError(res, 503, "The registered GitHub workflow run could not be read.");
            const Json run = safeRun(parseJson(response));
            (*task)["run"] = run;
            (*task)["state"] = run.value("status", std::string("unknown"));
            updateTask(config, taskId, *task);
            res.set_content(Json{{"success", true}, {"message", "Registered workflow status retrieved."}, {"data", *task}}.dump(), "application/json");
        } catch (const std::exception&) {
            sendGithubError(res, 503, "GitHub Actions task state is unavailable.");
        }
    }

    void APIRoutes::handleGitHubActionsCancel(const httplib::Request& req, httplib::Response& res) {
        const GithubConfig config = githubConfig();
        if (!validConfig(config)) return sendGithubError(res, 503, "GitHub Actions control is not configured.");
        const std::string runId = pathRunId(req);
        try {
            const Json task = runForTask(config, req, runId);
            const GithubResponse response = githubRequest(config, "POST", "/repos/" + config.repository + "/actions/runs/" + runId + "/cancel", "{}");
            if (response.status < 200 || response.status >= 300)
                return sendGithubError(res, 503, "Cancellation of the registered workflow run was not confirmed.");
            Json updated = task;
            updated["cancel_requested"] = true;
            updateTask(config, task.value("task_id", std::string()), updated);
            res.status = 202;
            res.set_content(Json{{"success", true}, {"message", "Workflow cancellation requested."}, {"data", {{"task_id", task["task_id"]}, {"run_id", task["run_id"]}, {"cancel_requested", true}}}}.dump(), "application/json");
        } catch (const std::invalid_argument&) { sendGithubError(res, 400, "A valid task_id is required."); }
        catch (const std::out_of_range&) { sendGithubError(res, 404, "Workflow run is not registered to this task."); }
        catch (const std::exception&) { sendGithubError(res, 503, "GitHub Actions task state is unavailable."); }
    }

    void APIRoutes::handleGitHubActionsResult(const httplib::Request& req, httplib::Response& res) {
        const GithubConfig config = githubConfig();
        if (!validConfig(config)) return sendGithubError(res, 503, "GitHub Actions control is not configured.");
        const std::string runId = pathRunId(req);
        try {
            const Json task = runForTask(config, req, runId);
            const GithubResponse listing = githubRequest(config, "GET", "/repos/" + config.repository + "/actions/runs/" + runId + "/artifacts?per_page=20");
            if (listing.status < 200 || listing.status >= 300) return sendGithubError(res, 503, "Workflow result metadata could not be read.");
            const Json artifacts = parseJson(listing);
            const std::string expectedName = "nmide-result-" + task.value("task_id", std::string());
            Json selected;
            if (artifacts.contains("artifacts") && artifacts["artifacts"].is_array()) {
                for (const auto& artifact : artifacts["artifacts"]) {
                    if (artifact.value("name", std::string()) == expectedName && !artifact.value("expired", true)) {
                        selected = artifact;
                        break;
                    }
                }
            }
            if (selected.is_null()) return sendGithubError(res, 404, "No retained result artifact is available for this task.");
            const std::string artifactId = std::to_string(selected.value("id", 0LL));
            if (!validRunId(artifactId) || artifactId == "0") return sendGithubError(res, 502, "GitHub returned an invalid result artifact identity.");
            const GithubResponse redirect = githubRequest(config, "GET", "/repos/" + config.repository + "/actions/artifacts/" + artifactId + "/zip", {}, 1024);
            if (redirect.status != 302 && redirect.status != 301) return sendGithubError(res, 503, "GitHub did not provide a bounded artifact download.");
            auto location = redirect.headers.find("Location");
            if (location == redirect.headers.end()) return sendGithubError(res, 502, "GitHub result redirect did not include a download location.");
            const GithubResponse archive = artifactStorageGet(location->second);
            if (archive.tooLarge) return sendGithubError(res, 413, "Result artifact exceeds the 64 MiB transfer limit.");
            if (archive.status < 200 || archive.status >= 300) return sendGithubError(res, 503, "The bounded result artifact could not be downloaded.");
            res.status = 200;
            res.set_header("Content-Disposition", "attachment; filename=nmide-result.zip");
            res.set_content(archive.body, "application/zip");
        } catch (const std::invalid_argument&) { sendGithubError(res, 400, "A valid task_id is required."); }
        catch (const std::out_of_range&) { sendGithubError(res, 404, "Workflow run is not registered to this task."); }
        catch (const std::exception&) { sendGithubError(res, 503, "GitHub Actions task state is unavailable."); }
    }
}
