// test_broadcast.cpp — channel broadcasting.
#include "test_harness.hpp"

#include <cstddef>
#include <string>

#include "broadcast.hpp"

TEST(broadcast_delivers_to_channel_subscribers) {
    Broadcaster b;
    std::string log;
    b.subscribe("orders", [&](const std::string& ev, const std::string& p) { log += ev + ":" + p + ";"; });
    b.subscribe("orders", [&](const std::string& ev, const std::string&) { log += "also:" + ev + ";"; });

    b.broadcast("orders", "shipped", "42");
    CHECK_EQ(log, std::string("shipped:42;also:shipped;"));
    CHECK_EQ(b.subscribers("orders"), static_cast<std::size_t>(2));
}

TEST(broadcast_isolated_by_channel) {
    Broadcaster b;
    bool fired = false;
    b.subscribe("a", [&](const std::string&, const std::string&) { fired = true; });
    b.broadcast("b", "event"); // different channel
    CHECK(!fired);
    CHECK_EQ(b.subscribers("b"), static_cast<std::size_t>(0));
}
