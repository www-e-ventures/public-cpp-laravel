// author.hpp — a second persistent model + mapper.
// Exists mainly to make the mapper-boilerplate measurement concrete: compare this
// against article.hpp to gauge whether codegen (see docs/eloquent-notes.md)
// would pay for itself across many models.
#pragma once
#include <cstdint>
#include <string>

#include "database.hpp"
#include "repository.hpp"

struct Author {
    std::int64_t id = 0;
    std::string name;
    std::string email;
    std::int64_t article_count = 0;
};

struct AuthorMapper {
    static std::string table() { return "authors"; }

    static Author hydrate(const Row& r) {
        return Author{
            r.get<std::int64_t>("id"),
            r.get<std::string>("name"),
            r.get<std::string>("email"),
            r.get<std::int64_t>("article_count"),
        };
    }

    static Row to_row(const Author& a) {
        Row r;
        r.set("id", a.id);
        r.set("name", a.name);
        r.set("email", a.email);
        r.set("article_count", a.article_count);
        return r;
    }

    static std::int64_t id(const Author& a) { return a.id; }
    static void set_id(Author& a, std::int64_t id) { a.id = id; }
};

using AuthorRepository = Repository<Author, AuthorMapper>;
