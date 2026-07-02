// scheduler.hpp — a tiny cron-style scheduler (Laravel: the console Schedule).
//
// Register named tasks with a cadence, then TICK the schedule periodically —
// `cpp-artisan schedule:run` from real cron, a loop thread, or anything that calls
// run_pending() every so often. The scheduler itself never sleeps or spawns
// threads; it just answers "what is due now?" and runs it. Pair heavy work with
// DbQueue: the scheduled fn pushes a job, the queue worker does the lifting.
//
// Last-run times persist through a Connection (table "schedule_runs": id, name,
// last_run) so a restarted process doesn't double-fire a daily task; without a
// Connection they live in memory (fine for tests and single-run processes).
// Schemaless backends auto-create the table; for SQLite, migrate it first.
//
// Cadences:
//   every(seconds, name, fn)  — due when at least `seconds` passed since the last
//                               run (first tick runs immediately).
//   daily_at("HH:MM", name, fn) — due once per LOCAL day, at the first tick at or
//                               after HH:MM (a box that was off at 03:00 runs the
//                               03:00 task when it comes back that day — the
//                               daily-turn shape door games need).
#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "database.hpp"

class Schedule {
public:
    explicit Schedule(std::shared_ptr<Connection> conn = nullptr,
                      std::string table = "schedule_runs")
        : conn_(std::move(conn)), table_(std::move(table)) {}

    Schedule& every(std::chrono::seconds interval, const std::string& name,
                    std::function<void()> fn) {
        auto secs = static_cast<std::int64_t>(interval.count());
        tasks_.push_back({name, std::move(fn), [secs](std::int64_t now, std::int64_t last) {
                              // A never-run task (last == 0) fires on the first tick
                              // regardless of how `now` compares to the interval.
                              return last == 0 || now - last >= secs;
                          }});
        return *this;
    }

    Schedule& daily_at(const std::string& hhmm, const std::string& name,
                       std::function<void()> fn) {
        int hour = 0, minute = 0;
        parse_hhmm(hhmm, hour, minute);
        tasks_.push_back({name, std::move(fn), [hour, minute](std::int64_t now, std::int64_t last) {
                              std::int64_t trigger = today_at(now, hour, minute);
                              return now >= trigger && last < trigger;
                          }});
        return *this;
    }

    // Run every task due at `now` (unix seconds; 0 = wall clock — injectable for
    // tests). The last-run stamp is recorded BEFORE the task runs, so a task that
    // throws doesn't re-fire on every tick; the exception still propagates.
    std::size_t run_pending(std::int64_t now = 0) {
        if (now == 0) now = static_cast<std::int64_t>(std::time(nullptr));
        std::size_t ran = 0;
        for (const auto& t : tasks_) {
            if (!t.due(now, last_run(t.name))) continue;
            record_run(t.name, now);
            ++ran;
            t.fn();
        }
        return ran;
    }

private:
    struct Task {
        std::string name;
        std::function<void()> fn;
        std::function<bool(std::int64_t now, std::int64_t last)> due;
    };

    static void parse_hhmm(const std::string& s, int& hour, int& minute) {
        auto colon = s.find(':');
        if (colon == std::string::npos) return; // "03" -> 03:00; garbage -> 00:00
        hour = std::atoi(s.substr(0, colon).c_str());
        minute = std::atoi(s.substr(colon + 1).c_str());
    }

    // Unix time of today's HH:MM in the process's LOCAL time zone (the operator's
    // "daily turn at 3am" expectation, not UTC's).
    static std::int64_t today_at(std::int64_t now, int hour, int minute) {
        std::time_t t = static_cast<std::time_t>(now);
        std::tm tm{};
        localtime_r(&t, &tm);
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = 0;
        return static_cast<std::int64_t>(std::mktime(&tm));
    }

    std::int64_t last_run(const std::string& name) {
        if (!conn_) {
            auto it = memory_.find(name);
            return it == memory_.end() ? 0 : it->second;
        }
        Query q;
        q.wheres.push_back({"name", Op::Eq, Value{name}});
        for (const auto& r : conn_->select(table_, q))
            return r.has("last_run") ? r.get<std::int64_t>("last_run") : 0;
        return 0;
    }

    void record_run(const std::string& name, std::int64_t now) {
        if (!conn_) {
            memory_[name] = now;
            return;
        }
        Query q;
        q.wheres.push_back({"name", Op::Eq, Value{name}});
        auto rows = conn_->select(table_, q);
        if (rows.empty()) {
            Row r;
            r.set("name", name);
            r.set("last_run", now);
            conn_->insert(table_, std::move(r));
        } else {
            Row r = rows.front();
            r.set("last_run", now);
            conn_->update(table_, rows.front().get<std::int64_t>("id"), r);
        }
    }

    std::shared_ptr<Connection> conn_;
    std::string table_;
    std::vector<Task> tasks_;
    std::unordered_map<std::string, std::int64_t> memory_;
};
