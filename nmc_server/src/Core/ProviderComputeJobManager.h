// Bounded asynchronous execution and durable status for provider operations.
#pragma once

#include "ProviderCompute.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace NMC::Server {

    /**
     * @brief Runs provider CLI operations outside HTTP request workers.
     *
     * Accepted jobs and mutation idempotency records are committed to a
     * private, atomic state file before a worker can launch a provider command.
     * An operation interrupted while running is never replayed automatically.
     */
    class ProviderComputeJobManager final {
    public:
        /** A durable submission result and its HTTP response details. */
        struct Submission {
            bool accepted{false};
            int httpStatus{503};
            std::string message;
            nlohmann::json job{nlohmann::json::object()};
        };

        using AuditSink = std::function<void(const std::string&, const std::string&, int64_t, nlohmann::json)>;

        /** Load retained state from the configured persistent path. */
        explicit ProviderComputeJobManager(AuditSink auditSink = {});
        ~ProviderComputeJobManager();

        ProviderComputeJobManager(const ProviderComputeJobManager&) = delete;
        ProviderComputeJobManager& operator=(const ProviderComputeJobManager&) = delete;

        /** Start the bounded worker pool after state storage has been checked. */
        bool start();
        /** Stop accepting work, cancel work not yet launched and join workers. */
        void stop();
        /** Report whether durable storage and all workers are available. */
        bool available() const;
        /**
         * @brief Validate and durably enqueue one provider operation.
         *
         * The request is checked locally before it enters the queue. Mutations
         * require a stable key so a client can safely retry after a lost reply.
         */
        Submission submit(const std::string& operation,
                          const nlohmann::json& request,
                          const std::string& idempotencyKey = {});
        /** Return a redacted public view of a retained job, if it exists. */
        std::optional<nlohmann::json> get(const std::string& jobId) const;

    private:
        struct Job {
            std::string id;
            std::string operation;
            std::string provider;
            std::string scope;
            std::string idempotencyKey;
            std::string requestFingerprint;
            std::string status;
            int64_t createdAtMs{0};
            int64_t startedAtMs{0};
            int64_t completedAtMs{0};
            ProviderComputeResult result;
            nlohmann::json request{nlohmann::json::object()};
        };

        struct IdempotencyRecord {
            std::string jobId;
            std::string fingerprint;
            int64_t expiresAtMs{0};
        };

        // Keep cloud API pressure bounded independently of HTTP request volume.
        static constexpr size_t WorkerCount = 2;
        static constexpr size_t MaximumQueuedJobs = 32;
        static constexpr size_t MaximumRetainedJobs = 128;
        static constexpr size_t MaximumIdempotencyRecords = 1024;
        static constexpr size_t MaximumStoreBytes = 16 * 1024 * 1024;
        static constexpr int64_t JobRetentionMs = 24LL * 60 * 60 * 1000;
        static constexpr int64_t IdempotencyRetentionMs = 7LL * 24 * 60 * 60 * 1000;

        mutable std::mutex mutex_;
        std::condition_variable condition_;
        std::deque<std::string> queue_;
        std::unordered_map<std::string, Job> jobs_;
        std::unordered_map<std::string, IdempotencyRecord> idempotency_;
        std::vector<std::thread> workers_;
        std::filesystem::path statePath_;
        AuditSink auditSink_;
        bool available_{false};
        bool running_{false};
        bool stopping_{false};

        /** Consume queued jobs until shutdown is requested. */
        void workerLoop();
        /** Load state and convert interrupted work to non-replay terminal states. */
        void loadState();
        /** Atomically replace the owner-only state snapshot. Caller holds mutex_. */
        bool persistLocked() const;
        /** Expire retained jobs and idempotency records according to their limits. */
        void pruneLocked(int64_t nowMs);
        nlohmann::json jobToJson(const Job& job, bool persistent) const;
        nlohmann::json publicJob(const Job& job) const;
        void audit(const Job& job, const std::string& action, const nlohmann::json& result = nlohmann::json::object()) const;
    };

} // namespace NMC::Server
