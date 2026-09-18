#include "DeviceLink/Protocol/PacketFrame.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{

enum class SimulatedBehavior
{
    Echo,
    Delay,
    Disconnect,
    CorruptCrc,
    CorruptThenEcho,
    Fragmented,
    Telemetry,
};

class WinsockSession final
{
public:
    WinsockSession()
    {
        const int result = ::WSAStartup(MAKEWORD(2, 2), &m_data);
        m_started = result == 0;
    }

    ~WinsockSession()
    {
        if (m_started)
        {
            ::WSACleanup();
        }
    }

    [[nodiscard]] bool IsStarted() const noexcept
    {
        return m_started;
    }

    WinsockSession(const WinsockSession&) = delete;
    WinsockSession& operator=(const WinsockSession&) = delete;

private:
    WSADATA m_data{};
    bool m_started{};
};

class SocketHandle final
{
public:
    explicit SocketHandle(SOCKET socket = INVALID_SOCKET) noexcept
        : m_socket(socket)
    {
    }

    ~SocketHandle()
    {
        Close();
    }

    SocketHandle(SocketHandle&& other) noexcept
        : m_socket(std::exchange(other.m_socket, INVALID_SOCKET))
    {
    }

    SocketHandle& operator=(SocketHandle&& other) noexcept
    {
        if (this != &other)
        {
            Close();
            m_socket = std::exchange(other.m_socket, INVALID_SOCKET);
        }
        return *this;
    }

    [[nodiscard]] SOCKET Get() const noexcept
    {
        return m_socket;
    }

    [[nodiscard]] bool IsValid() const noexcept
    {
        return m_socket != INVALID_SOCKET;
    }

    void Close() noexcept
    {
        if (IsValid())
        {
            ::closesocket(std::exchange(m_socket, INVALID_SOCKET));
        }
    }

    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;

private:
    SOCKET m_socket;
};

[[nodiscard]] bool SendAll(SOCKET socket, std::span<const std::byte> bytes)
{
    std::size_t offset{};
    while (offset < bytes.size())
    {
        const int sent = ::send(socket,
            reinterpret_cast<const char*>(bytes.data() + offset),
            static_cast<int>(bytes.size() - offset), 0);
        if (sent == SOCKET_ERROR || sent == 0)
        {
            return false;
        }
        offset += static_cast<std::size_t>(sent);
    }
    return true;
}

[[nodiscard]] SocketHandle CreateListener(std::uint16_t port)
{
    SocketHandle listener(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!listener.IsValid())
    {
        return listener;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(listener.Get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) ==
            SOCKET_ERROR ||
        ::listen(listener.Get(), SOMAXCONN) == SOCKET_ERROR)
    {
        listener.Close();
    }
    return listener;
}

[[nodiscard]] bool ParsePort(std::string_view text, std::uint16_t& port)
{
    unsigned int parsed{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size() || parsed == 0 || parsed > 65535)
    {
        return false;
    }
    port = static_cast<std::uint16_t>(parsed);
    return true;
}

[[nodiscard]] bool ParseBehavior(std::string_view text, SimulatedBehavior& behavior)
{
    if (text == "echo")
    {
        behavior = SimulatedBehavior::Echo;
    }
    else if (text == "delay")
    {
        behavior = SimulatedBehavior::Delay;
    }
    else if (text == "disconnect")
    {
        behavior = SimulatedBehavior::Disconnect;
    }
    else if (text == "corrupt-crc")
    {
        behavior = SimulatedBehavior::CorruptCrc;
    }
    else if (text == "corrupt-then-echo")
    {
        behavior = SimulatedBehavior::CorruptThenEcho;
    }
    else if (text == "fragmented")
    {
        behavior = SimulatedBehavior::Fragmented;
    }
    else if (text == "telemetry")
    {
        behavior = SimulatedBehavior::Telemetry;
    }
    else
    {
        return false;
    }
    return true;
}

[[nodiscard]] std::string_view BehaviorName(SimulatedBehavior behavior) noexcept
{
    switch (behavior)
    {
    case SimulatedBehavior::Echo:
        return "echo";
    case SimulatedBehavior::Delay:
        return "delay";
    case SimulatedBehavior::Disconnect:
        return "disconnect";
    case SimulatedBehavior::CorruptCrc:
        return "corrupt-crc";
    case SimulatedBehavior::CorruptThenEcho:
        return "corrupt-then-echo";
    case SimulatedBehavior::Fragmented:
        return "fragmented";
    case SimulatedBehavior::Telemetry:
        return "telemetry";
    }
    return "unknown";
}

