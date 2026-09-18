#pragma once

#include "DeviceLink/Application/DeviceEventQueue.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace DeviceLink::Application
{
struct TelemetrySample final
{
    std::int64_t timestampUnixMilliseconds{};
    std::string deviceId;
    std::uint16_t messageType{};
    std::uint32_t sequence{};
    std::size_t payloadByteCount{};
};

class TelemetryService final
{
public:
    using TimestampProvider = std::function<std::int64_t()>;

    TelemetryService(
        std::vector<std::uint16_t> telemetryMessageTypes,
        std::size_t historyLimit,
        TimestampProvider timestampProvider);

    [[nodiscard]] bool ApplyDeviceEvent(const DeviceEvent& event);
    [[nodiscard]] std::vector<TelemetrySample> RecentSamples() const;

private:
    [[nodiscard]] bool IsTelemetryMessageType(std::uint16_t messageType) const noexcept;

    std::vector<std::uint16_t> m_telemetryMessageTypes;
    std::size_t m_historyLimit;
    TimestampProvider m_timestampProvider;
    mutable std::mutex m_mutex;
    std::vector<TelemetrySample> m_samples;
};
} // namespace DeviceLink::Application
