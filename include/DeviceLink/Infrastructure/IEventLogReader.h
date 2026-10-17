#pragma once

#include "DeviceLink/Infrastructure/EventRepository.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace DeviceLink::Infrastructure
{
class IEventLogReader
{
public:
    virtual ~IEventLogReader() = default;

    [[nodiscard]] virtual std::vector<EventLogEntry> ReadRecent(
        std::size_t maximumEntryCount) const = 0;
    [[nodiscard]] virtual std::vector<EventLogEntry> ReadWindow(
        const std::string& deviceId,
        std::int64_t fromUnixMilliseconds,
        std::int64_t throughUnixMilliseconds,
        std::size_t maximumEntryCount) const = 0;
};
} // namespace DeviceLink::Infrastructure
