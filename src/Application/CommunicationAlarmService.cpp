#include "DeviceLink/Application/CommunicationAlarmService.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace DeviceLink::Application
{
CommunicationAlarmService::CommunicationAlarmService(TimestampProvider timestampProvider)
    : m_timestampProvider(std::move(timestampProvider))
{
    if (!m_timestampProvider)
    {
        throw std::invalid_argument("CommunicationAlarmService requires a timestamp provider");
    }
}

void CommunicationAlarmService::ApplyDeviceEvent(const DeviceEvent& event)
{
    const auto* const transportError = std::get_if<TransportError>(&event.payload);
    const auto* const heartbeat = std::get_if<TelemetryHeartbeatChanged>(&event.payload);
    if (transportError == nullptr && heartbeat == nullptr)
    {
        return;
    }

    const std::int64_t timestamp = m_timestampProvider();
    const std::scoped_lock lock(m_mutex);
    if (heartbeat != nullptr &&
        (heartbeat->state == TelemetryHeartbeatState::Healthy ||
         heartbeat->state == TelemetryHeartbeatState::Inactive ||
         heartbeat->state == TelemetryHeartbeatState::Waiting))
    {
        for (auto& storedAlarm : m_alarms)
        {
            if (!storedAlarm.acknowledged &&
                storedAlarm.alarm.kind == CommunicationAlarmKind::TelemetryHeartbeat &&
                storedAlarm.alarm.deviceId == event.deviceId)
            {
                storedAlarm.acknowledged = true;
            }
        }
        return;
    }

    const CommunicationAlarmKind kind = heartbeat != nullptr
        ? CommunicationAlarmKind::TelemetryHeartbeat
        : CommunicationAlarmKind::Transport;
    std::string message;
    AlarmSeverity severity = AlarmSeverity::Warning;
    if (heartbeat != nullptr)
    {
        severity = heartbeat->state == TelemetryHeartbeatState::Fault
            ? AlarmSeverity::Critical : AlarmSeverity::Warning;
        message = heartbeat->state == TelemetryHeartbeatState::Fault
            ? "Telemetry heartbeat fault: no data for "
            : "Telemetry heartbeat warning: no data for ";
        message += std::to_string(heartbeat->ageMilliseconds) + " ms";
    }
    else
    {
        message = transportError->message;
    }
    const auto existing = std::find_if(m_alarms.begin(), m_alarms.end(),
        [&event, kind, &message](const StoredAlarm& storedAlarm) {
            return !storedAlarm.acknowledged && storedAlarm.alarm.deviceId == event.deviceId &&
                storedAlarm.alarm.kind == kind &&
                (kind == CommunicationAlarmKind::TelemetryHeartbeat ||
                    storedAlarm.alarm.message == message);
        });
    if (existing != m_alarms.end())
    {
        existing->alarm.timestampUnixMilliseconds = timestamp;
        existing->alarm.message = std::move(message);
        existing->alarm.severity = severity;
        ++existing->alarm.occurrenceCount;
        return;
    }

    m_alarms.push_back({
        .alarm = {
            .id = m_nextAlarmId++,
            .timestampUnixMilliseconds = timestamp,
            .deviceId = event.deviceId,
            .message = std::move(message),
            .severity = severity,
            .kind = kind,
        },
    });
}

bool CommunicationAlarmService::Acknowledge(std::uint64_t alarmId)
{
    const std::scoped_lock lock(m_mutex);
    const auto alarm = std::find_if(m_alarms.begin(), m_alarms.end(),
        [alarmId](const StoredAlarm& storedAlarm) { return storedAlarm.alarm.id == alarmId; });
    if (alarm == m_alarms.end() || alarm->acknowledged)
    {
        return false;
    }
    alarm->acknowledged = true;
    return true;
}

std::vector<CommunicationAlarm> CommunicationAlarmService::ActiveAlarms() const
{
    const std::scoped_lock lock(m_mutex);
    std::vector<CommunicationAlarm> alarms;
    for (const auto& storedAlarm : m_alarms)
    {
        if (!storedAlarm.acknowledged)
        {
            alarms.push_back(storedAlarm.alarm);
        }
    }
    return alarms;
}
} // namespace DeviceLink::Application
