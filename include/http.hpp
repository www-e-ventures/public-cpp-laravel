// http.hpp — HTTP contracts
// Laravel: Illuminate\Http\{Request,Response}
#pragma once
#include <string>
#include <unordered_map>

// Inbound request. route_params is populated by the Router after a match; cookies are
// populated by the session middleware from the Cookie header.
struct Request {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> route_params; // {id => 42}
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    // Default member initializer so existing Request{...} brace-inits (which don't
    // list cookies) still compile cleanly under -Wmissing-field-initializers.
    std::unordered_map<std::string, std::string> cookies{}; // {lwsid => ...}
};

// Outbound response. Defaults to a 200 text/plain body.
struct Response {
    int status = 200;
    std::string body;
    std::unordered_map<std::string, std::string> headers{{"Content-Type", "text/plain"}};

    static Response json(const std::string& payload, int status = 200) {
        Response r;
        r.status = status;
        r.body = payload;
        r.headers["Content-Type"] = "application/json";
        return r;
    }

    // 302 redirect to a URL.
    static Response redirect(const std::string& url) {
        Response r;
        r.status = 302;
        r.headers["Location"] = url;
        return r;
    }
};

// Split "a=1&b=2" (form bodies) or "a=1; b=2" (cookie headers) into a map.
inline std::unordered_map<std::string, std::string> parse_pairs(const std::string& s, char sep) {
    std::unordered_map<std::string, std::string> out;
    std::size_t i = 0;
    auto trim = [](std::string v) {
        std::size_t b = v.find_first_not_of(' ');
        if (b == std::string::npos) return std::string{};
        std::size_t e = v.find_last_not_of(' ');
        return v.substr(b, e - b + 1);
    };
    while (i < s.size()) {
        std::size_t next = s.find(sep, i);
        std::string pair = s.substr(i, next == std::string::npos ? std::string::npos : next - i);
        std::size_t eq = pair.find('=');
        if (eq != std::string::npos) out[trim(pair.substr(0, eq))] = trim(pair.substr(eq + 1));
        if (next == std::string::npos) break;
        i = next + 1;
    }
    return out;
}

inline std::unordered_map<std::string, std::string> parse_form(const std::string& body) {
    return parse_pairs(body, '&');
}
inline std::unordered_map<std::string, std::string> parse_cookies(const std::string& header) {
    return parse_pairs(header, ';');
}
