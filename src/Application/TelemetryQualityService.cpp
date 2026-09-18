#include "DeviceLink/Application/TelemetryQualityService.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace DeviceLink::Application
{
TelemetryQualityService::TelemetryQualityService(
    std::vector<std::uint16_t> telemetryMessageTypes,
    TimestampProvider timestampProvider)
    : m_telemetryMessageTypes(std::move(telemetryMessageTypes)),
      m_timestampProvider(std::move(timestampProvider))
{
    if (!m_timestampProvider)
    {
        throw std::invalid_argument("TelemetryQualityService requires a timestamp provider");
    }
    std::sort(m_telemetryMessageTypes.begin(), m_telemetryMessageTypes.end());
    m_telemetryMessageTypes.erase(
        std::unique(m_telemetryMessageTypes.begin(), m_telemetryMessageTypes.end()),
        m_telemetryMessageTypes.end());
}

bool TelemetryQualityService::ApplyDeviceEvent(const DeviceEvent& event)
{
    const auto* const frame = std::get_if<FrameReceived>(&event.payload);
    if (frame == nullptr || !IsTelemetryMessageType(frame->frame.header.messageType))
    {
        return false;
    }

    const std::int64_t timestamp = m_timestampProvider();
    const std::uint32_t sequence = frame->frame.header.sequence;
    const std::scoped_lock lock(m_mutex);
    auto& quality = m_devices[event.deviceId];
    auto& metrics = quality.metrics;
    ++metrics.receivedFrameCount;

    if (!quality.lastSequence)
    {
        quality.lastSequence = sequence;
    }
    else
    {
        const std::uint32_t sequenceDelta = sequence - *quality.lastSequence;
        if (sequenceDelta > 1 && sequenceDelta < 0x80000000U)
        {
            metrics.estimatedMissingFrameCount += sequenceDelta - 1;
            quality.lastSequence = sequence;
        }
        else if (sequenceDelta == 1)
        {
            quality.lastSequence = sequence;
        }
        else if (sequenceDelta >= 0x80000000U)
        {
            ++metrics.outOfOrderFrameCount;
        }
    }

    if (quality.lastTimestampMilliseconds)
    {
        const std::int64_t interval = (std::max)(
            std::int64_t{0}, timestamp - *quality.lastTimestampMilliseconds);
        metrics.lastIntervalMilliseconds = interval;
        quality.totalIntervalMilliseconds += static_cast<std::uint64_t>(interval);
        ++quality.intervalCount;
        metrics.averageIntervalMilliseconds = static_cast<std::int64_t>(
            quality.totalIntervalMilliseconds / quality.intervalCount);
        if (quality.priorIntervalMilliseconds)
        {
            const std::int64_t jitter = std::llabs(interval - *quality.priorIntervalMilliseconds);
            quality.totalJitterMilliseconds += static_cast<std::uint64_t>(jitter);
            ++quality.jitterCount;
            metrics.averageJitterMilliseconds = static_cast<std::int64_t>(
                quality.totalJitterMilliseconds / quality.jitterCount);
        }
        quality.priorIntervalMilliseconds = interval;
    }
    quality.lastTimestampMilliseconds = timestamp;

    const double expectedFrameCount = static_cast<double>(metrics.receivedFrameCount) +
        static_cast<double>(metrics.estimatedMissingFrameCount);
    metrics.deliveryRatePercent = expectedFrameCount == 0.0 ? 100.0 :
        static_cast<double>(metrics.receivedFrameCount) * 100.0 / expectedFrameCount;
    return true;
}

std::optional<TelemetryQualityMetrics> TelemetryQualityService::MetricsFor(
    const std::string& deviceId) const
{
    const std::scoped_lock lock(m_mutex);
    const auto item = m_devices.find(deviceId);
    if (item == m_devices.end())
    {
        return std::nullopt;
    }
    return item->second.metrics;
}

bool TelemetryQualityService::IsTelemetryMessageType(std::uint16_t messageType) const noexcept
{
    return std::binary_search(
        m_telemetryMessageTypes.begin(), m_telemetryMessageTypes.end(), messageType);
}
} // namespace DeviceLink::Application
