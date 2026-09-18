#pragma once

#include "DeviceLink/Core/ConnectionStateMachine.h"
#include "DeviceLink/Protocol/PacketFrame.h"
#include "DeviceLink/Transport/ITransport.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace DeviceLink::Core
{

class DeviceSession final
{
public:
    using StateHandler = std::function<void(ConnectionState)>;
    using FrameHandler = std::function<void(Protocol::PacketFrame)>;
    using ErrorHandler = std::function<void(std::string)>;

    DeviceSession(
        std::unique_ptr<Transport::ITransport> transport,
        StateHandler stateHandler = {},
        FrameHandler frameHandler = {},
        ErrorHandler errorHandler = {});
    ~DeviceSession();

    DeviceSession(const DeviceSession&) = delete;
    DeviceSession& operator=(const DeviceSession&) = delete;
    DeviceSession(DeviceSession&&) = delete;
    DeviceSession& operator=(DeviceSession&&) = delete;

    [[nodiscard]] bool Connect(const char* host, std::uint16_t port);
    void Disconnect() noexcept;
    [[nodiscard]] bool SendFrame(const Protocol::PacketFrame& frame);
    [[nodiscard]] ConnectionState State() const noexcept;

private:
    [[nodiscard]] bool ApplyEvent(ConnectionEvent event) noexcept;
    void HandleReceivedBytes(std::vector<std::byte> bytes) noexcept;
    void HandleTransportError(std::string error) noexcept;
    void NotifyError(std::string error) noexcept;

    std::unique_ptr<Transport::ITransport> m_transport;
    StateHandler m_stateHandler;
    FrameHandler m_frameHandler;
    ErrorHandler m_errorHandler;
    mutable std::mutex m_mutex;
    ConnectionStateMachine m_stateMachine;
    Protocol::FrameStreamParser m_frameParser;
};

} // namespace DeviceLink::Core
