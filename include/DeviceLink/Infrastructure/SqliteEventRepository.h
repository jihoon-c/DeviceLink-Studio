#pragma once

#include "DeviceLink/Infrastructure/EventRepository.h"
#include "DeviceLink/Infrastructure/IEventLogReader.h"

#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace DeviceLink::Infrastructure
{
class SqliteEventRepository final : public IEventRepository, public IEventLogReader
{
public:
    explicit SqliteEventRepository(const std::filesystem::path& databasePath);
    ~SqliteEventRepository() override;

    SqliteEventRepository(const SqliteEventRepository&) = delete;
    SqliteEventRepository& operator=(const SqliteEventRepository&) = delete;

    void Append(const EventLogEntry& entry) override;
    [[nodiscard]] std::vector<EventLogEntry> ReadAll() const;
    [[nodiscard]] std::vector<EventLogEntry> ReadRecent(
        std::size_t maximumEntryCount) const override;
    [[nodiscard]] std::vector<StoredFramePayload> ReadFramePayloads(
        const std::string& deviceId,
        const std::string& category = "frame-received") const;
    [[nodiscard]] std::size_t PruneBefore(
        std::int64_t timestampUnixMilliseconds) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace DeviceLink::Infrastructure
