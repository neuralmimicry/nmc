// server/TraceyStateStore.h
#pragma once

#include <cstdint>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

namespace NMC::Server {

    /**
     * @brief Persists Tracey control-plane state without blocking request paths.
     *
     * The store maintains a debounced snapshot for quick restart hydration and can
     * optionally mirror samples/logs plus the latest snapshot into PostgreSQL when
     * a DSN is configured.
     */
    class TraceyStateStore {
    public:
        struct Config {
            std::filesystem::path snapshotPath;
            int64_t flushIntervalMs{5000};
            std::string postgresDsn;
            std::function<nlohmann::json(int64_t)> snapshotBuilder;
        };

        explicit TraceyStateStore(Config config);
        ~TraceyStateStore();

        nlohmann::json loadSnapshot();
        void start();
        void stop();

        void scheduleSnapshot(int64_t updatedEpochMs);
        void recordStatusSample(const std::string& agentId, nlohmann::json sample);
        void recordAgentLog(const std::string& agentId, nlohmann::json entry);

        bool enabled() const;
        bool postgresConfigured() const;

    private:
        struct Event {
            enum class Type {
                StatusSample,
                AgentLog
            };

            Type type{Type::StatusSample};
            std::string agentId;
            nlohmann::json payload;
        };

        Config config_;
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::deque<Event> events_;
        int64_t pendingSnapshotEpochMs_{0};
        int64_t snapshotDirtySinceMs_{0};
        bool snapshotDirty_{false};
        bool running_{false};
        bool stopRequested_{false};
        std::thread worker_;

        void runWorker();
        nlohmann::json readFileSnapshotEnvelope() const;
        bool writeFileSnapshotEnvelope(const nlohmann::json& envelope) const;
    };

} // namespace NMC::Server
