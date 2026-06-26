// test_relations.cpp — relationships + soft deletes.
#include "test_harness.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "database.hpp"
#include "models/article.hpp"
#include "models/comment.hpp"
#include "relations.hpp"

namespace {
std::size_t n(int v) { return static_cast<std::size_t>(v); }
} // namespace

TEST(relation_has_many_and_belongs_to) {
    auto conn = std::make_shared<MemoryConnection>();
    ArticleRepository articles(conn);
    CommentRepository comments(conn);

    Article post{0, "post", 0, true};
    articles.insert(post);
    Article other{0, "other", 0, true};
    articles.insert(other);

    Comment c1{0, post.id, "first", true, false};
    Comment c2{0, post.id, "second", false, false};
    Comment c3{0, other.id, "elsewhere", true, false};
    comments.insert(c1);
    comments.insert(c2);
    comments.insert(c3);

    // has_many returns a builder → chainable.
    auto all_for_post = has_many<Comment, CommentMapper>(*conn, "article_id", post.id).get();
    CHECK_EQ(all_for_post.size(), n(2));

    auto approved_for_post = has_many<Comment, CommentMapper>(*conn, "article_id", post.id)
                                 .where("approved", Value{true})
                                 .get();
    CHECK_EQ(approved_for_post.size(), n(1));
    CHECK_EQ(approved_for_post.front().body, std::string("first"));

    // belongs_to: from a comment back to its article.
    auto parent = belongs_to<Article, ArticleMapper>(*conn, c1.article_id);
    CHECK(parent.has_value());
    CHECK_EQ(parent->title, std::string("post"));
}

TEST(relation_has_one) {
    auto conn = std::make_shared<MemoryConnection>();
    ArticleRepository articles(conn);
    CommentRepository comments(conn);
    Article post{0, "post", 0, true};
    articles.insert(post);
    Comment only{0, post.id, "solo", true, false};
    comments.insert(only);

    auto one = has_one<Comment, CommentMapper>(*conn, "article_id", post.id);
    CHECK(one.has_value());
    CHECK_EQ(one->body, std::string("solo"));
}

// Eager loading: one IN query for all parents, grouped by foreign key (no N+1).
TEST(relation_eager_load_has_many) {
    auto conn = std::make_shared<MemoryConnection>();
    ArticleRepository articles(conn);
    CommentRepository comments(conn);

    Article a{0, "a", 0, true};
    Article b{0, "b", 0, true};
    articles.insert(a);
    articles.insert(b);

    Comment c1{0, a.id, "a1", true, false};
    Comment c2{0, a.id, "a2", true, false};
    Comment c3{0, b.id, "b1", true, false};
    comments.insert(c1);
    comments.insert(c2);
    comments.insert(c3);

    auto grouped = load_has_many<Comment, CommentMapper>(
        *conn, "article_id", {a.id, b.id}, [](const Comment& c) { return c.article_id; });

    CHECK_EQ(grouped[a.id].size(), n(2));
    CHECK_EQ(grouped[b.id].size(), n(1));
    CHECK_EQ(grouped[a.id].front().body, std::string("a1"));
}

// Soft delete: mark deleted, then exclude via the query (explicit, no auto-scope).
TEST(relation_soft_delete_excludes) {
    auto conn = std::make_shared<MemoryConnection>();
    ArticleRepository articles(conn);
    CommentRepository comments(conn);
    Article post{0, "post", 0, true};
    articles.insert(post);

    Comment c1{0, post.id, "keep", true, false};
    Comment c2{0, post.id, "remove", true, false};
    comments.insert(c1);
    comments.insert(c2);

    c2.deleted = true; // soft delete
    CHECK(comments.update(c2));

    auto live = has_many<Comment, CommentMapper>(*conn, "article_id", post.id)
                    .where("deleted", Value{false})
                    .get();
    CHECK_EQ(live.size(), n(1));
    CHECK_EQ(live.front().body, std::string("keep"));

    // Without the filter, both rows are still present (soft, not hard, delete).
    auto total = has_many<Comment, CommentMapper>(*conn, "article_id", post.id).count();
    CHECK_EQ(total, n(2));
}
