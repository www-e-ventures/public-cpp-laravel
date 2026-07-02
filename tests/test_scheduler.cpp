// test_scheduler.cpp — the cron-style Schedule: every()/daily_at() cadences and
// last-run persistence through a Connection. Time is injected (unix seconds), so
// nothing here sleeps.
#include "test_harness.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <memory>
#include <string>

#include "database.hpp"
#include "scheduler.hpp"

namespace {
// Unix time of today's HH:MM in local time, mirroring the scheduler's own math so
// the daily_at tests hold in any timezone.
std::int64_t local_today_at(std::int64_t now, int hour, int minute) {
    std::time_t t = static_cast<std::time_t>(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = 0;
    return static_cast<std::int64_t>(std::mktime(&tm));
}
} // namespace

TEST(schedule_every_runs_then_waits_out_the_interval) {
    Schedule s;
    int runs = 0;
    s.every(std::chrono::seconds(60), "tick", [&] { ++runs; });

    CHECK_EQ(s.run_pending(1000), static_cast<std::size_t>(1)); // first tick fires
    CHECK_EQ(s.run_pending(1030), static_cast<std::size_t>(0)); // mid-interval: no
    CHECK_EQ(s.run_pending(1060), static_cast<std::size_t>(1)); // interval elapsed
    CHECK_EQ(runs, 2);
}

TEST(schedule_daily_at_fires_once_per_day_even_on_late_ticks) {
    Schedule s;
    int runs = 0;
    s.daily_at("03:00", "daily-turn", [&] { ++runs; });

    std::int64_t noon = 1751371200; // an arbitrary fixed instant
    std::int64_t three_am = local_today_at(noon, 3, 0);

    CHECK_EQ(s.run_pending(three_am - 60), static_cast<std::size_t>(0)); // before 03:00
    // The box was busy/off at 03:00 sharp; a tick hours later still runs the task —
    // the "daily turn happens even if cron ticked late" shape.
    CHECK_EQ(s.run_pending(three_am + 4 * 3600), static_cast<std::size_t>(1));
    CHECK_EQ(s.run_pending(three_am + 5 * 3600), static_cast<std::size_t>(0)); // once per day
    // Next local day at 03:00: due again.
    CHECK_EQ(s.run_pending(local_today_at(three_am + 86400, 3, 0)), static_cast<std::size_t>(1));
    CHECK_EQ(runs, 2);
}

TEST(schedule_persists_last_run_across_restarts) {
    auto conn = std::make_shared<MemoryConnection>();
    int runs = 0;

    {
        Schedule s(conn);
        s.every(std::chrono::seconds(3600), "hourly", [&] { ++runs; });
        CHECK_EQ(s.run_pending(1000), static_cast<std::size_t>(1));
    }
    {
        // A NEW Schedule (a restarted process) sees the stamp in the database and
        // does NOT re-fire inside the interval.
        Schedule s(conn);
        s.every(std::chrono::seconds(3600), "hourly", [&] { ++runs; });
        CHECK_EQ(s.run_pending(1100), static_cast<std::size_t>(0));
        CHECK_EQ(s.run_pending(1000 + 3600), static_cast<std::size_t>(1));
    }
    CHECK_EQ(runs, 2);
}

TEST(schedule_throwing_task_does_not_refire_every_tick) {
    Schedule s;
    int attempts = 0;
    s.every(std::chrono::seconds(60), "flaky", [&] {
        ++attempts;
        throw std::runtime_error("boom");
    });

    bool threw = false;
    try {
        s.run_pending(1000);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    // The stamp was recorded before the run, so the next tick inside the interval
    // does not hammer the failing task.
    CHECK_EQ(s.run_pending(1030), static_cast<std::size_t>(0));
    CHECK_EQ(attempts, 1);
}
