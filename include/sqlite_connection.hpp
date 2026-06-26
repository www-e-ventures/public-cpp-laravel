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

    std::int64_t insert(const std::string& table, Row row) override;
    std::optional<Row> find(const std::string& table, std::int64_t id) const override;
    std::vector<Row> all(const std::string& table) const override;
    std::vector<Row> select(const std::string& table, const Query& query) const override;
    bool update(const std::string& table, std::int64_t id, const Row& row) override;
    bool remove(const std::string& table, std::int64_t id) override;
    void statement(const std::string& sql) override;

private:
    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_; // serialize access (thread-safe across worker threads)
};
