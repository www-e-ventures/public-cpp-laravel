// providers.hpp — demo service provider(s)
#pragma once
#include "service_provider.hpp"

// Registers the user subsystem's bindings (UserService, UserController) and warms
// the service in boot(). Mirrors a Laravel app/Providers/*ServiceProvider.
class UserServiceProvider : public ServiceProvider {
public:
    using ServiceProvider::ServiceProvider;

    void register_() override;
    void boot() override;

    bool booted = false; // observable proof boot() ran (used by tests)
};

// Registers the ORM slice: the Connection contract (bound to the in-memory impl)
// and the Article repository. Demonstrates the ORM wiring through the container +
// service providers from Phases 1-3.
class DatabaseServiceProvider : public ServiceProvider {
public:
    using ServiceProvider::ServiceProvider;

    void register_() override;
    void boot() override; // runs the app migrations once bindings exist
};
