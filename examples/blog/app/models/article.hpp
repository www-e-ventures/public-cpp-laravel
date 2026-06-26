// article.hpp — a persistent model (plain typed struct) + its hand-written mapper.
#pragma once
#include <cstdint>
#include <string>

#include "database.hpp"
#include "repository.hpp"

// The model: no base class, no magic — just data.
struct Article {
    std::int64_t id = 0;
    std::string title;
    std::int64_t views = 0;
    bool published = false;
};

// The mapper: the explicit stand-in for the reflection C++ lacks. This is the
// mechanical boilerplate the codegen tool generates verbatim from the struct.
struct ArticleMapper {
    static std::string table() { return "articles"; }

    static Article hydrate(const Row& r) {
        return Article{
            r.get<std::int64_t>("id"),
            r.get<std::string>("title"),
            r.get<std::int64_t>("views"),
            r.get<bool>("published"),
        };
    }

    static Row to_row(const Article& a) {
        Row r;
        r.set("id", a.id);
        r.set("title", a.title);
        r.set("views", a.views);
        r.set("published", a.published);
        return r;
    }

    static std::int64_t id(const Article& a) { return a.id; }
    static void set_id(Article& a, std::int64_t id) { a.id = id; }
};

using ArticleRepository = Repository<Article, ArticleMapper>;
