#include "DeviceLink/Core/ConnectionStateMachine.h"

namespace DeviceLink::Core
{

ConnectionState ConnectionStateMachine::State() const noexcept
{
    return m_state;
}

bool ConnectionStateMachine::Apply(ConnectionEvent event) noexcept
{
    switch (m_state)
    {
    case ConnectionState::Disconnected:
        if (event == ConnectionEvent::ConnectRequested)
        {
            m_state = ConnectionState::Connecting;
            return true;
        }
        break;

    case ConnectionState::Connecting:
        if (event == ConnectionEvent::ConnectSucceeded)
        {
            m_state = ConnectionState::Connected;
            return true;
        }
        if (event == ConnectionEvent::ConnectFailed)
        {
            m_state = ConnectionState::Faulted;
            return true;
        }
        if (event == ConnectionEvent::DisconnectRequested)
        {
            m_state = ConnectionState::Disconnecting;
            return true;
        }
        if (event == ConnectionEvent::TransportLost)
        {
            m_state = ConnectionState::Faulted;
            return true;
        }
        break;

    case ConnectionState::Connected:
        if (event == ConnectionEvent::DisconnectRequested)
        {
            m_state = ConnectionState::Disconnecting;
            return true;
        }
        if (event == ConnectionEvent::TransportLost)
        {
            m_state = ConnectionState::Faulted;
            return true;
        }
        break;

    case ConnectionState::Disconnecting:
        if (event == ConnectionEvent::DisconnectCompleted)
        {
            m_state = ConnectionState::Disconnected;
            return true;
        }
        if (event == ConnectionEvent::TransportLost)
        {
            m_state = ConnectionState::Faulted;
            return true;
        }
        break;

    case ConnectionState::Faulted:
        if (event == ConnectionEvent::ResetFault)
        {
            m_state = ConnectionState::Disconnected;
            return true;
        }
        if (event == ConnectionEvent::DisconnectRequested)
        {
            m_state = ConnectionState::Disconnecting;
            return true;
        }
        break;
    }
    return false;
}

} // namespace DeviceLink::Core
