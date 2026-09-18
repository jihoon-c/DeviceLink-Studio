#include "DeviceLink/Application/TelemetryService.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace DeviceLink::Application
{
TelemetryService::TelemetryService(
    std::vector<std::uint16_t> telemetryMessageTypes,
    std::size_t historyLimit,
    TimestampProvider timestampProvider)
    : m_telemetryMessageTypes(std::move(telemetryMessageTypes))
    , m_historyLimit(historyLimit)
    , m_timestampProvider(std::move(timestampProvider))
{
    if (!m_timestampProvider)
    {
        throw std::invalid_argument("TelemetryService requires a timestamp provider");
    }
    std::sort(m_telemetryMessageTypes.begin(), m_telemetryMessageTypes.end());
    m_telemetryMessageTypes.erase(
        std::unique(m_telemetryMessageTypes.begin(), m_telemetryMessageTypes.end()),
        m_telemetryMessageTypes.end());
}

bool TelemetryService::ApplyDeviceEvent(const DeviceEvent& event)
{
    const auto* const frameReceived = std::get_if<FrameReceived>(&event.payload);
    if (frameReceived == nullptr || !IsTelemetryMessageType(frameReceived->frame.header.messageType) ||
        m_historyLimit == 0)
    {
        return false;
    }

    const std::scoped_lock lock(m_mutex);
    m_samples.push_back({
        .timestampUnixMilliseconds = m_timestampProvider(),
        .deviceId = event.deviceId,
        .messageType = frameReceived->frame.header.messageType,
        .sequence = frameReceived->frame.header.sequence,
        .payloadByteCount = frameReceived->frame.payload.size(),
    });
    if (m_samples.size() > m_historyLimit)
    {
        m_samples.erase(m_samples.begin(), m_samples.begin() +
            static_cast<std::ptrdiff_t>(m_samples.size() - m_historyLimit));
    }
    return true;
}

std::vector<TelemetrySample> TelemetryService::RecentSamples() const
{
    const std::scoped_lock lock(m_mutex);
    return m_samples;
}

bool TelemetryService::IsTelemetryMessageType(std::uint16_t messageType) const noexcept
{
    return std::binary_search(
        m_telemetryMessageTypes.begin(), m_telemetryMessageTypes.end(), messageType);
}
} // namespace DeviceLink::Application