[[nodiscard]] bool SendFrame(SOCKET socket, const DeviceLink::Protocol::PacketFrame& frame,
    std::mutex& sendMutex, bool corruptCrc = false, bool fragmented = false)
{
    auto bytes = DeviceLink::Protocol::SerializeFrame(frame);
    if (!bytes)
    {
        return false;
    }
    if (corruptCrc)
    {
        bytes->back() ^= std::byte{0xFF};
    }

    std::scoped_lock lock(sendMutex);
    if (fragmented && bytes->size() > 1)
    {
        const std::size_t splitOffset = bytes->size() / 2;
        if (!SendAll(socket, std::span<const std::byte>(*bytes).first(splitOffset)))
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        return SendAll(socket, std::span<const std::byte>(*bytes).subspan(splitOffset));
    }
    return SendAll(socket, *bytes);
}

void SendTelemetry(std::stop_token stopToken, SOCKET socket, std::mutex& sendMutex)
{
    std::uint32_t sequence{};
    while (!stopToken.stop_requested())
    {
        DeviceLink::Protocol::PacketFrame telemetry{};
        telemetry.header.messageType = 0x7001;
        telemetry.header.sequence = sequence++;
        telemetry.payload = {static_cast<std::byte>(telemetry.header.sequence & 0xFF)};
        telemetry.header.payloadSize = static_cast<std::uint32_t>(telemetry.payload.size());
        if (!SendFrame(socket, telemetry, sendMutex))
        {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void ServeClient(SocketHandle client, SimulatedBehavior behavior)
{
    DeviceLink::Protocol::FrameStreamParser parser;
    std::array<std::byte, 4096> buffer{};
    std::mutex sendMutex;
    std::jthread telemetryThread;
    if (behavior == SimulatedBehavior::Telemetry)
    {
        telemetryThread = std::jthread(SendTelemetry, client.Get(), std::ref(sendMutex));
    }

    while (true)
    {
        const int received = ::recv(client.Get(), reinterpret_cast<char*>(buffer.data()),
            static_cast<int>(buffer.size()), 0);
        if (received <= 0)
        {
            std::cout << "Client disconnected.\n";
            return;
        }

        const auto frames = parser.Consume(std::span<const std::byte>(buffer).first(
            static_cast<std::size_t>(received)));
        for (const auto& frame : frames)
        {
            std::cout << "Frame: type=" << frame.header.messageType
                      << ", sequence=" << frame.header.sequence
                      << ", payload=" << frame.payload.size() << " bytes\n";
            if (behavior == SimulatedBehavior::Disconnect)
            {
                std::cout << "Disconnect scenario completed.\n";
                return;
            }
            if (behavior == SimulatedBehavior::Delay)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
            if (behavior == SimulatedBehavior::CorruptThenEcho &&
                !SendFrame(client.Get(), frame, sendMutex, true))
            {
                std::cerr << "Failed to send corrupt simulated response.\n";
                return;
            }
            const bool corruptCrc = behavior == SimulatedBehavior::CorruptCrc;
            const bool fragmented = behavior == SimulatedBehavior::Fragmented;
            if (!SendFrame(client.Get(), frame, sendMutex, corruptCrc, fragmented))
            {
                std::cerr << "Failed to send simulated response.\n";
                return;
            }
        }
    }
}

} // namespace

int main(int argc, char* argv[])
{
    std::uint16_t port = 50001;
    SimulatedBehavior behavior = SimulatedBehavior::Echo;
    if (argc == 2)
    {
        if (!ParsePort(argv[1], port) && !ParseBehavior(argv[1], behavior))
        {
            std::cerr << "Usage: DeviceLinkSimulator [port] "
                         "[echo|delay|disconnect|corrupt-crc|corrupt-then-echo|fragmented|telemetry]\n";
            return 1;
        }
    }
    else if (argc == 3)
    {
        if (!ParsePort(argv[1], port) || !ParseBehavior(argv[2], behavior))
        {
            std::cerr << "Usage: DeviceLinkSimulator [port] "
                         "[echo|delay|disconnect|corrupt-crc|corrupt-then-echo|fragmented|telemetry]\n";
            return 1;
        }
    }
    else if (argc > 3)
    {
        std::cerr << "Usage: DeviceLinkSimulator [port] "
                     "[echo|delay|disconnect|corrupt-crc|corrupt-then-echo|fragmented|telemetry]\n";
        return 1;
    }

    WinsockSession winsock;
    if (!winsock.IsStarted())
    {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    SocketHandle listener = CreateListener(port);
    if (!listener.IsValid())
    {
        std::cerr << "Unable to listen on 127.0.0.1:" << port << ".\n";
        return 1;
    }

    std::cout << "DeviceLink simulator (" << BehaviorName(behavior)
              << ") listening on 127.0.0.1:" << port << ".\n";
    std::cout << "Waiting for one client...\n";
    SocketHandle client(::accept(listener.Get(), nullptr, nullptr));
    if (!client.IsValid())
    {
        std::cerr << "Accept failed.\n";
        return 1;
    }

    std::cout << "Client connected.\n";
    ServeClient(std::move(client), behavior);
    return 0;
}
