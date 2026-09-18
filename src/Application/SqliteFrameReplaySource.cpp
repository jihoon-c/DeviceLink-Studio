#include "DeviceLink/Application/SqliteFrameReplaySource.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>

namespace DeviceLink::Application
{
SqliteFrameReplaySource::SqliteFrameReplaySource(
    const Infrastructure::SqliteEventRepository& repository)
    : m_repository(repository)
{
}

std::vector<ReplayFrame> SqliteFrameReplaySource::Load(const std::string& deviceId) const
{
    std::vector<ReplayFrame> replayFrames;
    std::int64_t previousTimestamp{};
    bool hasPreviousTimestamp{};

    for (const auto& storedFrame : m_repository.ReadFramePayloads(deviceId, "frame-sent"))
    {
        Protocol::FrameStreamParser parser;
        const auto frames = parser.Consume(storedFrame.payload);
        if (frames.size() != 1 || parser.BufferedByteCount() != 0)
        {
            continue;
        }

        std::chrono::milliseconds delay{};
        if (hasPreviousTimestamp)
        {
            const std::int64_t elapsedMilliseconds = std::max(
                std::int64_t{0}, storedFrame.timestampUnixMilliseconds - previousTimestamp);
            const auto maximumDelay = std::chrono::milliseconds::max().count();
            delay = std::chrono::milliseconds(std::min(
                elapsedMilliseconds, static_cast<std::int64_t>(maximumDelay)));
        }

        replayFrames.push_back({
            .delayBeforeSend = delay,
            .frame = frames.front(),
        });
        previousTimestamp = storedFrame.timestampUnixMilliseconds;
        hasPreviousTimestamp = true;
    }
    return replayFrames;
}
} // namespace DeviceLink::Application
