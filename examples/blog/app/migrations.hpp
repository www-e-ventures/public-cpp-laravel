// migrations.hpp — the demo app's schema (Laravel: database/migrations/*).
// Run by DatabaseServiceProvider::boot(); idempotent, so safe on every boot.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "migration.hpp"
#include "schema.hpp"

struct CreateArticles : Migration {
    std::string name() const override { return "2026_01_01_000001_create_articles"; }
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

struct CreateAuthors : Migration {
    std::string name() const override { return "2026_01_01_000002_create_authors"; }
    void up(Connection& c) override {
        Schema::create(c, "authors", [](Blueprint& t) {
            t.id();
            t.string("name");
            t.string("email");
            t.integer("article_count");
        });
    }
    void down(Connection& c) override { Schema::drop_if_exists(c, "authors"); }
};

struct CreateComments : Migration {
    std::string name() const override { return "2026_01_01_000003_create_comments"; }
    void up(Connection& c) override {
        Schema::create(c, "comments", [](Blueprint& t) {
            t.id();
            t.integer("article_id");
            t.string("body");
            t.boolean("approved");
            t.boolean("deleted");
        });
    }
    void down(Connection& c) override { Schema::drop_if_exists(c, "comments"); }
};

inline std::vector<std::shared_ptr<Migration>> app_migrations() {
    return {
        std::make_shared<CreateArticles>(),
        std::make_shared<CreateAuthors>(),
        std::make_shared<CreateComments>(),
    };
}
