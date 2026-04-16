// server/TraceyStateStore.cpp
#include "TraceyStateStore.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <system_error>
#include <utility>
#include <vector>

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

    std::filesystem::path defaultTraceySnapshotPath() {
        if (const char* raw = std::getenv("NMC_TRACEY_STATE_ROOT")) {
            const std::string value = trimCopy(raw);
            if (!value.empty()) {
                return std::filesystem::path(value) / "tracey_state_snapshot.json";
            }
        }
        if (const char* stateHome = std::getenv("XDG_STATE_HOME")) {
            const std::string value = trimCopy(stateHome);
            if (!value.empty()) {
                return std::filesystem::path(value) / "nmc" / "tracey_state" / "tracey_state_snapshot.json";
            }
        }
        if (const char* home = std::getenv("HOME")) {
            const std::string value = trimCopy(home);
            if (!value.empty()) {
                return std::filesystem::path(value) / ".local" / "state" / "nmc" / "tracey_state" / "tracey_state_snapshot.json";
            }
        }
        return std::filesystem::temp_directory_path() / "nmc-tracey-state" / "tracey_state_snapshot.json";
    }

    json normalizeSnapshotEnvelope(json envelope) {
        if (!envelope.is_object()) {
            return json::object();
        }
        if (!envelope.contains("payload")) {
            return json::object();
        }
        return {
                {"updated_epoch_ms", std::max<int64_t>(0, envelope.value("updated_epoch_ms", 0LL))},
                {"payload", envelope["payload"]}
        };
    }

#ifdef NMC_HAS_LIBPQXX
    void ensureTraceySchema(pqxx::connection& connection) {
        pqxx::work txn(connection);
        txn.exec(R"sql(
            CREATE TABLE IF NOT EXISTS nmc_tracey_snapshots (
                snapshot_key TEXT PRIMARY KEY,
                updated_epoch_ms BIGINT NOT NULL,
                payload_json JSONB NOT NULL
            )
        )sql");
        txn.exec(R"sql(
            CREATE TABLE IF NOT EXISTS nmc_tracey_status_samples (
                agent_id TEXT NOT NULL,
                ts_ms BIGINT NOT NULL,
                source TEXT NOT NULL,
                payload_json JSONB NOT NULL,
                PRIMARY KEY (agent_id, ts_ms, source)
            )
        )sql");
        txn.exec(R"sql(
            CREATE INDEX IF NOT EXISTS idx_nmc_tracey_status_samples_ts
            ON nmc_tracey_status_samples (ts_ms DESC)
        )sql");
        txn.exec(R"sql(
            CREATE TABLE IF NOT EXISTS nmc_tracey_agent_logs (
                agent_id TEXT NOT NULL,
                ts_ms BIGINT NOT NULL,
                category TEXT NOT NULL,
                level TEXT NOT NULL,
                message TEXT NOT NULL,
                payload_json JSONB NOT NULL,
                PRIMARY KEY (agent_id, ts_ms, category, level, message)
            )
        )sql");
        txn.exec(R"sql(
            CREATE INDEX IF NOT EXISTS idx_nmc_tracey_agent_logs_ts
            ON nmc_tracey_agent_logs (ts_ms DESC)
        )sql");
        txn.commit();
    }

    json readPostgresSnapshotEnvelope(const std::string& dsn) {
        if (trimCopy(dsn).empty()) {
            return json::object();
        }
        try {
            pqxx::connection connection(dsn);
            ensureTraceySchema(connection);
            pqxx::nontransaction txn(connection);
            const pqxx::result result = txn.exec_params(
                    "SELECT updated_epoch_ms, payload_json::text "
                    "FROM nmc_tracey_snapshots WHERE snapshot_key = $1",
                    "primary"
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
            std::cerr << "[WARN] Unable to load Tracey snapshot from PostgreSQL: " << error.what() << std::endl;
            return json::object();
        }
    }
#endif

} // namespace

namespace NMC::Server {

