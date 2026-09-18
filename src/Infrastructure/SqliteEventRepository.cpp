#include "DeviceLink/Infrastructure/SqliteEventRepository.h"

#include <sqlite3.h>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace DeviceLink::Infrastructure
{
namespace
{
[[noreturn]] void ThrowSqliteError(sqlite3* database, const char* context)
{
    throw std::runtime_error(std::string(context) + ": " + sqlite3_errmsg(database));
}

class Statement final
{
public:
    Statement(sqlite3* database, const char* sql)
    {
        if (sqlite3_prepare_v2(database, sql, -1, &m_statement, nullptr) != SQLITE_OK)
        {
            ThrowSqliteError(database, "SQLite statement preparation failed");
        }
    }

    ~Statement()
    {
        if (m_statement != nullptr)
        {
            sqlite3_finalize(m_statement);
        }
    }

    [[nodiscard]] sqlite3_stmt* Get() const noexcept
    {
        return m_statement;
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

private:
    sqlite3_stmt* m_statement{};
};

void EnsurePayloadColumn(sqlite3* database)
{
    Statement tableInfo(database, "PRAGMA table_info(event_log);");
    bool hasPayloadColumn{};
    while (sqlite3_step(tableInfo.Get()) == SQLITE_ROW)
    {
        const auto* const columnName = sqlite3_column_text(tableInfo.Get(), 1);
        if (columnName != nullptr && std::string_view(
                reinterpret_cast<const char*>(columnName)) == "payload")
        {
            hasPayloadColumn = true;
            break;
        }
    }

    if (!hasPayloadColumn &&
        sqlite3_exec(database, "ALTER TABLE event_log ADD COLUMN payload BLOB;",
            nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        ThrowSqliteError(database, "SQLite event payload migration failed");
    }
}

[[nodiscard]] EventLogEntry ReadEventLogEntry(sqlite3_stmt* statement)
{
    EventLogEntry entry{
        .timestampUnixMilliseconds = sqlite3_column_int64(statement, 0),
        .deviceId = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1)),
        .category = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2)),
        .detail = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3)),
    };
    const int payloadSize = sqlite3_column_bytes(statement, 4);
    const auto* const payload = static_cast<const std::byte*>(sqlite3_column_blob(statement, 4));
    if (payload != nullptr && payloadSize > 0)
    {
        entry.payload.assign(payload, payload + payloadSize);
    }
    return entry;
}
} // namespace

struct SqliteEventRepository::Impl final
{
    explicit Impl(const std::filesystem::path& databasePath)
    {
        const auto pathText = databasePath.string();
        if (sqlite3_open_v2(pathText.c_str(), &database,
                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                nullptr) != SQLITE_OK)
        {
            const std::string message = database == nullptr ? "SQLite database opening failed" :
                std::string("SQLite database opening failed: ") + sqlite3_errmsg(database);
            if (database != nullptr)
            {
                sqlite3_close(database);
            }
            throw std::runtime_error(message);
        }

        const char* createTable =
            "CREATE TABLE IF NOT EXISTS event_log ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "timestamp_unix_ms INTEGER NOT NULL,"
            "device_id TEXT NOT NULL,"
            "category TEXT NOT NULL,"
            "detail TEXT NOT NULL,"
            "payload BLOB);";
        char* errorMessage{};
        if (sqlite3_exec(database, createTable, nullptr, nullptr, &errorMessage) != SQLITE_OK)
        {
            const std::string message = errorMessage == nullptr ? "SQLite schema creation failed" :
                std::string("SQLite schema creation failed: ") + errorMessage;
            sqlite3_free(errorMessage);
            sqlite3_close(database);
            database = nullptr;
            throw std::runtime_error(message);
        }
        EnsurePayloadColumn(database);
        if (sqlite3_exec(database,
                "CREATE INDEX IF NOT EXISTS idx_event_log_device_category_id "
                "ON event_log (device_id, category, id);",
                nullptr, nullptr, &errorMessage) != SQLITE_OK)
        {
            const std::string message = errorMessage == nullptr ? "SQLite index creation failed" :
                std::string("SQLite index creation failed: ") + errorMessage;
            sqlite3_free(errorMessage);
            sqlite3_close(database);
            database = nullptr;
            throw std::runtime_error(message);
        }
    }

    ~Impl()
    {
        if (database != nullptr)
        {
            sqlite3_close(database);
        }
    }

    sqlite3* database{};
};

SqliteEventRepository::SqliteEventRepository(const std::filesystem::path& databasePath)
    : m_impl(std::make_unique<Impl>(databasePath))
{
}

SqliteEventRepository::~SqliteEventRepository() = default;

