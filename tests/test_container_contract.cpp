// test_container_contract.cpp — the Container boundary, exercised through the contract.
#include "test_harness.hpp"

#include <memory>
#include <stdexcept>
#include <string>

#include "container.hpp"
#include "container_contract.hpp"

namespace {

struct Service {
    int v = 41;
};
struct Dependent {
    explicit Dependent(std::shared_ptr<Service> s) : svc(std::move(s)) {}
    std::shared_ptr<Service> svc;
};

// Code that depends only on the abstraction, not the concrete Container.
std::shared_ptr<Service> resolve_via_contract(ContainerContract& c) {
    return c.resolve<Service>();
}

} // namespace

TEST(container_contract_bind_and_resolve_through_interface) {
    Container concrete;
    ContainerContract& c = concrete; // use only the interface from here on

    c.bind<Service>([](ContainerContract&) { return std::make_shared<Service>(); });
    CHECK(c.bound<Service>());

    auto s = resolve_via_contract(c);
    CHECK(s != nullptr);
    CHECK_EQ(s->v, 42 - 1);
}

TEST(container_contract_singleton_and_instance) {
    Container concrete;
    ContainerContract& c = concrete;

    c.singleton<Service>([](ContainerContract&) { return std::make_shared<Service>(); });
    CHECK_EQ(c.resolve<Service>().get(), c.resolve<Service>().get()); // memoised

    auto preset = std::make_shared<Service>();
    preset->v = 7;
    Container other;
    ContainerContract& c2 = other;
    c2.instance<Service>(preset);
    CHECK_EQ(c2.resolve<Service>()->v, 7);
}

TEST(container_contract_bind_auto_through_interface) {
    Container concrete;
    ContainerContract& c = concrete;

    c.singleton_auto<Service>();
    c.bind_auto<Dependent, Service>();
    auto d = c.resolve<Dependent>();
    CHECK(d->svc != nullptr);
}

TEST(container_contract_unbound_throws) {
    Container concrete;
    ContainerContract& c = concrete;
    bool threw = false;
    try {
        c.resolve<Service>();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}
