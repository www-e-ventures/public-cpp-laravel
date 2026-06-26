// service_provider.hpp — two-phase service provider
// Laravel: Illuminate\Support\ServiceProvider
//
// Providers are the unit of bootstrapping. The Kernel runs them in two phases:
//   1. register_() on every provider — bind services into the container.
//   2. boot()     on every provider — runs only after all registers complete,
//                                      so it may resolve and use other services.
// The split is what makes provider order not matter: nothing resolves until every
// binding exists. (See Kernel::register_providers.)
#pragma once
#include <memory>
#include <utility>

#include "container_contract.hpp"

class ServiceProvider {
public:
    explicit ServiceProvider(std::shared_ptr<ContainerContract> app) : app_(std::move(app)) {}
    virtual ~ServiceProvider() = default;

    // Register phase. Bind services into the container. MUST NOT resolve — other
    // providers may not have registered yet. (Laravel: register())
    // Note: `register` is a reserved keyword in C++, hence the trailing underscore.
    virtual void register_() {}

    // Boot phase. Runs after every provider has registered; safe to resolve here.
    // (Laravel: boot())
    virtual void boot() {}

protected:
    std::shared_ptr<ContainerContract> app_;
};
