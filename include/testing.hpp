// testing.hpp — Laravel-flavoured HTTP testing support
// Laravel: Illuminate\Testing\TestResponse + the TestCase get()/post() helpers.
//
// Lets feature tests read like Laravel/Pest tests:
//   HttpClient http(*app.kernel, __fails);
//   http.get("/articles").assertOk().assertExactBody("[]");
//   http.withToken("secret").post("/articles", "title=Hi").assertCreated().assertSee("Hi");
//
// Test-only and header-only: zero cost to the shipped binaries. Fluent assertions
// report into the harness's per-test failure counter (pass the test's __fails sink).
#pragma once
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>

#include "http.hpp"
#include "kernel.hpp"

class TestResponse {
public:
    TestResponse(Response res, int& fails, std::string label)
        : res_(std::move(res)), fails_(&fails), label_(std::move(label)) {}

    TestResponse& assertStatus(int expected) {
        return check(res_.status == expected,
                     "assertStatus(" + std::to_string(expected) + ") — got " +
                         std::to_string(res_.status));
    }
    TestResponse& assertOk() { return assertStatus(200); }
    TestResponse& assertCreated() { return assertStatus(201); }
    TestResponse& assertNotFound() { return assertStatus(404); }
    TestResponse& assertUnauthorized() { return assertStatus(401); }
    TestResponse& assertUnprocessable() { return assertStatus(422); }

    TestResponse& assertSee(const std::string& text) {
        return check(res_.body.find(text) != std::string::npos, "assertSee(\"" + text + "\")");
    }
    TestResponse& assertDontSee(const std::string& text) {
        return check(res_.body.find(text) == std::string::npos, "assertDontSee(\"" + text + "\")");
    }
    // Substring match against the (hand-rolled) JSON body — good enough here.
    TestResponse& assertJsonFragment(const std::string& fragment) {
        return check(res_.body.find(fragment) != std::string::npos,
                     "assertJsonFragment(" + fragment + ")");
    }
    TestResponse& assertExactBody(const std::string& body) {
        return check(res_.body == body, "assertExactBody — got \"" + res_.body + "\"");
    }
    TestResponse& assertHeader(const std::string& key, const std::string& value) {
        auto it = res_.headers.find(key);
        return check(it != res_.headers.end() && it->second == value,
                     "assertHeader(" + key + ": " + value + ")");
    }

    const Response& raw() const { return res_; }

private:
    TestResponse& check(bool ok, const std::string& what) {
        if (!ok) {
            std::cerr << "    FAIL [" << label_ << "] " << what << "\n"
                      << "         body: " << res_.body << "\n";
            ++(*fails_);
        }
        return *this;
    }

    Response res_;
    int* fails_;
    std::string label_;
};

class HttpClient {
public:
    HttpClient(Kernel& kernel, int& fails) : kernel_(&kernel), fails_(&fails) {}

    // Persisted across subsequent requests (cf. Laravel's actingAs / withToken).
    HttpClient& withToken(const std::string& token) {
        default_headers_["Authorization"] = token;
        return *this;
    }
    HttpClient& withHeader(const std::string& key, const std::string& value) {
        default_headers_[key] = value;
        return *this;
    }

    TestResponse get(const std::string& uri) { return send("GET", uri, ""); }
    TestResponse post(const std::string& uri, const std::string& body = "") {
        return send("POST", uri, body);
    }

private:
    TestResponse send(const std::string& method, const std::string& uri, const std::string& body) {
        Request req;
        req.method = method;
        req.path = uri;
        req.body = body;
        req.headers = default_headers_;
        return TestResponse(kernel_->handle(std::move(req)), *fails_, method + " " + uri);
    }

    Kernel* kernel_;
    int* fails_;
    std::unordered_map<std::string, std::string> default_headers_;
};
