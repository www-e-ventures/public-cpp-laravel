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
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "bootstrap.hpp"
#include "db_queue.hpp"
#include "generators.hpp"
#include "http_server.hpp"
#include "migrations.hpp"
#include "project_scaffold.hpp"
#include "scheduler.hpp"

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
                 "  cpp-artisan migrate                        apply pending migrations\n"
                 "  cpp-artisan migrate:rollback               undo the app's migrations\n"
                 "  cpp-artisan queue:work [poll_seconds]     run queued jobs (Ctrl-C to stop)\n"
                 "  cpp-artisan schedule:run                   one scheduler tick (call from cron)\n"
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

// Booting the app applies migrations (DatabaseServiceProvider::boot is idempotent),
// so `migrate` is boot + report. Persistence needs a real database: set LIPP_DB to
// a file path, otherwise the SQLite build uses :memory: and each run starts fresh.
int migrate(bool rollback) {
    App app = bootstrap();
    auto conn = app.container->resolve<Connection>();
    if (rollback) {
        Migrator(*conn).rollback(app_migrations());
        std::cout << "rolled back " << app_migrations().size() << " migration(s)\n";
        return 0;
    }
    for (const auto& m : app_migrations()) std::cout << "  applied " << m->name() << "\n";
    std::cout << "migrations up to date\n";
    return 0;
}

// The demo app's queue handlers + schedule, shared by queue:work and schedule:run.
// An app would register its own; "log" just proves the plumbing end to end.
DbQueue demo_queue(std::shared_ptr<Connection> conn) {
    DbQueue q(std::move(conn));
    q.handler("log", [](const std::string& payload) { std::cout << "[job] " << payload << "\n"; });
    return q;
}

Schedule demo_schedule(std::shared_ptr<Connection> conn, DbQueue& q) {
    Schedule s(std::move(conn));
    // The scheduled fn stays tiny: it queues work, the queue worker does the lifting.
    s.every(std::chrono::seconds(60), "heartbeat", [&q] { q.push("log", "heartbeat"); });
    s.daily_at("03:00", "daily-report", [&q] { q.push("log", "daily report"); });
    return s;
}

std::atomic<bool>* g_worker_running = nullptr;

int queue_work(const std::vector<std::string>& args) {
    int poll = args.size() > 1 ? std::atoi(args[1].c_str()) : 1;
    if (poll < 1) poll = 1;
    App app = bootstrap();
    auto conn = app.container->resolve<Connection>();
    DbQueue q = demo_queue(conn);

    std::atomic<bool> running{true};
    g_worker_running = &running;
    std::signal(SIGINT, [](int) { if (g_worker_running) *g_worker_running = false; });
    std::signal(SIGTERM, [](int) { if (g_worker_running) *g_worker_running = false; });

    std::cout << "queue:work polling every " << poll << "s (Ctrl-C to stop)\n";
    while (running) {
        std::size_t n = q.work();
        if (n) std::cout << "processed " << n << " job(s), " << q.pending() << " pending\n";
        for (int i = 0; i < poll * 10 && running; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "queue:work stopped (" << q.pending() << " pending, " << q.failed()
              << " failed)\n";
    return 0;
}

int schedule_run() {
    App app = bootstrap();
    auto conn = app.container->resolve<Connection>();
    DbQueue q = demo_queue(conn);
    Schedule s = demo_schedule(conn, q);
    std::size_t ran = s.run_pending();
    std::cout << "schedule:run — " << ran << " task(s) fired, " << q.pending()
              << " job(s) queued\n";
    return 0;
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
    if (cmd == "migrate") return migrate(/*rollback=*/false);
    if (cmd == "migrate:rollback") return migrate(/*rollback=*/true);
    if (cmd == "queue:work") return queue_work(args);
    if (cmd == "schedule:run") return schedule_run();

    std::cerr << "Unknown command: " << cmd << "\n\n";
    usage();
    return 1;
}
