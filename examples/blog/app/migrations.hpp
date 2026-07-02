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

// The queue driver's tables + the scheduler's last-run stamps (db_queue.hpp /
// scheduler.hpp document the columns). Needed on SQLite; the in-memory backend
// auto-creates tables so this is a no-op there.
struct CreateQueueTables : Migration {
    std::string name() const override { return "2026_07_01_000004_create_queue_tables"; }
    void up(Connection& c) override {
        Schema::create(c, "jobs", [](Blueprint& t) {
            t.id();
            t.string("job");
            t.string("payload");
            t.integer("attempts");
            t.integer("available_at");
            t.integer("reserved_at");
        });
        Schema::create(c, "failed_jobs", [](Blueprint& t) {
            t.id();
            t.string("job");
            t.string("payload");
            t.string("error");
        });
        Schema::create(c, "schedule_runs", [](Blueprint& t) {
            t.id();
            t.string("name");
            t.integer("last_run");
        });
    }
    void down(Connection& c) override {
        Schema::drop_if_exists(c, "jobs");
        Schema::drop_if_exists(c, "failed_jobs");
        Schema::drop_if_exists(c, "schedule_runs");
    }
};

inline std::vector<std::shared_ptr<Migration>> app_migrations() {
    return {
        std::make_shared<CreateArticles>(),
        std::make_shared<CreateAuthors>(),
        std::make_shared<CreateComments>(),
        std::make_shared<CreateQueueTables>(),
    };
}
