// test_router_contract.cpp — the Router boundary, exercised through the contract.
#include "test_harness.hpp"

#include <string>

#include "http.hpp"
#include "router.hpp"
#include "router_contract.hpp"

namespace {
Request req(std::string method, std::string path) {
    Request r;
    r.method = std::move(method);
    r.path = std::move(path);
    return r;
}
} // namespace

TEST(router_contract_matches_and_extracts_params) {
    Router concrete;
    RouterContract& router = concrete; // interface only

    router.get("/articles/{id}", [](Request&) { return Response{200, "hit"}; });

    auto matched = router.match(req("GET", "/articles/42"));
    CHECK(matched.has_value());
    CHECK_EQ(matched->second.at("id"), std::string("42"));

    CHECK(!router.match(req("GET", "/articles")).has_value()); // no id segment
    CHECK(!router.match(req("POST", "/articles/42")).has_value()); // wrong verb
}

TEST(router_contract_supports_put_patch_delete) {
    Router concrete;
    RouterContract& router = concrete;
    router.put("/articles/{id}", [](Request&) { return Response{200, "put"}; });
    router.patch("/articles/{id}", [](Request&) { return Response{200, "patch"}; });
    router.del("/tokens/{id}", [](Request&) { return Response{200, "del"}; });

    CHECK(router.match(req("PUT", "/articles/7")).has_value());
    CHECK(router.match(req("PATCH", "/articles/7")).has_value());
    CHECK(router.match(req("DELETE", "/tokens/7")).has_value());
    CHECK(!router.match(req("GET", "/articles/7")).has_value()); // no GET registered
}

TEST(router_contract_catch_all_matches_nested_paths) {
    Router concrete;
    RouterContract& router = concrete;
    router.get("/dist/{path*}", [](Request&) { return Response{200, "static"}; });
    router.get("/u/{id}", [](Request&) { return Response{200, "u"}; });

    auto m = router.match(req("GET", "/dist/js/app.wasm"));
    CHECK(m.has_value());
    CHECK_EQ(m->second.at("path"), std::string("js/app.wasm")); // captured incl. slashes
    CHECK(!router.match(req("GET", "/u/a/b")).has_value());      // {id} is one segment only
}

TEST(router_contract_list_reports_registered_routes) {
    Router concrete;
    RouterContract& router = concrete;

    router.get("/health", [](Request&) { return Response{200, "ok"}; });
    router.post("/articles", [](Request&) { return Response{201, ""}; });

    auto routes = router.list();
    CHECK_EQ(routes.size(), static_cast<std::size_t>(2));
    CHECK_EQ(routes[0].method, std::string("GET"));
    CHECK_EQ(routes[0].uri, std::string("/health"));
    CHECK_EQ(routes[1].method, std::string("POST"));
}

TEST(router_contract_allowed_methods_for_a_known_path) {
    Router concrete;
    RouterContract& router = concrete;

    router.get("/articles/{id}", [](Request&) { return Response{200, ""}; });
    router.put("/articles/{id}", [](Request&) { return Response{200, ""}; });
    router.del("/articles/{id}", [](Request&) { return Response{200, ""}; });
    router.get("/health", [](Request&) { return Response{200, ""}; });

    auto allowed = router.allowed_methods("/articles/42");
    CHECK_EQ(allowed.size(), static_cast<std::size_t>(3)); // GET, PUT, DELETE
    CHECK_EQ(allowed[0], std::string("GET"));
    CHECK_EQ(allowed[1], std::string("PUT"));
    CHECK_EQ(allowed[2], std::string("DELETE"));

    CHECK(router.allowed_methods("/nowhere").empty()); // unknown path stays a 404
}

TEST(router_contract_allowed_methods_deduplicates) {
    Router concrete;
    RouterContract& router = concrete;

    // Two GET routes whose patterns both match the same path must report GET once.
    router.get("/a/{x}", [](Request&) { return Response{200, ""}; });
    router.get("/a/{y*}", [](Request&) { return Response{200, ""}; });

    auto allowed = router.allowed_methods("/a/1");
    CHECK_EQ(allowed.size(), static_cast<std::size_t>(1));
    CHECK_EQ(allowed[0], std::string("GET"));
}
