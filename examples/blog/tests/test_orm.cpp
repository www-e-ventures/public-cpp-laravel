// test_orm.cpp — typed repository over the in-memory connection.
#include "test_harness.hpp"

#include <cstddef>
#include <memory>
#include <string>

#include "container.hpp"
#include "database.hpp"
#include "models/article.hpp"
#include "providers.hpp"

namespace {
ArticleRepository fresh_repo() {
    return ArticleRepository(std::make_shared<MemoryConnection>());
}
} // namespace

TEST(orm_insert_assigns_id_and_find_round_trips) {
    auto repo = fresh_repo();
    Article a{0, "First", 10, true};
    repo.insert(a);
    CHECK(a.id > 0); // id written back into the model

    auto got = repo.find(a.id);
    CHECK(got.has_value());
    CHECK_EQ(got->title, std::string("First"));
    CHECK_EQ(got->views, static_cast<std::int64_t>(10));
    CHECK(got->published);
}

TEST(orm_all_and_autoincrement) {
    auto repo = fresh_repo();
    Article a{0, "A", 0, false};
    Article b{0, "B", 0, true};
    repo.insert(a);
    repo.insert(b);
    CHECK_EQ(b.id, a.id + 1);
    CHECK_EQ(repo.all().size(), static_cast<std::size_t>(2));
}

TEST(orm_where_filters_by_column) {
    auto repo = fresh_repo();
    Article pub{0, "published one", 1, true};
    Article draft{0, "draft one", 2, false};
    repo.insert(pub);
    repo.insert(draft);

    auto published = repo.where("published", Value{true});
    CHECK_EQ(published.size(), static_cast<std::size_t>(1));
    CHECK_EQ(published.front().title, std::string("published one"));
}

TEST(orm_update_persists_changes) {
    auto repo = fresh_repo();
    Article a{0, "old", 0, false};
    repo.insert(a);

    a.title = "new";
    a.views = 99;
    CHECK(repo.update(a));

    auto got = repo.find(a.id);
    CHECK(got.has_value());
    CHECK_EQ(got->title, std::string("new"));
    CHECK_EQ(got->views, static_cast<std::int64_t>(99));
}

TEST(orm_remove_deletes) {
    auto repo = fresh_repo();
    Article a{0, "x", 0, false};
    repo.insert(a);
    CHECK(repo.remove(a));
    CHECK(!repo.find(a.id).has_value());
}

// Integration: resolve the repository through the container + service provider,
// proving the ORM wires into the framework (interface->impl binding
// plus a shared Connection singleton behind transient repositories).
TEST(orm_resolves_through_container) {
    auto c = std::make_shared<Container>();
    DatabaseServiceProvider provider(c);
    provider.register_();
    provider.boot(); // runs migrations (required for the SQLite backend; no-op on memory)

    auto repo = c->resolve<ArticleRepository>();
    Article a{0, "via container", 0, true};
    repo->insert(a);
    CHECK(a.id > 0);

    // A freshly resolved (transient) repository sees the same Connection singleton.
    auto repo2 = c->resolve<ArticleRepository>();
    CHECK(repo2->find(a.id).has_value());
    CHECK(c->resolve<Connection>().get() == c->resolve<Connection>().get());
}
