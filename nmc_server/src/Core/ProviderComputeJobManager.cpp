// Bounded worker queue for provider CLIs. A restart never replays a mutation.
#include "ProviderComputeJobManager.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <openssl/sha.h>
#include <cstdio>
#include <sstream>
#include <utility>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace NMC::Server {
namespace {

using Json = nlohmann::json;

int64_t nowEpochMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string trimmedEnv(const char* name) {
    const char* raw = std::getenv(name);
    if (raw == nullptr) return {};
    std::string value(raw);
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::filesystem::path defaultStatePath() {
    const std::string configured = trimmedEnv("NMC_PROVIDER_JOB_STATE_PATH");
    if (!configured.empty()) return configured;

    const std::string inventory = trimmedEnv("NMC_DEVICE_INVENTORY_PATH");
    if (!inventory.empty()) {
        return std::filesystem::path(inventory).parent_path() / "provider-compute-jobs.json";
    }

    return "/var/lib/nmc/provider-compute-jobs.json";
}

std::string hashRequest(const std::string& operation, const Json& request) {
    const std::string input = operation + "\n" + request.dump();
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest.data());
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const unsigned char byte : digest) output << std::setw(2) << static_cast<unsigned int>(byte);
    return output.str();
}

std::string randomJobId() {
    std::array<unsigned char, 16> bytes{};
    const int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return {};
    size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = ::read(fd, bytes.data() + offset, bytes.size() - offset);
        if (count > 0) {
            offset += static_cast<size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            ::close(fd);
            return {};
        }
    }
    ::close(fd);

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const unsigned char byte : bytes) output << std::setw(2) << static_cast<unsigned int>(byte);
    return output.str();
}

bool validIdempotencyKey(const std::string& key) {
    if (key.size() < 16 || key.size() > 64) return false;
    const auto alphaNumeric = [](unsigned char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
    };
    if (!alphaNumeric(static_cast<unsigned char>(key.front())) ||
        !alphaNumeric(static_cast<unsigned char>(key.back()))) return false;
    return std::all_of(key.begin(), key.end(), [&](unsigned char ch) {
        return alphaNumeric(ch) || ch == '-';
    });
}

bool writeAll(int fd, const char* data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        const ssize_t count = ::write(fd, data + offset, length - offset);
        if (count > 0) {
            offset += static_cast<size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

} // namespace

ProviderComputeJobManager::ProviderComputeJobManager(AuditSink auditSink)
        : statePath_(defaultStatePath()), auditSink_(std::move(auditSink)) {
    loadState();
}

ProviderComputeJobManager::~ProviderComputeJobManager() {
    stop();
}

bool ProviderComputeJobManager::start() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!available_) return false;
        if (running_) return true;
        stopping_ = false;
        running_ = true;
    }
    try {
        workers_.reserve(WorkerCount);
        for (size_t index = 0; index < WorkerCount; ++index) {
            workers_.emplace_back(&ProviderComputeJobManager::workerLoop, this);
        }
    } catch (const std::exception& error) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        condition_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
        workers_.clear();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
            available_ = false;
        }
        std::cerr << "[WARN] Provider job workers could not start: " << error.what() << std::endl;
        return false;
    }
    return true;
}

void ProviderComputeJobManager::stop() {
    std::vector<Json> cancelled;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ && workers_.empty()) return;
        stopping_ = true;
        const int64_t nowMs = nowEpochMs();
        for (const std::string& jobId : queue_) {
            auto found = jobs_.find(jobId);
            if (found == jobs_.end()) continue;
            found->second.status = "cancelled";
            found->second.completedAtMs = nowMs;
            found->second.result = {false, 503, "NMC stopped before the provider command began; no provider command was launched.", Json::object()};
            cancelled.push_back(publicJob(found->second));
        }
        queue_.clear();
        pruneLocked(nowMs);
        if (!persistLocked()) available_ = false;
    }
    condition_.notify_all();
    for (const auto& item : cancelled) {
        try {
            if (auditSink_) auditSink_(item.value("job_id", std::string{}), "cancelled", nowEpochMs(), item);
        } catch (const std::exception& error) {
            std::cerr << "[WARN] Provider job audit callback failed: " << error.what() << std::endl;
        }
    }
    for (std::thread& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
}

