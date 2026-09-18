#include "DeviceLink/Application/EventLogQueryService.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace
{
std::string Lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool ContainsCaseInsensitive(const std::string& value, const std::string& expected)
{
    return expected.empty() || Lowercase(value).find(Lowercase(expected)) != std::string::npos;
}
} // namespace

namespace DeviceLink::Application
{
EventLogQueryService::EventLogQueryService(
    const Infrastructure::IEventLogReader& eventLogReader,
    PrepareRead prepareRead)
    : m_eventLogReader(eventLogReader), m_prepareRead(std::move(prepareRead))
{
}

std::vector<EventLogSummary> EventLogQueryService::LoadRecent(
    std::size_t maximumEntryCount) const
{
    return LoadRecent(EventLogFilter{.maximumEntryCount = maximumEntryCount});
}

std::vector<EventLogSummary> EventLogQueryService::LoadRecent(
    const EventLogFilter& filter) const
{
    if (filter.maximumEntryCount == 0)
    {
        return {};
    }
    if (m_prepareRead)
    {
        m_prepareRead();
    }
    constexpr std::size_t kExpandedScanLimit = 5000;
    const std::size_t scanCount = filter.maximumEntryCount >= kExpandedScanLimit / 10
        ? filter.maximumEntryCount
        : filter.maximumEntryCount * 10;
    std::vector<EventLogSummary> summaries;
    for (const auto& entry : m_eventLogReader.ReadRecent(scanCount))
    {
        if ((!filter.deviceId.empty() && entry.deviceId != filter.deviceId) ||
            (!filter.category.empty() && entry.category != filter.category) ||
            (!ContainsCaseInsensitive(entry.deviceId, filter.containsText) &&
             !ContainsCaseInsensitive(entry.category, filter.containsText) &&
             !ContainsCaseInsensitive(entry.detail, filter.containsText)))
        {
            continue;
        }
        summaries.push_back({
            .timestampUnixMilliseconds = entry.timestampUnixMilliseconds,
            .deviceId = entry.deviceId,
            .category = entry.category,
            .detail = entry.detail,
            .payloadByteCount = entry.payload.size(),
        });
    }
    if (summaries.size() > filter.maximumEntryCount)
    {
        summaries.erase(summaries.begin(), summaries.end() - filter.maximumEntryCount);
    }
    return summaries;
}
} // namespace DeviceLink::Application
