// database.hpp — connection contract + value/row wire types + query description
// Laravel: Illuminate\Database\ConnectionInterface (loosely)
//
// `Value`/`Row` are the dynamically-typed DB wire format; they live ONLY behind the
// mapper. `Query` is a backend-neutral description of a SELECT (wheres/order/limit)
// that a Connection executes — so the fluent QueryBuilder (repository.hpp) stays
// driver-agnostic and a SQL backend would translate Query -> SQL.
#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

// A single cell. C++20's variant converting-constructor rules (P0608) make
// `Value{"x"}` select std::string rather than bool, so string literals are safe.
using Value = std::variant<std::int64_t, double, std::string, bool>;

// Coerce a cell to T. Backends differ in how they represent values — SQLite has no
// real bool and returns INTEGER, so get<bool> must accept an int64. Coercing here
// keeps mappers identical across the in-memory and SQL backends.
template <typename T>
T value_as(const Value& v) {
    return std::visit(
        [](const auto& x) -> T {
            using X = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::string>) {
                if constexpr (std::is_same_v<X, std::string>) return x;
                else if constexpr (std::is_same_v<X, bool>) return x ? "1" : "0";
                else return std::to_string(x);
            } else if constexpr (std::is_same_v<T, bool>) {
                if constexpr (std::is_same_v<X, std::string>) return !x.empty() && x != "0";
                else return static_cast<bool>(x);
            } else { // numeric T (std::int64_t / double)
                if constexpr (std::is_same_v<X, std::string>) return static_cast<T>(0);
                else return static_cast<T>(x);
            }
        },
        v);
}

struct Row {
    std::unordered_map<std::string, Value> data;

    template <typename T>
    T get(const std::string& key) const { return value_as<T>(data.at(key)); }

    void set(const std::string& key, Value v) { data[key] = std::move(v); }
    bool has(const std::string& key) const { return data.find(key) != data.end(); }
};

// True for a safe SQL identifier: [A-Za-z_][A-Za-z0-9_]*. Column/table names are
// spliced into SQL as text (they can't be bound like values), so anything that
// reaches a query from user input — a ?sort= parameter mapped to order_by(), say —
// must pass this or be refused. The QueryBuilder and the SQL backends both check.
inline bool is_sql_identifier(const std::string& s) {
    if (s.empty()) return false;
    auto word = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    if (!word(s[0])) return false;
    for (char c : s)
        if (!word(c) && !(c >= '0' && c <= '9')) return false;
    return true;
}

// Comparison operators for a where clause. `In` matches against `values`.
enum class Op { Eq, Ne, Lt, Lte, Gt, Gte, In };

struct WhereClause {
    std::string column;
    Op op;
    Value value;
    std::vector<Value> values{}; // used by Op::In (default so {col,op,value} inits still work)
};

struct OrderClause {
    std::string column;
    bool descending;
};

// A backend-neutral SELECT description. Empty == "all rows".
struct Query {
    std::vector<WhereClause> wheres;       // ANDed together
    std::optional<OrderClause> order;
    std::optional<std::size_t> limit;
    std::optional<std::size_t> offset;
};

// The connection contract. Slice 1/2 ship only MemoryConnection; a SQL driver can
// implement this later without Repository or QueryBuilder changing.
class Connection {
public:
    virtual ~Connection() = default;
    virtual std::int64_t insert(const std::string& table, Row row) = 0; // returns new id
    virtual std::optional<Row> find(const std::string& table, std::int64_t id) const = 0;
    virtual std::vector<Row> all(const std::string& table) const = 0;
    virtual std::vector<Row> select(const std::string& table, const Query& query) const = 0;
    virtual bool update(const std::string& table, std::int64_t id, const Row& row) = 0;
    virtual bool remove(const std::string& table, std::int64_t id) = 0;
    // Execute raw DDL/SQL (migrations). Schemaless backends may treat this as a no-op.
    virtual void statement(const std::string& sql) = 0;
};

// In-memory backend: table name -> rows, with per-table auto-increment ids.
// Zero-dependency (true to the working agreements); not thread-safe (slice scope).
class MemoryConnection : public Connection {
public:
    std::int64_t insert(const std::string& table, Row row) override;
    std::optional<Row> find(const std::string& table, std::int64_t id) const override;
    std::vector<Row> all(const std::string& table) const override;
    std::vector<Row> select(const std::string& table, const Query& query) const override;
    bool update(const std::string& table, std::int64_t id, const Row& row) override;
    bool remove(const std::string& table, std::int64_t id) override;
    void statement(const std::string&) override {} // schemaless: tables auto-create on insert

private:
    std::unordered_map<std::string, std::vector<Row>> tables_;
    std::unordered_map<std::string, std::int64_t> next_id_;
    mutable std::mutex mutex_; // thread-safe: shared across HTTP worker threads
};