bool ProviderComputeJobManager::available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return available_ && running_ && !stopping_;
}

ProviderComputeJobManager::Submission ProviderComputeJobManager::submit(
        const std::string& operationInput,
        const Json& request,
        const std::string& idempotencyKeyInput) {
    const std::string operation = operationInput;
    if (operation != "list" && operation != "create" && operation != "action") {
        return {false, 400, "operation must be one of: list, create, action.", Json::object()};
    }
    if (!request.is_object()) {
        return {false, 400, "Provider job request must be a JSON object.", Json::object()};
    }

    Json canonicalRequest = request;
    for (const char* fieldName : {"provider", "action"}) {
        const auto field = canonicalRequest.find(fieldName);
        if (field == canonicalRequest.end() || !field->is_string()) continue;
        std::string value = field->get<std::string>();
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        *field = std::move(value);
    }

    std::string idempotencyKey = idempotencyKeyInput;
    if (operation != "list" && idempotencyKey.empty()) {
        const char* fieldName = operation == "create" ? "idempotency_key" : "request_id";
        const auto found = canonicalRequest.find(fieldName);
        if (found == canonicalRequest.end() || !found->is_string()) {
            return {false, 400, std::string(fieldName) + " must be a string.", Json::object()};
        }
        idempotencyKey = found->get<std::string>();
    }
    if (operation != "list" && !validIdempotencyKey(idempotencyKey)) {
        return {false, 400, "Provider mutations require a 16-to-64 character idempotency key containing letters, digits or hyphens.", Json::object()};
    }

    const ProviderComputeResult preflight = ProviderCompute::preflight(operation, canonicalRequest);
    if (!preflight.success) {
        return {false, preflight.httpStatus, preflight.message, preflight.data};
    }

    const std::string provider = canonicalRequest.value("provider", std::string{});
    const std::string scope = canonicalRequest.value("scope", std::string{});
    const std::string fingerprint = hashRequest(operation, canonicalRequest);
    const std::string jobId = randomJobId();
    if (jobId.empty()) {
        return {false, 503, "A secure provider job identifier could not be generated.", Json::object()};
    }

    Job job;
    job.id = jobId;
    job.operation = operation;
    job.provider = provider;
    job.scope = scope;
    job.idempotencyKey = idempotencyKey;
    job.requestFingerprint = fingerprint;
    job.status = "queued";
    job.createdAtMs = nowEpochMs();
    job.request = std::move(canonicalRequest);

    std::string dedupe;
    if (operation != "list") dedupe = operation + "\n" + provider + "\n" + scope + "\n" + idempotencyKey;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!available_ || !running_ || stopping_) {
            return {false, 503, "Provider job state storage or worker capacity is unavailable; no provider operation was launched.", Json::object()};
        }
        pruneLocked(job.createdAtMs);
        if (!dedupe.empty()) {
            const auto previous = idempotency_.find(dedupe);
            if (previous != idempotency_.end()) {
                if (previous->second.fingerprint != fingerprint) {
                    return {false, 409, "This idempotency key was already used for a different provider request.", Json::object()};
                }
                const auto priorJob = jobs_.find(previous->second.jobId);
                if (priorJob == jobs_.end()) {
                    return {false, 409, "The earlier idempotent operation is outside retained job history; reconcile provider state before submitting a new key.",
                            {{"job_id", previous->second.jobId}, {"status", "history_expired"}}};
                }
                return {true, 202, "The existing provider job matches this idempotent request.", publicJob(priorJob->second)};
            }
            if (idempotency_.size() >= MaximumIdempotencyRecords) {
                return {false, 503, "The provider idempotency ledger is full; reconcile and expire old job records before submitting mutations.", Json::object()};
            }
        }
        if (queue_.size() >= MaximumQueuedJobs) {
            return {false, 429, "The bounded provider job queue is full; retry after an active job completes.", Json::object()};
        }

        jobs_.emplace(job.id, job);
        queue_.push_back(job.id);
        if (!dedupe.empty()) {
            idempotency_[dedupe] = {job.id, fingerprint, job.createdAtMs + IdempotencyRetentionMs};
        }
        if (!persistLocked()) {
            queue_.pop_back();
            jobs_.erase(job.id);
            if (!dedupe.empty()) idempotency_.erase(dedupe);
            available_ = false;
            return {false, 503, "Provider job state could not be durably recorded; no provider operation was launched.", Json::object()};
        }
        job = jobs_.at(job.id);
    }
    // Wake every idle worker: two closely spaced submissions must not both
    // signal the same thread while another worker remains asleep.
    condition_.notify_all();
    audit(job, "queued");
    return {true, 202, "Provider operation accepted as an asynchronous job.", publicJob(job)};
}

