// test_facades.cpp — facade static proxy behaviour, including its sharp edges.
#include "test_harness.hpp"

#include <memory>
#include <stdexcept>

#include "container.hpp"
#include "facade.hpp"
#include "facades.hpp"
#include "user_service.hpp"

// A facade forwards to the service resolved from the bound container.
TEST(facade_proxies_to_container_service) {
    auto c = std::make_shared<Container>();
    c->singleton_auto<UserService>();
    Facade::set_container(c);

    CHECK(Users::find("42").find("Ada Lovelace") != std::string::npos);
    CHECK(Users::find("999").empty());

    Facade::clear_container(); // isolate from other tests (the cost made concrete)
}

// The hidden-dependency cost: with no container bound, the failure is at *runtime*,
// not compile time — the call site looks dependency-free but isn't.
TEST(facade_without_container_throws) {
    Facade::clear_container();
    bool threw = false;
    try {
        Users::find("42");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}
