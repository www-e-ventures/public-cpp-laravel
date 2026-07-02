// http_auth.hpp — the HTTP-side auth kit: session cookies, CSRF, login glue, and a
// rate limiter. (Laravel: StartSession / VerifyCsrfToken / Authenticate /
// ThrottleRequests, condensed.)
//
// These pieces used to live in the blog example, which meant every consumer
// re-implemented them (and the BBS hand-rolled its own admin sessions). They are
// security-sensitive enough to want ONE audited copy: CSPRNG ids (session.hpp),
// constant-time token compares, session-id regeneration on login, and cookie flags.
// Zero-dependency, header-only, works against the SessionStore/UserProvider/
// CacheContract contracts.
//
// Typical wiring (see examples/blog):
//   auto sessions = std::make_shared<ArraySessionStore>();
//   kernel->global_middleware({httpauth::session_middleware(sessions)});
//   router->post("/login", login_handler, {httpauth::verify_csrf(sessions)});
//   router->get("/admin", admin_handler, {httpauth::require_auth(sessions)});
#pragma once
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "auth.hpp"
#include "cache.hpp"
#include "http.hpp"
#include "pipeline.hpp"
#include "session.hpp"

namespace httpauth {

// How the session cookie is issued. `lwsid` is the framework's session cookie name
// of record (the Livewire client and the examples speak it); change it per app.
// Set `secure` when serving over HTTPS (behind a TLS-terminating proxy counts).
// SameSite=Lax is the modern default: cookies ride top-level navigations but not
// cross-site subrequests, which blunts CSRF without breaking normal links.
struct CookieOptions {
    std::string name = "lwsid";
    std::string path = "/";
    bool http_only = true;
    bool secure = false;
    std::string same_site = "Lax"; // "Lax" | "Strict" | "None" | "" (omit)
};

inline std::string cookie_header(const std::string& value, const CookieOptions& o) {
    std::string h = o.name + "=" + value + "; Path=" + o.path;
    if (o.http_only) h += "; HttpOnly";
    if (o.secure) h += "; Secure";
    if (!o.same_site.empty()) h += "; SameSite=" + o.same_site;
    return h;
}

// Compare two tokens without leaking WHERE they differ through timing. Bearer
// comparisons (CSRF here) must not early-exit on the first mismatched byte.
inline bool constant_time_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return diff == 0;
}

// The request's session handle, from its cookie (empty id if the middleware hasn't
// run — such a session reads as empty and writes go nowhere useful, so register
// session_middleware globally).
inline Session session_of(const Request& req, SessionStore& store,
                          const CookieOptions& opts = {}) {
    auto it = req.cookies.find(opts.name);
    return Session(store, it == req.cookies.end() ? "" : it->second);
}

// Reads the Cookie header into req.cookies and ensures a session id exists, issuing
// a Set-Cookie for new sessions. Register globally so every request has a session.
// If a handler already set the session cookie itself (attempt_login does, after
// regenerating the id), that Set-Cookie wins — this middleware won't clobber it.
inline Middleware session_middleware(std::shared_ptr<SessionStore> store,
                                     CookieOptions opts = {}) {
    (void)store; // the store is used by controllers; the middleware manages the id
    return [opts](Request& req, Next next) -> Response {
        auto it = req.headers.find("Cookie");
        req.cookies = parse_cookies(it == req.headers.end() ? "" : it->second);
        bool fresh = req.cookies.find(opts.name) == req.cookies.end();
        if (fresh) req.cookies[opts.name] = new_session_id();

        Response res = next(req);
        if (fresh && res.headers.find("Set-Cookie") == res.headers.end())
            res.headers["Set-Cookie"] = cookie_header(req.cookies.at(opts.name), opts);
        return res;
    };
}

// Get (or lazily mint, CSPRNG) the session's CSRF token — embed it in forms as
// `_token` (or hand it to JS for an X-CSRF-Token header).
inline std::string csrf_token(Session& s) {
    auto t = s.get("_csrf");
    if (t) return *t;
    std::string token = csprng_hex(16);
    s.put("_csrf", token);
    return token;
}

