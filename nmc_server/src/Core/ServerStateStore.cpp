// server/ServerStateStore.cpp
#include "ServerStateStore.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <system_error>
#include <utility>

#ifdef NMC_HAS_LIBPQXX
#include <pqxx/pqxx>
#endif

namespace {

    using json = nlohmann::json;

    std::string trimCopy(const std::string& value) {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return "";
        }
        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    int64_t nowEpochMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    std::filesystem::path defaultServerSnapshotPath() {
        if (const char* raw = std::getenv("NMC_SERVER_STATE_ROOT")) {
            const std::string value = trimCopy(raw);
            if (!value.empty()) {
                return std::filesystem::path(value) / "server_state_snapshot.json";
            }
        }
        if (const char* raw = std::getenv("NMC_STATE_ROOT")) {
            const std::string value = trimCopy(raw);
            if (!value.empty()) {
                return std::filesystem::path(value) / "server_state_snapshot.json";
            }
        }
        if (const char* raw = std::getenv("NMC_TRACEY_STATE_ROOT")) {
            const std::string value = trimCopy(raw);
            if (!value.empty()) {
                return std::filesystem::path(value) / "server_state_snapshot.json";
            }
        }
        if (const char* stateHome = std::getenv("XDG_STATE_HOME")) {
            const std::string value = trimCopy(stateHome);
            if (!value.empty()) {
                return std::filesystem::path(value) / "nmc" / "server_state" / "server_state_snapshot.json";
            }
        }
        if (const char* home = std::getenv("HOME")) {
            const std::string value = trimCopy(home);
            if (!value.empty()) {
                return std::filesystem::path(value) / ".local" / "state" / "nmc" / "server_state" / "server_state_snapshot.json";
            }
        }
        return std::filesystem::temp_directory_path() / "nmc-server-state" / "server_state_snapshot.json";
    }

    json normalizeSnapshotEnvelope(json envelope) {
        if (!envelope.is_object() || !envelope.contains("payload")) {
            return json::object();
        }
        return {
                {"updated_epoch_ms", std::max<int64_t>(0, envelope.value("updated_epoch_ms", 0LL))},
                {"payload", envelope["payload"]}
        };
    }

#ifdef NMC_HAS_LIBPQXX
    void ensureServerStateSchema(pqxx::connection& connection) {
        pqxx::work txn(connection);
        txn.exec(R"sql(
            CREATE TABLE IF NOT EXISTS nmc_server_state_snapshots (
                snapshot_key TEXT PRIMARY KEY,
                updated_epoch_ms BIGINT NOT NULL,
                payload_json JSONB NOT NULL
            )
        )sql");
        txn.exec(R"sql(
            CREATE INDEX IF NOT EXISTS idx_nmc_server_state_snapshots_updated_epoch_ms
            ON nmc_server_state_snapshots (updated_epoch_ms DESC)
        )sql");
        txn.commit();
    }

    json readPostgresSnapshotEnvelope(const std::string& dsn, const std::string& snapshotKey) {
        if (trimCopy(dsn).empty()) {
            return json::object();
        }
        try {
            pqxx::connection connection(dsn);
            ensureServerStateSchema(connection);
            pqxx::nontransaction txn(connection);
            const pqxx::result result = txn.exec_params(
                    "SELECT updated_epoch_ms, payload_json::text "
                    "FROM nmc_server_state_snapshots WHERE snapshot_key = $1",
                    trimCopy(snapshotKey).empty() ? std::string("primary") : trimCopy(snapshotKey)
            );
            if (result.empty()) {
                return json::object();
            }
            json payload = json::parse(result[0][1].c_str(), nullptr, false);
            if (payload.is_discarded()) {
                return json::object();
            }
            return {
                    {"updated_epoch_ms", result[0][0].as<int64_t>(0)},
                    {"payload", std::move(payload)}
            };
        } catch (const std::exception& error) {
            std::cerr << "[WARN] Unable to load server-state snapshot from PostgreSQL: " << error.what() << std::endl;
            return json::object();
        }
    }
#endif

} // namespace

namespace NMC::Server {

    ServerStateStore::ServerStateStore(Config config)
            : config_(std::move(config)) {
        if (config_.snapshotPath.empty()) {
            config_.snapshotPath = defaultServerSnapshotPath();
        }
        if (trimCopy(config_.snapshotKey).empty()) {
            config_.snapshotKey = "primary";
        }
        config_.flushIntervalMs = std::max<int64_t>(250, std::min<int64_t>(config_.flushIntervalMs, 60000));
    }

