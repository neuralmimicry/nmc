// server/ServerStateStore.h
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
     * @brief Persists non-Tracey in-memory registries off the request path.
     *
     * The store keeps the latest registry snapshot on disk for restart hydration and
     * can optionally mirror the same snapshot into PostgreSQL. Snapshot generation
     * is debounced and executed on a background worker to avoid blocking handlers.
     */
    class ServerStateStore {
    public:
        struct Config {
            std::filesystem::path snapshotPath;
            int64_t flushIntervalMs{5000};
            std::string postgresDsn;
            std::string snapshotKey{"primary"};
            std::function<nlohmann::json(int64_t)> snapshotBuilder;
        };

        explicit ServerStateStore(Config config);
        ~ServerStateStore();

        nlohmann::json loadSnapshot();
        void start();
        void stop();

        void scheduleSnapshot(int64_t updatedEpochMs);
        void recordEvent(const std::string& entityType,
                         const std::string& entityKey,
                         const std::string& action,
                         int64_t tsMs,
                         nlohmann::json payload);

        bool enabled() const;
        bool postgresConfigured() const;

    private:
        struct Event {
            std::string entityType;
            std::string entityKey;
            std::string action;
            int64_t tsMs{0};
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
