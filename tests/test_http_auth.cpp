// test_http_auth.cpp — the httpauth kit: session cookies, CSRF, login glue with
// session-fixation defense, and the fixed-window rate limiter.
#include "test_harness.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>

#include "auth.hpp"
#include "cache.hpp"
#include "http.hpp"
#include "http_auth.hpp"
#include "pipeline.hpp"
#include "session.hpp"

namespace {

// Run a request through one middleware into a canned handler.
Response run_mw(Middleware mw, Request& req, Response handler_result = Response{200, "ok"}) {
    return mw(req, [&](Request&) { return handler_result; });
}

std::string cookie_value(const Response& res, const std::string& name) {
    auto it = res.headers.find("Set-Cookie");
    if (it == res.headers.end()) return "";
    auto p = it->second.find(name + "=");
    if (p != 0) return ""; // the kit writes the session cookie first in the header
    auto start = name.size() + 1;
    auto end = it->second.find(';', start);
    return it->second.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

} // namespace

TEST(httpauth_cookie_header_carries_flags) {
    httpauth::CookieOptions o;
    o.secure = true;
    std::string h = httpauth::cookie_header("abc", o);
    CHECK_EQ(h, std::string("lwsid=abc; Path=/; HttpOnly; Secure; SameSite=Lax"));

    httpauth::CookieOptions bare;
    bare.name = "sid";
    bare.http_only = false;
    bare.same_site = "";
    CHECK_EQ(httpauth::cookie_header("x", bare), std::string("sid=x; Path=/"));
}

TEST(httpauth_constant_time_equal) {
    CHECK(httpauth::constant_time_equal("", ""));
    CHECK(httpauth::constant_time_equal("token", "token"));
    CHECK(!httpauth::constant_time_equal("token", "tokem"));
    CHECK(!httpauth::constant_time_equal("token", "toke")); // length differs
}

TEST(httpauth_session_middleware_issues_cookie_once) {
    auto store = std::make_shared<ArraySessionStore>();
    auto mw = httpauth::session_middleware(store);

    Request fresh{"GET", "/", {}, {}, ""};
    Response r1 = run_mw(mw, fresh);
    std::string sid = cookie_value(r1, "lwsid");
    CHECK_EQ(sid.size(), static_cast<std::size_t>(32)); // a CSPRNG id was minted

    Request returning{"GET", "/", {}, {{"Cookie", "lwsid=" + sid}}, ""};
    Response r2 = run_mw(mw, returning);
    CHECK(r2.headers.find("Set-Cookie") == r2.headers.end()); // known id: no re-issue
    CHECK_EQ(returning.cookies.at("lwsid"), sid);             // parsed for handlers
}

TEST(httpauth_session_middleware_does_not_clobber_handler_cookie) {
    auto store = std::make_shared<ArraySessionStore>();
    auto mw = httpauth::session_middleware(store);

    // A handler that set its own session cookie (attempt_login does, after
    // regenerating the id) must win over the middleware's fresh-session Set-Cookie.
    Request req{"GET", "/", {}, {}, ""};
    Response from_handler{200, "ok"};
    from_handler.headers["Set-Cookie"] = "lwsid=regenerated; Path=/; HttpOnly";
    Response out = run_mw(mw, req, from_handler);
    CHECK_EQ(cookie_value(out, "lwsid"), std::string("regenerated"));
}

TEST(httpauth_csrf_mint_and_verify) {
    auto store = std::make_shared<ArraySessionStore>();
    Session s(*store, "sid-1");
    std::string token = httpauth::csrf_token(s);
    CHECK_EQ(token.size(), static_cast<std::size_t>(32));       // CSPRNG
    CHECK_EQ(httpauth::csrf_token(s), token);                   // stable per session

    auto mw = httpauth::verify_csrf(store);
    Request good{"POST", "/x", {}, {}, "_token=" + token};
    good.cookies["lwsid"] = "sid-1";
    CHECK_EQ(run_mw(mw, good).status, 200);

    Request header_path{"POST", "/x", {}, {{"X-CSRF-Token", token}}, ""};
    header_path.cookies["lwsid"] = "sid-1";
    CHECK_EQ(run_mw(mw, header_path).status, 200);

    Request bad{"POST", "/x", {}, {}, "_token=wrong"};
    bad.cookies["lwsid"] = "sid-1";
    CHECK_EQ(run_mw(mw, bad).status, 419);

    Request no_session{"POST", "/x", {}, {}, "_token=" + token};
    CHECK_EQ(run_mw(mw, no_session).status, 419); // no session -> no expected token
}

TEST(httpauth_require_auth_gates_by_session) {
    auto store = std::make_shared<ArraySessionStore>();
    auto mw = httpauth::require_auth(store);

    Request anon{"GET", "/admin", {}, {}, ""};
    anon.cookies["lwsid"] = "sid-1";
    CHECK_EQ(run_mw(mw, anon).status, 401);

    store->put("sid-1", "auth_id", "1");
    Request known{"GET", "/admin", {}, {}, ""};
    known.cookies["lwsid"] = "sid-1";
    CHECK_EQ(run_mw(mw, known).status, 200);
}

TEST(httpauth_attempt_login_regenerates_and_stamps_cookie) {
    auto store = std::make_shared<ArraySessionStore>();
    ArrayUserProvider users;
    users.add({1, "ada", "secret"});

    Request req{"POST", "/login", {}, {}, ""};
    req.cookies["lwsid"] = "planted"; // pre-login id an attacker could know
    store->put("planted", "_csrf", "t"); // pre-login session data

    Response res{302, ""};
    CHECK(!httpauth::attempt_login(req, res, *store, users, "ada", "wrong"));
    CHECK(res.headers.find("Set-Cookie") == res.headers.end()); // failure: no cookie

    CHECK(httpauth::attempt_login(req, res, *store, users, "ada", "secret"));
    std::string fresh = cookie_value(res, "lwsid");
    CHECK_EQ(fresh.size(), static_cast<std::size_t>(32));
    CHECK(fresh != std::string("planted"));
    CHECK_EQ(req.cookies.at("lwsid"), fresh);            // downstream sees the new id
    CHECK(!store->has("planted", "auth_id"));            // the planted id is dead
    CHECK(store->has(fresh, "auth_id"));                 // the fresh id is logged in
    auto csrf = store->get(fresh, "_csrf");
    CHECK(csrf.has_value());                             // session data moved along
}

TEST(httpauth_logout_flushes_session) {
    auto store = std::make_shared<ArraySessionStore>();
    store->put("sid-1", "auth_id", "1");
    store->put("sid-1", "_csrf", "t");

    Request req{"POST", "/logout", {}, {}, ""};
    req.cookies["lwsid"] = "sid-1";
    httpauth::logout(req, *store);
    CHECK(!store->has("sid-1", "auth_id"));
    CHECK(!store->has("sid-1", "_csrf"));
}

TEST(httpauth_throttle_blocks_over_budget_and_keys_by_ip) {
    auto cache = std::static_pointer_cast<CacheContract>(std::make_shared<ArrayCache>());
    auto mw = httpauth::throttle(cache, 2, std::chrono::seconds(60));

    Request a{"GET", "/ping", {}, {}, ""};
    a.remote_addr = "10.0.0.1";
    CHECK_EQ(run_mw(mw, a).status, 200);
    CHECK_EQ(run_mw(mw, a).status, 200);
    Response blocked = run_mw(mw, a);
    CHECK_EQ(blocked.status, 429); // over budget
    CHECK(blocked.headers.count("Retry-After") == 1);

    Request b{"GET", "/ping", {}, {}, ""};
    b.remote_addr = "10.0.0.2"; // a different peer has its own budget
    CHECK_EQ(run_mw(mw, b).status, 200);
}

TEST(httpauth_throttle_window_rolls_over) {
    auto cache = std::static_pointer_cast<CacheContract>(std::make_shared<ArrayCache>());
    auto mw = httpauth::throttle(cache, 1, std::chrono::seconds(1));

    Request req{"GET", "/ping", {}, {}, ""};
    req.remote_addr = "10.0.0.3";
    CHECK_EQ(run_mw(mw, req).status, 200);
    CHECK_EQ(run_mw(mw, req).status, 429); // budget spent

    // The window boundary lives in the cached VALUE, so it rolls over even though
    // every hit re-put the entry (the old implementation reset the TTL each hit and
    // sustained traffic never unblocked).
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    CHECK_EQ(run_mw(mw, req).status, 200);
}
