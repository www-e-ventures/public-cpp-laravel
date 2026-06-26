// throttle.hpp — a rate-limiting middleware (Laravel: ThrottleRequests / throttle:N,W).
//
// Counts requests per (path, client) in the cache over a sliding window; returns 429
// once the count exceeds `max`. The client key is the session cookie (lwsid), falling
// back to "anon". Counting is a get-then-put, so it's approximate under heavy
// concurrency — adequate for a dev limiter, not a hard quota.
#pragma once
#include <chrono>
#include <memory>
#include <string>

#include "cache.hpp"
#include "http.hpp"
#include "pipeline.hpp"

inline Middleware throttle(std::shared_ptr<CacheContract> cache, int max,
                           std::chrono::seconds window) {
    return [cache, max, window](Request& req, Next next) -> Response {
        std::string client = req.cookies.count("lwsid") ? req.cookies.at("lwsid") : "anon";
        std::string key = "throttle:" + req.path + ":" + client;

        int count = 1;
        auto current = cache->get(key);
        if (current) count = std::stoi(*current) + 1;
        cache->put(key, std::to_string(count),
                   std::chrono::duration_cast<std::chrono::milliseconds>(window));

        if (count > max) {
            Response r = Response::json(R"({"error":"too many requests"})", 429);
            r.headers["Retry-After"] = std::to_string(window.count());
            return r;
        }
        return next(req);
    };
}
