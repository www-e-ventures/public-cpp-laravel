// test_kernel_contract.cpp — the Kernel boundary, driven against a FAKE router.
// Proves Kernel depends only on RouterContract: we inject a hand-built fake and
// control matching, with no real Router involved.
#include "test_harness.hpp"

#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "container.hpp"
#include "http.hpp"
#include "kernel.hpp"
#include "router_contract.hpp"

namespace {

// A controllable RouterContract: returns whatever route it's told to (or nothing).
class FakeRouter : public RouterContract {
public:
    void add(const std::string&, const std::string&, Handler, std::vector<Middleware>) override {}
    std::vector<RouteInfo> list() const override { return {}; }

    std::optional<std::pair<Route, std::unordered_map<std::string, std::string>>>
    match(const Request&) const override {
        return canned_;
    }

    void will_match(Route r) {
        canned_ = std::make_pair(std::move(r), std::unordered_map<std::string, std::string>{});
    }
    void will_not_match() { canned_ = std::nullopt; }

private:
    std::optional<std::pair<Route, std::unordered_map<std::string, std::string>>> canned_;
};

Route route_with(Handler h, std::vector<Middleware> mw = {}) {
    return Route{"GET", "/x", std::regex("^/x$"), {}, std::move(h), std::move(mw)};
}

} // namespace

TEST(kernel_returns_404_when_router_does_not_match) {
    auto fake = std::make_shared<FakeRouter>();
    fake->will_not_match();
    Kernel kernel(std::make_shared<Container>(), fake);

    Response r = kernel.handle(Request{"GET", "/anything", {}, {}, ""});
    CHECK_EQ(r.status, 404);
}

TEST(kernel_invokes_matched_handler) {
    auto fake = std::make_shared<FakeRouter>();
    fake->will_match(route_with([](Request&) { return Response{200, "handled"}; }));
    Kernel kernel(std::make_shared<Container>(), fake);

    Response r = kernel.handle(Request{"GET", "/x", {}, {}, ""});
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.body, std::string("handled"));
}

TEST(kernel_applies_global_middleware) {
    auto fake = std::make_shared<FakeRouter>();
    fake->will_match(route_with([](Request&) { return Response{200, "handler"}; }));
    Kernel kernel(std::make_shared<Container>(), fake);

    // A global middleware that short-circuits before the handler ever runs.
    kernel.global_middleware({[](Request&, Next) { return Response{403, "blocked"}; }});

    Response r = kernel.handle(Request{"GET", "/x", {}, {}, ""});
    CHECK_EQ(r.status, 403);
    CHECK_EQ(r.body, std::string("blocked"));
}
