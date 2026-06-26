// router_contract.hpp — the routing boundary
// Laravel: Illuminate\Contracts\Routing\Registrar (loosely)
//
// Owns the route data types (Route, RouteInfo) and the RouterContract interface.
// Concrete routers implement add/match/list; get/post are non-virtual sugar so
// callers and implementations share one definition.
#pragma once
#include <optional>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "http.hpp"
#include "pipeline.hpp"

struct Route {
    std::string method;
    std::string uri; // original pattern, kept for introspection (route:list)
    std::regex pattern;
    std::vector<std::string> param_names;
    Handler handler;
    std::vector<Middleware> middleware;
};

// A flattened, regex-free view of a route for tooling (cpp-artisan route:list).
struct RouteInfo {
    std::string method;
    std::string uri;
};

class RouterContract {
public:
    virtual ~RouterContract() = default;

    virtual void add(const std::string& method, const std::string& uri, Handler h,
                     std::vector<Middleware> mw) = 0;

    virtual std::optional<std::pair<Route, std::unordered_map<std::string, std::string>>>
    match(const Request& req) const = 0;

    virtual std::vector<RouteInfo> list() const = 0;

    // Convenience verbs shared by every router.
    void get(const std::string& uri, Handler h, std::vector<Middleware> mw = {}) {
        add("GET", uri, std::move(h), std::move(mw));
    }
    void post(const std::string& uri, Handler h, std::vector<Middleware> mw = {}) {
        add("POST", uri, std::move(h), std::move(mw));
    }
};