    ServerStateStore::~ServerStateStore() {
        stop();
    }

    bool ServerStateStore::enabled() const {
        return !config_.snapshotPath.empty() || postgresConfigured();
    }

    bool ServerStateStore::postgresConfigured() const {
        return !trimCopy(config_.postgresDsn).empty();
    }

    json ServerStateStore::readFileSnapshotEnvelope() const {
        if (config_.snapshotPath.empty()) {
            return json::object();
        }
        std::ifstream input(config_.snapshotPath);
        if (!input.is_open()) {
            return json::object();
        }
        json parsed = json::parse(input, nullptr, false);
        if (parsed.is_discarded()) {
            std::cerr << "[WARN] Ignoring invalid server-state snapshot file: " << config_.snapshotPath << std::endl;
            return json::object();
        }
        return normalizeSnapshotEnvelope(std::move(parsed));
    }

    bool ServerStateStore::writeFileSnapshotEnvelope(const json& envelope) const {
        if (config_.snapshotPath.empty() || !envelope.is_object()) {
            return false;
        }
        try {
            if (!config_.snapshotPath.parent_path().empty()) {
                std::filesystem::create_directories(config_.snapshotPath.parent_path());
            }
            const std::filesystem::path tempPath = config_.snapshotPath.string() + ".tmp";
            {
                std::ofstream output(tempPath, std::ios::out | std::ios::trunc);
                output << envelope.dump(2);
                output.flush();
                if (!output.good()) {
                    return false;
                }
            }
            std::error_code ec;
            std::filesystem::rename(tempPath, config_.snapshotPath, ec);
            if (ec) {
                std::filesystem::remove(config_.snapshotPath, ec);
                ec.clear();
                std::filesystem::rename(tempPath, config_.snapshotPath, ec);
                if (ec) {
                    std::cerr << "[WARN] Failed to persist server-state snapshot file: " << ec.message() << std::endl;
                    std::filesystem::remove(tempPath, ec);
                    return false;
                }
            }
            return true;
        } catch (const std::exception& error) {
            std::cerr << "[WARN] Failed to persist server-state snapshot file: " << error.what() << std::endl;
            return false;
        }
    }

    json ServerStateStore::loadSnapshot() {
        json bestEnvelope = readFileSnapshotEnvelope();
#ifdef NMC_HAS_LIBPQXX
        if (postgresConfigured()) {
            json postgresEnvelope = readPostgresSnapshotEnvelope(config_.postgresDsn, config_.snapshotKey);
            if (postgresEnvelope.is_object() &&
                postgresEnvelope.value("updated_epoch_ms", 0LL) >= bestEnvelope.value("updated_epoch_ms", 0LL)) {
                bestEnvelope = std::move(postgresEnvelope);
            }
        }
#else
        if (postgresConfigured()) {
            std::cerr << "[WARN] Server-state PostgreSQL persistence requested but libpqxx support is not available in this build." << std::endl;
        }
#endif
        if (!bestEnvelope.is_object()) {
            return json::object();
        }
        return bestEnvelope.value("payload", json::object());
    }

