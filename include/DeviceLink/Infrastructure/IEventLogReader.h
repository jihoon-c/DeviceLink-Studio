#pragma once

#include "DeviceLink/Infrastructure/EventRepository.h"

#include <cstddef>
#include <vector>

namespace DeviceLink::Infrastructure
{
class IEventLogReader
{
public:
    virtual ~IEventLogReader() = default;

    [[nodiscard]] virtual std::vector<EventLogEntry> ReadRecent(
        std::size_t maximumEntryCount) const = 0;
};
} // namespace DeviceLink::Infrastructure
