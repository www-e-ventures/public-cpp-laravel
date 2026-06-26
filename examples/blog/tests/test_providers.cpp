// test_providers.cpp — service provider two-phase lifecycle.
#include "test_harness.hpp"

#include <memory>
#include <string>

#include "container.hpp"
#include "kernel.hpp"
#include "providers.hpp"
#include "router.hpp"
#include "service_provider.hpp"
#include "user_service.hpp"

namespace {

bool contains(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
}

// A value a provider can register and another can read in boot().
struct Marker {
    int value = 0;
};

// register_() binds Marker; never resolves.
struct RegistersMarker : ServiceProvider {
    using ServiceProvider::ServiceProvider;
    void register_() override {
        app_->singleton<Marker>([](ContainerContract&) {
            auto m = std::make_shared<Marker>();
            m->value = 7;
            return m;
        });
    }
};

// boot() resolves the Marker bound by *another* provider.
struct ReadsMarker : ServiceProvider {
    using ServiceProvider::ServiceProvider;
    int seen = -1;
    void boot() override { seen = app_->resolve<Marker>()->value; }
};

} // namespace

// The real demo provider binds its services and runs boot().
TEST(provider_registers_and_boots) {
    auto c = std::make_shared<Container>();
    auto router = std::make_shared<Router>();
    auto p = std::make_shared<UserServiceProvider>(c);

    Kernel k(c, router);
    k.register_providers({p});

    CHECK(p->booted);                 // boot() ran
    CHECK(c->bound<UserService>());   // register_() bound the service
    auto svc = c->resolve<UserService>();
    CHECK(contains(svc->find("42"), "Ada Lovelace"));
}

// The decisive property of two-phase boot: a provider's boot() sees bindings from
// providers registered *after* it, because all register_() run before any boot().
TEST(provider_boot_runs_after_all_register) {
    auto c = std::make_shared<Container>();
    auto router = std::make_shared<Router>();
    auto reader = std::make_shared<ReadsMarker>(c);     // listed FIRST...
    auto writer = std::make_shared<RegistersMarker>(c); // ...but registers the Marker

    Kernel k(c, router);
    k.register_providers({reader, writer}); // order: reader before writer

    CHECK_EQ(reader->seen, 7); // boot() still saw the binding despite ordering
}
