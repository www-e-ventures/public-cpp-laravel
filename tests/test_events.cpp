// test_events.cpp — the event dispatcher.
#include "test_harness.hpp"

#include <cstddef>
#include <memory>
#include <string>

#include "events.hpp"
#include "queue.hpp"

namespace {
struct UserRegistered {
    std::string email;
};
struct OrderShipped {
    int id;
};

// A class-based listener.
class ShipmentLog : public Listener<OrderShipped> {
public:
    int total = 0;
    void handle(const OrderShipped& e) override { total += e.id; }
};
} // namespace

TEST(events_dispatch_invokes_listener) {
    EventDispatcher d;
    std::string seen;
    d.listen<UserRegistered>([&](const UserRegistered& e) { seen = e.email; });

    d.dispatch(UserRegistered{"ada@x.io"});
    CHECK_EQ(seen, std::string("ada@x.io"));
}

TEST(events_multiple_listeners_all_fire) {
    EventDispatcher d;
    int count = 0;
    d.listen<OrderShipped>([&](const OrderShipped&) { ++count; });
    d.listen<OrderShipped>([&](const OrderShipped& e) { count += e.id; });

    d.dispatch(OrderShipped{10});
    CHECK_EQ(count, 11); // 1 + 10
    CHECK_EQ(d.listener_count<OrderShipped>(), static_cast<std::size_t>(2));
}

TEST(events_class_based_listener) {
    EventDispatcher d;
    auto log = std::make_shared<ShipmentLog>();
    d.subscribe<OrderShipped>(log);
    d.dispatch(OrderShipped{5});
    d.dispatch(OrderShipped{3});
    CHECK_EQ(log->total, 8);
}

TEST(events_queued_listener_defers_to_queue) {
    EventDispatcher d;
    ArrayQueue q;
    int total = 0;
    d.listen_queued<OrderShipped>(q, [&](const OrderShipped& e) { total += e.id; });

    d.dispatch(OrderShipped{5}); // enqueued, not run yet
    CHECK_EQ(total, 0);
    CHECK_EQ(q.size(), static_cast<std::size_t>(1));
    q.work();
    CHECK_EQ(total, 5);
}

TEST(events_unrelated_type_not_triggered) {
    EventDispatcher d;
    bool fired = false;
    d.listen<UserRegistered>([&](const UserRegistered&) { fired = true; });

    d.dispatch(OrderShipped{1}); // different type
    CHECK(!fired);
    d.dispatch(UserRegistered{"x"}); // no crash when... has a listener; also test empty type:
    CHECK(fired);
    CHECK_EQ(d.listener_count<OrderShipped>(), static_cast<std::size_t>(0));
}
