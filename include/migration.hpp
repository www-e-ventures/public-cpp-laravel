// migration.hpp — migrations + a small migrator (Laravel: Migration + Migrator)
//
// A Migration has up()/down(); the Migrator runs pending ones, tracking applied
// migrations in a `migrations` table so run() is idempotent, and supports rollback.
// Backend-agnostic: works against SqliteConnection (real DDL) or MemoryConnection
// (DDL is a no-op, but the bookkeeping still works).
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "database.hpp"

class Migration {
public:
    virtual ~Migration() = default;
    virtual std::string name() const = 0;
    virtual void up(Connection& conn) = 0;
    virtual void down(Connection& conn) = 0;
};

class Migrator {
public:
    explicit Migrator(Connection& conn) : conn_(&conn) {}

    // Apply every not-yet-applied migration, in order.
    void run(const std::vector<std::shared_ptr<Migration>>& migrations) {
        ensure_table();
        for (const auto& m : migrations) {
            if (applied(m->name())) continue;
            m->up(*conn_);
            record(m->name());
        }
    }

    // Roll back applied migrations, in reverse order.
    void rollback(const std::vector<std::shared_ptr<Migration>>& migrations) {
        ensure_table();
        for (auto it = migrations.rbegin(); it != migrations.rend(); ++it) {
            if (!applied((*it)->name())) continue;
            (*it)->down(*conn_);
            forget((*it)->name());
        }
    }

private:
    void ensure_table() {
        conn_->statement("CREATE TABLE IF NOT EXISTS migrations (id INTEGER PRIMARY KEY "
                         "AUTOINCREMENT, name TEXT NOT NULL DEFAULT '')");
    }
    bool applied(const std::string& name) {
        Query q;
        q.wheres.push_back({"name", Op::Eq, Value{name}});
        return !conn_->select("migrations", q).empty();
    }
    void record(const std::string& name) {
        Row r;
        r.set("name", name);
        conn_->insert("migrations", std::move(r));
    }
    void forget(const std::string& name) {
        // names are code constants, not user input.
        conn_->statement("DELETE FROM migrations WHERE name = '" + name + "'");
    }

    Connection* conn_;
};
