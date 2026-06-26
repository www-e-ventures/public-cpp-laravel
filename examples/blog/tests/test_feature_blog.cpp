// test_feature_blog.cpp — FEATURE / SMOKE tests for the Articles app.
//
// Written with the Laravel-flavoured HTTP DSL (include/testing.hpp) so they read
// like Laravel/Pest feature tests: get()/post() then fluent assertOk()/assertSee().
// Each test boots a fresh app (and a fresh in-memory DB), so they're independent.
#include "test_harness.hpp"

#include "bootstrap.hpp"
#include "testing.hpp"

// Full create -> list -> show round trip through the HTTP stack.
TEST(feature_create_then_list_and_show) {
    auto app = bootstrap();
    HttpClient http(*app.kernel, __fails);

    http.get("/articles").assertOk().assertExactBody("[]");

    http.withToken("secret-token")
        .post("/articles", "title=Hello&views=3&published=1")
        .assertCreated()
        .assertSee("Hello")
        .assertJsonFragment("\"id\":1");

    http.get("/articles").assertOk().assertSee("Hello");

    http.get("/articles/1")
        .assertOk()
        .assertJsonFragment("\"views\":3")
        .assertJsonFragment("\"published\":true");
}

// Writes are guarded: the auth middleware short-circuits before the controller,
// and nothing is persisted.
TEST(feature_create_requires_auth) {
    auto app = bootstrap();
    HttpClient http(*app.kernel, __fails);

    http.post("/articles", "title=NoAuth").assertUnauthorized();
    http.get("/articles").assertOk().assertExactBody("[]");
}

// Controller-level validation: empty title is rejected with 422.
TEST(feature_validation_rejects_empty_title) {
    auto app = bootstrap();
    HttpClient http(*app.kernel, __fails);

    http.withToken("secret-token")
        .post("/articles", "views=9&published=1")
        .assertUnprocessable()
        .assertSee("title is required");
}

// Unknown id returns a JSON 404 from the controller (distinct from an unrouted 404).
TEST(feature_show_missing_returns_404) {
    auto app = bootstrap();
    HttpClient http(*app.kernel, __fails);

    http.get("/articles/999").assertNotFound().assertSee("not found");
}

// The HTML route renders a Blade-lite view listing the articles.
TEST(feature_html_view_renders) {
    auto app = bootstrap();
    HttpClient http(*app.kernel, __fails);

    http.withToken("secret-token").post("/articles", "title=Bladey&published=1").assertCreated();
    http.get("/articles.html")
        .assertOk()
        .assertHeader("Content-Type", "text/html")
        .assertSee("<li>Bladey</li>");
}
