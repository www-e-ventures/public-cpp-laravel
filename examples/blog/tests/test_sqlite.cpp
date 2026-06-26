// test_sqlite.cpp — real SQLite backend + migrations.
// Separate executable (links libsqlite3) so the core test suite stays zero-dep.
#include "test_harness.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "migration.hpp"
#include "models/article.hpp"
#include "schema.hpp"
#include "sqlite_connection.hpp"

namespace {

std::size_t n(int v) { return static_cast<std::size_t>(v); }

// A migration for the articles table (Laravel: a database/migrations file).
struct CreateArticles : Migration {
    std::string name() const override { return "2026_01_01_create_articles"; }
    void up(Connection& c) override {
        Schema::create(c, "articles", [](Blueprint& t) {
            t.id();
            t.string("title");
            t.integer("views");
            t.boolean("published");
        });
    }
    void down(Connection& c) override { Schema::drop_if_exists(c, "articles"); }
};

std::vector<std::shared_ptr<Migration>> migrations() {
    return {std::make_shared<CreateArticles>()};
}

} // namespace

// Full CRUD + query builder over a real SQLite database (in-memory file).
TEST(sqlite_crud_and_query) {
    auto conn = std::make_shared<SqliteConnection>(":memory:");
    Migrator(*conn).run(migrations());
    ArticleRepository repo(conn);

    Article a{0, "alpha", 5, true};
    Article b{0, "bravo", 20, false};
    repo.insert(a);
    repo.insert(b);
    CHECK(a.id > 0);
    CHECK_EQ(b.id, a.id + 1); // real AUTOINCREMENT

    auto got = repo.find(a.id);
    CHECK(got.has_value());
    CHECK_EQ(got->title, std::string("alpha"));
    CHECK_EQ(got->views, std::int64_t{5});
    CHECK(got->published); // bool round-trips through INTEGER via value_as coercion

    CHECK_EQ(repo.all().size(), n(2));

    auto published = repo.query().where("published", Value{true}).get();
    CHECK_EQ(published.size(), n(1));
    CHECK_EQ(published.front().title, std::string("alpha"));

    auto top = repo.query().where("views", Op::Gte, Value{std::int64_t{1}}).order_by("views", true).limit(1).get();
    CHECK_EQ(top.size(), n(1));
    CHECK_EQ(top.front().title, std::string("bravo")); // 20 is highest

    a.title = "ALPHA";
    CHECK(repo.update(a));
    CHECK_EQ(repo.find(a.id)->title, std::string("ALPHA"));

    CHECK(repo.remove(b));
    CHECK(!repo.find(b.id).has_value());
    CHECK_EQ(repo.all().size(), n(1));
}

// Pagination over real SQL (exercises LIMIT/OFFSET in SqliteConnection::select).
TEST(sqlite_pagination) {
    auto conn = std::make_shared<SqliteConnection>(":memory:");
    Migrator(*conn).run(migrations());
    ArticleRepository repo(conn);
    for (int i = 1; i <= 5; ++i) {
        Article a{0, "post-" + std::to_string(i), i, true};
        repo.insert(a);
    }

    auto p = repo.query().order_by("id").paginate(2, 2); // rows 3 and 4
    CHECK_EQ(p.total, n(5));
    CHECK_EQ(p.last_page, n(3)); // ceil(5/2)
    CHECK_EQ(p.items.size(), n(2));
    CHECK_EQ(p.items.front().title, std::string("post-3"));
}

// run() is idempotent: applying the same migrations twice is a no-op, not an error.
TEST(sqlite_migrations_idempotent) {
    auto conn = std::make_shared<SqliteConnection>(":memory:");
    Migrator(*conn).run(migrations());
    Migrator(*conn).run(migrations()); // would throw "table already exists" if not tracked

    ArticleRepository repo(conn);
    Article a{0, "x", 0, false};
    repo.insert(a);
    CHECK(a.id > 0);
}

// rollback() drops the schema via down(); re-running migrations rebuilds it.
TEST(sqlite_migrations_rollback) {
    auto conn = std::make_shared<SqliteConnection>(":memory:");
    Migrator(*conn).run(migrations());
    Migrator(*conn).rollback(migrations());

    Migrator(*conn).run(migrations()); // rebuild after rollback
    ArticleRepository repo(conn);
    Article a{0, "again", 1, true};
    repo.insert(a);
    CHECK(repo.find(a.id).has_value());
}

int main() { return RUN_ALL_TESTS(); }
