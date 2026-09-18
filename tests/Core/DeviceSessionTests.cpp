#include "DeviceLink/Core/DeviceSession.h"

#include <stdexcept>
#include <utility>
#include <vector>

namespace
{

class FakeTransport final : public DeviceLink::Transport::ITransport
{
public:
    void SetReceiveHandler(ReceiveHandler receiveHandler) override
    {
        m_receiveHandler = std::move(receiveHandler);
    }

    void SetErrorHandler(ErrorHandler errorHandler) override
    {
        m_errorHandler = std::move(errorHandler);
    }

    bool Connect(const char*, std::uint16_t) override
    {
        m_connected = m_connectResult;
        return m_connectResult;
    }

    void Disconnect() noexcept override
    {
        m_connected = false;
    }

    bool Send(std::span<const std::byte> bytes) override
    {
        if (!m_connected)
        {
            return false;
        }
        m_sent.assign(bytes.begin(), bytes.end());
        return true;
    }

    bool IsConnected() const noexcept override
    {
        return m_connected;
    }

    void EmitBytes(std::vector<std::byte> bytes)
    {
        m_receiveHandler(std::move(bytes));
    }

    void EmitError(std::string error)
    {
        m_errorHandler(std::move(error));
    }

private:
    ReceiveHandler m_receiveHandler;
    ErrorHandler m_errorHandler;
    bool m_connectResult{true};
    bool m_connected{false};
    std::vector<std::byte> m_sent;
};

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void DeviceSessionTest()
{
    auto fakeTransport = std::make_unique<FakeTransport>();
    FakeTransport* const fake = fakeTransport.get();
    std::vector<DeviceLink::Core::ConnectionState> states;
    std::vector<DeviceLink::Protocol::PacketFrame> frames;
    std::vector<std::string> errors;
    DeviceLink::Core::DeviceSession session(
        std::move(fakeTransport),
        [&](DeviceLink::Core::ConnectionState state) { states.push_back(state); },
        [&](DeviceLink::Protocol::PacketFrame frame) { frames.push_back(std::move(frame)); },
        [&](std::string error) { errors.push_back(std::move(error)); });

    Require(session.Connect("loopback", 5000), "Session connect failed");
    Require(session.State() == DeviceLink::Core::ConnectionState::Connected,
            "Session is not connected");

    DeviceLink::Protocol::PacketFrame frame{
        .header = {.messageType = 0x23, .sequence = 9, .payloadSize = 2},
        .payload = {std::byte{0xAA}, std::byte{0xBB}},
    };
    const auto bytes = DeviceLink::Protocol::SerializeFrame(frame);
    Require(bytes.has_value(), "Frame serialization failed");
    fake->EmitBytes(std::vector<std::byte>(bytes->begin(), bytes->begin() + 4));
    fake->EmitBytes(std::vector<std::byte>(bytes->begin() + 4, bytes->end()));
    Require(frames.size() == 1 && frames.front().header.sequence == 9,
            "Session did not deliver parsed frame");

    Require(session.SendFrame(frame), "Session frame send failed");
    fake->EmitError("Connection reset");
    Require(session.State() == DeviceLink::Core::ConnectionState::Faulted,
            "Transport failure did not fault the session");
    Require(!errors.empty(), "Transport error was not forwarded");
}

} // namespace

void RunDeviceSessionTests()
{
    DeviceSessionTest();
}