std::optional<Json> ProviderComputeJobManager::get(const std::string& jobId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = jobs_.find(jobId);
    if (found == jobs_.end()) return std::nullopt;
    return publicJob(found->second);
}

void ProviderComputeJobManager::workerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) return;
            const std::string jobId = queue_.front();
            queue_.pop_front();
            auto found = jobs_.find(jobId);
            if (found == jobs_.end()) continue;
            found->second.status = "running";
            found->second.startedAtMs = nowEpochMs();
            if (!persistLocked()) {
                found->second.status = "outcome_unknown";
                found->second.completedAtMs = nowEpochMs();
                found->second.result = {false, 503, "Provider job state could not be durably updated; reconcile provider state before retrying.", Json::object()};
                available_ = false;
                persistLocked();
                job = found->second;
                lock.unlock();
                audit(job, "outcome_unknown");
                continue;
            }
            job = found->second;
        }

        audit(job, "started");
        ProviderComputeResult result;
        try {
            if (job.operation == "list") {
                result = ProviderCompute::list(job.provider,
                                               job.scope,
                                               job.request.value("region", std::string{}));
            } else if (job.operation == "create") {
                result = ProviderCompute::create(job.request);
            } else {
                result = ProviderCompute::action(job.request);
            }
        } catch (const std::exception& error) {
            std::cerr << "[WARN] Provider job " << job.id << " failed with an internal exception: " << error.what() << std::endl;
            result = {false, 500, "The provider operation failed internally; inspect the job record and provider state.", Json::object()};
        } catch (...) {
            result = {false, 500, "The provider operation failed internally; inspect the job record and provider state.", Json::object()};
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto found = jobs_.find(job.id);
            if (found == jobs_.end()) continue;
            found->second.result = std::move(result);
            found->second.status = found->second.result.success ? "succeeded" : "failed";
            found->second.completedAtMs = nowEpochMs();
            found->second.request = Json::object();
            pruneLocked(found->second.completedAtMs);
            if (!persistLocked()) {
                // The last durable state remains "running"; restart recovery will
                // expose it as outcome-unknown instead of replaying the command.
                available_ = false;
            }
            job = found->second;
        }
        condition_.notify_all();
        audit(job, job.status);
    }
}

