// verify_driver.cpp — proves the autowire-generated registrations compile and work.
// Pairs with a generated register_autowired(Container&) (see CMake target
// autowire_demo). Lifetimes can't be inferred from a ctor signature, so leaf
// services are still declared by hand here — exactly the documented limit.
#include <cstdio>

#include "container.hpp"
#include "sample.hpp"

void register_autowired(Container& container); // provided by generated .cpp

int main() {
    Container c;
    c.singleton_auto<SampleService>(); // lifetime is a hand-declared decision
    register_autowired(c);             // generated: bind_auto<SampleController, SampleService>()

    auto ctrl = c.resolve<SampleController>();
    bool ok = ctrl && ctrl->svc && ctrl->svc->ping() == "pong";
    std::printf("autowire_demo: ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
}
