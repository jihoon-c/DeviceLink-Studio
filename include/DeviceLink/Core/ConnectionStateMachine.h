#pragma once

namespace DeviceLink::Core
{

enum class ConnectionState
{
    Disconnected,
    Connecting,
    Connected,
    Disconnecting,
    Faulted,
};

enum class ConnectionEvent
{
    ConnectRequested,
    ConnectSucceeded,
    ConnectFailed,
    DisconnectRequested,
    DisconnectCompleted,
    TransportLost,
    ResetFault,
};

class ConnectionStateMachine final
{
public:
    [[nodiscard]] ConnectionState State() const noexcept;
    [[nodiscard]] bool Apply(ConnectionEvent event) noexcept;

private:
    ConnectionState m_state{ConnectionState::Disconnected};
};

} // namespace DeviceLink::Core
