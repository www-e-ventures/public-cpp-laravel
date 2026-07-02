// auth_routes.hpp — the example's login/logout/me controller, on the framework's
// httpauth kit (session cookie middleware, CSRF, guards, and the fixation-safe
// login live in include/http_auth.hpp; this file is just the app-side wiring).
#pragma once
#include <memory>
#include <string>
#include <utility>

#include "auth.hpp"
#include "http.hpp"
#include "http_auth.hpp"
#include "session.hpp"

class AuthController {
public:
    AuthController(std::shared_ptr<SessionStore> store, std::shared_ptr<UserProvider> users)
        : store_(std::move(store)), users_(std::move(users)) {}

    Response show_login(Request& req) {
        Session s = httpauth::session_of(req, *store_);
        std::string token = httpauth::csrf_token(s); // embed it so the POST can echo it back

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
        Response r = Response::redirect("/me");
        // attempt_login regenerates the session id on success (fixation defense)
        // and stamps the new cookie onto the response it's handed.
        if (httpauth::attempt_login(req, r, *store_, *users_, form["username"], form["password"]))
            return r;
        return Response::json(R"({"error":"invalid credentials"})", 401);
    }

    Response logout(Request& req) {
        httpauth::logout(req, *store_);
        return Response::redirect("/login");
    }

    Response me(Request& req) {
        Guard g(*users_, httpauth::session_of(req, *store_));
        auto u = g.user();
        if (!u) return Response::json(R"({"error":"unauthenticated"})", 401);
        return Response::json("{\"id\":" + std::to_string(u->id) + ",\"username\":\"" + u->username +
                              "\"}");
    }

private:
    std::shared_ptr<SessionStore> store_;
    std::shared_ptr<UserProvider> users_;
};
