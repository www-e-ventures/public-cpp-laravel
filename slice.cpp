// slice.cpp — Laravel-in-C++ vertical slice
// Container (type-erased) + Contracts + Router + Middleware pipeline
// C++20.  g++ -std=c++20 -O2 slice.cpp -o slice
#include <any>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// HTTP contracts  (Laravel: Illuminate\Http\{Request,Response})
// ─────────────────────────────────────────────────────────────────────────────
struct Request {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> route_params; // {id => 42}
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct Response {
    int status = 200;
    std::string body;
    std::unordered_map<std::string, std::string> headers{{"Content-Type", "text/plain"}};

    static Response json(const std::string& payload, int status = 200) {
        Response r;
        r.status = status;
        r.body = payload;
        r.headers["Content-Type"] = "application/json";
        return r;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Container  (Laravel: Illuminate\Container\Container)
// Type-erased: factories keyed by std::type_index, resolve<T>() casts back.
// Supports bind (transient) and singleton (resolved once, memoised).
// ─────────────────────────────────────────────────────────────────────────────
class Container {
public:
    // Transient binding: factory runs every resolve().
    template <typename T>
    void bind(std::function<std::shared_ptr<T>(Container&)> factory) {
        factories_[std::type_index(typeid(T))] =
            [factory](Container& c) -> std::any { return factory(c); };
    }

    // Singleton: factory runs once, result memoised.
    template <typename T>
    void singleton(std::function<std::shared_ptr<T>(Container&)> factory) {
        auto key = std::type_index(typeid(T));
        factories_[key] = [factory, this, key](Container& c) -> std::any {
            auto it = instances_.find(key);
            if (it != instances_.end()) return it->second;
            std::any made = factory(c);
            instances_[key] = made;
            return made;
        };
    }

    // Register an already-built instance as a singleton.
    template <typename T>
    void instance(std::shared_ptr<T> obj) {
        instances_[std::type_index(typeid(T))] = obj;
        factories_[std::type_index(typeid(T))] =
            [obj](Container&) -> std::any { return obj; };
    }

    template <typename T>
    std::shared_ptr<T> resolve() {
        auto key = std::type_index(typeid(T));
        auto it = factories_.find(key);
        if (it == factories_.end())
            throw std::runtime_error(std::string("Unresolved binding: ") + typeid(T).name());
        std::any made = it->second(*this);
        return std::any_cast<std::shared_ptr<T>>(made);
    }

    template <typename T>
    bool bound() const {
        return factories_.count(std::type_index(typeid(T))) > 0;
    }

private:
    std::unordered_map<std::type_index, std::function<std::any(Container&)>> factories_;
    std::unordered_map<std::type_index, std::any> instances_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Middleware pipeline  (Laravel: Illuminate\Pipeline\Pipeline)
// Each middleware: (Request&, Next) -> Response.  Composed right-to-left so the
// first registered middleware is the outermost layer.
// ─────────────────────────────────────────────────────────────────────────────
using Next = std::function<Response(Request&)>;
using Middleware = std::function<Response(Request&, Next)>;
using Handler = std::function<Response(Request&)>;

class Pipeline {
public:
    Pipeline& through(std::vector<Middleware> mws) {
        middlewares_ = std::move(mws);
        return *this;
    }
    Response run(Request& req, Handler dest) {
        Next next = dest; // innermost is the route handler
        for (auto it = middlewares_.rbegin(); it != middlewares_.rend(); ++it) {
            Middleware mw = *it;
            Next prev = next;
            next = [mw, prev](Request& r) { return mw(r, prev); };
        }
        return next(req);
    }

private:
    std::vector<Middleware> middlewares_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Router  (Laravel: Illuminate\Routing\Router)
// {param} segments compile to a regex with named-ish capture, params extracted
// into Request.route_params.  Per-route middleware stack supported.
// ─────────────────────────────────────────────────────────────────────────────
struct Route {
    std::string method;
    std::regex pattern;
    std::vector<std::string> param_names;
    Handler handler;
    std::vector<Middleware> middleware;
};

class Router {
public:
    void add(const std::string& method, const std::string& uri, Handler h,
             std::vector<Middleware> mw = {}) {
        std::vector<std::string> names;
        std::string rx = "^";
        std::regex seg(R"(\{(\w+)\})");
        auto begin = std::sregex_iterator(uri.begin(), uri.end(), seg);
        auto end = std::sregex_iterator();
        std::size_t last = 0;
        for (auto it = begin; it != end; ++it) {
            auto m = *it;
            rx += escape(uri.substr(last, m.position() - last));
            rx += "([^/]+)";
            names.push_back(m[1].str());
            last = m.position() + m.length();
        }
        rx += escape(uri.substr(last)) + "$";
        routes_.push_back({method, std::regex(rx), names, std::move(h), std::move(mw)});
    }

    void get(const std::string& uri, Handler h, std::vector<Middleware> mw = {}) {
        add("GET", uri, std::move(h), std::move(mw));
    }
    void post(const std::string& uri, Handler h, std::vector<Middleware> mw = {}) {
        add("POST", uri, std::move(h), std::move(mw));
    }

    std::optional<std::pair<Route, std::unordered_map<std::string, std::string>>>
    match(const Request& req) const {
        for (const auto& r : routes_) {
            if (r.method != req.method) continue;
            std::smatch m;
            if (std::regex_match(req.path, m, r.pattern)) {
                std::unordered_map<std::string, std::string> params;
                for (std::size_t i = 0; i < r.param_names.size(); ++i)
                    params[r.param_names[i]] = m[i + 1].str();
                return std::make_pair(r, params);
            }
        }
        return std::nullopt;
    }

private:
    static std::string escape(const std::string& s) {
        static const std::regex special(R"([.^$|()\[\]{}*+?\\])");
        return std::regex_replace(s, special, R"(\$&)");
    }
    std::vector<Route> routes_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Kernel  (Laravel: Illuminate\Foundation\Http\Kernel)
// Ties container → router → global middleware → pipeline.
// ─────────────────────────────────────────────────────────────────────────────
class Kernel {
public:
    Kernel(std::shared_ptr<Container> c, std::shared_ptr<Router> r)
        : container_(std::move(c)), router_(std::move(r)) {}

    void global_middleware(std::vector<Middleware> mw) { global_ = std::move(mw); }

    Response handle(Request req) {
        auto matched = router_->match(req);
        if (!matched) return Response{404, "Not Found"};
        auto [route, params] = *matched;
        req.route_params = std::move(params);

        std::vector<Middleware> stack = global_;
        stack.insert(stack.end(), route.middleware.begin(), route.middleware.end());

        Pipeline pipe;
        return pipe.through(stack).run(req, route.handler);
    }

private:
    std::shared_ptr<Container> container_;
    std::shared_ptr<Router> router_;
    std::vector<Middleware> global_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Application code — proves the seams with a real service + controller.
// ─────────────────────────────────────────────────────────────────────────────

// A domain service resolved from the container (Laravel: an injected service).
class UserService {
public:
    std::string find(const std::string& id) {
        if (id == "42") return R"({"id":42,"name":"Ada Lovelace"})";
        return "";
    }
};

// A controller that depends on UserService — pulled from the container.
class UserController {
public:
    explicit UserController(std::shared_ptr<UserService> svc) : svc_(std::move(svc)) {}
    Response show(Request& req) {
        auto id = req.route_params["id"];
        auto user = svc_->find(id);
        if (user.empty())
            return Response::json(R"({"error":"not found"})", 404);
        return Response::json(user);
    }

private:
    std::shared_ptr<UserService> svc_;
};

// Middleware examples.
Response logging_mw(Request& req, Next next) {
    std::cerr << "[log] --> " << req.method << " " << req.path << "\n";
    Response res = next(req);
    std::cerr << "[log] <-- " << res.status << "\n";
    return res;
}

Middleware require_token(const std::string& expected) {
    return [expected](Request& req, Next next) -> Response {
        auto it = req.headers.find("Authorization");
        if (it == req.headers.end() || it->second != expected)
            return Response::json(R"({"error":"unauthorized"})", 401);
        return next(req);
    };
}

// ─────────────────────────────────────────────────────────────────────────────
int main() {
    auto container = std::make_shared<Container>();

    // Service providers register bindings.
    container->singleton<UserService>(
        [](Container&) { return std::make_shared<UserService>(); });

    container->bind<UserController>([](Container& c) {
        return std::make_shared<UserController>(c.resolve<UserService>());
    });

    auto router = std::make_shared<Router>();

    // Route → resolve controller from container, invoke action. (Laravel does
    // this reflectively; here it's an explicit closure, which is the honest map.)
    router->get(
        "/users/{id}",
        [container](Request& req) {
            return container->resolve<UserController>()->show(req);
        },
        {require_token("secret-token")} // per-route middleware
    );

    router->get("/health", [](Request&) { return Response{200, "ok"}; });

    Kernel kernel(container, router);
    kernel.global_middleware({logging_mw});

    auto dump = [](const std::string& label, const Response& r) {
        std::cout << label << "  status=" << r.status
                  << "  ct=" << r.headers.at("Content-Type")
                  << "  body=" << r.body << "\n";
    };

    // 1. Happy path, authorised.
    dump("auth+found ", kernel.handle({"GET", "/users/42", {}, {{"Authorization", "secret-token"}}, ""}));
    // 2. Authorised but missing record.
    dump("auth+miss  ", kernel.handle({"GET", "/users/7", {}, {{"Authorization", "secret-token"}}, ""}));
    // 3. Missing token → middleware short-circuits before controller.
    dump("no-token   ", kernel.handle({"GET", "/users/42", {}, {}, ""}));
    // 4. Unrouted path.
    dump("404        ", kernel.handle({"GET", "/nope", {}, {}, ""}));
    // 5. Singleton proof: same UserService instance across two resolves.
    std::cout << "singleton  same_instance="
              << (container->resolve<UserService>().get() ==
                  container->resolve<UserService>().get())
              << "\n";
    return 0;
}