    TraceyStateStore::TraceyStateStore(Config config)
            : config_(std::move(config)) {
        if (config_.snapshotPath.empty()) {
            config_.snapshotPath = defaultTraceySnapshotPath();
        }
        config_.flushIntervalMs = std::max<int64_t>(250, std::min<int64_t>(config_.flushIntervalMs, 60000));
    }

    TraceyStateStore::~TraceyStateStore() {
        stop();
    }

    bool TraceyStateStore::enabled() const {
        return !config_.snapshotPath.empty() || postgresConfigured();
    }

    bool TraceyStateStore::postgresConfigured() const {
        return !trimCopy(config_.postgresDsn).empty();
    }

    json TraceyStateStore::readFileSnapshotEnvelope() const {
        if (config_.snapshotPath.empty()) {
            return json::object();
        }
        std::ifstream input(config_.snapshotPath);
        if (!input.is_open()) {
            return json::object();
        }
        json parsed = json::parse(input, nullptr, false);
        if (parsed.is_discarded()) {
            std::cerr << "[WARN] Ignoring invalid Tracey snapshot file: " << config_.snapshotPath << std::endl;
            return json::object();
        }
        return normalizeSnapshotEnvelope(std::move(parsed));
    }

    bool TraceyStateStore::writeFileSnapshotEnvelope(const json& envelope) const {
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
                    std::cerr << "[WARN] Failed to persist Tracey snapshot file: " << ec.message() << std::endl;
                    std::filesystem::remove(tempPath, ec);
                    return false;
                }
            }
            return true;
        } catch (const std::exception& error) {
            std::cerr << "[WARN] Failed to persist Tracey snapshot file: " << error.what() << std::endl;
            return false;
        }
    }

    json TraceyStateStore::loadSnapshot() {
        json bestEnvelope = readFileSnapshotEnvelope();
#ifdef NMC_HAS_LIBPQXX
        if (postgresConfigured()) {
            json postgresEnvelope = readPostgresSnapshotEnvelope(config_.postgresDsn);
            if (postgresEnvelope.is_object() &&
                postgresEnvelope.value("updated_epoch_ms", 0LL) >= bestEnvelope.value("updated_epoch_ms", 0LL)) {
                bestEnvelope = std::move(postgresEnvelope);
            }
        }
#else
        if (postgresConfigured()) {
            std::cerr << "[WARN] Tracey PostgreSQL persistence requested but libpqxx support is not available in this build." << std::endl;
        }
#endif
        if (!bestEnvelope.is_object()) {
            return json::object();
        }
        return bestEnvelope.value("payload", json::object());
    }

    void TraceyStateStore::start() {
        if (!enabled()) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            return;
        }
        stopRequested_ = false;
        running_ = true;
        worker_ = std::thread(&TraceyStateStore::runWorker, this);
    }

    void TraceyStateStore::stop() {
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

    void TraceyStateStore::scheduleSnapshot(int64_t updatedEpochMs) {
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

    void TraceyStateStore::recordStatusSample(const std::string& agentId, json sample) {
        if (!postgresConfigured() || trimCopy(agentId).empty()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (events_.size() >= 50000) {
                events_.pop_front();
            }
            events_.push_back({Event::Type::StatusSample, agentId, std::move(sample)});
        }
        cv_.notify_one();
    }

    void TraceyStateStore::recordAgentLog(const std::string& agentId, json entry) {
        if (!postgresConfigured() || trimCopy(agentId).empty()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (events_.size() >= 50000) {
                events_.pop_front();
            }
            events_.push_back({Event::Type::AgentLog, agentId, std::move(entry)});
        }
        cv_.notify_one();
    }

    void TraceyStateStore::runWorker() {
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
                ensureTraceySchema(*postgresConnection);
                postgresSchemaReady = true;
                return true;
            } catch (const std::exception& error) {
                postgresConnection.reset();
                postgresSchemaReady = false;
                if ((currentMs - lastPostgresErrorMs) >= 30000) {
                    std::cerr << "[WARN] Tracey PostgreSQL persistence is unavailable: " << error.what() << std::endl;
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
            std::vector<Event> batch;
            int64_t snapshotEpochMs = 0;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                auto readyToFlush = [&]() -> bool {
                    if (stopRequested_ || !events_.empty()) {
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
                if (stopRequested_ && !snapshotDirty_ && events_.empty()) {
                    break;
                }
                if (snapshotDirty_ &&
                    (stopRequested_ || snapshotDirtySinceMs_ <= 0 || (nowEpochMs() - snapshotDirtySinceMs_) >= config_.flushIntervalMs)) {
                    snapshotEpochMs = pendingSnapshotEpochMs_ > 0 ? pendingSnapshotEpochMs_ : nowEpochMs();
                    snapshotDirty_ = false;
                    snapshotDirtySinceMs_ = 0;
                }
                const size_t batchSize = std::min<size_t>(events_.size(), 512);
                batch.reserve(batchSize);
                for (size_t index = 0; index < batchSize; ++index) {
                    batch.push_back(std::move(events_.front()));
                    events_.pop_front();
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

            if (postgresConfigured() && (snapshotEnvelope.is_object() || !batch.empty())) {
#ifdef NMC_HAS_LIBPQXX
                if (ensurePostgresReady()) {
                    try {
                        pqxx::work txn(*postgresConnection);
                        if (snapshotEnvelope.is_object()) {
                            const json payload = snapshotEnvelope.value("payload", json::object());
                            txn.exec_params(
                                    "INSERT INTO nmc_tracey_snapshots (snapshot_key, updated_epoch_ms, payload_json) "
                                    "VALUES ($1, $2, $3::jsonb) "
                                    "ON CONFLICT (snapshot_key) DO UPDATE "
                                    "SET updated_epoch_ms = EXCLUDED.updated_epoch_ms, payload_json = EXCLUDED.payload_json",
                                    "primary",
                                    snapshotEnvelope.value("updated_epoch_ms", 0LL),
                                    payload.dump()
                            );
                        }
                        for (const auto& event : batch) {
                            if (!event.payload.is_object()) {
                                continue;
                            }
                            if (event.type == Event::Type::StatusSample) {
                                txn.exec_params(
                                        "INSERT INTO nmc_tracey_status_samples (agent_id, ts_ms, source, payload_json) "
                                        "VALUES ($1, $2, $3, $4::jsonb) "
                                        "ON CONFLICT (agent_id, ts_ms, source) DO UPDATE "
                                        "SET payload_json = EXCLUDED.payload_json",
                                        event.agentId,
                                        std::max<int64_t>(0, event.payload.value("ts_ms", 0LL)),
                                        trimCopy(event.payload.value("source", std::string("unknown"))),
                                        event.payload.dump()
                                );
                                continue;
                            }
                            txn.exec_params(
                                    "INSERT INTO nmc_tracey_agent_logs (agent_id, ts_ms, category, level, message, payload_json) "
                                    "VALUES ($1, $2, $3, $4, $5, $6::jsonb) "
                                    "ON CONFLICT (agent_id, ts_ms, category, level, message) DO UPDATE "
                                    "SET payload_json = EXCLUDED.payload_json",
                                    event.agentId,
                                    std::max<int64_t>(0, event.payload.value("ts_ms", 0LL)),
                                    trimCopy(event.payload.value("category", std::string("general"))),
                                    trimCopy(event.payload.value("level", std::string("info"))),
                                    trimCopy(event.payload.value("message", std::string("Tracey event"))),
                                    event.payload.dump()
                            );
                        }
                        txn.commit();
                    } catch (const std::exception& error) {
                        postgresConnection.reset();
                        postgresSchemaReady = false;
                        const int64_t currentMs = nowEpochMs();
                        if ((currentMs - lastPostgresErrorMs) >= 30000) {
                            std::cerr << "[WARN] Tracey PostgreSQL flush failed: " << error.what() << std::endl;
                            lastPostgresErrorMs = currentMs;
                        }
                    }
                }
#else
                if (!warnedUnsupportedPostgres) {
                    std::cerr << "[WARN] Tracey PostgreSQL persistence requested but libpqxx support is not available in this build." << std::endl;
                    warnedUnsupportedPostgres = true;
                }
#endif
            }
        }
    }

} // namespace NMC::Server
