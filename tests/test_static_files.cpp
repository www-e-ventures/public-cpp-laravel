// test_static_files.cpp — static serving: MIME, byte-exact bodies (incl. NULs),
// ETag/304, byte-range (206/416), and path-traversal refusal. Uses a temp dir.
#include "test_harness.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include "http.hpp"
#include "static_files.hpp"

namespace {
namespace fs = std::filesystem;

// A fresh temp dir with two files; the .wasm body carries embedded NULs so we can
// prove the path is byte-safe (not truncated at the first '\0').
struct Fixture {
    fs::path dir;
    std::string wasm_body;
    Fixture() {
        dir = fs::temp_directory_path() / "cpplaravel_static_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir / "sub", ec);
        wasm_body = std::string(1, '\0') + "hello" + std::string(1, '\0') + "world"; // 12 bytes
        std::ofstream(dir / "app.wasm", std::ios::binary)
            .write(wasm_body.data(), static_cast<std::streamsize>(wasm_body.size()));
        std::ofstream(dir / "sub" / "page.html") << "<h1>hi</h1>";
    }
    ~Fixture() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

Request get(const std::string& hdr = "", const std::string& val = "") {
    Request r;
    r.method = "GET";
    if (!hdr.empty()) r.headers[hdr] = val;
    return r;
}
} // namespace

TEST(static_mime_types) {
    CHECK_EQ(staticfiles::mime_type("x/app.wasm"), std::string("application/wasm"));
    CHECK_EQ(staticfiles::mime_type("a.JS"), std::string("application/javascript"));
    CHECK_EQ(staticfiles::mime_type("index.html"), std::string("text/html; charset=utf-8"));
    CHECK_EQ(staticfiles::mime_type("noext"), std::string("application/octet-stream"));
}

TEST(static_serves_binary_byte_exact_with_headers) {
    Fixture fx;
    auto res = staticfiles::serve(fx.dir.string(), "app.wasm", get());
    CHECK_EQ(res.status, 200);
    CHECK_EQ(res.headers.at("Content-Type"), std::string("application/wasm"));
    CHECK_EQ(res.body, fx.wasm_body); // byte-exact, including the embedded NULs
    CHECK_EQ(res.body.size(), fx.wasm_body.size());
    CHECK(res.headers.count("ETag") == 1);
    CHECK_EQ(res.headers.at("Accept-Ranges"), std::string("bytes"));
}

TEST(static_serves_nested_path) {
    Fixture fx;
    auto res = staticfiles::serve(fx.dir.string(), "sub/page.html", get());
    CHECK_EQ(res.status, 200);
    CHECK_EQ(res.body, std::string("<h1>hi</h1>"));
    CHECK_EQ(res.headers.at("Content-Type"), std::string("text/html; charset=utf-8"));
}

TEST(static_missing_file_is_404) {
    Fixture fx;
    CHECK_EQ(staticfiles::serve(fx.dir.string(), "nope.txt", get()).status, 404);
}

TEST(static_rejects_traversal) {
    Fixture fx;
    CHECK_EQ(staticfiles::serve(fx.dir.string(), "../../etc/passwd", get()).status, 404);
    CHECK_EQ(staticfiles::serve(fx.dir.string(), "/etc/passwd", get()).status, 404);
}

TEST(static_range_returns_206_partial) {
    Fixture fx;
    auto res = staticfiles::serve(fx.dir.string(), "app.wasm", get("Range", "bytes=0-3"));
    CHECK_EQ(res.status, 206);
    CHECK_EQ(res.body.size(), static_cast<std::size_t>(4));
    CHECK_EQ(res.body, fx.wasm_body.substr(0, 4));
    CHECK_EQ(res.headers.at("Content-Range"),
             std::string("bytes 0-3/") + std::to_string(fx.wasm_body.size()));
}

TEST(static_suffix_range) {
    Fixture fx;
    auto res = staticfiles::serve(fx.dir.string(), "app.wasm", get("Range", "bytes=-5"));
    CHECK_EQ(res.status, 206);
    CHECK_EQ(res.body, fx.wasm_body.substr(fx.wasm_body.size() - 5));
}

TEST(static_unsatisfiable_range_is_416) {
    Fixture fx;
    auto res = staticfiles::serve(fx.dir.string(), "app.wasm", get("Range", "bytes=9999-"));
    CHECK_EQ(res.status, 416);
    CHECK(res.headers.count("Content-Range") == 1);
}

TEST(static_if_none_match_is_304) {
    Fixture fx;
    auto etag = staticfiles::serve(fx.dir.string(), "app.wasm", get()).headers.at("ETag");
    auto res = staticfiles::serve(fx.dir.string(), "app.wasm", get("If-None-Match", etag));
    CHECK_EQ(res.status, 304);
    CHECK(res.body.empty());
}
