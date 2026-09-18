#include "DeviceLink/Core/DeviceSession.h"

#include <stdexcept>
#include <utility>

namespace DeviceLink::Core
{

DeviceSession::DeviceSession(
    std::unique_ptr<Transport::ITransport> transport,
    StateHandler stateHandler,
    FrameHandler frameHandler,
    ErrorHandler errorHandler)
    : m_transport(std::move(transport)),
      m_stateHandler(std::move(stateHandler)),
      m_frameHandler(std::move(frameHandler)),
      m_errorHandler(std::move(errorHandler))
{
    if (!m_transport)
    {
        throw std::invalid_argument("DeviceSession requires a transport");
    }
    m_transport->SetReceiveHandler([this](std::vector<std::byte> bytes) {
        HandleReceivedBytes(std::move(bytes));
    });
    m_transport->SetErrorHandler([this](std::string error) {
        HandleTransportError(std::move(error));
    });
}

DeviceSession::~DeviceSession()
{
    m_transport->SetReceiveHandler({});
    m_transport->SetErrorHandler({});
    m_transport->Disconnect();
}

bool DeviceSession::Connect(const char* host, std::uint16_t port)
{
    if (!ApplyEvent(ConnectionEvent::ConnectRequested))
    {
        return false;
    }
    if (m_transport->Connect(host, port))
    {
        return ApplyEvent(ConnectionEvent::ConnectSucceeded);
    }
    static_cast<void>(ApplyEvent(ConnectionEvent::ConnectFailed));
    return false;
}

void DeviceSession::Disconnect() noexcept
{
    if (!ApplyEvent(ConnectionEvent::DisconnectRequested))
    {
        return;
    }
    m_transport->Disconnect();
    static_cast<void>(ApplyEvent(ConnectionEvent::DisconnectCompleted));
}

bool DeviceSession::SendFrame(const Protocol::PacketFrame& frame)
{
    const auto bytes = Protocol::SerializeFrame(frame);
    if (!bytes)
    {
        NotifyError("Cannot send an invalid protocol frame");
        return false;
    }
    return m_transport->Send(*bytes);
}

ConnectionState DeviceSession::State() const noexcept
{
    std::scoped_lock lock(m_mutex);
    return m_stateMachine.State();
}

bool DeviceSession::ApplyEvent(ConnectionEvent event) noexcept
{
    ConnectionState state{};
    {
        std::scoped_lock lock(m_mutex);
        if (!m_stateMachine.Apply(event))
        {
            return false;
        }
        state = m_stateMachine.State();
    }
    try
    {
        if (m_stateHandler)
        {
            m_stateHandler(state);
        }
    }
    catch (...)
    {
        NotifyError("State handler threw an exception");
    }
    return true;
}

void DeviceSession::HandleReceivedBytes(std::vector<std::byte> bytes) noexcept
{
    try
    {
        std::vector<Protocol::PacketFrame> frames;
        {
            std::scoped_lock lock(m_mutex);
            frames = m_frameParser.Consume(bytes);
        }
        for (auto& frame : frames)
        {
            if (m_frameHandler)
            {
                m_frameHandler(std::move(frame));
            }
        }
    }
    catch (const std::exception& exception)
    {
        NotifyError(std::string("Frame handling failed: ") + exception.what());
    }
    catch (...)
    {
        NotifyError("Frame handling failed with an unknown exception");
    }
}

void DeviceSession::HandleTransportError(std::string error) noexcept
{
    static_cast<void>(ApplyEvent(ConnectionEvent::TransportLost));
    NotifyError(std::move(error));
}

void DeviceSession::NotifyError(std::string error) noexcept
{
    try
    {
        if (m_errorHandler)
        {
            m_errorHandler(std::move(error));
        }
    }
    catch (...)
    {
        // Application error handlers must not unwind a transport worker thread.
    }
}

} // namespace DeviceLink::Core
