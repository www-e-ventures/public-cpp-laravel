// test_coco_web.cpp — feature test of the coco-web reference app through
// kernel.handle(): static serving (MIME), the JSON API (ORM + query string), and
// token-gated writes. No sockets — the same way the blog feature tests run.
#include "test_harness.hpp"

#include <string>

#include "app.hpp"
#include "token.hpp"

namespace {
const std::string kSecret = "test-secret";

cocoweb::App make_app() { return cocoweb::bootstrap(COCO_WEB_PUBLIC_DIR, kSecret); }

Request req(std::string method, std::string path, std::string body = "",
            const std::string& token = "") {
    Request r;
    r.method = std::move(method);
    // Mirror what the HTTP server does: split the query string into req.query and
    // keep req.path clean (the router matches on the clean path).
    auto q = path.find('?');
    if (q != std::string::npos) {
        r.query = parse_pairs(path.substr(q + 1), '&', /*decode=*/true);
        path = path.substr(0, q);
    }
    r.path = std::move(path);
    r.body = std::move(body);
    if (!token.empty()) r.headers["Authorization"] = "Bearer " + token;
    return r;
}

bool has(const std::string& s, const std::string& n) { return s.find(n) != std::string::npos; }
} // namespace

TEST(cocoweb_serves_spa_index) {
    auto app = make_app();
    auto res = app.kernel->handle(req("GET", "/"));
    CHECK_EQ(res.status, 200);
    CHECK_EQ(res.headers.at("Content-Type"), std::string("text/html; charset=utf-8"));
    CHECK(has(res.body, "coco-web"));
}

TEST(cocoweb_serves_asset_with_mime) {
    auto app = make_app();
    auto res = app.kernel->handle(req("GET", "/assets/app.js"));
    CHECK_EQ(res.status, 200);
    CHECK_EQ(res.headers.at("Content-Type"), std::string("application/javascript"));
}

TEST(cocoweb_json_api_lists_items) {
    auto app = make_app();
    auto res = app.kernel->handle(req("GET", "/api/items"));
    CHECK_EQ(res.status, 200);
    CHECK_EQ(res.headers.at("Content-Type"), std::string("application/json"));
    CHECK(has(res.body, "first"));
    CHECK(has(res.body, "second"));
}

TEST(cocoweb_query_string_limits) {
    auto app = make_app();
    auto res = app.kernel->handle(req("GET", "/api/items?limit=1"));
    CHECK_EQ(res.status, 200);
    CHECK(has(res.body, "first"));
    CHECK(!has(res.body, "second")); // limited to 1
}

TEST(cocoweb_write_requires_token_with_ability) {
    auto app = make_app();
    // No token -> 403 from the requires_ability middleware.
    CHECK_EQ(app.kernel->handle(req("POST", "/api/items", "name=third")).status, 403);

    // A token lacking the ability -> 403.
    tok::Claims weak;
    weak.sub = "u";
    weak.abilities = {"items:read"};
    CHECK_EQ(app.kernel->handle(req("POST", "/api/items", "name=third", tok::sign(weak, kSecret)))
                 .status,
             403);

    // A token carrying items:write -> 201, and the item shows up in the list.
    tok::Claims ok;
    ok.sub = "u";
    ok.abilities = {"items:write"};
    auto created =
        app.kernel->handle(req("POST", "/api/items", "name=third", tok::sign(ok, kSecret)));
    CHECK_EQ(created.status, 201);
    CHECK(has(created.body, "third"));
    CHECK(has(app.kernel->handle(req("GET", "/api/items")).body, "third"));
}


TEST(cocoweb_savestate_binary_roundtrip) {
    auto app = make_app();

    // A machine-snapshot-shaped blob: NULs, high bytes, not valid UTF-8 anywhere.
    std::string blob;
    for (int i = 0; i < 512; ++i) blob.push_back(static_cast<char>(i % 256));

    // Writes are gated on the "saves:write" ability (requires_ability answers 403).
    CHECK_EQ(app.kernel->handle(req("POST", "/api/savestates?name=level1", blob)).status, 403);

    tok::Claims c;
    c.sub = "coco";
    c.abilities = {"saves:write"};
    auto created =
        app.kernel->handle(req("POST", "/api/savestates?name=level1", blob, tok::sign(c, kSecret)));
    CHECK_EQ(created.status, 201);
    auto idpos = created.body.find(":");
    std::string id = created.body.substr(idpos + 1, created.body.find("}") - idpos - 1);

    // GET returns the blob byte-for-byte via Response::bytes.
    auto got = app.kernel->handle(req("GET", "/api/savestates/" + id));
    CHECK_EQ(got.status, 200);
    CHECK_EQ(got.headers.at("Content-Type"), std::string("application/octet-stream"));
    CHECK_EQ(got.body.size(), blob.size());
    CHECK(got.body == blob); // byte-exact, incl. NULs + high bytes

    CHECK_EQ(app.kernel->handle(req("GET", "/api/savestates/999")).status, 404);
    CHECK_EQ(app.kernel->handle(req("GET", "/api/savestates/junk")).status, 422);
}

int main() { return RUN_ALL_TESTS(); }
