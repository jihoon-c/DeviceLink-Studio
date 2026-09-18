#pragma once

#include "DeviceLink/Core/ConnectionStateMachine.h"
#include "DeviceLink/Protocol/PacketFrame.h"

#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace DeviceLink::Application
{

struct ConnectionStateChanged final
{
    Core::ConnectionState state;
};

struct FrameReceived final
{
    Protocol::PacketFrame frame;
};

struct FrameSent final
{
    Protocol::PacketFrame frame;
    bool replayable{true};
};

struct TransportError final
{
    std::string message;
};

enum class TelemetryHeartbeatState
{
    Inactive,
    Waiting,
    Healthy,
    Warning,
    Fault,
};

struct TelemetryHeartbeatChanged final
{
    TelemetryHeartbeatState state{TelemetryHeartbeatState::Inactive};
    std::int64_t ageMilliseconds{};
};

using DeviceEventPayload =
    std::variant<ConnectionStateChanged, FrameReceived, FrameSent, TransportError,
        TelemetryHeartbeatChanged>;

struct DeviceEvent final
{
    std::string deviceId;
    DeviceEventPayload payload;
};

class DeviceEventQueue final
{
public:
    using WakeupHandler = std::function<void()>;

    void SetWakeupHandler(WakeupHandler wakeupHandler);
    void Push(DeviceEvent event);
    [[nodiscard]] std::optional<DeviceEvent> TryPop();
    [[nodiscard]] std::vector<DeviceEvent> Drain();
    [[nodiscard]] std::size_t Size() const noexcept;

private:
    mutable std::mutex m_mutex;
    WakeupHandler m_wakeupHandler;
    std::deque<DeviceEvent> m_events;
};

} // namespace DeviceLink::Application
