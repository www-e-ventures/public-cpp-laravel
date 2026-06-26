// bootstrap.hpp — wires the demo app (container bindings, routes, global middleware)
// Shared by the demo executable (main.cpp) and the test suite so both exercise the
// exact same wiring.
#pragma once
#include <memory>

#include "container.hpp"
#include "kernel.hpp"
#include "router.hpp"

struct App {
    std::shared_ptr<Container> container;
    std::shared_ptr<Router> router;
    std::shared_ptr<Kernel> kernel;
};

App bootstrap();
