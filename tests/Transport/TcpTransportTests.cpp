#include "DeviceLink/Transport/TcpTransport.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <array>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

using namespace std::chrono_literals;

class LoopbackServer final
{
public:
    explicit LoopbackServer(std::size_t expectedPayloadSize)
        : m_expectedPayloadSize(expectedPayloadSize)
    {
        WSADATA data{};
        if (::WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            throw std::runtime_error("Test WSAStartup failed");
        }

        m_listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listener == INVALID_SOCKET)
        {
            throw std::runtime_error("Test socket failed");
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(m_listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
            ::listen(m_listener, 1) == SOCKET_ERROR)
        {
            throw std::runtime_error("Test server setup failed");
        }

        int addressSize = sizeof(address);
        if (::getsockname(m_listener, reinterpret_cast<sockaddr*>(&address), &addressSize) == SOCKET_ERROR)
        {
            throw std::runtime_error("Test getsockname failed");
        }
        m_port = ntohs(address.sin_port);
        m_thread = std::jthread([this] { Run(); });
    }

    ~LoopbackServer()
    {
        if (m_client != INVALID_SOCKET)
        {
            ::shutdown(m_client, SD_BOTH);
            ::closesocket(m_client);
        }
        ::closesocket(m_listener);
        if (m_thread.joinable())
        {
            m_thread.join();
        }
        ::WSACleanup();
    }

    [[nodiscard]] std::uint16_t Port() const noexcept { return m_port; }

    [[nodiscard]] bool WaitForPayload(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(m_mutex);
        return m_condition.wait_for(lock, timeout, [this] { return !m_payload.empty(); });
    }

    [[nodiscard]] std::vector<std::byte> Payload() const
    {
        std::scoped_lock lock(m_mutex);
        return m_payload;
    }

private:
    void Run()
    {
        m_client = ::accept(m_listener, nullptr, nullptr);
        if (m_client == INVALID_SOCKET)
        {
            return;
        }

        std::array<std::byte, 64> buffer{};
        std::vector<std::byte> payload;
        while (payload.size() < m_expectedPayloadSize)
        {
            const int received = ::recv(
                m_client, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
            if (received <= 0)
            {
                return;
            }
            payload.insert(payload.end(), buffer.begin(), buffer.begin() + received);
        }

        {
            std::scoped_lock lock(m_mutex);
            m_payload = payload;
        }
        m_condition.notify_one();
        ::send(m_client, reinterpret_cast<const char*>(payload.data()),
               static_cast<int>(payload.size()), 0);
    }

    SOCKET m_listener{INVALID_SOCKET};
    SOCKET m_client{INVALID_SOCKET};
    std::uint16_t m_port{};
    std::size_t m_expectedPayloadSize{};
    std::jthread m_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::vector<std::byte> m_payload;
};

class ConnectionCyclingServer final
{
public:
    ConnectionCyclingServer(std::size_t payloadSize, std::size_t expectedSessionCount)
        : m_payloadSize(payloadSize), m_expectedSessionCount(expectedSessionCount)
    {
        WSADATA data{};
        if (::WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            throw std::runtime_error("Cycling server WSAStartup failed");
        }
        m_listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listener == INVALID_SOCKET)
        {
            ::WSACleanup();
            throw std::runtime_error("Cycling server socket failed");
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(m_listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
            ::listen(m_listener, SOMAXCONN) == SOCKET_ERROR)
        {
            ::closesocket(m_listener);
            ::WSACleanup();
            throw std::runtime_error("Cycling server setup failed");
        }
        int addressSize = sizeof(address);
        if (::getsockname(m_listener, reinterpret_cast<sockaddr*>(&address), &addressSize) == SOCKET_ERROR)
        {
            ::closesocket(m_listener);
            ::WSACleanup();
            throw std::runtime_error("Cycling server getsockname failed");
        }
        m_port = ntohs(address.sin_port);
        m_thread = std::jthread([this] { Run(); });
    }

    ~ConnectionCyclingServer()
    {
        ::closesocket(m_listener);
        if (m_thread.joinable())
        {
            m_thread.join();
        }
        ::WSACleanup();
    }

    ConnectionCyclingServer(const ConnectionCyclingServer&) = delete;
    ConnectionCyclingServer& operator=(const ConnectionCyclingServer&) = delete;

    [[nodiscard]] std::uint16_t Port() const noexcept { return m_port; }

    [[nodiscard]] bool WaitForCompletedSessions(
        std::size_t count, std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(m_mutex);
        return m_condition.wait_for(lock, timeout, [this, count] {
            return m_completedSessionCount >= count || m_failed;
        }) && !m_failed;
    }

private:
    static bool ReceiveExact(SOCKET client, std::span<std::byte> destination)
    {
        std::size_t receivedSize{};
        while (receivedSize < destination.size())
        {
            const int received = ::recv(client,
                reinterpret_cast<char*>(destination.data() + receivedSize),
                static_cast<int>(destination.size() - receivedSize), 0);
            if (received <= 0)
            {
                return false;
            }
            receivedSize += static_cast<std::size_t>(received);
        }
        return true;
    }

