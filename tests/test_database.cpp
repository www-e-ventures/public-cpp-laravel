// test_database.cpp — the Connection contract's concurrency + transaction pieces
// (update_if compare-and-set, begin/commit/rollback, the transaction() helper),
// against MemoryConnection. The SQLite twins live in examples/blog/tests/test_sqlite.cpp.
#include "test_harness.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include "database.hpp"

namespace {
std::int64_t seed_job(Connection& c, std::int64_t reserved_at) {
    Row r;
    r.set("job", std::string("email"));
    r.set("reserved_at", reserved_at);
    return c.insert("jobs", std::move(r));
}
} // namespace

TEST(database_update_if_is_a_compare_and_set) {
    MemoryConnection c;
    std::int64_t id = seed_job(c, 0);

    Row claim;
    claim.set("job", std::string("email"));
    claim.set("reserved_at", std::int64_t{100});

    // First claim wins (guard matches the observed value)...
    CHECK(c.update_if("jobs", id, "reserved_at", Value{std::int64_t{0}}, claim));
    // ...the second, still guarding on 0, loses: the row moved on.
    CHECK(!c.update_if("jobs", id, "reserved_at", Value{std::int64_t{0}}, claim));

    auto row = c.find("jobs", id);
    CHECK(row.has_value());
    CHECK_EQ(row->get<std::int64_t>("reserved_at"), std::int64_t{100});

    CHECK(!c.update_if("jobs", 999, "reserved_at", Value{std::int64_t{0}}, claim)); // no row
}

TEST(database_transaction_commits_on_success) {
    MemoryConnection c;
    transaction(c, [&] {
        seed_job(c, 0);
        seed_job(c, 0);
    });
    CHECK_EQ(c.all("jobs").size(), static_cast<std::size_t>(2));
}

TEST(database_transaction_rolls_back_on_throw) {
    MemoryConnection c;
    seed_job(c, 0); // pre-existing row must survive the rollback

    bool threw = false;
    try {
        transaction(c, [&] {
            seed_job(c, 0);
            throw std::runtime_error("halfway failure");
        });
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    CHECK_EQ(c.all("jobs").size(), static_cast<std::size_t>(1)); // the insert unwound

    // Ids don't get reused after a rollback-restored counter... they do here (the
    // snapshot restores next_id_ too), which is exactly what "the transaction never
    // happened" means for the in-memory backend.
    std::int64_t id = seed_job(c, 0);
    CHECK_EQ(id, std::int64_t{2});
}
