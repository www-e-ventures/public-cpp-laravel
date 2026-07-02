// database.cpp — see database.hpp (MemoryConnection)
#include "database.hpp"

#include <algorithm>

namespace {
// Compare a cell against a clause value. Columns hold one type, so the variant
// relational operators compare like-with-like; mismatched alternatives compare by
// index, which we never rely on.
bool matches(const Value& cell, Op op, const Value& v) {
    switch (op) {
        case Op::Eq:  return cell == v;
        case Op::Ne:  return cell != v;
        case Op::Lt:  return cell < v;
        case Op::Lte: return cell <= v;
        case Op::Gt:  return cell > v;
        case Op::Gte: return cell >= v;
        case Op::In:  return false; // handled in select() against the value list
    }
    return false;
}
} // namespace

std::int64_t MemoryConnection::insert(const std::string& table, Row row) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::int64_t id = ++next_id_[table]; // ids start at 1
    row.set("id", id);
    tables_[table].push_back(std::move(row));
    return id;
}

std::optional<Row> MemoryConnection::find(const std::string& table, std::int64_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto t = tables_.find(table);
    if (t == tables_.end()) return std::nullopt;
    for (const auto& r : t->second)
        if (r.has("id") && r.get<std::int64_t>("id") == id) return r;
    return std::nullopt;
}

std::vector<Row> MemoryConnection::all(const std::string& table) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto t = tables_.find(table);
    if (t == tables_.end()) return {};
    return t->second;
}

std::vector<Row> MemoryConnection::select(const std::string& table, const Query& query) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Row> rows;
    auto t = tables_.find(table);
    if (t == tables_.end()) return rows;

    // Filter: a row must satisfy every where clause.
    for (const auto& r : t->second) {
        bool ok = true;
        for (const auto& w : query.wheres) {
            auto it = r.data.find(w.column);
            if (it == r.data.end()) { ok = false; break; }
            if (w.op == Op::In) {
                bool found = false;
                for (const auto& v : w.values)
                    if (it->second == v) { found = true; break; }
                if (!found) { ok = false; break; }
            } else if (!matches(it->second, w.op, w.value)) {
                ok = false;
                break;
            }
        }
        if (ok) rows.push_back(r);
    }

    // Order (stable, so equal keys keep insertion order).
    if (query.order) {
        const auto& oc = *query.order;
        std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
            const Value& av = a.data.at(oc.column);
            const Value& bv = b.data.at(oc.column);
            return oc.descending ? (bv < av) : (av < bv);
        });
    }

    // Offset, then limit.
    if (query.offset) {
        if (*query.offset >= rows.size()) rows.clear();
        else rows.erase(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(*query.offset));
    }
    if (query.limit && rows.size() > *query.limit) rows.resize(*query.limit);
    return rows;
}

bool MemoryConnection::update(const std::string& table, std::int64_t id, const Row& row) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto t = tables_.find(table);
    if (t == tables_.end()) return false;
    for (auto& r : t->second) {
        if (r.has("id") && r.get<std::int64_t>("id") == id) {
            r = row;
            r.set("id", id); // id is immutable regardless of the supplied row
            return true;
        }
    }
    return false;
}

bool MemoryConnection::remove(const std::string& table, std::int64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto t = tables_.find(table);
    if (t == tables_.end()) return false;
    auto& rows = t->second;
    auto before = rows.size();
    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [&](const Row& r) {
                                  return r.has("id") && r.get<std::int64_t>("id") == id;
                              }),
               rows.end());
    return rows.size() != before;
}

bool MemoryConnection::update_if(const std::string& table, std::int64_t id,
                                 const std::string& guard_col, const Value& guard_value,
                                 const Row& row) {
    // One lock across the compare AND the write — a real compare-and-set, so two
    // queue workers racing to claim the same job can't both win.
    std::lock_guard<std::mutex> lock(mutex_);
    auto t = tables_.find(table);
    if (t == tables_.end()) return false;
    for (auto& r : t->second) {
        if (!r.has("id") || r.get<std::int64_t>("id") != id) continue;
        auto g = r.data.find(guard_col);
        if (g == r.data.end() || !(g->second == guard_value)) return false;
        r = row;
        r.set("id", id);
        return true;
    }
    return false;
}

void MemoryConnection::begin() {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = std::make_pair(tables_, next_id_);
}

void MemoryConnection::commit() {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.reset();
}

void MemoryConnection::rollback() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_) return; // rollback with no open transaction: no-op
    tables_ = std::move(snapshot_->first);
    next_id_ = std::move(snapshot_->second);
    snapshot_.reset();
}
