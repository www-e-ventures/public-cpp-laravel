// sqlite_connection.hpp — a real SQL backend for the Connection contract.
// Laravel: Illuminate\Database\SQLiteConnection.
//
// Optional add-on: keeps the core framework zero-dependency. Builds only when
// libsqlite3 is found (CMake-gated); link against it explicitly. Translates the
// backend-neutral Query (from QueryBuilder) into SQL, so nothing above it changes.
#pragma once
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "database.hpp"

struct sqlite3; // opaque, avoids leaking <sqlite3.h> into the header

class SqliteConnection : public Connection {
public:
    explicit SqliteConnection(const std::string& path); // ":memory:" or a file
    ~SqliteConnection() override;

    SqliteConnection(const SqliteConnection&) = delete;
    SqliteConnection& operator=(const SqliteConnection&) = delete;

    // Throws std::runtime_error when SQLite refuses the write (constraint violation,
    // missing table, ...) — a failed insert must not look like a success with id 0.
    std::int64_t insert(const std::string& table, Row row) override;
    std::optional<Row> find(const std::string& table, std::int64_t id) const override;
    std::vector<Row> all(const std::string& table) const override;
    std::vector<Row> select(const std::string& table, const Query& query) const override;
    bool update(const std::string& table, std::int64_t id, const Row& row) override;
    bool remove(const std::string& table, std::int64_t id) override;
    void statement(const std::string& sql) override;

    // SELECT COUNT(*) — the database produces the scalar; no rows are loaded.
    std::size_t count(const std::string& table, const Query& query = {}) const override;

    // One UPDATE ... WHERE id = ? AND guard = ? — atomic in the database, the
    // queue's claim primitive.
    bool update_if(const std::string& table, std::int64_t id, const std::string& guard_col,
                   const Value& guard_value, const Row& row) override;

    // BEGIN / COMMIT / ROLLBACK. The connection is shared across threads, so a
    // transaction brackets everything that lands while it's open — keep them short
    // (see the transaction() helper in database.hpp).
    void begin() override;
    void commit() override;
    void rollback() override;

private:
    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_; // serialize access (thread-safe across worker threads)
};
