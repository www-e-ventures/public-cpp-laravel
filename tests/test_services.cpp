// test_services.cpp — cache / queue / mail contracts + default drivers.
#include "test_harness.hpp"

#include <chrono>
#include <cstddef>
#include <ctime>
#include <stdexcept>
#include <string>
#include <thread>

#include <memory>

#include "cache.hpp"
#include "database.hpp"
#include "db_queue.hpp"
#include "mail.hpp"
#include "mailable.hpp"
#include "queue.hpp"
#include "smtp.hpp"
#include "view.hpp"

TEST(cache_put_get_has_forget) {
    ArrayCache c;
    CHECK(!c.has("k"));
    c.put("k", "v");
    CHECK(c.has("k"));
    auto v = c.get("k"); // hold the optional so the reference below isn't dangling
    CHECK(v.has_value());
    CHECK_EQ(*v, std::string("v"));
    CHECK_EQ(c.get_or("missing", "def"), std::string("def"));
    c.forget("k");
    CHECK(!c.has("k"));
}

TEST(cache_remember_computes_once) {
    ArrayCache c;
    int calls = 0;
    auto compute = [&] { ++calls; return std::string("built"); };
    CHECK_EQ(c.remember("k", compute), std::string("built"));
    CHECK_EQ(c.remember("k", compute), std::string("built")); // served from cache
    CHECK_EQ(calls, 1);
}

TEST(cache_tags_flush_invalidates_group) {
    ArrayCache c;
    c.tags({"posts"}).put("post.1", "A");
    c.tags({"posts", "home"}).put("post.2", "B");
    c.put("user.1", "C"); // untagged

    CHECK(c.has("post.1"));
    c.tags({"posts"}).flush(); // drops everything tagged "posts"
    CHECK(!c.has("post.1"));
    CHECK(!c.has("post.2"));
    CHECK(c.has("user.1")); // untagged entry survives
}

TEST(cache_ttl_expires) {
    ArrayCache c;
    c.put("k", "v", std::chrono::milliseconds(20));
    CHECK(c.has("k")); // present before expiry
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(!c.has("k"));            // expired
    CHECK(!c.get("k").has_value());
}

TEST(cache_no_ttl_persists) {
    ArrayCache c;
    c.put("k", "v"); // no expiry
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(c.has("k"));
}

TEST(queue_sync_runs_immediately) {
    SyncQueue q;
    int ran = 0;
    q.push([&] { ++ran; });
    CHECK_EQ(ran, 1); // already executed
}

TEST(queue_array_defers_until_work) {
    ArrayQueue q;
    int ran = 0;
    q.push([&] { ++ran; });
    q.push([&] { ++ran; });
    CHECK_EQ(ran, 0); // nothing run yet
    CHECK_EQ(q.size(), static_cast<std::size_t>(2));
    q.work();
    CHECK_EQ(ran, 2);
    CHECK_EQ(q.size(), static_cast<std::size_t>(0));
}

TEST(smtp_formats_message_and_commands) {
    Mail m{"ada@x.io", "Hi", "Welcome"};
    std::string email = format_email("noreply@app.io", m);
    CHECK(email.find("From: noreply@app.io") != std::string::npos);
    CHECK(email.find("To: ada@x.io") != std::string::npos);
    CHECK(email.find("Subject: Hi") != std::string::npos);
    CHECK(email.find("\r\n\r\nWelcome") != std::string::npos); // headers/body separator

    auto cmds = smtp_commands("noreply@app.io", m);
    CHECK_EQ(cmds.front(), std::string("HELO localhost"));
    CHECK_EQ(cmds[1], std::string("MAIL FROM:<noreply@app.io>"));
    CHECK_EQ(cmds[2], std::string("RCPT TO:<ada@x.io>"));
    CHECK_EQ(cmds[3], std::string("DATA"));
    CHECK_EQ(cmds.back(), std::string("QUIT"));
}

TEST(db_queue_push_and_work) {
    auto conn = std::make_shared<MemoryConnection>();
    DbQueue q(conn);
    std::string log;
    q.handler("email", [&](const std::string& to) { log += "email:" + to + ";"; });

    q.push("email", "ada@x.io");
    q.push("email", "bob@x.io");
    CHECK_EQ(q.pending(), static_cast<std::size_t>(2));

    CHECK_EQ(q.work(), static_cast<std::size_t>(2)); // drained in order
    CHECK_EQ(q.pending(), static_cast<std::size_t>(0));
    CHECK_EQ(log, std::string("email:ada@x.io;email:bob@x.io;"));
}

TEST(db_queue_retries_then_marks_failed) {
    auto conn = std::make_shared<MemoryConnection>();
    DbQueue q(conn, /*max_attempts=*/3);
    q.handler("boom", [](const std::string&) { throw std::runtime_error("nope"); });
    q.push("boom", "x");

    q.work();
    CHECK_EQ(q.pending(), static_cast<std::size_t>(1)); // attempt 1: re-queued
    q.work();
    CHECK_EQ(q.pending(), static_cast<std::size_t>(1)); // attempt 2: re-queued
    q.work();
    CHECK_EQ(q.pending(), static_cast<std::size_t>(0)); // attempt 3: moved to failed
    CHECK_EQ(q.failed(), static_cast<std::size_t>(1));
}

