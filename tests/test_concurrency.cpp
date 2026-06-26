// test_concurrency.cpp — thread-safety of the container + in-memory connection
// (the foundation for the multithreaded HTTP server).
#include "test_harness.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "container.hpp"
#include "database.hpp"

namespace {
struct Counter {
    static std::atomic<int> constructed;
    Counter() { ++constructed; }
    int value = 1;
};
std::atomic<int> Counter::constructed{0};
} // namespace

// A singleton resolved from many threads is constructed exactly once, and every
// thread gets the same instance.
TEST(concurrent_singleton_resolves_once) {
    Container c;
    Counter::constructed = 0;
    c.singleton_auto<Counter>();

    constexpr int N = 16;
    std::vector<std::thread> threads;
    std::vector<Counter*> ptrs(N, nullptr);
    for (int i = 0; i < N; ++i)
        threads.emplace_back([&, i] { ptrs[i] = c.resolve<Counter>().get(); });
    for (auto& t : threads) t.join();

    for (int i = 1; i < N; ++i) CHECK_EQ(ptrs[i], ptrs[0]);
    CHECK_EQ(Counter::constructed.load(), 1);
}

// Transient resolution from many threads doesn't corrupt the container.
TEST(concurrent_transient_resolves_are_safe) {
    Container c;
    c.singleton_auto<Counter>();
    c.bind_auto<Counter>(); // override as transient for this test is not needed; resolve singleton
    std::atomic<int> ok{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 16; ++i)
        threads.emplace_back([&] {
            for (int j = 0; j < 100; ++j)
                if (c.resolve<Counter>()) ++ok;
        });
    for (auto& t : threads) t.join();
    CHECK_EQ(ok.load(), 1600);
}

// Concurrent inserts into the in-memory connection don't lose rows or races on ids.
TEST(concurrent_memory_inserts_are_safe) {
    auto conn = std::make_shared<MemoryConnection>();
    constexpr int threads = 8, per = 50;
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; ++t)
        ts.emplace_back([&] {
            for (int i = 0; i < per; ++i) {
                Row r;
                r.set("v", static_cast<std::int64_t>(i));
                conn->insert("t", std::move(r));
            }
        });
    for (auto& t : ts) t.join();
    CHECK_EQ(conn->all("t").size(), static_cast<std::size_t>(threads * per));
}
