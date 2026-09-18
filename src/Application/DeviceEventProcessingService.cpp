#include "DeviceLink/Application/DeviceEventProcessingService.h"

namespace DeviceLink::Application
{
DeviceEventProcessingService::DeviceEventProcessingService(
    DeviceEventPersistence& eventPersistence,
    CommunicationAlarmService& alarmService,
    TelemetryService& telemetryService,
    TelemetryQualityService& telemetryQualityService,
    TelemetryHeartbeatWatchdog& heartbeatWatchdog) noexcept
    : m_eventPersistence(eventPersistence)
    , m_alarmService(alarmService)
    , m_telemetryService(telemetryService)
    , m_telemetryQualityService(telemetryQualityService)
    , m_heartbeatWatchdog(heartbeatWatchdog)
{
}

void DeviceEventProcessingService::Process(const DeviceEvent& event) const
{
    m_eventPersistence.Persist(event);
    m_alarmService.ApplyDeviceEvent(event);
    static_cast<void>(m_telemetryService.ApplyDeviceEvent(event));
    static_cast<void>(m_telemetryQualityService.ApplyDeviceEvent(event));
    m_heartbeatWatchdog.ApplyDeviceEvent(event);
}
} // namespace DeviceLink::Application
