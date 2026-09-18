#pragma once

#include "DeviceLink/Application/CommunicationAlarmService.h"
#include "DeviceLink/Application/DeviceEventPersistence.h"
#include "DeviceLink/Application/TelemetryService.h"
#include "DeviceLink/Application/TelemetryQualityService.h"
#include "DeviceLink/Application/TelemetryHeartbeatWatchdog.h"

namespace DeviceLink::Application
{
class DeviceEventProcessingService final
{
public:
    DeviceEventProcessingService(
        DeviceEventPersistence& eventPersistence,
        CommunicationAlarmService& alarmService,
        TelemetryService& telemetryService,
        TelemetryQualityService& telemetryQualityService,
        TelemetryHeartbeatWatchdog& heartbeatWatchdog) noexcept;

    void Process(const DeviceEvent& event) const;

private:
    DeviceEventPersistence& m_eventPersistence;
    CommunicationAlarmService& m_alarmService;
    TelemetryService& m_telemetryService;
    TelemetryQualityService& m_telemetryQualityService;
    TelemetryHeartbeatWatchdog& m_heartbeatWatchdog;
};
} // namespace DeviceLink::Application
