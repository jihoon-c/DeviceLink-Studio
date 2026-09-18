#include "DeviceLink/Application/DeviceRuntime.h"

#include <utility>

namespace DeviceLink::Application
{
DeviceRuntime::DeviceRuntime(
    std::unique_ptr<Infrastructure::IEventRepository> eventRepository,
    std::vector<std::uint16_t> telemetryMessageTypes,
    std::size_t telemetryHistoryLimit,
    DeviceEventPersistence::TimestampProvider timestampProvider,
    TelemetryHeartbeatWatchdogOptions heartbeatOptions)
    : m_eventStore(std::move(eventRepository))
    , m_eventPersistence(m_eventStore, timestampProvider)
    , m_alarmService(timestampProvider)
    , m_telemetryService(telemetryMessageTypes, telemetryHistoryLimit, timestampProvider)
    , m_telemetryQualityService(telemetryMessageTypes, timestampProvider)
    , m_heartbeatWatchdog(std::move(telemetryMessageTypes), heartbeatOptions)
    , m_eventProcessor(
        m_eventPersistence, m_alarmService, m_telemetryService, m_telemetryQualityService,
        m_heartbeatWatchdog)
{
    m_heartbeatWatchdog.SetEventHandler([this](DeviceEvent event) {
        m_eventProcessor.Process(event);
        m_deviceManager.Events().Push(std::move(event));
    });
    m_deviceManager.SetEventObserver([this](const DeviceEvent& event) {
        m_eventProcessor.Process(event);
    });
}

DeviceRuntime::~DeviceRuntime()
{
    m_deviceManager.SetEventObserver({});
    m_heartbeatWatchdog.Stop();
}

DeviceManager& DeviceRuntime::Devices() noexcept
{
    return m_deviceManager;
}

const DeviceManager& DeviceRuntime::Devices() const noexcept
{
    return m_deviceManager;
}

const CommunicationAlarmService& DeviceRuntime::Alarms() const noexcept
{
    return m_alarmService;
}

CommunicationAlarmService& DeviceRuntime::Alarms() noexcept
{
    return m_alarmService;
}

const TelemetryService& DeviceRuntime::Telemetry() const noexcept
{
    return m_telemetryService;
}

const TelemetryQualityService& DeviceRuntime::TelemetryQuality() const noexcept
{
    return m_telemetryQualityService;
}

const TelemetryHeartbeatWatchdog& DeviceRuntime::HeartbeatWatchdog() const noexcept
{
    return m_heartbeatWatchdog;
}

void DeviceRuntime::SetHeartbeatOptions(TelemetryHeartbeatWatchdogOptions options)
{
    m_heartbeatWatchdog.SetOptions(options);
}

void DeviceRuntime::FlushEventLog()
{
    m_eventStore.Flush();
}

void DeviceRuntime::PruneEventLogBefore(std::int64_t timestampUnixMilliseconds)
{
    m_eventStore.PruneBefore(timestampUnixMilliseconds);
}
} // namespace DeviceLink::Application