    void ServerStateStore::start() {
        if (!enabled()) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            return;
        }
        stopRequested_ = false;
        running_ = true;
        worker_ = std::thread(&ServerStateStore::runWorker, this);
    }

    void ServerStateStore::stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) {
                return;
            }
            stopRequested_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }

    void ServerStateStore::scheduleSnapshot(int64_t updatedEpochMs) {
        if (!enabled()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pendingSnapshotEpochMs_ = updatedEpochMs > 0 ? updatedEpochMs : nowEpochMs();
            if (!snapshotDirty_) {
                snapshotDirtySinceMs_ = nowEpochMs();
            }
            snapshotDirty_ = true;
        }
        cv_.notify_one();
    }

    void ServerStateStore::runWorker() {
#ifdef NMC_HAS_LIBPQXX
        std::unique_ptr<pqxx::connection> postgresConnection;
        bool postgresSchemaReady = false;
        int64_t lastPostgresAttemptMs = 0;
        int64_t lastPostgresErrorMs = 0;
        auto ensurePostgresReady = [&]() -> bool {
            if (!postgresConfigured()) {
                return false;
            }
            if (postgresConnection && postgresConnection->is_open() && postgresSchemaReady) {
                return true;
            }
            const int64_t currentMs = nowEpochMs();
            if (currentMs - lastPostgresAttemptMs < 30000) {
                return false;
            }
            lastPostgresAttemptMs = currentMs;
            try {
                postgresConnection = std::make_unique<pqxx::connection>(config_.postgresDsn);
                ensureServerStateSchema(*postgresConnection);
                postgresSchemaReady = true;
                return true;
            } catch (const std::exception& error) {
                postgresConnection.reset();
                postgresSchemaReady = false;
                if ((currentMs - lastPostgresErrorMs) >= 30000) {
                    std::cerr << "[WARN] Server-state PostgreSQL persistence is unavailable: " << error.what() << std::endl;
                    lastPostgresErrorMs = currentMs;
                }
                return false;
            }
        };
#else
        bool warnedUnsupportedPostgres = false;
#endif

        for (;;) {
            json snapshotEnvelope;
            int64_t snapshotEpochMs = 0;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                auto readyToFlush = [&]() -> bool {
                    if (stopRequested_) {
                        return true;
                    }
                    if (!snapshotDirty_) {
                        return false;
                    }
                    const int64_t currentMs = nowEpochMs();
                    return snapshotDirtySinceMs_ <= 0 || (currentMs - snapshotDirtySinceMs_) >= config_.flushIntervalMs;
                };
                while (!readyToFlush()) {
                    int64_t waitMs = config_.flushIntervalMs;
                    if (snapshotDirty_ && snapshotDirtySinceMs_ > 0) {
                        const int64_t dueMs = snapshotDirtySinceMs_ + config_.flushIntervalMs;
                        waitMs = std::max<int64_t>(1, dueMs - nowEpochMs());
                    }
                    cv_.wait_for(lock, std::chrono::milliseconds(waitMs));
                }
                if (stopRequested_ && !snapshotDirty_) {
                    break;
                }
                if (snapshotDirty_ &&
                    (stopRequested_ || snapshotDirtySinceMs_ <= 0 || (nowEpochMs() - snapshotDirtySinceMs_) >= config_.flushIntervalMs)) {
                    snapshotEpochMs = pendingSnapshotEpochMs_ > 0 ? pendingSnapshotEpochMs_ : nowEpochMs();
                    snapshotDirty_ = false;
                    snapshotDirtySinceMs_ = 0;
                }
            }

            if (snapshotEpochMs > 0 && config_.snapshotBuilder) {
                snapshotEnvelope = {
                        {"updated_epoch_ms", snapshotEpochMs},
                        {"payload", config_.snapshotBuilder(snapshotEpochMs)}
                };
            }

            if (snapshotEnvelope.is_object()) {
                writeFileSnapshotEnvelope(snapshotEnvelope);
            }

            if (postgresConfigured() && snapshotEnvelope.is_object()) {
#ifdef NMC_HAS_LIBPQXX
                if (ensurePostgresReady()) {
                    try {
                        pqxx::work txn(*postgresConnection);
                        const json payload = snapshotEnvelope.value("payload", json::object());
                        txn.exec_params(
                                "INSERT INTO nmc_server_state_snapshots (snapshot_key, updated_epoch_ms, payload_json) "
                                "VALUES ($1, $2, $3::jsonb) "
                                "ON CONFLICT (snapshot_key) DO UPDATE "
                                "SET updated_epoch_ms = EXCLUDED.updated_epoch_ms, payload_json = EXCLUDED.payload_json",
                                trimCopy(config_.snapshotKey).empty() ? std::string("primary") : trimCopy(config_.snapshotKey),
                                snapshotEnvelope.value("updated_epoch_ms", 0LL),
                                payload.dump()
                        );
                        txn.commit();
                    } catch (const std::exception& error) {
                        postgresConnection.reset();
                        postgresSchemaReady = false;
                        const int64_t currentMs = nowEpochMs();
                        if ((currentMs - lastPostgresErrorMs) >= 30000) {
                            std::cerr << "[WARN] Server-state PostgreSQL flush failed: " << error.what() << std::endl;
                            lastPostgresErrorMs = currentMs;
                        }
                    }
                }
#else
                if (!warnedUnsupportedPostgres) {
                    std::cerr << "[WARN] Server-state PostgreSQL persistence requested but libpqxx support is not available in this build." << std::endl;
                    warnedUnsupportedPostgres = true;
                }
#endif
            }
        }
    }

} // namespace NMC::Server
