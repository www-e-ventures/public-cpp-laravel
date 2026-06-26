// test_http_server.cpp — pure parse/format functions of the HTTP server.
// The socket loop is verified manually (cpp-artisan serve + curl); the parsing and
// formatting — the parts with real logic — are unit-tested here.
#include "test_harness.hpp"

#include <string>

#include "http.hpp"
#include "http_server.hpp"

namespace {
bool has(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }
} // namespace

TEST(http_parse_request_line_and_headers) {
    auto req = parse_http_request(
        "GET /articles?page=2 HTTP/1.1\r\nHost: localhost\r\nAuthorization: secret-token\r\n\r\n");
    CHECK(req.has_value());
    CHECK_EQ(req->method, std::string("GET"));
    CHECK_EQ(req->path, std::string("/articles")); // query string dropped
    CHECK_EQ(req->headers.at("Authorization"), std::string("secret-token"));
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
