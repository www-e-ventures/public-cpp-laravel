// test_query_builder.cpp — fluent query builder + a second model.
#include "test_harness.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "database.hpp"
#include "models/article.hpp"
#include "models/author.hpp"

namespace {

// Seeds three articles: alpha(5,pub), bravo(20,pub), charlie(10,draft).
ArticleRepository seeded() {
    ArticleRepository repo(std::make_shared<MemoryConnection>());
    Article a1{0, "alpha", 5, true};
    Article a2{0, "bravo", 20, true};
    Article a3{0, "charlie", 10, false};
    repo.insert(a1);
    repo.insert(a2);
    repo.insert(a3);
    return repo;
}

std::size_t n(int v) { return static_cast<std::size_t>(v); }

} // namespace

TEST(query_where_equals) {
    auto repo = seeded();
    CHECK_EQ(repo.query().where("published", Value{true}).get().size(), n(2));
}

TEST(query_where_with_operator) {
    auto repo = seeded();
    auto big = repo.query().where("views", Op::Gt, Value{std::int64_t{9}}).get();
    CHECK_EQ(big.size(), n(2)); // bravo(20), charlie(10)
}

TEST(query_chained_wheres_are_anded) {
    auto repo = seeded();
    auto r = repo.query()
                 .where("published", Value{true})
                 .where("views", Op::Gte, Value{std::int64_t{10}})
                 .get();
    CHECK_EQ(r.size(), n(1)); // only bravo is published AND >= 10 views
    CHECK_EQ(r.front().title, std::string("bravo"));
}

TEST(query_order_by_and_limit) {
    auto repo = seeded();
    auto top2 = repo.query().order_by("views", /*descending=*/true).limit(2).get();
    CHECK_EQ(top2.size(), n(2));
    CHECK_EQ(top2[0].title, std::string("bravo"));   // 20
    CHECK_EQ(top2[1].title, std::string("charlie")); // 10
}

TEST(query_first_and_count) {
    auto repo = seeded();
    auto first = repo.query().where("published", Value{true}).order_by("views").first();
    CHECK(first.has_value());
    CHECK_EQ(first->title, std::string("alpha")); // smallest views among published (5)

    CHECK_EQ(repo.query().where("published", Value{false}).count(), n(1));
    CHECK(!repo.query().where("title", Value{std::string("nope")}).first().has_value());
}

TEST(query_pagination) {
    auto repo = seeded(); // 3 articles: alpha, bravo, charlie
    auto p1 = repo.query().order_by("id").paginate(1, 2);
    CHECK_EQ(p1.items.size(), n(2));
    CHECK_EQ(p1.total, n(3));
    CHECK_EQ(p1.page, n(1));
    CHECK_EQ(p1.last_page, n(2)); // ceil(3/2)
    CHECK_EQ(p1.items.front().title, std::string("alpha"));

    auto p2 = repo.query().order_by("id").paginate(2, 2);
    CHECK_EQ(p2.items.size(), n(1)); // remainder
    CHECK_EQ(p2.items.front().title, std::string("charlie"));
}

TEST(query_pagination_respects_where) {
    auto repo = seeded();
    auto page = repo.query().where("published", Value{true}).order_by("id").paginate(1, 10);
    CHECK_EQ(page.total, n(2)); // alpha + bravo are published; charlie isn't
    CHECK_EQ(page.items.size(), n(2));
    CHECK_EQ(page.last_page, n(1));
}

// The second model works with the same Repository/QueryBuilder — the only new code
// was its struct + mapper (the boilerplate codegen would generate).
TEST(query_second_model_author) {
    AuthorRepository repo(std::make_shared<MemoryConnection>());
    Author a{0, "Ada", "ada@x.io", 3};
    repo.insert(a);

    auto got = repo.find(a.id);
    CHECK(got.has_value());
    CHECK_EQ(got->email, std::string("ada@x.io"));

    auto byName = repo.query().where("name", Value{std::string("Ada")}).get();
    CHECK_EQ(byName.size(), n(1));
    CHECK_EQ(byName.front().article_count, std::int64_t{3});
}
