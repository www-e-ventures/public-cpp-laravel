// comment.hpp — a child model demonstrating relationships + soft deletes.
// A Comment belongs to an Article (article_id) and carries a `deleted` flag for
// soft deletion (Laravel: SoftDeletes — kept explicit here, no global auto-scope).
#pragma once
#include <cstdint>
#include <string>

#include "database.hpp"
#include "repository.hpp"

struct Comment {
    std::int64_t id = 0;
    std::int64_t article_id = 0;
    std::string body;
    bool approved = false;
    bool deleted = false; // soft-delete marker
};

struct CommentMapper {
    static std::string table() { return "comments"; }

    static Comment hydrate(const Row& r) {
        return Comment{
            r.get<std::int64_t>("id"),
            r.get<std::int64_t>("article_id"),
            r.get<std::string>("body"),
            r.get<bool>("approved"),
            r.get<bool>("deleted"),
        };
    }

    static Row to_row(const Comment& c) {
        Row r;
        r.set("id", c.id);
        r.set("article_id", c.article_id);
        r.set("body", c.body);
        r.set("approved", c.approved);
        r.set("deleted", c.deleted);
        return r;
    }

    static std::int64_t id(const Comment& c) { return c.id; }
    static void set_id(Comment& c, std::int64_t id) { c.id = id; }
};

using CommentRepository = Repository<Comment, CommentMapper>;
