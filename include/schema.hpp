// schema.hpp — schema builder (Laravel: Illuminate\Database\Schema\Blueprint + Schema)
//
// Pure SQL generation, no backend dependency: a Blueprint accumulates column
// definitions and renders CREATE TABLE, which is run via Connection::statement().
// On the schemaless in-memory backend, statement() is a no-op (tables auto-create);
// on SqliteConnection it actually creates the table.
#pragma once
#include <string>
#include <vector>

#include "database.hpp"

class Blueprint {
public:
    Blueprint& id() {
        cols_.push_back("id INTEGER PRIMARY KEY AUTOINCREMENT");
        return *this;
    }
    Blueprint& integer(const std::string& name) {
        cols_.push_back(name + " INTEGER NOT NULL DEFAULT 0");
        return *this;
    }
    Blueprint& boolean(const std::string& name) {
        cols_.push_back(name + " INTEGER NOT NULL DEFAULT 0"); // SQLite has no real bool
        return *this;
    }
    Blueprint& string(const std::string& name) {
        cols_.push_back(name + " TEXT NOT NULL DEFAULT ''");
        return *this;
    }
    Blueprint& real(const std::string& name) {
        cols_.push_back(name + " REAL NOT NULL DEFAULT 0");
        return *this;
    }

    std::string create_sql(const std::string& table, bool if_not_exists) const {
        std::string sql = "CREATE TABLE ";
        if (if_not_exists) sql += "IF NOT EXISTS ";
        sql += table + " (";
        for (std::size_t i = 0; i < cols_.size(); ++i) {
            if (i) sql += ", ";
            sql += cols_[i];
        }
        sql += ")";
        return sql;
    }

private:
    std::vector<std::string> cols_;
};

namespace Schema {

// Schema::create(conn, "articles", [](Blueprint& t){ t.id(); t.string("title"); ... });
template <typename Fn>
void create(Connection& conn, const std::string& table, Fn define) {
    Blueprint b;
    define(b);
    conn.statement(b.create_sql(table, /*if_not_exists=*/false));
}

template <typename Fn>
void create_if_not_exists(Connection& conn, const std::string& table, Fn define) {
    Blueprint b;
    define(b);
    conn.statement(b.create_sql(table, /*if_not_exists=*/true));
}

inline void drop_if_exists(Connection& conn, const std::string& table) {
    conn.statement("DROP TABLE IF EXISTS " + table);
}

} // namespace Schema
