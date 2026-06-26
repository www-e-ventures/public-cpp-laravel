// pipeline.cpp — see pipeline.hpp
#include "pipeline.hpp"

#include <utility>

Pipeline& Pipeline::through(std::vector<Middleware> mws) {
    middlewares_ = std::move(mws);
    return *this;
}

Response Pipeline::run(Request& req, Handler dest) {
    Next next = dest; // innermost is the route handler
    for (auto it = middlewares_.rbegin(); it != middlewares_.rend(); ++it) {
        Middleware mw = *it;
        Next prev = next;
        next = [mw, prev](Request& r) { return mw(r, prev); };
    }
    return next(req);
}
