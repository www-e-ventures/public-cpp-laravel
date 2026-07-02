// kernel.cpp — see kernel.hpp
#include "kernel.hpp"

#include <cstdio>
#include <exception>
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
    // A throw anywhere below (a handler's std::stoi on a bad param, a failed
    // container resolution, a DB error) must become a 500, not std::terminate —
    // on the threaded HttpServer an escaped exception would kill every worker.
    // The what() goes to stderr for the operator; the client gets no details.
    try {
        auto matched = router_->match(req);
        if (!matched) {
            // Path exists under another verb → 405 with Allow; unknown path → 404.
            auto allowed = router_->allowed_methods(req.path);
            if (allowed.empty()) return Response{404, "Not Found"};
            std::string allow;
            for (const auto& m : allowed) {
                if (!allow.empty()) allow += ", ";
                allow += m;
            }
            Response res{405, "Method Not Allowed"};
            res.headers["Allow"] = allow;
            return res;
        }
        auto [route, params] = *matched;
        req.route_params = std::move(params);

        std::vector<Middleware> stack = global_;
        stack.insert(stack.end(), route.middleware.begin(), route.middleware.end());

        Pipeline pipe;
        return pipe.through(stack).run(req, route.handler);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[kernel] unhandled exception on %s %s: %s\n",
                     req.method.c_str(), req.path.c_str(), e.what());
        return Response{500, "Internal Server Error"};
    } catch (...) {
        std::fprintf(stderr, "[kernel] unhandled non-std exception on %s %s\n",
                     req.method.c_str(), req.path.c_str());
        return Response{500, "Internal Server Error"};
    }
}
