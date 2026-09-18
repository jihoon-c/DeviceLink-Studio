#include "DeviceLink/Infrastructure/AsyncLogger.h"
#include "DeviceLink/Infrastructure/AsyncEventStore.h"
#include "DeviceLink/Infrastructure/AtomicTextFileStore.h"
#include "DeviceLink/Infrastructure/SqliteEventRepository.h"
#include "DeviceLink/Infrastructure/TextFileLogger.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

class TestLogFile final
{
public:
    TestLogFile()
    {
        const auto uniqueSuffix = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        m_path = std::filesystem::temp_directory_path() /
            ("DeviceLinkInfrastructureTest-" + uniqueSuffix + ".log");
    }

    ~TestLogFile()
    {
        std::error_code error;
        std::filesystem::remove(m_path, error);
    }

    [[nodiscard]] const std::filesystem::path& Path() const noexcept
    {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

class TestDatabase final
{
public:
    TestDatabase()
    {
        const auto uniqueSuffix = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        m_path = std::filesystem::temp_directory_path() /
            ("DeviceLinkInfrastructureTest-" + uniqueSuffix + ".sqlite");
    }

    ~TestDatabase()
    {
        std::error_code error;
        std::filesystem::remove(m_path, error);
    }

    [[nodiscard]] const std::filesystem::path& Path() const noexcept
    {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

class RecordingLogger final : public DeviceLink::Infrastructure::ILogger
{
public:
    struct Entry
    {
        DeviceLink::Infrastructure::LogLevel level;
        std::string message;
    };

    void Log(DeviceLink::Infrastructure::LogLevel level, std::string_view message) override
    {
        const std::scoped_lock lock(m_mutex);
        m_entries.push_back(Entry{level, std::string(message)});
    }

    [[nodiscard]] std::vector<Entry> Entries() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_entries;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<Entry> m_entries;
};

void TextFileLoggerWritesLevelAndMessage()
{
    TestLogFile logFile;
    {
        DeviceLink::Infrastructure::TextFileLogger logger(logFile.Path());
        logger.Log(DeviceLink::Infrastructure::LogLevel::Info, "Connected to simulator");
        logger.Log(DeviceLink::Infrastructure::LogLevel::Error, "Connection lost");
    }

    std::ifstream input(logFile.Path());
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    Require(contents.find("[INFO] Connected to simulator") != std::string::npos,
            "Info log entry differs");
    Require(contents.find("[ERROR] Connection lost") != std::string::npos,
            "Error log entry differs");
}

void AsyncLoggerDrainsEntriesBeforeShutdown()
{
    auto sink = std::make_unique<RecordingLogger>();
    auto* const recordingSink = sink.get();
    DeviceLink::Infrastructure::AsyncLogger logger(std::move(sink));

    logger.Log(DeviceLink::Infrastructure::LogLevel::Info, "connect requested");
    logger.Log(DeviceLink::Infrastructure::LogLevel::Warning, "connection delayed");
    logger.Flush();

    const auto entries = recordingSink->Entries();
    Require(entries.size() == 2, "Async logger must deliver every queued entry");
    Require(entries[0].level == DeviceLink::Infrastructure::LogLevel::Info &&
                entries[0].message == "connect requested",
            "Async logger must preserve entry order");
    Require(entries[1].level == DeviceLink::Infrastructure::LogLevel::Warning &&
                entries[1].message == "connection delayed",
            "Async logger must preserve entry contents");

    logger.Shutdown();
    logger.Log(DeviceLink::Infrastructure::LogLevel::Error, "must not be queued after shutdown");
    Require(logger.DroppedEntryCount() == 1, "Entries submitted after shutdown must be rejected");
    Require(logger.FailedEntryCount() == 0, "Recording sink must not report write failures");
}

void AsyncEventStorePersistsEntriesInSqlite()
{
    TestDatabase database;
    {
        auto repository = std::make_unique<DeviceLink::Infrastructure::SqliteEventRepository>(
            database.Path());
        DeviceLink::Infrastructure::AsyncEventStore store(std::move(repository));
        store.Append({
            .timestampUnixMilliseconds = 1000,
            .deviceId = "device-a",
            .category = "connection",
            .detail = "connected",
        });
        store.Append({
            .timestampUnixMilliseconds = 1010,
            .deviceId = "device-a",
            .category = "frame",
            .detail = "sequence=7",
        });
        store.Flush();
        store.Shutdown();
        store.Append({
            .timestampUnixMilliseconds = 1020,
            .deviceId = "device-a",
            .category = "ignored",
            .detail = "after shutdown",
        });
        Require(store.DroppedEntryCount() == 1, "Entries submitted after store shutdown must drop");
        Require(store.FailedEntryCount() == 0, "SQLite event store write failed");
    }

    DeviceLink::Infrastructure::SqliteEventRepository reader(database.Path());
    const auto entries = reader.ReadAll();
    Require(entries.size() == 2, "SQLite event count differs");
    Require(entries[0].timestampUnixMilliseconds == 1000 &&
                entries[0].deviceId == "device-a" &&
                entries[0].category == "connection" &&
                entries[0].detail == "connected",
            "First SQLite event differs");
    Require(entries[1].timestampUnixMilliseconds == 1010 &&
                entries[1].category == "frame" &&
                entries[1].detail == "sequence=7",
            "Second SQLite event differs");
}

void AtomicTextFileStoreReplacesFileContents()
{
    TestLogFile file;
    DeviceLink::Infrastructure::AtomicTextFileStore store;
    Require(store.WriteAtomically(file.Path(), "first").succeeded,
            "Initial atomic text write failed");
    Require(store.WriteAtomically(file.Path(), "second\r\nline").succeeded,
            "Replacement atomic text write failed");
    const auto result = store.Read(file.Path());
    Require(result.Succeeded() && *result.contents == "second\r\nline",
            "Atomic text file contents differ");
    Require(!store.WriteAtomically({}, "invalid").succeeded,
            "Atomic text store accepted an empty path");
}

void SqliteEventRepositoryPrunesExpiredEntries()
{
    TestDatabase database;
    DeviceLink::Infrastructure::SqliteEventRepository repository(database.Path());
    repository.Append({
        .timestampUnixMilliseconds = 1000,
        .deviceId = "device-a",
        .category = "connection-state",
        .detail = "connected",
    });
    repository.Append({
        .timestampUnixMilliseconds = 2000,
        .deviceId = "device-a",
        .category = "frame-received",
        .detail = "sequence=1",
    });
    repository.Append({
        .timestampUnixMilliseconds = 3000,
        .deviceId = "device-b",
        .category = "connection-state",
        .detail = "connected",
    });

    Require(repository.PruneBefore(2000) == 1, "Expired event prune count differs");
    const auto entries = repository.ReadAll();
    Require(entries.size() == 2 && entries[0].timestampUnixMilliseconds == 2000 &&
                entries[1].timestampUnixMilliseconds == 3000,
            "Expired events were not pruned correctly");
    Require(repository.PruneBefore(2000) == 0, "Pruning retained entries removed data");
}

void SqliteEventRepositoryReadsRecentEntriesInChronologicalOrder()
{
    TestDatabase database;
    DeviceLink::Infrastructure::SqliteEventRepository repository(database.Path());
    for (std::int64_t timestamp = 1000; timestamp <= 4000; timestamp += 1000)
    {
        repository.Append({
            .timestampUnixMilliseconds = timestamp,
            .deviceId = "device-a",
            .category = "frame-received",
            .detail = "timestamp=" + std::to_string(timestamp),
        });
    }

    Require(repository.ReadRecent(0).empty(), "Zero recent entry count returned data");
    const auto entries = repository.ReadRecent(2);
    Require(entries.size() == 2 && entries[0].timestampUnixMilliseconds == 3000 &&
                entries[1].timestampUnixMilliseconds == 4000,
            "Recent event query order differs");
}

void AsyncEventStorePrunesEntriesInQueueOrder()
{
    TestDatabase database;
    {
        auto repository = std::make_unique<DeviceLink::Infrastructure::SqliteEventRepository>(
            database.Path());
        DeviceLink::Infrastructure::AsyncEventStore store(std::move(repository));
        store.Append({
            .timestampUnixMilliseconds = 1000,
            .deviceId = "device-a",
            .category = "connection-state",
            .detail = "old",
        });
        store.Append({
            .timestampUnixMilliseconds = 2000,
            .deviceId = "device-a",
            .category = "connection-state",
            .detail = "retained",
        });
        store.PruneBefore(2000);
        store.Append({
            .timestampUnixMilliseconds = 3000,
            .deviceId = "device-a",
            .category = "connection-state",
            .detail = "new",
        });
        store.Flush();
        Require(store.FailedEntryCount() == 0, "Asynchronous pruning failed");
    }

    DeviceLink::Infrastructure::SqliteEventRepository reader(database.Path());
    const auto entries = reader.ReadAll();
    Require(entries.size() == 2 && entries[0].detail == "retained" &&
                entries[1].detail == "new",
            "Asynchronous pruning order differs");
}

} // namespace

int main()
{
    try
    {
        TextFileLoggerWritesLevelAndMessage();
        AsyncLoggerDrainsEntriesBeforeShutdown();
        AsyncEventStorePersistsEntriesInSqlite();
        AtomicTextFileStoreReplacesFileContents();
        SqliteEventRepositoryPrunesExpiredEntries();
        SqliteEventRepositoryReadsRecentEntriesInChronologicalOrder();
        AsyncEventStorePrunesEntriesInQueueOrder();
        std::cout << "Infrastructure tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Infrastructure test failure: " << exception.what() << '\n';
        return 1;
    }
}
