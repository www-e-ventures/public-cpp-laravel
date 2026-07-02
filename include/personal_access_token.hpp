// personal_access_token.hpp — server-side API tokens, hashed at rest, over the ORM
// Connection contract. Optional: needs OpenSSL (via token.hpp) for hashing + random
// generation. For long-lived credentials where you own the database (the stateless
// signed tokens in token.hpp are the no-database alternative).
//
// The plaintext token is shown to the caller exactly once (on issue); only its
// SHA-256 hash is stored, so a leaked database yields no usable tokens. Lookup hashes
// the presented token and matches the stored hash. Tokens carry plain-string
// abilities (same vocabulary as token.hpp / gate.hpp) and an optional expiry.
//
// The table (default "personal_access_tokens") is owned by the app's migrations; the
// columns used are: id, user_id, name, token_hash, abilities (space-separated),
// expires_at (unix seconds, 0 = never). MemoryConnection auto-creates it.
#pragma once
#include <cstdint>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "database.hpp"
#include "token.hpp"

namespace pat {

// Returned by issue(): the row id plus the one-time plaintext token to hand out.
struct Issued {
    std::int64_t id = 0;
    std::string plaintext;
};

// An authenticated token's record (never carries the secret).
struct Record {
    std::int64_t id = 0;
    std::int64_t user_id = 0;
    std::int64_t expires_at = 0; // 0 = never
    std::string name;
    std::vector<std::string> abilities;

    bool can(const std::string& ability) const {
        for (const auto& a : abilities)
            if (a == ability || a == "*") return true;
        return false;
    }
};

class Store {
public:
    explicit Store(std::shared_ptr<Connection> db,
                   std::string table = "personal_access_tokens")
        : db_(std::move(db)), table_(std::move(table)) {}

    // Create a token for a user. Stores only the hash; returns the plaintext once.
    Issued issue(std::int64_t user_id, const std::string& name,
                 const std::vector<std::string>& abilities, std::int64_t expires_at = 0) {
        std::string plain = tok::random_token();
        Row row;
        row.set("user_id", user_id);
        row.set("name", name);
        row.set("token_hash", tok::sha256_hex(plain));
        row.set("abilities", join(abilities));
        row.set("expires_at", expires_at);
        std::int64_t id = db_->insert(table_, std::move(row));
        return {id, plain};
    }

    // Authenticate a presented plaintext token. Returns its Record if the hash matches
    // an unexpired row, else nullopt. `now` (unix seconds) is injectable for tests.
    //
    // The lookup is WHERE token_hash = ? (a bound value through the Query contract) —
    // NOT a scan of every row, so it stays O(index) as tokens accumulate. Put a
    // UNIQUE index on token_hash in the app's migration:
    //   CREATE UNIQUE INDEX idx_pat_token_hash ON personal_access_tokens(token_hash);
    // Matching on the deterministic SHA-256 in the database is the standard shape
    // (Sanctum does the same): an attacker probing timing learns which HASH bytes
    // match, and inverting SHA-256 from that is not a practical oracle.
    std::optional<Record> authenticate(const std::string& presented, std::int64_t now = 0) const {
        if (now == 0) now = static_cast<std::int64_t>(std::time(nullptr));
        Query q;
        q.wheres.push_back({"token_hash", Op::Eq, Value{tok::sha256_hex(presented)}});
        for (const auto& r : db_->select(table_, q)) {
            std::int64_t exp = r.has("expires_at") ? r.get<std::int64_t>("expires_at") : 0;
            if (exp != 0 && now > exp) return std::nullopt; // expired
            return to_record(r);
        }
        return std::nullopt;
    }

    // Revoke by row id (e.g. from a sysop dashboard). Returns false if not found.
    bool revoke(std::int64_t id) { return db_->remove(table_, id); }

private:
    static std::string join(const std::vector<std::string>& v) {
        std::string s;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (i) s += ' ';
            s += v[i];
        }
        return s;
    }
    static std::vector<std::string> split_ws(const std::string& s) {
        std::vector<std::string> out;
        std::string cur;
        for (char c : s) {
            if (c == ' ') {
                if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) out.push_back(cur);
        return out;
    }
    static Record to_record(const Row& r) {
        Record rec;
        rec.id = r.get<std::int64_t>("id");
        rec.user_id = r.has("user_id") ? r.get<std::int64_t>("user_id") : 0;
        rec.name = r.has("name") ? r.get<std::string>("name") : "";
        rec.abilities = split_ws(r.has("abilities") ? r.get<std::string>("abilities") : "");
        rec.expires_at = r.has("expires_at") ? r.get<std::int64_t>("expires_at") : 0;
        return rec;
    }

    std::shared_ptr<Connection> db_;
    std::string table_;
};

} // namespace pat
