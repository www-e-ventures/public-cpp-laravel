// repository.hpp — generic typed repository + fluent query builder over a Connection
//
// The repository-style alternative to Eloquent's ActiveRecord:
// models are plain typed structs; a hand-written Mapper bridges model <-> Row.
// No magic attribute access, no global state — dependencies are explicit and the whole
// thing is compile-checked.
//
// Mapper<T> must provide:
//   static std::string  table();
//   static T            hydrate(const Row&);
//   static Row          to_row(const T&);
//   static std::int64_t id(const T&);
//   static void         set_id(T&, std::int64_t);
#pragma once
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "database.hpp"

// A page of results (cf. Eloquent's LengthAwarePaginator).
template <typename T>
struct Page {
    std::vector<T> items;
    std::size_t total = 0;     // rows matching the query (ignoring page limits)
    std::size_t page = 1;      // 1-based
    std::size_t per_page = 0;
    std::size_t last_page = 1;
};

// Fluent SELECT builder (cf. Eloquent's query builder). Accumulates a backend-neutral
// Query and hydrates results through the Mapper:
//   repo.query().where("published", Value{true}).order_by("views", true).limit(10).get();
template <typename T, typename Mapper>
class QueryBuilder {
public:
    explicit QueryBuilder(Connection& conn) : conn_(&conn) {}

    // Column names are spliced into SQL (they can't be bound like values), so an app
    // that maps user input to a column — /articles?sort=views -> order_by(sort) —
    // would be injectable. Refuse anything that isn't a plain identifier, loudly:
    // the throw happens at the call site naming the bad column, and the HTTP layer
    // turns it into a 500 rather than executing attacker SQL. (Values are safe —
    // they're bound as parameters by the SQL backends.)
    QueryBuilder& where(const std::string& column, Value value) {
        query_.wheres.push_back({checked(column), Op::Eq, std::move(value)});
        return *this;
    }
    QueryBuilder& where(const std::string& column, Op op, Value value) {
        query_.wheres.push_back({checked(column), op, std::move(value)});
        return *this;
    }
    QueryBuilder& where_in(const std::string& column, std::vector<Value> values) {
        WhereClause w;
        w.column = checked(column);
        w.op = Op::In;
        w.values = std::move(values);
        query_.wheres.push_back(std::move(w));
        return *this;
    }
    QueryBuilder& order_by(const std::string& column, bool descending = false) {
        query_.order = OrderClause{checked(column), descending};
        return *this;
    }
    QueryBuilder& limit(std::size_t n) {
        query_.limit = n;
        return *this;
    }
    QueryBuilder& offset(std::size_t n) {
        query_.offset = n;
        return *this;
    }

    // One page of results plus the total count (ignores any limit/offset already set).
    Page<T> paginate(std::size_t page, std::size_t per_page) const {
        Query base = query_;
        base.limit.reset();
        base.offset.reset();
        std::size_t total = conn_->select(Mapper::table(), base).size();

        Query q = base;
        q.limit = per_page;
        q.offset = (page == 0 ? 0 : page - 1) * per_page;
        std::vector<T> items;
        for (const auto& r : conn_->select(Mapper::table(), q)) items.push_back(Mapper::hydrate(r));

        std::size_t last_page = per_page ? (total + per_page - 1) / per_page : 1;
        return Page<T>{std::move(items), total, page, per_page, last_page};
    }

    std::vector<T> get() const {
        std::vector<T> out;
        for (const auto& r : conn_->select(Mapper::table(), query_)) out.push_back(Mapper::hydrate(r));
        return out;
    }

    std::optional<T> first() const {
        Query q = query_;
        q.limit = 1;
        auto rows = conn_->select(Mapper::table(), q);
        if (rows.empty()) return std::nullopt;
        return Mapper::hydrate(rows.front());
    }

    std::size_t count() const { return conn_->select(Mapper::table(), query_).size(); }

private:
    static const std::string& checked(const std::string& column) {
        if (!is_sql_identifier(column))
            throw std::invalid_argument("unsafe SQL identifier: " + column);
        return column;
    }

    Connection* conn_;
    Query query_;
};

template <typename T, typename Mapper>
class Repository {
public:
    explicit Repository(std::shared_ptr<Connection> conn) : conn_(std::move(conn)) {}

    std::optional<T> find(std::int64_t id) const {
        auto row = conn_->find(Mapper::table(), id);
        if (!row) return std::nullopt;
        return Mapper::hydrate(*row);
    }

    std::vector<T> all() const {
        std::vector<T> out;
        for (const auto& r : conn_->all(Mapper::table())) out.push_back(Mapper::hydrate(r));
        return out;
    }

    // Start a fluent query.
    QueryBuilder<T, Mapper> query() const { return QueryBuilder<T, Mapper>(*conn_); }

    // Convenience for the common single-equality case.
    std::vector<T> where(const std::string& column, Value value) const {
        return query().where(column, std::move(value)).get();
    }

    // Persists a new model and writes the connection-assigned id back into it.
    void insert(T& model) const {
        Row row = Mapper::to_row(model);
        row.data.erase("id"); // the connection assigns the id
        Mapper::set_id(model, conn_->insert(Mapper::table(), std::move(row)));
    }

    bool update(const T& model) const {
        return conn_->update(Mapper::table(), Mapper::id(model), Mapper::to_row(model));
    }

    bool remove(const T& model) const {
        return conn_->remove(Mapper::table(), Mapper::id(model));
    }

private:
    std::shared_ptr<Connection> conn_;
};
