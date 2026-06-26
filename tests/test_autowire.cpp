// test_autowire.cpp — exercises the variadic autowiring helpers.
#include "test_harness.hpp"

#include <memory>
#include <string>

#include "container.hpp"

namespace {

// A small dependency graph: Service has no deps; Repo depends on it; Controller
// depends on both, in a specific order, to prove deps are forwarded positionally.
struct Service {
    std::string name() const { return "service"; }
};

struct Repo {
    explicit Repo(std::shared_ptr<Service> s) : svc(std::move(s)) {}
    std::shared_ptr<Service> svc;
};

struct Controller {
    Controller(std::shared_ptr<Service> s, std::shared_ptr<Repo> r)
        : svc(std::move(s)), repo(std::move(r)) {}
    std::shared_ptr<Service> svc;
    std::shared_ptr<Repo> repo;
};

} // namespace

// bind_auto wires a multi-dep constructor with no factory lambda, forwarding the
// listed deps positionally.
TEST(bind_auto_forwards_deps) {
    Container c;
    c.singleton_auto<Service>();
    c.bind_auto<Repo, Service>();
    c.bind_auto<Controller, Service, Repo>();

    auto ctrl = c.resolve<Controller>();
    CHECK(ctrl->svc != nullptr);
    CHECK(ctrl->repo != nullptr);
    CHECK(ctrl->repo->svc != nullptr);
    CHECK_EQ(ctrl->svc->name(), std::string("service"));
}

// singleton_auto memoises: every resolve hands back the same instance, and that
// instance is shared by everything that depends on it.
TEST(singleton_auto_is_shared) {
    Container c;
    c.singleton_auto<Service>();
    c.bind_auto<Repo, Service>();

    auto s1 = c.resolve<Service>();
    auto s2 = c.resolve<Service>();
    CHECK_EQ(s1.get(), s2.get());

    auto repo = c.resolve<Repo>();
    CHECK_EQ(repo->svc.get(), s1.get()); // injected dep is the same singleton
}

// bind_auto is transient: each resolve builds a fresh instance.
TEST(bind_auto_is_transient) {
    Container c;
    c.singleton_auto<Service>();
    c.bind_auto<Repo, Service>();

    auto r1 = c.resolve<Repo>();
    auto r2 = c.resolve<Repo>();
    CHECK(r1.get() != r2.get());           // distinct Repo instances
    CHECK_EQ(r1->svc.get(), r2->svc.get()); // ...sharing the one Service singleton
}
