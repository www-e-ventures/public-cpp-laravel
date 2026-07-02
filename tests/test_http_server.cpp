// test_http_server.cpp — pure parse/format functions of the HTTP server.
// The socket loop is verified manually (cpp-artisan serve + curl); the parsing and
// formatting — the parts with real logic — are unit-tested here.
#include "test_harness.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "http.hpp"
#include "http_server.hpp"
#include "kernel_contract.hpp"

namespace {
bool has(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }
struct StubKernel : KernelContract {
    Response handle(Request) override { return Response{200, "ok"}; }
};
} // namespace

TEST(http_parse_request_line_and_headers) {
    auto req = parse_http_request(
        "GET /articles?page=2 HTTP/1.1\r\nHost: localhost\r\nAuthorization: secret-token\r\n\r\n");
    CHECK(req.has_value());
    CHECK_EQ(req->method, std::string("GET"));
    CHECK_EQ(req->path, std::string("/articles"));     // path stays query-free
    CHECK_EQ(req->query.at("page"), std::string("2"));  // query parsed into req->query
    CHECK_EQ(req->headers.at("Authorization"), std::string("secret-token"));
}

TEST(http_url_decode_percent_and_plus) {
    CHECK_EQ(url_decode("hello+world"), std::string("hello world"));
    CHECK_EQ(url_decode("a%20b%2Fc"), std::string("a b/c"));
    CHECK_EQ(url_decode("100%25"), std::string("100%"));
    CHECK_EQ(url_decode("a+b", /*plus_as_space=*/false), std::string("a+b")); // keep '+'
    CHECK_EQ(url_decode("bad%2"), std::string("bad%2"));  // malformed escape left as-is
}

TEST(http_parse_form_url_decodes) {
    auto f = parse_form("title=hello+world&path=%2Fapi%2Fx&raw=a%26b");
    CHECK_EQ(f.at("title"), std::string("hello world"));
    CHECK_EQ(f.at("path"), std::string("/api/x"));
    CHECK_EQ(f.at("raw"), std::string("a&b")); // %26 is a literal '&', not a separator
}

TEST(http_parse_query_string_into_request) {
    auto req = parse_http_request(
        "GET /api/messages?since=42&q=hello+world&limit=20 HTTP/1.1\r\nHost: x\r\n\r\n");
    CHECK(req.has_value());
    CHECK_EQ(req->path, std::string("/api/messages"));
    CHECK_EQ(req->query.at("since"), std::string("42"));
    CHECK_EQ(req->query.at("q"), std::string("hello world"));
    CHECK_EQ(req->query.at("limit"), std::string("20"));
}

TEST(http_no_query_leaves_empty_map) {
    auto req = parse_http_request("GET /health HTTP/1.1\r\nHost: x\r\n\r\n");
    CHECK(req.has_value());
    CHECK(req->query.empty());
}

TEST(http_parse_body) {
    auto req = parse_http_request(
        "POST /articles HTTP/1.1\r\nContent-Type: text/plain\r\n\r\ntitle=Hi&published=1");
    CHECK(req.has_value());
    CHECK_EQ(req->method, std::string("POST"));
    CHECK_EQ(req->body, std::string("title=Hi&published=1"));
}

TEST(http_parse_malformed_returns_nullopt) {
    auto req = parse_http_request("GARBAGE\r\n\r\n");
    CHECK(!req.has_value());
}

TEST(http_format_response_sets_status_and_length) {
    std::string out = format_http_response(Response::json("[]"));
    CHECK(has(out, "HTTP/1.1 200 OK"));
    CHECK(has(out, "Content-Type: application/json"));
    CHECK(has(out, "Content-Length: 2"));
    CHECK(has(out, "\r\n\r\n[]"));
}

TEST(http_format_response_reason_phrases) {
    CHECK(has(format_http_response(Response{404, "x"}), "404 Not Found"));
    CHECK(has(format_http_response(Response{201, "x"}), "201 Created"));
}

// stop() must break a running serve() from another thread. Written to be
// hang-proof: it retries stop() a bounded number of times and detaches rather
// than block the suite if a regression ever leaves serve() stuck.
TEST(http_server_stop_ends_serve) {
    auto kernel = std::make_shared<StubKernel>();
    HttpServer server(kernel, 48771);
    std::atomic<bool> done{false};
    int result = 1;
    std::thread t([&] {
        result = server.serve(2);
        done = true;
    });

    // A stop() that lands before serve() finished binding is a no-op; the next
    // iteration takes effect once the accept loop is up. Bounded at ~2s.
    for (int i = 0; i < 200 && !done; ++i) {
        server.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    CHECK(done.load());
    if (done.load()) {
        t.join();
        CHECK_EQ(result, 0);
    } else {
        t.detach(); // serve() hung — don't wedge the rest of the suite
    }
}

TEST(http_content_length_is_case_insensitive) {
    // Any casing counts (RFC 9110); previously only two exact spellings were seen,
    // so "Content-length: 5" under-read the body.
    std::string head = "POST /x HTTP/1.1\r\nContent-length: 5\r\nHost: x";
    auto n = parse_content_length(head);
    CHECK(n.has_value());
    CHECK_EQ(*n, static_cast<std::size_t>(5));

    CHECK(!parse_content_length("GET /x HTTP/1.1\r\nHost: x").has_value()); // absent
    CHECK(!parse_content_length("GET /x HTTP/1.1\r\nContent-Length: abc").has_value());
    CHECK(!parse_content_length("GET /x HTTP/1.1\r\nContent-Length: -1").has_value());
}

TEST(http_content_length_matches_header_names_only) {
    // A header VALUE containing the words must not be mistaken for the header.
    std::string head = "GET /x HTTP/1.1\r\nX-Note: content-length: 999\r\nContent-Length: 7";
    auto n = parse_content_length(head);
    CHECK(n.has_value());
    CHECK_EQ(*n, static_cast<std::size_t>(7));
}

TEST(http_detects_chunked_transfer_encoding) {
    CHECK(has_chunked_body("POST /x HTTP/1.1\r\nTransfer-Encoding: chunked"));
    CHECK(has_chunked_body("POST /x HTTP/1.1\r\ntransfer-encoding: gzip, Chunked"));
    CHECK(!has_chunked_body("POST /x HTTP/1.1\r\nContent-Length: 3"));
}

TEST(http_format_response_new_reason_phrases) {
    CHECK(has(format_http_response(Response{302, "x"}), "302 Found"));
    CHECK(has(format_http_response(Response{400, "x"}), "400 Bad Request"));
    CHECK(has(format_http_response(Response{405, "x"}), "405 Method Not Allowed"));
    CHECK(has(format_http_response(Response{411, "x"}), "411 Length Required"));
    CHECK(has(format_http_response(Response{413, "x"}), "413 Payload Too Large"));
    CHECK(has(format_http_response(Response{431, "x"}), "431 Request Header Fields Too Large"));
}