void SqliteEventRepository::Append(const EventLogEntry& entry)
{
    Statement statement(m_impl->database,
        "INSERT INTO event_log (timestamp_unix_ms, device_id, category, detail, payload) "
        "VALUES (?, ?, ?, ?, ?);");
    sqlite3_stmt* const rawStatement = statement.Get();
    if (sqlite3_bind_int64(rawStatement, 1, entry.timestampUnixMilliseconds) != SQLITE_OK ||
        sqlite3_bind_text(rawStatement, 2, entry.deviceId.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(rawStatement, 3, entry.category.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(rawStatement, 4, entry.detail.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        (entry.payload.empty() ? sqlite3_bind_null(rawStatement, 5) :
            sqlite3_bind_blob(rawStatement, 5, entry.payload.data(),
                static_cast<int>(entry.payload.size()), SQLITE_TRANSIENT)) != SQLITE_OK ||
        sqlite3_step(rawStatement) != SQLITE_DONE)
    {
        ThrowSqliteError(m_impl->database, "SQLite event insertion failed");
    }
}

std::vector<EventLogEntry> SqliteEventRepository::ReadAll() const
{
    Statement statement(m_impl->database,
        "SELECT timestamp_unix_ms, device_id, category, detail, payload FROM event_log ORDER BY id;");
    std::vector<EventLogEntry> entries;
    while (true)
    {
        const int result = sqlite3_step(statement.Get());
        if (result == SQLITE_DONE)
        {
            return entries;
        }
        if (result != SQLITE_ROW)
        {
            ThrowSqliteError(m_impl->database, "SQLite event query failed");
        }

        entries.push_back(ReadEventLogEntry(statement.Get()));
    }
}

std::vector<EventLogEntry> SqliteEventRepository::ReadRecent(std::size_t maximumEntryCount) const
{
    if (maximumEntryCount == 0)
    {
        return {};
    }

    Statement statement(m_impl->database,
        "SELECT timestamp_unix_ms, device_id, category, detail, payload FROM "
        "(SELECT timestamp_unix_ms, device_id, category, detail, payload, id FROM event_log "
        "ORDER BY id DESC LIMIT ?) ORDER BY id;");
    const auto maximumSqliteCount = static_cast<std::size_t>(
        (std::numeric_limits<std::int64_t>::max)());
    const auto boundedCount = (std::min)(maximumEntryCount, maximumSqliteCount);
    if (sqlite3_bind_int64(statement.Get(), 1, static_cast<std::int64_t>(boundedCount)) != SQLITE_OK)
    {
        ThrowSqliteError(m_impl->database, "SQLite recent event query binding failed");
    }

    std::vector<EventLogEntry> entries;
    entries.reserve(boundedCount);
    while (true)
    {
        const int result = sqlite3_step(statement.Get());
        if (result == SQLITE_DONE)
        {
            return entries;
        }
        if (result != SQLITE_ROW)
        {
            ThrowSqliteError(m_impl->database, "SQLite recent event query failed");
        }
        entries.push_back(ReadEventLogEntry(statement.Get()));
    }
}

std::vector<StoredFramePayload> SqliteEventRepository::ReadFramePayloads(
    const std::string& deviceId, const std::string& category) const
{
    Statement statement(m_impl->database,
        "SELECT timestamp_unix_ms, payload FROM event_log "
        "WHERE device_id = ? AND category = ? AND payload IS NOT NULL ORDER BY id;");
    if (sqlite3_bind_text(statement.Get(), 1, deviceId.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement.Get(), 2, category.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
    {
        ThrowSqliteError(m_impl->database, "SQLite frame query binding failed");
    }

    std::vector<StoredFramePayload> frames;
    while (true)
    {
        const int result = sqlite3_step(statement.Get());
        if (result == SQLITE_DONE)
        {
            return frames;
        }
        if (result != SQLITE_ROW)
        {
            ThrowSqliteError(m_impl->database, "SQLite frame query failed");
        }

        const int payloadSize = sqlite3_column_bytes(statement.Get(), 1);
        const auto* const payload = static_cast<const std::byte*>(
            sqlite3_column_blob(statement.Get(), 1));
        if (payload != nullptr && payloadSize > 0)
        {
            frames.push_back({
                .timestampUnixMilliseconds = sqlite3_column_int64(statement.Get(), 0),
                .payload = std::vector<std::byte>(payload, payload + payloadSize),
            });
        }
    }
}

std::size_t SqliteEventRepository::PruneBefore(std::int64_t timestampUnixMilliseconds)
{
    Statement statement(m_impl->database,
        "DELETE FROM event_log WHERE timestamp_unix_ms < ?;");
    if (sqlite3_bind_int64(statement.Get(), 1, timestampUnixMilliseconds) != SQLITE_OK ||
        sqlite3_step(statement.Get()) != SQLITE_DONE)
    {
        ThrowSqliteError(m_impl->database, "SQLite event pruning failed");
    }
    return static_cast<std::size_t>(sqlite3_changes(m_impl->database));
}
} // namespace DeviceLink::Infrastructure
