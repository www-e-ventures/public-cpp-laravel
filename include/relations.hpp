// relations.hpp — explicit relationships (Eloquent: hasMany/belongsTo/hasOne)
//
// No dynamic `$model->comments` magic — each relationship is a free function that
// returns a QueryBuilder scoped by the foreign key, so callers can chain further
// constraints (e.g. .where("approved", Value{true}).get()). This is the honest
// repository-style mapping: relationships are queries.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "database.hpp"
#include "repository.hpp"

// Parent has many Child: Child rows whose `foreign_key` == parent id.
template <typename Child, typename ChildMapper>
QueryBuilder<Child, ChildMapper> has_many(Connection& c, const std::string& foreign_key,
                                          std::int64_t parent_id) {
    return QueryBuilder<Child, ChildMapper>(c).where(foreign_key, Value{parent_id});
}

// Parent has one Child: the first such row.
template <typename Child, typename ChildMapper>
std::optional<Child> has_one(Connection& c, const std::string& foreign_key,
                             std::int64_t parent_id) {
    return has_many<Child, ChildMapper>(c, foreign_key, parent_id).first();
}

// Child belongs to Parent: the parent row with id == foreign_id.
template <typename Parent, typename ParentMapper>
std::optional<Parent> belongs_to(Connection& c, std::int64_t foreign_id) {
    return QueryBuilder<Parent, ParentMapper>(c).where("id", Value{foreign_id}).first();
}

// Eager-load a has-many for many parents at once, avoiding N+1: one WHERE fk IN (...)
// query, results grouped by foreign key. `key_of` reads the FK from a Child (since the
// model is a typed struct with no generic field access). Returns fk -> children.
template <typename Child, typename ChildMapper, typename KeyFn>
std::unordered_map<std::int64_t, std::vector<Child>> load_has_many(
    Connection& c, const std::string& foreign_key, const std::vector<std::int64_t>& parent_ids,
    KeyFn key_of) {
    std::vector<Value> values;
    values.reserve(parent_ids.size());
    for (auto id : parent_ids) values.push_back(Value{id});

    std::unordered_map<std::int64_t, std::vector<Child>> grouped;
    for (const auto& child :
         QueryBuilder<Child, ChildMapper>(c).where_in(foreign_key, std::move(values)).get())
        grouped[key_of(child)].push_back(child);
    return grouped;
}
