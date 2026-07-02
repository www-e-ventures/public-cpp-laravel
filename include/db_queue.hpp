// db_queue.hpp — a database-backed queue (Laravel: the "database" queue driver).
//
// Closures can't be serialized, so jobs are persisted as rows via a Connection and
// named handlers process them. Columns: job, payload, attempts, available_at (unix
// seconds; 0 = now — delayed jobs), reserved_at (0 = unclaimed — the worker's claim
// stamp). Schemaless backends auto-create the tables; for SQLite, migrate them first:
//
//   jobs:        id, job TEXT, payload TEXT, attempts INTEGER,
//                available_at INTEGER, reserved_at INTEGER
//   failed_jobs: id, job TEXT, payload TEXT, error TEXT
//
// work() claims each due job with an atomic compare-and-set on reserved_at
// (Connection::update_if), so several workers — threads or separate processes on the
// same database — never double-process a job. Success deletes the row; a thrown
// exception bumps attempts and either re-queues it (after `retry_backoff * attempts`
// seconds) or, at max attempts, moves it to failed_jobs. A job whose worker crashed
// mid-run is reclaimed after `retry_after` seconds.
#pragma once
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "database.hpp"

class DbQueue {
public:
    using Handler = std::function<void(const std::string& payload)>;

    // retry_backoff: seconds added per accumulated attempt before a failed job runs
    // again (0 = immediately). retry_after: seconds after which a still-reserved job
    // is presumed orphaned by a crashed worker and becomes claimable again.
    explicit DbQueue(std::shared_ptr<Connection> conn, int max_attempts = 3,
                     std::int64_t retry_backoff = 0, std::int64_t retry_after = 90)
        : conn_(std::move(conn)),
          max_attempts_(max_attempts),
          retry_backoff_(retry_backoff),
          retry_after_(retry_after) {}

    DbQueue& handler(const std::string& job, Handler h) {
        handlers_[job] = std::move(h);
        return *this;
    }

    // Queue a job; `delay` postpones its first run by that many seconds.
    void push(const std::string& job, const std::string& payload = "", std::int64_t delay = 0) {
        Row r;
        r.set("job", job);
        r.set("payload", payload);
        r.set("attempts", std::int64_t{0});
        r.set("available_at", delay > 0 ? clock_now() + delay : std::int64_t{0});
        r.set("reserved_at", std::int64_t{0});
        conn_->insert("jobs", std::move(r));
    }

    std::size_t pending() const { return conn_->all("jobs").size(); }
    std::size_t failed() const { return conn_->all("failed_jobs").size(); }

    // One pass: claim + attempt every job that is due at `now` (unix seconds; 0 =
    // wall clock — injectable for tests). Returns the number of jobs processed.
    // Safe to call from several workers at once; the claim decides who runs what.
    std::size_t work(std::int64_t now = 0) {
        if (now == 0) now = clock_now();
        std::size_t processed = 0;
        for (const auto& row : conn_->all("jobs")) {
            std::int64_t id = row.get<std::int64_t>("id");
            std::int64_t available = row.has("available_at") ? row.get<std::int64_t>("available_at") : 0;
            std::int64_t reserved = row.has("reserved_at") ? row.get<std::int64_t>("reserved_at") : 0;
            if (available > now) continue;                          // delayed / backing off
            if (reserved != 0 && now - reserved < retry_after_) continue; // another worker has it

            // Claim: flip reserved_at from the exact value we observed (0, or a
            // stale stamp) to now. If another worker got here first, skip.
            Row claim = row;
            claim.set("reserved_at", now);
            if (!conn_->update_if("jobs", id, "reserved_at", Value{reserved}, claim)) continue;

            auto it = handlers_.find(row.get<std::string>("job"));
            if (it == handlers_.end()) { // unknown job: drop it
                conn_->remove("jobs", id);
                ++processed;
                continue;
            }
            try {
                it->second(row.get<std::string>("payload"));
                conn_->remove("jobs", id); // success
            } catch (const std::exception& e) {
                std::int64_t attempts = row.get<std::int64_t>("attempts") + 1;
                if (attempts >= max_attempts_) {
                    Row f;
                    f.set("job", row.get<std::string>("job"));
                    f.set("payload", row.get<std::string>("payload"));
                    f.set("error", std::string(e.what()));
                    conn_->insert("failed_jobs", std::move(f));
                    conn_->remove("jobs", id);
                } else {
                    Row u = row;
                    u.set("attempts", attempts);
                    u.set("reserved_at", std::int64_t{0}); // release the claim
                    u.set("available_at", retry_backoff_ > 0 ? now + retry_backoff_ * attempts
                                                             : std::int64_t{0});
                    conn_->update("jobs", id, u); // re-queue for another attempt
                }
            }
            ++processed;
        }
        return processed;
    }

private:
    static std::int64_t clock_now() { return static_cast<std::int64_t>(std::time(nullptr)); }

    std::shared_ptr<Connection> conn_;
    std::int64_t max_attempts_;
    std::int64_t retry_backoff_;
    std::int64_t retry_after_;
    std::unordered_map<std::string, Handler> handlers_;
};
