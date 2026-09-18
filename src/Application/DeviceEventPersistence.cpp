#include "DeviceLink/Application/DeviceEventPersistence.h"

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace DeviceLink::Application
{
namespace
{
[[nodiscard]] std::string ToString(Core::ConnectionState state)
{
    switch (state)
    {
    case Core::ConnectionState::Disconnected:
        return "disconnected";
    case Core::ConnectionState::Connecting:
        return "connecting";
    case Core::ConnectionState::Connected:
        return "connected";
    case Core::ConnectionState::Disconnecting:
        return "disconnecting";
    case Core::ConnectionState::Faulted:
        return "faulted";
    }
    return "unknown";
}

[[nodiscard]] std::string ToString(TelemetryHeartbeatState state)
{
    switch (state)
    {
    case TelemetryHeartbeatState::Inactive: return "inactive";
    case TelemetryHeartbeatState::Waiting: return "waiting";
    case TelemetryHeartbeatState::Healthy: return "healthy";
    case TelemetryHeartbeatState::Warning: return "warning";
    case TelemetryHeartbeatState::Fault: return "fault";
    }
    return "unknown";
}
} // namespace

DeviceEventPersistence::DeviceEventPersistence(
    Infrastructure::AsyncEventStore& eventStore,
    TimestampProvider timestampProvider)
    : m_eventStore(eventStore)
    , m_timestampProvider(std::move(timestampProvider))
{
    if (!m_timestampProvider)
    {
        throw std::invalid_argument("DeviceEventPersistence requires a timestamp provider");
    }
}

void DeviceEventPersistence::Persist(const DeviceEvent& event) const
{
    Infrastructure::EventLogEntry entry{};
    entry.timestampUnixMilliseconds = m_timestampProvider();
    entry.deviceId = event.deviceId;
    std::visit(
        [&entry](const auto& payload) {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, ConnectionStateChanged>)
            {
                entry.category = "connection-state";
                entry.detail = ToString(payload.state);
            }
            else if constexpr (std::is_same_v<Payload, FrameReceived>)
            {
                entry.category = "frame-received";
                entry.detail = "type=" + std::to_string(payload.frame.header.messageType) +
                    ";sequence=" + std::to_string(payload.frame.header.sequence) +
                    ";payload-size=" + std::to_string(payload.frame.payload.size());
                if (const auto serializedFrame = Protocol::SerializeFrame(payload.frame);
                    serializedFrame.has_value())
                {
                    entry.payload = *serializedFrame;
                }
            }
            else if constexpr (std::is_same_v<Payload, FrameSent>)
            {
                entry.category = payload.replayable ? "frame-sent" : "frame-replayed";
                entry.detail = "type=" + std::to_string(payload.frame.header.messageType) +
                    ";sequence=" + std::to_string(payload.frame.header.sequence) +
                    ";payload-size=" + std::to_string(payload.frame.payload.size());
                if (payload.replayable)
                {
                    if (const auto serializedFrame = Protocol::SerializeFrame(payload.frame);
                        serializedFrame.has_value())
                    {
                        entry.payload = *serializedFrame;
                    }
                }
            }
            else if constexpr (std::is_same_v<Payload, TransportError>)
            {
                entry.category = "transport-error";
                entry.detail = payload.message;
            }
            else
            {
                entry.category = "telemetry-heartbeat";
                entry.detail = "state=" + ToString(payload.state) +
                    ";age-ms=" + std::to_string(payload.ageMilliseconds);
            }
        },
        event.payload);
    m_eventStore.Append(std::move(entry));
}
} // namespace DeviceLink::Application
