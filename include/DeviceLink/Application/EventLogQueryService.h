#pragma once

#include "DeviceLink/Infrastructure/IEventLogReader.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace DeviceLink::Application
{
struct EventLogSummary final
{
    std::int64_t timestampUnixMilliseconds{};
    std::string deviceId;
    std::string category;
    std::string detail;
    std::size_t payloadByteCount{};
};

struct EventLogFilter final
{
    std::string deviceId;
    std::string category;
    std::string containsText;
    std::size_t maximumEntryCount{200};
};

class EventLogQueryService final
{
public:
    using PrepareRead = std::function<void()>;

    explicit EventLogQueryService(
        const Infrastructure::IEventLogReader& eventLogReader,
        PrepareRead prepareRead = {});

    [[nodiscard]] std::vector<EventLogSummary> LoadRecent(
        std::size_t maximumEntryCount) const;
    [[nodiscard]] std::vector<EventLogSummary> LoadRecent(
        const EventLogFilter& filter) const;
    [[nodiscard]] std::vector<EventLogSummary> LoadWindow(
        const std::string& deviceId,
        std::int64_t fromUnixMilliseconds,
        std::int64_t throughUnixMilliseconds,
        std::size_t maximumEntryCount) const;

private:
    const Infrastructure::IEventLogReader& m_eventLogReader;
    PrepareRead m_prepareRead;
};
} // namespace DeviceLink::Application
