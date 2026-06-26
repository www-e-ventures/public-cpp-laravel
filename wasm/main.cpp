// wasm/main.cpp — exposes the C++ request lifecycle to JavaScript via Emscripten.
//
// The same framework (container, router, middleware, kernel, repository ORM with the
// in-memory backend, Blade) is compiled to WebAssembly and runs entirely in the page —
// JS hands a request to lipp_handle() and gets the response back, no network involved.
// The socket HTTP server is left out (no sockets in the browser); everything above it
// is pure compute and compiles unchanged.
// Builds two ways: to WASM via Emscripten, and natively into a shared library that
// any C-FFI host (PHP, Python, a C++ host, ...) can call — same C entry points.
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include <memory>
#include <string>
#include <utility>

#include "container.hpp"
#include "database.hpp"
#include "kernel.hpp"
#include "router.hpp"

#include "article_controller.hpp"
#include "models/article.hpp"

namespace {

struct App {
    std::shared_ptr<Container> container;
    std::shared_ptr<Router> router;
    std::shared_ptr<Kernel> kernel;
};

App build() {
    auto c = std::make_shared<Container>();
    c->singleton<Connection>([](ContainerContract&) {
        return std::static_pointer_cast<Connection>(std::make_shared<MemoryConnection>());
    });
    c->bind_auto<ArticleRepository, Connection>();
    c->bind_auto<ArticleController, ArticleRepository>();

    auto repo = c->resolve<ArticleRepository>();
    Article a1{0, "Hello from compiled C++", 7, true};
    Article a2{0, "Running in your browser via WebAssembly", 42, true};
    repo->insert(a1);
    repo->insert(a2);

    auto r = std::make_shared<Router>();
    r->get("/articles", [c](Request& rq) { return c->resolve<ArticleController>()->index(rq); });
    r->get("/articles/{id}", [c](Request& rq) { return c->resolve<ArticleController>()->show(rq); });
    r->post("/articles", [c](Request& rq) { return c->resolve<ArticleController>()->store(rq); });
    r->get("/articles.html", [c](Request& rq) { return c->resolve<ArticleController>()->html(rq); });

    auto k = std::make_shared<Kernel>(c, r);
    return {c, r, k};
}

App& app() {
    static App instance = build();
    return instance;
}

int g_status = 200;
std::string g_body; // keeps the last response alive for the returned char*

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
const char* lipp_handle(const char* method, const char* path, const char* body) {
    Request req;
    req.method = method ? method : "GET";
    req.path = path ? path : "/";
    req.body = body ? body : "";
    Response res = app().kernel->handle(std::move(req));
    g_status = res.status;
    g_body = res.body;
    return g_body.c_str();
}

EMSCRIPTEN_KEEPALIVE
int lipp_status() { return g_status; }
}