    static bool SendExact(SOCKET client, std::span<const std::byte> source)
    {
        std::size_t sentSize{};
        while (sentSize < source.size())
        {
            const int sent = ::send(client,
                reinterpret_cast<const char*>(source.data() + sentSize),
                static_cast<int>(source.size() - sentSize), 0);
            if (sent <= 0)
            {
                return false;
            }
            sentSize += static_cast<std::size_t>(sent);
        }
        return true;
    }

    void Run()
    {
        for (std::size_t session = 0; session < m_expectedSessionCount; ++session)
        {
            const SOCKET client = ::accept(m_listener, nullptr, nullptr);
            if (client == INVALID_SOCKET)
            {
                MarkFailed();
                return;
            }
            std::vector<std::byte> payload(m_payloadSize);
            const bool exchanged = ReceiveExact(client, payload) && SendExact(client, payload);
            ::shutdown(client, SD_BOTH);
            ::closesocket(client);
            if (!exchanged)
            {
                MarkFailed();
                return;
            }
            {
                const std::scoped_lock lock(m_mutex);
                ++m_completedSessionCount;
            }
            m_condition.notify_all();
        }
    }

    void MarkFailed()
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_failed = true;
        }
        m_condition.notify_all();
    }

    SOCKET m_listener{INVALID_SOCKET};
    std::uint16_t m_port{};
    std::size_t m_payloadSize{};
    std::size_t m_expectedSessionCount{};
    std::jthread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::size_t m_completedSessionCount{};
    bool m_failed{};
};

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void ConnectSendReceiveAndDisconnect()
{
    const std::array firstPayload{std::byte{0x00}, std::byte{0x7F}};
    const std::array secondPayload{std::byte{0x80}, std::byte{0xFF}};
    const std::vector<std::byte> expectedPayload{
        std::byte{0x00}, std::byte{0x7F}, std::byte{0x80}, std::byte{0xFF}};
    LoopbackServer server(expectedPayload.size());
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::byte> receivedBytes;
    std::vector<std::string> errors;

    DeviceLink::Transport::TcpTransport transport(
        [&](std::vector<std::byte> bytes) {
            {
                std::scoped_lock lock(mutex);
                receivedBytes.insert(receivedBytes.end(), bytes.begin(), bytes.end());
            }
            condition.notify_one();
        },
        [&](std::string error) {
            std::scoped_lock lock(mutex);
            errors.push_back(std::move(error));
        });

    Require(transport.Connect("127.0.0.1", server.Port()), "Connect failed");
    Require(transport.IsConnected(), "Transport should be connected");
    Require(transport.Connect("127.0.0.1", server.Port()), "Repeated Connect failed");

    Require(transport.Send(std::span<const std::byte>(firstPayload)), "First Send failed");
    Require(transport.Send(std::span<const std::byte>(secondPayload)), "Second Send failed");
    Require(server.WaitForPayload(2s), "Server did not receive payload");

    {
        std::unique_lock lock(mutex);
        Require(condition.wait_for(lock, 2s, [&] { return receivedBytes.size() == expectedPayload.size(); }),
                "Client did not receive echoed bytes");
        Require(errors.empty(), "Transport reported an unexpected error");
    }
    Require(server.Payload() == expectedPayload,
            "Server payload differs");
    Require(receivedBytes == expectedPayload,
            "Echo payload differs");
    DeviceLink::Transport::TcpTransportStatistics statistics;
    const auto statisticsDeadline = std::chrono::steady_clock::now() + 2s;
    do
    {
        statistics = transport.GetStatistics();
        if (statistics.sentByteCount == expectedPayload.size() &&
            statistics.receivedByteCount == expectedPayload.size())
        {
            break;
        }
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < statisticsDeadline);
    Require(statistics.sentByteCount == expectedPayload.size() &&
                statistics.receivedByteCount == expectedPayload.size() &&
                statistics.rejectedSendCount == 0 && statistics.queuedSendByteCount == 0,
            "Transport byte statistics differ");

    transport.Disconnect();
    Require(!transport.IsConnected(), "Transport should be disconnected");
    transport.Disconnect();
}

void RejectSendWhileDisconnected()
{
    std::vector<std::string> errors;
    DeviceLink::Transport::TcpTransport transport(
        [](std::vector<std::byte>) {},
        [&](std::string error) { errors.push_back(std::move(error)); });

    const std::array payload{std::byte{0x01}};
    Require(!transport.Send(std::span<const std::byte>(payload)),
            "Disconnected Send should fail");
    Require(!errors.empty(), "Disconnected Send should report an error");
    Require(transport.GetStatistics().rejectedSendCount == 1,
            "Rejected send count differs");
}

