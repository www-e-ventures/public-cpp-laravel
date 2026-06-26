// test_feature_auth.cpp — the HTTP auth flow end-to-end (login -> cookie -> /me),
// including CSRF protection on the login POST.
#include "test_harness.hpp"

#include <string>

#include "bootstrap.hpp"
#include "testing.hpp"

namespace {
std::string sid_from(const std::string& set_cookie) {
    auto p = set_cookie.find("lwsid=");
    if (p == std::string::npos) return "";
    p += 6;
    auto e = set_cookie.find(';', p);
    return set_cookie.substr(p, e == std::string::npos ? std::string::npos : e - p);
}
std::string token_from(const std::string& html) {
    std::string needle = "name=\"_token\" value=\"";
    auto p = html.find(needle);
    if (p == std::string::npos) return "";
    p += needle.size();
    return html.substr(p, html.find('"', p) - p);
}

// GET /login to obtain a fresh session id + its CSRF token.
struct LoginPage {
    std::string sid;
    std::string token;
};
LoginPage open_login(Kernel& kernel, int& fails) {
    HttpClient http(kernel, fails);
    auto page = http.get("/login");
    std::string sc =
        page.raw().headers.count("Set-Cookie") ? page.raw().headers.at("Set-Cookie") : "";
    return {sid_from(sc), token_from(page.raw().body)};
}
} // namespace

TEST(auth_login_with_csrf_then_me_is_authorized) {
    auto app = bootstrap();
    auto lp = open_login(*app.kernel, __fails);
    CHECK(!lp.sid.empty());
    CHECK(!lp.token.empty());

    HttpClient http(*app.kernel, __fails);
    http.withHeader("Cookie", "lwsid=" + lp.sid);
    http.post("/login", "username=ada&password=secret&_token=" + lp.token)
        .assertStatus(302)
        .assertHeader("Location", "/me");
    http.get("/me").assertOk().assertSee("ada");
}

TEST(auth_login_without_csrf_token_is_rejected) {
    auto app = bootstrap();
    auto lp = open_login(*app.kernel, __fails);

    HttpClient http(*app.kernel, __fails);
    http.withHeader("Cookie", "lwsid=" + lp.sid);
    http.post("/login", "username=ada&password=secret") // no _token
        .assertStatus(419)
        .assertSee("CSRF");
}

TEST(auth_me_requires_authentication) {
    auto app = bootstrap();
    HttpClient http(*app.kernel, __fails);
    http.get("/me").assertStatus(401).assertSee("unauthenticated");
}

TEST(auth_login_rejects_bad_credentials) {
    auto app = bootstrap();
    auto lp = open_login(*app.kernel, __fails);

    HttpClient http(*app.kernel, __fails);
    http.withHeader("Cookie", "lwsid=" + lp.sid);
    http.post("/login", "username=ada&password=wrong&_token=" + lp.token).assertStatus(401);
}

TEST(auth_logout_clears_session) {
    auto app = bootstrap();
    auto lp = open_login(*app.kernel, __fails);

    HttpClient http(*app.kernel, __fails);
    http.withHeader("Cookie", "lwsid=" + lp.sid);
    http.post("/login", "username=ada&password=secret&_token=" + lp.token).assertStatus(302);
    http.get("/me").assertOk();
    http.post("/logout", "_token=" + lp.token).assertStatus(302);
    http.get("/me").assertStatus(401);
}
