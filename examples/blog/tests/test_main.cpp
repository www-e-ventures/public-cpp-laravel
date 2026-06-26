// test_main.cpp — the five original slice scenarios, ported as real test cases.
// Each test boots the demo app via bootstrap() so it exercises the same wiring as
// the demo executable.
#include "test_harness.hpp"

#include "bootstrap.hpp"
#include "user_service.hpp"

namespace {

Request req(std::string method, std::string path,
            std::unordered_map<std::string, std::string> headers = {}) {
    Request r;
    r.method = std::move(method);
    r.path = std::move(path);
    r.headers = std::move(headers);
    return r;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

// 1. Happy path, authorised: controller resolves the user and returns JSON.
TEST(auth_and_found) {
    App app = bootstrap();
    Response r = app.kernel->handle(req("GET", "/users/42", {{"Authorization", "secret-token"}}));
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.headers.at("Content-Type"), std::string("application/json"));
    CHECK(contains(r.body, "Ada Lovelace"));
}

// 2. Authorised but the record is missing → 404 from the controller.
TEST(auth_but_missing) {
    App app = bootstrap();
    Response r = app.kernel->handle(req("GET", "/users/7", {{"Authorization", "secret-token"}}));
    CHECK_EQ(r.status, 404);
    CHECK_EQ(r.headers.at("Content-Type"), std::string("application/json"));
    CHECK(contains(r.body, "not found"));
}

// 3. Missing token → require_token middleware short-circuits before the controller.
TEST(missing_token_short_circuits) {
    App app = bootstrap();
    Response r = app.kernel->handle(req("GET", "/users/42"));
    CHECK_EQ(r.status, 401);
    CHECK(contains(r.body, "unauthorized"));
}

// 4. Unrouted path → kernel returns a plain 404.
TEST(unrouted_path_404) {
    App app = bootstrap();
    Response r = app.kernel->handle(req("GET", "/nope"));
    CHECK_EQ(r.status, 404);
    CHECK_EQ(r.body, std::string("Not Found"));
}

// 5. Singleton proof: two resolves of UserService yield the same instance.
TEST(singleton_identity) {
    App app = bootstrap();
    auto a = app.container->resolve<UserService>();
    auto b = app.container->resolve<UserService>();
    CHECK_EQ(a.get(), b.get());
}
