// providers.cpp — see providers.hpp
#include "providers.hpp"

#include <cstdlib>
#include <memory>

#include "article_controller.hpp"
#include "database.hpp"
#include "migrations.hpp"
#include "models/article.hpp"
#include "models/author.hpp"
#include "models/comment.hpp"
#include "user_controller.hpp"
#include "user_service.hpp"

#ifdef LIPP_HAVE_SQLITE
#include "sqlite_connection.hpp"
#endif

void UserServiceProvider::register_() {
    // Bind only — do NOT resolve here. Uses the autowiring helpers.
    app_->singleton_auto<UserService>();
    app_->bind_auto<UserController, UserService>();
}

void UserServiceProvider::boot() {
    // Safe to resolve now: every provider has registered. Warm the singleton so the
    // first request doesn't pay construction cost.
    app_->resolve<UserService>();
    booted = true;
}

void DatabaseServiceProvider::register_() {
    // Bind the Connection *contract* to a concrete backend — an interface->impl
    // binding (which the codegen tool can't infer from a constructor). When built
    // with SQLite, use it (path from $LIPP_DB, default an in-memory DB so each app
    // instance — and each test — gets a fresh database); otherwise MemoryConnection.
    app_->singleton<Connection>([](ContainerContract&) -> std::shared_ptr<Connection> {
#ifdef LIPP_HAVE_SQLITE
        const char* path = std::getenv("LIPP_DB");
        return std::make_shared<SqliteConnection>(path ? path : ":memory:");
#else
        return std::make_shared<MemoryConnection>();
#endif
    });
    app_->bind_auto<ArticleRepository, Connection>();
    app_->bind_auto<ArticleController, ArticleRepository>();
    app_->bind_auto<AuthorRepository, Connection>();
    app_->bind_auto<CommentRepository, Connection>();
}

void DatabaseServiceProvider::boot() {
    // Bindings exist now (boot runs after every provider's register_). Migrate the
    // schema — real DDL on SQLite, a harmless no-op on the in-memory backend.
    auto conn = app_->resolve<Connection>();
    Migrator(*conn).run(app_migrations());
}
