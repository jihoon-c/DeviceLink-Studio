#pragma once

#include "DeviceLink/Application/DeviceEventQueue.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace DeviceLink::Application
{
struct TelemetryQualityMetrics final
{
    std::uint64_t receivedFrameCount{};
    std::uint64_t estimatedMissingFrameCount{};
    std::uint64_t outOfOrderFrameCount{};
    std::int64_t lastIntervalMilliseconds{};
    std::int64_t averageIntervalMilliseconds{};
    std::int64_t averageJitterMilliseconds{};
    double deliveryRatePercent{100.0};
};

class TelemetryQualityService final
{
public:
    using TimestampProvider = std::function<std::int64_t()>;

    TelemetryQualityService(
        std::vector<std::uint16_t> telemetryMessageTypes,
        TimestampProvider timestampProvider);

    [[nodiscard]] bool ApplyDeviceEvent(const DeviceEvent& event);
    [[nodiscard]] std::optional<TelemetryQualityMetrics> MetricsFor(
        const std::string& deviceId) const;

private:
    struct DeviceQuality final
    {
        TelemetryQualityMetrics metrics;
        std::optional<std::uint32_t> lastSequence;
        std::optional<std::int64_t> lastTimestampMilliseconds;
        std::optional<std::int64_t> priorIntervalMilliseconds;
        std::uint64_t intervalCount{};
        std::uint64_t totalIntervalMilliseconds{};
        std::uint64_t jitterCount{};
        std::uint64_t totalJitterMilliseconds{};
    };

    [[nodiscard]] bool IsTelemetryMessageType(std::uint16_t messageType) const noexcept;

    std::vector<std::uint16_t> m_telemetryMessageTypes;
    TimestampProvider m_timestampProvider;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, DeviceQuality> m_devices;
};
} // namespace DeviceLink::Application