void ProviderComputeJobManager::loadState() {
    std::error_code ec;
    const std::filesystem::path parent = statePath_.parent_path();
    if (statePath_.empty() || !statePath_.is_absolute() || parent.empty() ||
        !std::filesystem::is_directory(parent, ec) || ec || std::filesystem::is_symlink(parent, ec)) {
        std::cerr << "[WARN] Provider job state path is not an absolute path in a persistent directory." << std::endl;
        return;
    }

    const int fd = ::open(statePath_.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 && errno != ENOENT) {
        std::cerr << "[WARN] Provider job state file could not be opened safely: " << std::strerror(errno) << std::endl;
        return;
    }
    if (fd >= 0) {
        struct stat details{};
        if (::fstat(fd, &details) != 0 || !S_ISREG(details.st_mode) || details.st_uid != ::geteuid() ||
            (details.st_mode & 0077) != 0 || details.st_size < 0 ||
            static_cast<uint64_t>(details.st_size) > MaximumStoreBytes) {
            ::close(fd);
            std::cerr << "[WARN] Provider job state file has unsafe ownership, permissions, type or size." << std::endl;
            return;
        }
        std::string contents(static_cast<size_t>(details.st_size), '\0');
        size_t offset = 0;
        while (offset < contents.size()) {
            const ssize_t count = ::read(fd, contents.data() + offset, contents.size() - offset);
            if (count > 0) {
                offset += static_cast<size_t>(count);
            } else if (count < 0 && errno == EINTR) {
                continue;
            } else {
                ::close(fd);
                std::cerr << "[WARN] Provider job state file could not be read completely." << std::endl;
                return;
            }
        }
        ::close(fd);

        try {
            const Json snapshot = Json::parse(contents, nullptr, false);
            if (snapshot.is_discarded() || !snapshot.is_object() || snapshot.value("schema_version", 0) != 1 ||
                !snapshot.value("jobs", Json::array()).is_array() ||
                !snapshot.value("idempotency", Json::array()).is_array()) {
                std::cerr << "[WARN] Provider job state file is malformed or has an unsupported schema." << std::endl;
                return;
            }
            for (const Json& raw : snapshot["jobs"]) {
            if (!raw.is_object()) continue;
            Job job;
            job.id = raw.value("job_id", std::string{});
            job.operation = raw.value("operation", std::string{});
            job.provider = raw.value("provider", std::string{});
            job.scope = raw.value("scope", std::string{});
            job.idempotencyKey = raw.value("idempotency_key", std::string{});
            job.requestFingerprint = raw.value("request_fingerprint", std::string{});
            job.status = raw.value("status", std::string{});
            job.createdAtMs = raw.value("created_at_ms", 0LL);
            job.startedAtMs = raw.value("started_at_ms", 0LL);
            job.completedAtMs = raw.value("completed_at_ms", 0LL);
            const Json rawResult = raw.value("result", Json::object());
            if (rawResult.is_object()) {
                job.result.success = rawResult.value("success", false);
                job.result.httpStatus = rawResult.value("http_status", 500);
                job.result.message = rawResult.value("message", std::string{});
                job.result.data = rawResult.value("data", Json::object());
            }
            if (job.id.size() != 32 || (job.operation != "list" && job.operation != "create" && job.operation != "action")) continue;
            if (job.status == "queued") {
                job.status = "cancelled";
                job.completedAtMs = nowEpochMs();
                job.result = {false, 503,
                              "NMC restarted before this provider operation began; no provider command was launched.",
                              Json::object()};
            } else if (job.status == "running") {
                job.status = "outcome_unknown";
                job.completedAtMs = nowEpochMs();
                job.result = {false, 409,
                              "NMC restarted while this provider operation was active; its outcome is unknown. Reconcile provider state before retrying.",
                              Json::object()};
            }
            job.request = Json::object();
            jobs_[job.id] = std::move(job);
            }
            for (const Json& raw : snapshot["idempotency"]) {
            if (!raw.is_object()) continue;
            const std::string key = raw.value("key", std::string{});
            const std::string jobId = raw.value("job_id", std::string{});
            const std::string fingerprint = raw.value("request_fingerprint", std::string{});
            const int64_t expiresAtMs = raw.value("expires_at_ms", 0LL);
            if (!key.empty() && jobId.size() == 32 && fingerprint.size() == 64 && expiresAtMs > nowEpochMs()) {
                idempotency_[key] = {jobId, fingerprint, expiresAtMs};
            }
            }
        } catch (const std::exception& error) {
            jobs_.clear();
            idempotency_.clear();
            std::cerr << "[WARN] Provider job state file contains invalid data: " << error.what() << std::endl;
            return;
        }
    }

    available_ = true;
    pruneLocked(nowEpochMs());
    if (!persistLocked()) {
        available_ = false;
        std::cerr << "[WARN] Provider job state could not be initialised durably." << std::endl;
    }
}

