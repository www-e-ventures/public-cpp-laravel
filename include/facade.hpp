// facade.hpp — static proxy over the container
// Laravel: Illuminate\Support\Facades\Facade
//
// A facade lets call sites write `Users::find(id)` instead of resolving a service
// and calling it. It does this by reading from a *process-global* container pointer
// set once at bootstrap. That global is hidden, mutable state — read the facade
// trade-off section in README.md before reaching for this over plain injection.
#pragma once
#include <memory>
#include <stdexcept>

#include "container.hpp"

class Facade {
public:
    // Point all facades at a container (call once during bootstrap).
    static void set_container(std::shared_ptr<Container> c) { app() = std::move(c); }

    // Reset the global — mainly so tests can isolate themselves. The need for this
    // is itself the cost: facades carry shared state across call sites and tests.
    static void clear_container() { app().reset(); }

protected:
    static std::shared_ptr<Container>& app() {
        static std::shared_ptr<Container> c;
        return c;
    }

    template <typename Service>
    static std::shared_ptr<Service> resolve_root() {
        auto& c = app();
        if (!c)
            throw std::runtime_error("Facade used before set_container() — no container bound");
        return c->resolve<Service>();
    }
};

// Base for a concrete facade over a single service type. A concrete facade derives
// from this and exposes static methods that forward to root().
template <typename Service>
class FacadeFor : public Facade {
protected:
    static std::shared_ptr<Service> root() { return resolve_root<Service>(); }
};
