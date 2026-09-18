#pragma once

#include "DeviceLink/Application/DeviceEventQueue.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace DeviceLink::Application
{
enum class AlarmSeverity
{
    Warning,
    Critical,
};

enum class CommunicationAlarmKind
{
    Transport,
    TelemetryHeartbeat,
};

struct CommunicationAlarm final
{
    std::uint64_t id{};
    std::int64_t timestampUnixMilliseconds{};
    std::string deviceId;
    std::string message;
    AlarmSeverity severity{AlarmSeverity::Warning};
    CommunicationAlarmKind kind{CommunicationAlarmKind::Transport};
    std::uint32_t occurrenceCount{1};
};

class CommunicationAlarmService final
{
public:
    using TimestampProvider = std::function<std::int64_t()>;

    explicit CommunicationAlarmService(TimestampProvider timestampProvider);

    void ApplyDeviceEvent(const DeviceEvent& event);
    [[nodiscard]] bool Acknowledge(std::uint64_t alarmId);
    [[nodiscard]] std::vector<CommunicationAlarm> ActiveAlarms() const;

private:
    struct StoredAlarm final
    {
        CommunicationAlarm alarm;
        bool acknowledged{};
    };

    TimestampProvider m_timestampProvider;
    mutable std::mutex m_mutex;
    std::vector<StoredAlarm> m_alarms;
    std::uint64_t m_nextAlarmId{1};
};
} // namespace DeviceLink::Application