TEST(db_queue_drops_unknown_jobs) {
    auto conn = std::make_shared<MemoryConnection>();
    DbQueue q(conn);
    q.push("no-handler", "x");
    CHECK_EQ(q.work(), static_cast<std::size_t>(1)); // drained even without a handler
    CHECK_EQ(q.pending(), static_cast<std::size_t>(0));
}

TEST(mailable_renders_view_into_body) {
    Views views;
    views.define("welcome", "Hi {{ name }}, welcome aboard!");
    View data;
    data.set("name", "Ada");

    ArrayMailer m;
    m.send(mailable(views, "ada@x.io", "Welcome", "welcome", data));

    CHECK(m.sent_to("ada@x.io"));
    CHECK_EQ(m.sent().front().subject, std::string("Welcome"));
    CHECK_EQ(m.sent().front().body, std::string("Hi Ada, welcome aboard!"));
}

TEST(mail_array_records_messages) {
    ArrayMailer m;
    m.send({"ada@x.io", "Hi", "Welcome"});
    m.send({"bob@x.io", "Yo", "Hello"});
    CHECK_EQ(m.sent().size(), static_cast<std::size_t>(2));
    CHECK(m.sent_to("ada@x.io"));
    CHECK(!m.sent_to("nobody@x.io"));
    CHECK_EQ(m.sent().front().subject, std::string("Hi"));
}

// --- v0.9.0: the finished queue driver (claims, delays, backoff) --------------

TEST(db_queue_claim_prevents_double_processing) {
    auto conn = std::make_shared<MemoryConnection>();
    // Two workers over the SAME database — the shape of two bbsd processes or a
    // worker thread pool. The atomic claim (update_if on reserved_at) means each
    // job runs exactly once even though both workers scan the same rows.
    DbQueue w1(conn), w2(conn);
    int runs = 0;
    w1.handler("job", [&](const std::string&) { ++runs; });
    w2.handler("job", [&](const std::string&) { ++runs; });
    w1.push("job");
    w1.push("job");

    std::int64_t now = 1000;
    // w1 claims + drains both; w2's pass finds nothing claimable.
    CHECK_EQ(w1.work(now), static_cast<std::size_t>(2));
    CHECK_EQ(w2.work(now), static_cast<std::size_t>(0));
    CHECK_EQ(runs, 2);
}

TEST(db_queue_delayed_jobs_wait_for_available_at) {
    auto conn = std::make_shared<MemoryConnection>();
    DbQueue q(conn);
    std::string log;
    q.handler("job", [&](const std::string& p) { log += p; });

    q.push("job", "now");
    q.push("job", "later", /*delay=*/60);
    // The delay is measured from the wall clock at push time, so tick "now" far in
    // the future for the second pass.
    std::int64_t base = static_cast<std::int64_t>(std::time(nullptr));
    CHECK_EQ(q.work(base), static_cast<std::size_t>(1)); // only the undelayed job
    CHECK_EQ(log, std::string("now"));
    CHECK_EQ(q.work(base + 61), static_cast<std::size_t>(1)); // the delayed one is due
    CHECK_EQ(log, std::string("nowlater"));
}

TEST(db_queue_failure_backs_off_before_retry) {
    auto conn = std::make_shared<MemoryConnection>();
    DbQueue q(conn, /*max_attempts=*/3, /*retry_backoff=*/10);
    q.handler("boom", [](const std::string&) { throw std::runtime_error("nope"); });
    q.push("boom");

    std::int64_t now = 5000;
    CHECK_EQ(q.work(now), static_cast<std::size_t>(1));      // attempt 1 fails
    CHECK_EQ(q.work(now + 5), static_cast<std::size_t>(0));  // backing off (10s * 1)
    CHECK_EQ(q.work(now + 11), static_cast<std::size_t>(1)); // attempt 2 after backoff
    CHECK_EQ(q.pending(), static_cast<std::size_t>(1));      // still queued (max 3)
}

TEST(db_queue_reclaims_jobs_from_crashed_workers) {
    auto conn = std::make_shared<MemoryConnection>();
    DbQueue q(conn, /*max_attempts=*/3, /*retry_backoff=*/0, /*retry_after=*/90);
    int runs = 0;
    q.handler("job", [&](const std::string&) { ++runs; });
    q.push("job");

    // Simulate a worker that claimed the job and died: reserve it by hand.
    auto rows = conn->all("jobs");
    Row stuck = rows.front();
    stuck.set("reserved_at", std::int64_t{1000});
    conn->update("jobs", stuck.get<std::int64_t>("id"), stuck);

    CHECK_EQ(q.work(1050), static_cast<std::size_t>(0)); // within retry_after: leave it
    CHECK_EQ(q.work(1091), static_cast<std::size_t>(1)); // stale claim: reclaimed + run
    CHECK_EQ(runs, 1);
}
