#pragma once

#include "DeviceLink/Application/DeviceEventQueue.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace DeviceLink::Application
{
struct TelemetryHeartbeatWatchdogOptions final
{
    std::chrono::milliseconds warningAfter{1500};
    std::chrono::milliseconds faultAfter{3000};
    std::chrono::milliseconds pollInterval{100};
};

struct TelemetryHeartbeatStatus final
{
    TelemetryHeartbeatState state{TelemetryHeartbeatState::Inactive};
    std::int64_t ageMilliseconds{};
};

class TelemetryHeartbeatWatchdog final
{
public:
    using MonotonicTimestampProvider = std::function<std::int64_t()>;
    using EventHandler = std::function<void(DeviceEvent)>;

    TelemetryHeartbeatWatchdog(
        std::vector<std::uint16_t> telemetryMessageTypes,
        TelemetryHeartbeatWatchdogOptions options = {},
        MonotonicTimestampProvider timestampProvider = {});
    ~TelemetryHeartbeatWatchdog();

    TelemetryHeartbeatWatchdog(const TelemetryHeartbeatWatchdog&) = delete;
    TelemetryHeartbeatWatchdog& operator=(const TelemetryHeartbeatWatchdog&) = delete;

    void SetEventHandler(EventHandler eventHandler);
    void SetOptions(TelemetryHeartbeatWatchdogOptions options);
    void Stop() noexcept;
    void ApplyDeviceEvent(const DeviceEvent& event);
    void EvaluateNow();
    [[nodiscard]] TelemetryHeartbeatStatus StatusFor(const std::string& deviceId) const;
    [[nodiscard]] TelemetryHeartbeatWatchdogOptions Options() const;

private:
    struct DeviceHeartbeat final
    {
        TelemetryHeartbeatState state{TelemetryHeartbeatState::Inactive};
        std::int64_t lastTelemetryOrConnectionMilliseconds{};
        bool connected{};
    };

    [[nodiscard]] bool IsTelemetryMessageType(std::uint16_t messageType) const noexcept;
    static void ValidateOptions(const TelemetryHeartbeatWatchdogOptions& options);
    void Publish(DeviceEvent event) noexcept;
    void Run(std::stop_token stopToken) noexcept;

    std::vector<std::uint16_t> m_telemetryMessageTypes;
    TelemetryHeartbeatWatchdogOptions m_options;
    MonotonicTimestampProvider m_timestampProvider;
    mutable std::mutex m_mutex;
    std::condition_variable_any m_condition;
    EventHandler m_eventHandler;
    std::unordered_map<std::string, DeviceHeartbeat> m_devices;
    std::jthread m_worker;
};
} // namespace DeviceLink::Application
