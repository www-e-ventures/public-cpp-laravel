// router.hpp — request router
// Laravel: Illuminate\Routing\Router (implements RouterContract)
//
// {param} segments compile to a std::regex with capture groups; matched params are
// extracted into Request.route_params. Each route carries its own middleware stack,
// applied after the global stack (see Kernel).
#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "http.hpp"
#include "router_contract.hpp"

class Router : public RouterContract {
public:
    void add(const std::string& method, const std::string& uri, Handler h,
             std::vector<Middleware> mw) override;

    std::optional<std::pair<Route, std::unordered_map<std::string, std::string>>>
    match(const Request& req) const override;

    std::vector<RouteInfo> list() const override;

    std::vector<std::string> allowed_methods(const std::string& path) const override;

private:
    static std::string escape(const std::string& s);
    std::vector<Route> routes_;
};