void RejectSendWhenQueueCapacityIsDisabled()
{
    LoopbackServer server(1);
    std::mutex mutex;
    std::vector<std::string> errors;
    DeviceLink::Transport::TcpTransport transport(
        [](std::vector<std::byte>) {},
        [&](std::string error) {
            const std::scoped_lock lock(mutex);
            errors.push_back(std::move(error));
        },
        {.maximumQueuedSendBytes = 0});

    Require(transport.Connect("127.0.0.1", server.Port()),
            "Queue-limited transport connection failed");
    const std::array payload{std::byte{0x01}};
    Require(!transport.Send(std::span<const std::byte>(payload)),
            "Queue-disabled transport accepted a send");
    transport.Disconnect();
    const std::scoped_lock lock(mutex);
    Require(!errors.empty() && errors.front() == "Send queue capacity exceeded",
            "Queue capacity rejection error differs");
}

void ConnectTimeoutIsBounded()
{
    std::mutex mutex;
    std::vector<std::string> errors;
    DeviceLink::Transport::TcpTransport transport(
        {},
        [&](std::string error) {
            const std::scoped_lock lock(mutex);
            errors.push_back(std::move(error));
        },
        {.connectTimeout = 75ms});

    const auto started = std::chrono::steady_clock::now();
    Require(!transport.Connect("192.0.2.1", 65000),
            "Reserved test address unexpectedly accepted a connection");
    const auto elapsed = std::chrono::steady_clock::now() - started;
    Require(elapsed < 2s, "TCP connect exceeded its configured timeout bound");
    const std::scoped_lock lock(mutex);
    Require(!errors.empty(), "TCP connect timeout did not report an error");
}

void RecoverFromRepeatedPeerDisconnects(std::size_t cycleCount)
{
    constexpr std::size_t payloadSize = 16;
    ConnectionCyclingServer server(payloadSize, cycleCount);
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::byte> receivedBytes;
    std::size_t peerCloseErrorCount{};
    DeviceLink::Transport::TcpTransport transport(
        [&](std::vector<std::byte> bytes) {
            {
                const std::scoped_lock lock(mutex);
                receivedBytes.insert(receivedBytes.end(), bytes.begin(), bytes.end());
            }
            condition.notify_all();
        },
        [&](std::string error) {
            {
                const std::scoped_lock lock(mutex);
                if (error == "Connection closed by peer")
                {
                    ++peerCloseErrorCount;
                }
            }
            condition.notify_all();
        });

    for (std::size_t cycle = 0; cycle < cycleCount; ++cycle)
    {
        std::array<std::byte, payloadSize> payload{};
        for (std::size_t index = 0; index < payload.size(); ++index)
        {
            payload[index] = static_cast<std::byte>((cycle + index) & 0xFFU);
        }
        Require(transport.Connect("127.0.0.1", server.Port()),
            "Reconnect after peer disconnect failed");
        Require(transport.Send(payload), "Cycle send failed");
        Require(server.WaitForCompletedSessions(cycle + 1, 2s),
            "Cycling server did not complete a session");
        {
            std::unique_lock lock(mutex);
            Require(condition.wait_for(lock, 2s, [&] {
                return receivedBytes.size() >= (cycle + 1) * payloadSize;
            }), "Cycle echo was not received");
        }
        const auto disconnectDeadline = std::chrono::steady_clock::now() + 2s;
        while (transport.IsConnected() && std::chrono::steady_clock::now() < disconnectDeadline)
        {
            std::this_thread::sleep_for(1ms);
        }
        Require(!transport.IsConnected(), "Peer disconnect was not detected");
    }

    transport.Disconnect();
    const auto statistics = transport.GetStatistics();
    Require(statistics.sentByteCount == cycleCount * payloadSize,
        "Soak sent byte count differs");
    Require(statistics.receivedByteCount == cycleCount * payloadSize,
        "Soak received byte count differs");
    Require(statistics.queuedSendByteCount == 0,
        "Soak left bytes in the send queue");
    const std::scoped_lock lock(mutex);
    Require(peerCloseErrorCount == cycleCount,
        "Peer close notification count differs");
}

std::size_t ReadSoakCycleCount(int argumentCount, char* arguments[])
{
    constexpr std::size_t defaultCycleCount = 1000;
    if (argumentCount < 3)
    {
        return defaultCycleCount;
    }
    std::size_t cycleCount{};
    const std::string_view text(arguments[2]);
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), cycleCount);
    if (error != std::errc{} || end != text.data() + text.size() || cycleCount == 0)
    {
        throw std::runtime_error("Soak cycle count must be a positive integer");
    }
    return cycleCount;
}

} // namespace

int main(int argumentCount, char* arguments[])
{
    try
    {
        if (argumentCount >= 2 && std::string_view(arguments[1]) == "--soak")
        {
            const auto cycleCount = ReadSoakCycleCount(argumentCount, arguments);
            RecoverFromRepeatedPeerDisconnects(cycleCount);
            std::cout << "TcpTransport soak test passed: " << cycleCount << " cycles\n";
            return 0;
        }
        ConnectSendReceiveAndDisconnect();
        RejectSendWhileDisconnected();
        RejectSendWhenQueueCapacityIsDisabled();
        ConnectTimeoutIsBounded();
        RecoverFromRepeatedPeerDisconnects(5);
        std::cout << "TcpTransport tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "TcpTransport test failure: " << exception.what() << '\n';
        return 1;
    }
}