bool ProviderComputeJobManager::persistLocked() const {
    Json snapshot;
    snapshot["schema_version"] = 1;
    snapshot["updated_at_ms"] = nowEpochMs();
    snapshot["jobs"] = Json::array();
    snapshot["idempotency"] = Json::array();
    std::vector<const Job*> orderedJobs;
    orderedJobs.reserve(jobs_.size());
    for (const auto& item : jobs_) orderedJobs.push_back(&item.second);
    std::sort(orderedJobs.begin(), orderedJobs.end(), [](const Job* left, const Job* right) {
        return left->createdAtMs < right->createdAtMs;
    });
    for (const Job* job : orderedJobs) snapshot["jobs"].push_back(jobToJson(*job, true));
    std::vector<std::pair<std::string, IdempotencyRecord>> orderedKeys(idempotency_.begin(), idempotency_.end());
    std::sort(orderedKeys.begin(), orderedKeys.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    for (const auto& entry : orderedKeys) {
        snapshot["idempotency"].push_back({{"key", entry.first}, {"job_id", entry.second.jobId},
                                           {"request_fingerprint", entry.second.fingerprint},
                                           {"expires_at_ms", entry.second.expiresAtMs}});
    }
    const std::string contents = snapshot.dump();
    if (contents.size() > MaximumStoreBytes) return false;

    static std::atomic<uint64_t> temporarySequence{0};
    const std::filesystem::path temporaryPath = statePath_.string() + ".tmp." +
            std::to_string(::getpid()) + "." + std::to_string(temporarySequence.fetch_add(1));
    const int fd = ::open(temporaryPath.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return false;
    const bool written = writeAll(fd, contents.data(), contents.size()) && ::fchmod(fd, 0600) == 0 && ::fsync(fd) == 0;
    const int closeResult = ::close(fd);
    if (!written || closeResult != 0 || ::rename(temporaryPath.c_str(), statePath_.c_str()) != 0) {
        ::unlink(temporaryPath.c_str());
        return false;
    }

    const int directoryFd = ::open(statePath_.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directoryFd < 0) return false;
    const bool directorySynced = ::fsync(directoryFd) == 0;
    ::close(directoryFd);
    return directorySynced;
}

void ProviderComputeJobManager::pruneLocked(int64_t nowMs) {
    for (auto it = idempotency_.begin(); it != idempotency_.end();) {
        if (it->second.expiresAtMs <= nowMs) it = idempotency_.erase(it);
        else ++it;
    }
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        const bool active = it->second.status == "queued" || it->second.status == "running";
        if (!active && it->second.completedAtMs > 0 && nowMs - it->second.completedAtMs > JobRetentionMs) {
            it = jobs_.erase(it);
        } else {
            ++it;
        }
    }
    while (jobs_.size() > MaximumRetainedJobs) {
        auto oldest = jobs_.end();
        for (auto it = jobs_.begin(); it != jobs_.end(); ++it) {
            const bool active = it->second.status == "queued" || it->second.status == "running";
            if (active) continue;
            if (oldest == jobs_.end() || it->second.completedAtMs < oldest->second.completedAtMs) oldest = it;
        }
        if (oldest == jobs_.end()) break;
        jobs_.erase(oldest);
    }
}

Json ProviderComputeJobManager::jobToJson(const Job& job, bool persistent) const {
    Json value{
        {"job_id", job.id}, {"operation", job.operation}, {"provider", job.provider}, {"scope", job.scope},
        {"status", job.status}, {"created_at_ms", job.createdAtMs}, {"started_at_ms", job.startedAtMs},
        {"completed_at_ms", job.completedAtMs}
    };
    if (persistent) {
        value["idempotency_key"] = job.idempotencyKey;
        value["request_fingerprint"] = job.requestFingerprint;
    }
    if (job.status != "queued" && job.status != "running") {
        Json result{{"success", job.result.success}, {"http_status", job.result.httpStatus}, {"message", job.result.message}};
        Json data = job.result.data;
        bool truncated = false;
        if (persistent && data.dump().size() > 64 * 1024) {
            data = {{"result_details_persisted", false},
                    {"message", "Detailed provider response exceeded durable job history limits; query live provider inventory for current state."}};
            truncated = true;
        }
        result["data"] = std::move(data);
        result["result_details_truncated"] = truncated;
        value["result"] = std::move(result);
    }
    return value;
}

Json ProviderComputeJobManager::publicJob(const Job& job) const {
    Json value = jobToJson(job, false);
    value["status_url"] = "/providers/compute/jobs/" + job.id;
    return value;
}

void ProviderComputeJobManager::audit(const Job& job,
                                      const std::string& action,
                                      const Json& result) const {
    if (!auditSink_) return;
    Json payload{{"job_id", job.id}, {"operation", job.operation}, {"provider", job.provider},
                 {"scope", job.scope}, {"status", job.status}};
    if (result.is_object() && result.contains("result")) payload["result"] = result["result"];
    try {
        auditSink_(job.id, action, nowEpochMs(), std::move(payload));
    } catch (const std::exception& error) {
        std::cerr << "[WARN] Provider job audit callback failed: " << error.what() << std::endl;
    }
}

} // namespace NMC::Server