// Reject state-changing requests whose _token (form field or X-CSRF-Token header)
// doesn't match the session's token, in constant time. Returns 419 like Laravel.
inline Middleware verify_csrf(std::shared_ptr<SessionStore> store, CookieOptions opts = {}) {
    return [store, opts](Request& req, Next next) -> Response {
        Session s = session_of(req, *store, opts);
        auto expected = s.get("_csrf");
        auto form = parse_form(req.body);
        std::string provided = form.count("_token") ? form["_token"]
                               : req.headers.count("X-CSRF-Token") ? req.headers.at("X-CSRF-Token")
                                                                   : "";
        if (!expected || !constant_time_equal(provided, *expected))
            return Response::json(R"({"error":"CSRF token mismatch"})", 419);
        return next(req);
    };
}

// Route guard: 401 unless the session has an authenticated user.
inline Middleware require_auth(std::shared_ptr<SessionStore> store, CookieOptions opts = {}) {
    return [store, opts](Request& req, Next next) -> Response {
        if (!session_of(req, *store, opts).has("auth_id"))
            return Response::json(R"({"error":"unauthenticated"})", 401);
        return next(req);
    };
}

// Attempt a credentials login. On success the session id is REGENERATED (a
// pre-login id an attacker may have planted stops working — session fixation) and
// the new cookie is stamped onto `res`; req.cookies is updated so later middleware
// and the rest of the handler see the live id. Returns false on bad credentials.
inline bool attempt_login(Request& req, Response& res, SessionStore& store,
                          const UserProvider& users, const std::string& username,
                          const std::string& password, const CookieOptions& opts = {}) {
    Guard guard(users, session_of(req, store, opts));
    if (!guard.attempt(username, password)) return false;
    std::string id = guard.session().regenerate_id();
    req.cookies[opts.name] = id;
    res.headers["Set-Cookie"] = cookie_header(id, opts);
    return true;
}

// Log out by discarding the whole session (auth id, CSRF token, everything) — a
// fresh session id will be minted on the next request.
inline void logout(Request& req, SessionStore& store, const CookieOptions& opts = {}) {
    session_of(req, store, opts).flush();
}

// Fixed-window rate limiter: at most `max` requests per client per `window`; over
// budget answers 429 with Retry-After. The client key prefers the peer IP
// (Request.remote_addr, set by HttpServer) and falls back to the session cookie
// (off-socket hosts and tests), then a shared "anon" bucket.
//
// The cache value is "count|window_start_seconds": the window boundary lives in the
// VALUE, not the cache TTL, so hammering the endpoint cannot keep resetting the
// window (the previous example implementation re-put a fresh TTL on every hit —
// sustained traffic never rolled off and the client was locked out permanently).
// Counting is a get-then-put, so it's approximate under heavy concurrency —
// adequate as an abuse brake, not a hard quota.
inline Middleware throttle(std::shared_ptr<CacheContract> cache, int max,
                           std::chrono::seconds window, CookieOptions opts = {}) {
    return [cache, max, window, opts](Request& req, Next next) -> Response {
        std::string client = !req.remote_addr.empty() ? req.remote_addr
                             : req.cookies.count(opts.name) ? req.cookies.at(opts.name)
                                                            : "anon";
        std::string key = "throttle:" + req.path + ":" + client;

        auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
        long long start = now;
        int count = 0;
        if (auto current = cache->get(key)) {
            auto sep = current->find('|');
            if (sep != std::string::npos) {
                try {
                    count = std::stoi(current->substr(0, sep));
                    start = std::stoll(current->substr(sep + 1));
                } catch (...) {
                    count = 0;
                    start = now;
                }
            }
        }
        if (now - start >= window.count()) { // window rolled over — start a new one
            count = 0;
            start = now;
        }
        ++count;
        cache->put(key, std::to_string(count) + "|" + std::to_string(start),
                   std::chrono::duration_cast<std::chrono::milliseconds>(window) * 2);

        if (count > max) {
            long long retry = start + window.count() - now;
            Response r = Response::json(R"({"error":"too many requests"})", 429);
            r.headers["Retry-After"] = std::to_string(retry > 0 ? retry : window.count());
            return r;
        }
        return next(req);
    };
}

} // namespace httpauth
