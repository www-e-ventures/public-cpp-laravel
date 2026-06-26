// test_feature_throttle.cpp — rate limiting on GET /ping (3 per client per window).
#include "test_harness.hpp"

#include "bootstrap.hpp"
#include "testing.hpp"

TEST(throttle_allows_then_blocks) {
    auto app = bootstrap();
    HttpClient http(*app.kernel, __fails);
    http.withHeader("Cookie", "lwsid=client-1"); // stable client across requests

    http.get("/ping").assertOk(); // 1
    http.get("/ping").assertOk(); // 2
    http.get("/ping").assertOk(); // 3
    http.get("/ping").assertStatus(429).assertSee("too many requests"); // 4 -> blocked
}

TEST(throttle_is_per_client) {
    auto app = bootstrap();

    HttpClient a(*app.kernel, __fails);
    a.withHeader("Cookie", "lwsid=alice");
    a.get("/ping").assertOk();
    a.get("/ping").assertOk();
    a.get("/ping").assertOk();
    a.get("/ping").assertStatus(429); // alice is over

    HttpClient b(*app.kernel, __fails);
    b.withHeader("Cookie", "lwsid=bob");
    b.get("/ping").assertOk(); // bob has his own budget
}
