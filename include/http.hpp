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
    std::unordered_map<std::string, std::string> query{};   // {page => 2} from ?page=2
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

// Percent-decode a URL-encoded string: "%XX" becomes the byte it names, and (for
// application/x-www-form-urlencoded bodies and query strings) '+' becomes a space.
// A malformed escape is left as-is rather than dropped.
inline std::string url_decode(const std::string& s, bool plus_as_space = true) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+' && plus_as_space) {
            out.push_back(' ');
        } else if (s[i] == '%' && i + 2 < s.size()) {
            int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(s[i]); // not a valid escape — keep the '%'
            }
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// Split "a=1&b=2" (form bodies/query strings) or "a=1; b=2" (cookie headers) into a
// map. With `decode`, keys/values are url_decode()d — form bodies and query strings
// are percent-encoded; cookie headers are taken verbatim (callers pass decode=false).
inline std::unordered_map<std::string, std::string> parse_pairs(const std::string& s, char sep,
                                                                bool decode = false) {
    std::unordered_map<std::string, std::string> out;
    std::size_t i = 0;
    auto trim = [](std::string v) {
        std::size_t b = v.find_first_not_of(' ');
        if (b == std::string::npos) return std::string{};
        std::size_t e = v.find_last_not_of(' ');
        return v.substr(b, e - b + 1);
    };
    auto cook = [decode](const std::string& v) { return decode ? url_decode(v) : v; };
    while (i < s.size()) {
        std::size_t next = s.find(sep, i);
        std::string pair = s.substr(i, next == std::string::npos ? std::string::npos : next - i);
        std::size_t eq = pair.find('=');
        if (eq != std::string::npos)
            out[cook(trim(pair.substr(0, eq)))] = cook(trim(pair.substr(eq + 1)));
        if (next == std::string::npos) break;
        i = next + 1;
    }
    return out;
}

// Form bodies percent-decode (and treat '+' as space); cookie headers are verbatim.
inline std::unordered_map<std::string, std::string> parse_form(const std::string& body) {
    return parse_pairs(body, '&', /*decode=*/true);
}
inline std::unordered_map<std::string, std::string> parse_cookies(const std::string& header) {
    return parse_pairs(header, ';');
}
