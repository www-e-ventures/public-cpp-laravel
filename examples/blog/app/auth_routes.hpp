// auth_routes.hpp — HTTP wiring for auth: session-from-cookie middleware, an `auth`
// route guard, and a login/logout/me controller. Demonstrates the framework's
// session + guard primitives end-to-end over real cookies.
#pragma once
#include <memory>
#include <string>
#include <utility>

#include "auth.hpp"
#include "http.hpp"
#include "pipeline.hpp"
#include "session.hpp"

// Reads the Cookie header into req.cookies and ensures a session id exists, issuing a
// Set-Cookie for new sessions. Register globally so every request has a session.
inline Middleware session_middleware(std::shared_ptr<SessionStore> store) {
    (void)store; // the store is used by controllers; the middleware just manages the id
    return [](Request& req, Next next) -> Response {
        auto it = req.headers.find("Cookie");
        req.cookies = parse_cookies(it == req.headers.end() ? "" : it->second);
        bool fresh = req.cookies.find("lwsid") == req.cookies.end();
        if (fresh) req.cookies["lwsid"] = new_session_id();

        Response res = next(req);
        if (fresh)
            res.headers["Set-Cookie"] = "lwsid=" + req.cookies["lwsid"] + "; Path=/; HttpOnly";
        return res;
    };
}

// Get (or lazily create) the session's CSRF token.
inline std::string csrf_token(Session& s) {
    auto t = s.get("_csrf");
    if (t) return *t;
    std::string token = new_session_id();
    s.put("_csrf", token);
    return token;
}

// Reject state-changing requests whose _token (form field or X-CSRF-Token header)
// doesn't match the session's token. Returns 419 like Laravel.
inline Middleware verify_csrf(std::shared_ptr<SessionStore> store) {
    return [store](Request& req, Next next) -> Response {
        auto it = req.cookies.find("lwsid");
        Session s(*store, it == req.cookies.end() ? "" : it->second);
        auto expected = s.get("_csrf");
        auto form = parse_form(req.body);
        std::string provided = form.count("_token") ? form["_token"]
                               : req.headers.count("X-CSRF-Token") ? req.headers.at("X-CSRF-Token")
                                                                   : "";
        if (!expected || provided != *expected)
            return Response::json(R"({"error":"CSRF token mismatch"})", 419);
        return next(req);
    };
}

// Route guard: 401 unless the session has an authenticated user.
inline Middleware require_auth(std::shared_ptr<SessionStore> store) {
    return [store](Request& req, Next next) -> Response {
        auto it = req.cookies.find("lwsid");
        Session s(*store, it == req.cookies.end() ? "" : it->second);
        if (!s.has("auth_id"))
            return Response::json(R"({"error":"unauthenticated"})", 401);
        return next(req);
    };
}

class AuthController {
public:
    AuthController(std::shared_ptr<SessionStore> store, std::shared_ptr<UserProvider> users)
        : store_(std::move(store)), users_(std::move(users)) {}

    Response show_login(Request& req) {
        auto it = req.cookies.find("lwsid");
        Session s(*store_, it == req.cookies.end() ? "" : it->second);
        std::string token = csrf_token(s); // embed it so the POST can echo it back

        Response r;
        r.headers["Content-Type"] = "text/html";
        r.body =
            "<form method=\"post\" action=\"/login\">"
            "<input type=\"hidden\" name=\"_token\" value=\"" + token + "\">"
            "<input name=\"username\"><input name=\"password\" type=\"password\">"
            "<button>Log in</button></form>";
        return r;
    }

    Response login(Request& req) {
        auto form = parse_form(req.body);
        Guard g = guard(req);
        if (g.attempt(form["username"], form["password"])) return Response::redirect("/me");
        return Response::json(R"({"error":"invalid credentials"})", 401);
    }

    Response logout(Request& req) {
        guard(req).logout();
        return Response::redirect("/login");
    }

    Response me(Request& req) {
        auto u = guard(req).user();
        if (!u) return Response::json(R"({"error":"unauthenticated"})", 401);
        return Response::json("{\"id\":" + std::to_string(u->id) + ",\"username\":\"" + u->username +
                              "\"}");
    }

private:
    Guard guard(Request& req) const {
        auto it = req.cookies.find("lwsid");
        return Guard(*users_, Session(*store_, it == req.cookies.end() ? "" : it->second));
    }

    std::shared_ptr<SessionStore> store_;
    std::shared_ptr<UserProvider> users_;
};
