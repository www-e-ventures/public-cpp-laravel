// router.cpp — see router.hpp (get/post live in RouterContract)
#include "router.hpp"

void Router::add(const std::string& method, const std::string& uri, Handler h,
                 std::vector<Middleware> mw) {
    std::vector<std::string> names;
    std::string rx = "^";
    std::regex seg(R"(\{(\w+)(\*?)\})");
    auto begin = std::sregex_iterator(uri.begin(), uri.end(), seg);
    auto end = std::sregex_iterator();
    std::size_t last = 0;
    for (auto it = begin; it != end; ++it) {
        auto m = *it;
        rx += escape(uri.substr(last, m.position() - last));
        // {name} matches one path segment; {name*} is a catch-all (matches '/' too),
        // e.g. static file prefixes like /dist/{path*}.
        rx += (m[2].str() == "*") ? "(.*)" : "([^/]+)";
        names.push_back(m[1].str());
        last = m.position() + m.length();
    }
    rx += escape(uri.substr(last)) + "$";
    routes_.push_back({method, uri, std::regex(rx), names, std::move(h), std::move(mw)});
}

std::optional<std::pair<Route, std::unordered_map<std::string, std::string>>>
Router::match(const Request& req) const {
    for (const auto& r : routes_) {
        if (r.method != req.method) continue;
        std::smatch m;
        if (std::regex_match(req.path, m, r.pattern)) {
            std::unordered_map<std::string, std::string> params;
            for (std::size_t i = 0; i < r.param_names.size(); ++i)
                params[r.param_names[i]] = m[i + 1].str();
            return std::make_pair(r, params);
        }
    }
    return std::nullopt;
}

std::vector<std::string> Router::allowed_methods(const std::string& path) const {
    std::vector<std::string> out;
    for (const auto& r : routes_) {
        std::smatch m;
        if (!std::regex_match(path, m, r.pattern)) continue;
        bool seen = false;
        for (const auto& v : out) {
            if (v == r.method) {
                seen = true;
                break;
            }
        }
        if (!seen) out.push_back(r.method);
    }
    return out;
}

std::vector<RouteInfo> Router::list() const {
    std::vector<RouteInfo> out;
    out.reserve(routes_.size());
    for (const auto& r : routes_) out.push_back({r.method, r.uri});
    return out;
}

std::string Router::escape(const std::string& s) {
    static const std::regex special(R"([.^$|()\[\]{}*+?\\])");
    return std::regex_replace(s, special, R"(\$&)");
}
