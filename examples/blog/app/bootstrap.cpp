// bootstrap.cpp — see bootstrap.hpp
#include "bootstrap.hpp"

#include <chrono>
#include <memory>

#include "article_controller.hpp"
#include "auth_routes.hpp"
#include "counter.hpp"
#include "dashboard.hpp"
#include "facade.hpp"
#include "facades.hpp"
#include "cache.hpp"
#include "livewire.hpp"
#include "livewire_client.hpp"
#include "http_auth.hpp"
#include "middleware.hpp"
#include "providers.hpp"
#include "todo_list.hpp"
#include "user_controller.hpp"

App bootstrap() {
    auto container = std::make_shared<Container>();
    auto router = std::make_shared<Router>();

    // Route → resolve controller from container, invoke action. (Laravel does this
    // reflectively; here it's an explicit closure, which is the honest map.) The
    // closure resolves lazily at request time, so it's fine to register routes
    // before the provider binds the controller.
    router->get(
        "/users/{id}",
        [container](Request& req) {
            return container->resolve<UserController>()->show(req);
        },
        {require_token("secret-token")} // per-route middleware
    );

    router->get("/health", [](Request&) { return Response{200, "ok"}; });

    // Smoke-test app: a small Articles resource (the feature-test surface).
    router->get("/articles",
                [container](Request& r) { return container->resolve<ArticleController>()->index(r); });
    router->get("/articles.html",
                [container](Request& r) { return container->resolve<ArticleController>()->html(r); });
    router->get("/articles/{id}",
                [container](Request& r) { return container->resolve<ArticleController>()->show(r); });
    router->post(
        "/articles",
        [container](Request& r) { return container->resolve<ArticleController>()->store(r); },
        {require_token("secret-token")}); // writes require auth

    // cpp-livewire: a stateful Counter component + its AJAX endpoint + JS client.
    auto livewire = std::make_shared<Livewire>();
    livewire->component("counter", [] { return std::make_unique<Counter>(); });
    livewire->component("dashboard", [] { return std::make_unique<Dashboard>(); });
    livewire->component("todos", [] { return std::make_unique<TodoList>(); });

    auto page = [](const std::string& title, const std::string& body) {
        Response r;
        r.headers["Content-Type"] = "text/html";
        r.body = "<!doctype html><html><head><title>" + title + "</title></head><body>" + body +
                 "<script src=\"/livewire.js\"></script></body></html>";
        return r;
    };

    router->get("/counter", [livewire, page](Request&) {
        return page("Counter", livewire->mount("counter"));
    });
    router->get("/dashboard", [livewire, page](Request&) {
        return page("Dashboard", livewire->mount("dashboard"));
    });
    router->get("/todos", [livewire, page](Request&) {
        return page("Todos", livewire->mount("todos"));
    });
    router->post("/livewire", [livewire](Request& req) {
        return Response::json(livewire->handle(req.body));
    });
    router->get("/livewire.js", [](Request&) {
        Response r;
        r.headers["Content-Type"] = "application/javascript";
        r.body = livewire_js();
        return r;
    });

    // Auth: a shared session store + user provider (seed one user), HTTP login flow.
    auto sessions = std::make_shared<ArraySessionStore>();
    auto users = std::make_shared<ArrayUserProvider>();
    users->add({1, "ada", "secret"});
    auto auth = std::make_shared<AuthController>(sessions, users);

    router->get("/login", [auth](Request& r) { return auth->show_login(r); });
    router->post("/login", [auth](Request& r) { return auth->login(r); },
                 {httpauth::verify_csrf(sessions)});
    router->post("/logout", [auth](Request& r) { return auth->logout(r); },
                 {httpauth::verify_csrf(sessions)});
    router->get("/me", [auth](Request& r) { return auth->me(r); },
                {httpauth::require_auth(sessions)});

    // Rate-limited endpoint: at most 3 requests per client per minute.
    auto cache = std::static_pointer_cast<CacheContract>(std::make_shared<ArrayCache>());
    router->get("/ping", [](Request&) { return Response::json(R"({"pong":true})"); },
                {httpauth::throttle(cache, 3, std::chrono::seconds(60))});

    auto kernel = std::make_shared<Kernel>(container, router);
    // Session middleware runs on every request (after logging).
    kernel->global_middleware({logging_mw, httpauth::session_middleware(sessions)});

    // Service providers register + boot the app's bindings (which use the autowiring
    // helpers internally). The Kernel runs them in two phases: register all, then boot all.
    kernel->register_providers({
        std::make_shared<UserServiceProvider>(container),
        std::make_shared<DatabaseServiceProvider>(container),
    });

    // Point facades (e.g. Users::find) at this container. Hidden global state —
    // see README's facade trade-off; injection is the default, this is opt-in.
    Facade::set_container(container);

    return App{container, router, kernel};
}
