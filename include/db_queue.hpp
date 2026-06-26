// db_queue.hpp — a database-backed queue (Laravel: the "database" queue driver).
//
// Closures can't be serialized, so jobs are persisted as {job-name, payload, attempts}
// rows via a Connection; named handlers process them. work() makes one attempt at each
// pending job: success deletes the row, a thrown exception bumps attempts and either
// re-queues it or (at max attempts) moves it to "failed_jobs". Schemaless backends
// auto-create the tables; for SQLite, migrate them first.
#pragma once
#include <cstddef>
#include <cstdint>
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

    explicit DbQueue(std::shared_ptr<Connection> conn, int max_attempts = 3)
        : conn_(std::move(conn)), max_attempts_(max_attempts) {}

    DbQueue& handler(const std::string& job, Handler h) {
        handlers_[job] = std::move(h);
        return *this;
    }

    void push(const std::string& job, const std::string& payload = "") {
        Row r;
        r.set("job", job);
        r.set("payload", payload);
        r.set("attempts", std::int64_t{0});
        conn_->insert("jobs", std::move(r));
    }

    std::size_t pending() const { return conn_->all("jobs").size(); }
    std::size_t failed() const { return conn_->all("failed_jobs").size(); }

    // One attempt per pending job. Returns the number of jobs touched.
    std::size_t work() {
        std::size_t processed = 0;
        for (const auto& row : conn_->all("jobs")) {
            std::int64_t id = row.get<std::int64_t>("id");
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
                    conn_->update("jobs", id, u); // re-queue for another attempt
                }
            }
            ++processed;
        }
        return processed;
    }

private:
    std::shared_ptr<Connection> conn_;
    std::int64_t max_attempts_;
    std::unordered_map<std::string, Handler> handlers_;
};
