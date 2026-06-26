// cpp-artisan — a minimal artisan-style CLI for the Laravel-in-C++ demo app.
// Mirrors `php artisan`: a console entry point that boots the app and drives it.
//
//   cpp-artisan list
//   cpp-artisan route:list
//   cpp-artisan request <METHOD> <URI> [body] [--token <TOKEN>]
//   cpp-artisan --version
//
// `request` is a server-less way to exercise the kernel (like tinker + curl) until a
// real HTTP listener exists; it boots the app and runs one request through Kernel.
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "bootstrap.hpp"
#include "generators.hpp"
#include "http_server.hpp"
#include "project_scaffold.hpp"

namespace {

int usage() {
    std::cout << "cpp-artisan 0.1 — Laravel-in-C++\n\n"
                 "Usage:\n"
                 "  cpp-artisan list\n"
                 "  cpp-artisan new <Name> [dir]              scaffold a new app project\n"
                 "  cpp-artisan route:list\n"
                 "  cpp-artisan request <METHOD> <URI> [body] [--token <TOKEN>]\n"
                 "  cpp-artisan serve [port] [workers] (default 8080, 1 worker)\n"
                 "  cpp-artisan make:model <Name> [dir]       (default app/models)\n"
                 "  cpp-artisan make:controller <Name> [dir]  (default app)\n"
                 "  cpp-artisan make:migration <Name> [dir]   (default database/migrations)\n"
                 "  cpp-artisan --version\n";
    return 0;
}

int make_new(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "new needs a <Name>\n";
        return 2;
    }
    const std::string& name = args[1];
    std::string dir = args.size() > 2 ? args[2] + "/" + name : name;
    std::string err;
    if (!scaffold::create_project(name, dir, err)) {
        std::cerr << "cpp-artisan: " << err << "\n";
        return 1;
    }
    std::cout << "created project " << dir << "\n"
              << "  cd " << dir << " && cmake -S . -B build && cmake --build build && ctest --test-dir build\n";
    return 0;
}

int make(const std::vector<std::string>& args, bool controller) {
    if (args.size() < 2) {
        std::cerr << args[0] << " needs a <Name>\n";
        return 2;
    }
    const std::string& name = args[1];
    std::string dir = args.size() > 2 ? args[2] : (controller ? "app" : "app/models");
    std::string file = to_snake(name) + (controller ? "_controller.hpp" : ".hpp");
    std::string path = dir + "/" + file;
    std::string content = controller ? render_controller(name) : render_model(name);

    std::string err;
    if (!write_new_file(path, content, err)) {
        std::cerr << "cpp-artisan: " << err << "\n";
        return 1;
    }
    std::cout << "created " << path << "\n";
    return 0;
}

int make_migration(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "make:migration needs a <Name>\n";
        return 2;
    }
    const std::string& name = args[1];
    std::string dir = args.size() > 2 ? args[2] : "database/migrations";
    std::string path = dir + "/" + to_snake(name) + ".hpp";

    std::string err;
    if (!write_new_file(path, render_migration(name), err)) {
        std::cerr << "cpp-artisan: " << err << "\n";
        return 1;
    }
    std::cout << "created " << path << "\n";
    return 0;
}

int serve(const std::vector<std::string>& args) {
    int port = args.size() > 1 ? std::atoi(args[1].c_str()) : 8080;
    int workers = args.size() > 2 ? std::atoi(args[2].c_str()) : 1;
    App app = bootstrap();
    HttpServer server(app.kernel, port);
    return server.serve(workers);
}

int route_list() {
    App app = bootstrap();
    std::cout << std::left << std::setw(7) << "METHOD" << "URI\n";
    for (const auto& r : app.router->list())
        std::cout << std::left << std::setw(7) << r.method << r.uri << "\n";
    return 0;
}

int request(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cerr << "request needs <METHOD> <URI>\n";
        return 2;
    }
    std::string body, token;
    for (std::size_t i = 3; i < args.size(); ++i) {
        if (args[i] == "--token" && i + 1 < args.size())
            token = args[++i];
        else if (body.empty())
            body = args[i];
    }

    App app = bootstrap();
    Request req;
    req.method = args[1];
    req.path = args[2];
    req.body = body;
    if (!token.empty()) req.headers["Authorization"] = token;

    Response res = app.kernel->handle(std::move(req));
    std::cout << "HTTP " << res.status << "\n";
    for (const auto& h : res.headers) std::cout << h.first << ": " << h.second << "\n";
    std::cout << "\n" << res.body << "\n";
    return res.status < 400 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "list" || args[0] == "--help" || args[0] == "-h")
        return usage();

    const std::string& cmd = args[0];
    if (cmd == "--version") {
        std::cout << "cpp-artisan 0.1\n";
        return 0;
    }
    if (cmd == "new") return make_new(args);
    if (cmd == "route:list") return route_list();
    if (cmd == "request") return request(args);
    if (cmd == "serve") return serve(args);
    if (cmd == "make:model") return make(args, /*controller=*/false);
    if (cmd == "make:controller") return make(args, /*controller=*/true);
    if (cmd == "make:migration") return make_migration(args);

    std::cerr << "Unknown command: " << cmd << "\n\n";
    usage();
    return 1;
}
