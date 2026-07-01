// app.hpp — the coco-web reference app: a static SPA + a JSON API on cpp-laravel.
//
// Header-only bootstrap so both the demo executable and the feature test build the
// same wiring. It shows the surfaces an outside app (e.g. a separate coco-web-app
// repo that FetchContents the framework) would use:
//   * static file serving   (staticfiles::mount / serve — correct MIME, incl. .wasm)
//   * a JSON API over the ORM (MemoryConnection here; a real app swaps in SQLite)
//   * the query string        (?limit=N)
//   * token-gated writes       (tok::requires_ability — a signed token carrying an
//                               ability, verified with a shared secret, no DB lookup)
#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

#include "container.hpp"
#include "database.hpp"
#include "http.hpp"
#include "kernel.hpp"
#include "router.hpp"
#include "static_files.hpp"
#include "token.hpp"

namespace cocoweb {

struct App {
    std::shared_ptr<Container> container;
    std::shared_ptr<Router> router;
    std::shared_ptr<Kernel> kernel;
    std::shared_ptr<Connection> db;
};

inline std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}

inline App bootstrap(const std::string& public_dir, const std::string& token_secret) {
    auto container = std::make_shared<Container>();
    auto router = std::make_shared<Router>();
    auto db = std::make_shared<MemoryConnection>();

    // Seed a couple of items so the API returns something out of the box.
    for (const char* name : {"first", "second"}) {
        Row r;
        r.set("name", std::string(name));
        db->insert("items", std::move(r));
    }

    // Static SPA: "/" serves index.html; "/assets/<file>" serves assets (correct MIME,
    // including application/wasm for a WebAssembly module).
    router->get("/", [public_dir](Request& req) {
        return staticfiles::serve(public_dir, "index.html", req);
    });
    staticfiles::mount(*router, "/assets", public_dir);

    // JSON API over the ORM. GET supports ?limit=N (read off req.query).
    router->get("/api/items", [db](Request& req) {
        auto rows = db->all("items");
        std::size_t limit = rows.size();
        auto it = req.query.find("limit");
        if (it != req.query.end()) {
            try {
                limit = std::min(limit, static_cast<std::size_t>(std::stoul(it->second)));
            } catch (...) {
            }
        }
        std::ostringstream os;
        os << "[";
        for (std::size_t i = 0; i < limit; ++i) {
            if (i) os << ",";
            os << "{\"id\":" << rows[i].get<std::int64_t>("id") << ",\"name\":\""
               << json_escape(rows[i].get<std::string>("name")) << "\"}";
        }
        os << "]";
        return Response::json(os.str());
    });

    // Creating an item requires a signed token carrying the "items:write" ability.
    router->post(
        "/api/items",
        [db](Request& req) {
            auto form = parse_form(req.body);
            std::string name = form.count("name") ? form.at("name") : "";
            if (name.empty()) return Response::json(R"({"error":"name is required"})", 422);
            Row r;
            r.set("name", name);
            std::int64_t id = db->insert("items", std::move(r));
            return Response::json(
                "{\"id\":" + std::to_string(id) + ",\"name\":\"" + json_escape(name) + "\"}", 201);
        },
        {tok::requires_ability("items:write", token_secret)});

    auto kernel = std::make_shared<Kernel>(container, router);
    return App{container, router, kernel, db};
}

} // namespace cocoweb
