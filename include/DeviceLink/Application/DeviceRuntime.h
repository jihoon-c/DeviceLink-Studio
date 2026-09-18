#pragma once

#include "DeviceLink/Application/DeviceEventProcessingService.h"
#include "DeviceLink/Application/DeviceManager.h"
#include "DeviceLink/Infrastructure/AsyncEventStore.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace DeviceLink::Application
{
class DeviceRuntime final
{
public:
    DeviceRuntime(
        std::unique_ptr<Infrastructure::IEventRepository> eventRepository,
        std::vector<std::uint16_t> telemetryMessageTypes,
        std::size_t telemetryHistoryLimit,
        DeviceEventPersistence::TimestampProvider timestampProvider,
        TelemetryHeartbeatWatchdogOptions heartbeatOptions = {});
    ~DeviceRuntime();

    DeviceRuntime(const DeviceRuntime&) = delete;
    DeviceRuntime& operator=(const DeviceRuntime&) = delete;

    [[nodiscard]] DeviceManager& Devices() noexcept;
    [[nodiscard]] const DeviceManager& Devices() const noexcept;
    [[nodiscard]] const CommunicationAlarmService& Alarms() const noexcept;
    [[nodiscard]] CommunicationAlarmService& Alarms() noexcept;
    [[nodiscard]] const TelemetryService& Telemetry() const noexcept;
    [[nodiscard]] const TelemetryQualityService& TelemetryQuality() const noexcept;
    [[nodiscard]] const TelemetryHeartbeatWatchdog& HeartbeatWatchdog() const noexcept;
    void SetHeartbeatOptions(TelemetryHeartbeatWatchdogOptions options);
    void FlushEventLog();
    void PruneEventLogBefore(std::int64_t timestampUnixMilliseconds);

private:
    Infrastructure::AsyncEventStore m_eventStore;
    DeviceEventPersistence m_eventPersistence;
    CommunicationAlarmService m_alarmService;
    TelemetryService m_telemetryService;
    TelemetryQualityService m_telemetryQualityService;
    TelemetryHeartbeatWatchdog m_heartbeatWatchdog;
    DeviceEventProcessingService m_eventProcessor;
    DeviceManager m_deviceManager;
};
} // namespace DeviceLink::Application
