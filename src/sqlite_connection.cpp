// sqlite_connection.cpp — see sqlite_connection.hpp
#include "sqlite_connection.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <utility>
#include <vector>

namespace {

const char* op_sql(Op op) {
    switch (op) {
        case Op::Eq:  return "=";
        case Op::Ne:  return "!=";
        case Op::Lt:  return "<";
        case Op::Lte: return "<=";
        case Op::Gt:  return ">";
        case Op::Gte: return ">=";
    }
    return "=";
}

void bind_value(sqlite3_stmt* stmt, int idx, const Value& v) {
    std::visit(
        [&](const auto& x) {
            using X = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<X, std::int64_t>) sqlite3_bind_int64(stmt, idx, x);
            else if constexpr (std::is_same_v<X, double>) sqlite3_bind_double(stmt, idx, x);
            else if constexpr (std::is_same_v<X, bool>) sqlite3_bind_int64(stmt, idx, x ? 1 : 0);
            else sqlite3_bind_text(stmt, idx, x.c_str(), -1, SQLITE_TRANSIENT);
        },
        v);
}

Value read_column(sqlite3_stmt* stmt, int i) {
    switch (sqlite3_column_type(stmt, i)) {
        case SQLITE_INTEGER:
            return Value{static_cast<std::int64_t>(sqlite3_column_int64(stmt, i))};
        case SQLITE_FLOAT:
            return Value{sqlite3_column_double(stmt, i)};
        case SQLITE_TEXT: {
            auto* t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            return Value{std::string(t ? t : "")};
        }
        default:
            return Value{std::string("")};
    }
}

Row read_row(sqlite3_stmt* stmt) {
    Row row;
    int cols = sqlite3_column_count(stmt);
    for (int i = 0; i < cols; ++i)
        row.set(sqlite3_column_name(stmt, i), read_column(stmt, i));
    return row;
}

// Identifiers (table/column names) are spliced into the SQL text — they cannot be
// bound like values — so they must be plain identifiers. The QueryBuilder already
// refuses bad ones at the API boundary; this is the backend's own line of defense
// for callers that build Query/Row objects directly.
std::string ident(const std::string& name) {
    if (!is_sql_identifier(name))
        throw std::invalid_argument("sqlite: unsafe SQL identifier: " + name);
    return name;
}

} // namespace

SqliteConnection::SqliteConnection(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK)
        throw std::runtime_error("sqlite: cannot open " + path);
}

SqliteConnection::~SqliteConnection() { sqlite3_close(db_); }

void SqliteConnection::statement(const std::string& sql) {
    std::lock_guard<std::mutex> lock(mutex_);
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("sqlite: " + msg + " [" + sql + "]");
    }
}

std::int64_t SqliteConnection::insert(const std::string& table, Row row) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, Value>> cols(row.data.begin(), row.data.end());
    std::string names, marks;
    for (std::size_t i = 0; i < cols.size(); ++i) {
        if (i) { names += ", "; marks += ", "; }
        names += ident(cols[i].first);
        marks += "?";
    }
    std::string sql = "INSERT INTO " + ident(table) + " (" + names + ") VALUES (" + marks + ")";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return 0;
    for (std::size_t i = 0; i < cols.size(); ++i)
        bind_value(stmt, static_cast<int>(i + 1), cols[i].second);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return sqlite3_last_insert_rowid(db_);
}

std::optional<Row> SqliteConnection::find(const std::string& table, std::int64_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string sql = "SELECT * FROM " + ident(table) + " WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_int64(stmt, 1, id);
    std::optional<Row> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = read_row(stmt);
    sqlite3_finalize(stmt);
    return result;
}

std::vector<Row> SqliteConnection::all(const std::string& table) const {
    return select(table, Query{}); // select() takes the lock
}

std::vector<Row> SqliteConnection::select(const std::string& table, const Query& query) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string sql = "SELECT * FROM " + ident(table);
    for (std::size_t i = 0; i < query.wheres.size(); ++i) {
        const auto& w = query.wheres[i];
        sql += (i == 0 ? " WHERE " : " AND ");
        if (w.op == Op::In) {
            sql += ident(w.column) + " IN (";
            for (std::size_t j = 0; j < w.values.size(); ++j) sql += (j ? ",?" : "?");
            sql += ")";
        } else {
            sql += ident(w.column) + " " + op_sql(w.op) + " ?";
        }
    }
    if (query.order)
        sql += " ORDER BY " + ident(query.order->column) + (query.order->descending ? " DESC" : " ASC");
    if (query.limit) sql += " LIMIT " + std::to_string(*query.limit);
    if (query.offset) {
        if (!query.limit) sql += " LIMIT -1"; // SQLite requires LIMIT before OFFSET
        sql += " OFFSET " + std::to_string(*query.offset);
    }

    std::vector<Row> rows;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return rows;
    int idx = 1;
    for (const auto& w : query.wheres) {
        if (w.op == Op::In)
            for (const auto& v : w.values) bind_value(stmt, idx++, v);
        else
            bind_value(stmt, idx++, w.value);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) rows.push_back(read_row(stmt));
    sqlite3_finalize(stmt);
    return rows;
}

bool SqliteConnection::update(const std::string& table, std::int64_t id, const Row& row) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, Value>> cols;
    for (const auto& kv : row.data)
        if (kv.first != "id") cols.push_back(kv); // never reassign the PK

    std::string sets;
    for (std::size_t i = 0; i < cols.size(); ++i) {
        if (i) sets += ", ";
        sets += ident(cols[i].first) + " = ?";
    }
    std::string sql = "UPDATE " + ident(table) + " SET " + sets + " WHERE id = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    int idx = 1;
    for (const auto& c : cols) bind_value(stmt, idx++, c.second);
    sqlite3_bind_int64(stmt, idx, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return sqlite3_changes(db_) > 0;
}

bool SqliteConnection::remove(const std::string& table, std::int64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string sql = "DELETE FROM " + ident(table) + " WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return sqlite3_changes(db_) > 0;
}
