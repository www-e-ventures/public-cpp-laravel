// kernel.cpp — see kernel.hpp
#include "kernel.hpp"

#include <utility>

Kernel::Kernel(std::shared_ptr<ContainerContract> c, std::shared_ptr<RouterContract> r)
    : container_(std::move(c)), router_(std::move(r)) {}

void Kernel::global_middleware(std::vector<Middleware> mw) { global_ = std::move(mw); }

void Kernel::register_providers(std::vector<std::shared_ptr<ServiceProvider>> providers) {
    // First, every provider binds its services...
    for (auto& p : providers) p->register_();
    // ...then every provider boots, free to resolve anything registered.
    for (auto& p : providers) p->boot();
    providers_ = std::move(providers);
}

Response Kernel::handle(Request req) {
    auto matched = router_->match(req);
    if (!matched) return Response{404, "Not Found"};
    auto [route, params] = *matched;
    req.route_params = std::move(params);

    std::vector<Middleware> stack = global_;
    stack.insert(stack.end(), route.middleware.begin(), route.middleware.end());

    Pipeline pipe;
    return pipe.through(stack).run(req, route.handler);
}
