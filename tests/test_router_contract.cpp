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
