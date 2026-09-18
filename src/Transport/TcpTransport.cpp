#include "DeviceLink/Transport/TcpTransport.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

namespace DeviceLink::Transport
{
namespace
{

constexpr int kReceiveBufferSize = 16 * 1024;

class WinsockSession final
{
public:
    WinsockSession()
    {
        WSADATA data{};
        const int result = ::WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0)
        {
            throw std::runtime_error("WSAStartup failed: " + std::to_string(result));
        }
    }

    ~WinsockSession()
    {
        ::WSACleanup();
    }

    WinsockSession(const WinsockSession&) = delete;
    WinsockSession& operator=(const WinsockSession&) = delete;
};

class SocketHandle final
{
public:
    SocketHandle() noexcept = default;
    explicit SocketHandle(SOCKET socket) noexcept : m_socket(socket) {}

    ~SocketHandle()
    {
        Reset();
    }

    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;

    SocketHandle(SocketHandle&& other) noexcept : m_socket(other.Release()) {}

    SocketHandle& operator=(SocketHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset(other.Release());
        }
        return *this;
    }

    [[nodiscard]] SOCKET Get() const noexcept { return m_socket; }
    [[nodiscard]] bool IsValid() const noexcept { return m_socket != INVALID_SOCKET; }

    void Reset(SOCKET socket = INVALID_SOCKET) noexcept
    {
        if (IsValid())
        {
            ::closesocket(m_socket);
        }
        m_socket = socket;
    }

    [[nodiscard]] SOCKET Release() noexcept
    {
        return std::exchange(m_socket, INVALID_SOCKET);
    }

private:
    SOCKET m_socket{INVALID_SOCKET};
};

[[nodiscard]] std::string SocketError(std::string_view operation, int error)
{
    return std::string(operation) + " failed with Winsock error " + std::to_string(error);
}

} // namespace

class TcpTransport::Impl final
{
public:
    Impl(ReceiveHandler receiveHandler, ErrorHandler errorHandler, TcpTransportOptions options)
        : m_receiveHandler(std::move(receiveHandler)),
          m_errorHandler(std::move(errorHandler)),
          m_maximumQueuedSendBytes(options.maximumQueuedSendBytes),
          m_connectTimeout((std::max)(options.connectTimeout, std::chrono::milliseconds::zero()))
    {
    }

    ~Impl()
    {
        Disconnect();
    }

    void SetReceiveHandler(ReceiveHandler receiveHandler)
    {
        std::scoped_lock handlerLock(m_handlerMutex);
        m_receiveHandler = std::move(receiveHandler);
    }

    void SetErrorHandler(ErrorHandler errorHandler)
    {
        std::scoped_lock handlerLock(m_handlerMutex);
        m_errorHandler = std::move(errorHandler);
    }

    [[nodiscard]] bool Connect(const char* host, std::uint16_t port)
    {
        if (host == nullptr || *host == '\0')
        {
            NotifyError("Connect requires a non-empty host");
            return false;
        }

        std::scoped_lock operationLock(m_operationMutex);
        if (m_connected.load())
        {
            return true;
        }
        if (!JoinFinishedWorkerThreads())
        {
            return false;
        }

        SocketHandle connectedSocket = CreateConnectedSocket(host, port);
        if (!connectedSocket.IsValid())
        {
            return false;
        }

        const SOCKET socket = connectedSocket.Get();
        {
            std::scoped_lock stateLock(m_stateMutex);
            m_socket = std::move(connectedSocket);
            m_connected.store(true);
        }
        return StartWorkerThreads(socket);
    }

    void Disconnect() noexcept
    {
        std::scoped_lock operationLock(m_operationMutex);
        StopQueuedSends();
        {
            std::scoped_lock stateLock(m_stateMutex);
            m_connected.store(false);
            m_receiveThread.request_stop();
            m_sendThread.request_stop();
            if (m_socket.IsValid())
            {
                ::shutdown(m_socket.Get(), SD_BOTH);
                m_socket.Reset();
            }
        }

        if (m_receiveThread.joinable() &&
            m_receiveThread.get_id() != std::this_thread::get_id())
        {
            m_receiveThread.join();
        }
        if (m_sendThread.joinable() &&
            m_sendThread.get_id() != std::this_thread::get_id())
        {
            m_sendThread.join();
        }
    }

    [[nodiscard]] bool Send(std::span<const std::byte> bytes)
    {
        if (bytes.empty())
        {
            return true;
        }

        std::vector<std::byte> queuedBytes(bytes.begin(), bytes.end());
        bool accepted = false;
        {
            std::scoped_lock queueLock(m_sendQueueMutex);
            const bool fitsQueue = queuedBytes.size() <= m_maximumQueuedSendBytes &&
                m_queuedSendBytes <= m_maximumQueuedSendBytes - queuedBytes.size();
            if (m_acceptingSends && fitsQueue)
            {
                m_queuedSendBytes += queuedBytes.size();
                m_sendQueue.push_back(std::move(queuedBytes));
                accepted = true;
            }
        }
        if (!accepted)
        {
            ++m_rejectedSendCount;
            NotifyError(m_connected.load() ? "Send queue capacity exceeded" :
                "Send requires an active connection");
            return false;
        }
        m_sendCondition.notify_one();
        return true;
    }

    [[nodiscard]] bool IsConnected() const noexcept
    {
        return m_connected.load();
    }

    [[nodiscard]] TcpTransportStatistics GetStatistics() const noexcept
    {
        std::size_t queuedSendByteCount{};
        {
            const std::scoped_lock queueLock(m_sendQueueMutex);
            queuedSendByteCount = m_queuedSendBytes;
        }
        return {
            .sentByteCount = m_sentByteCount.load(),
            .receivedByteCount = m_receivedByteCount.load(),
            .rejectedSendCount = m_rejectedSendCount.load(),
            .discardedQueuedSendByteCount = m_discardedQueuedSendByteCount.load(),
            .queuedSendByteCount = queuedSendByteCount,
        };
    }

private:
    [[nodiscard]] bool StartWorkerThreads(SOCKET socket)
    {
        {
            std::scoped_lock queueLock(m_sendQueueMutex);
            m_acceptingSends = true;
        }
        try
        {
            m_receiveThread = std::jthread([this, socket](std::stop_token stopToken) {
                ReceiveLoop(socket, stopToken);
            });
            m_sendThread = std::jthread([this, socket](std::stop_token stopToken) {
                SendLoop(socket, stopToken);
            });
        }
        catch (const std::system_error& exception)
        {
            StopQueuedSends();
            {
                std::scoped_lock stateLock(m_stateMutex);
                m_connected.store(false);
                m_socket.Reset();
            }
            m_receiveThread.request_stop();
            if (m_receiveThread.joinable())
            {
                m_receiveThread.join();
            }
            NotifyError(std::string("Failed to start transport worker: ") + exception.what());
            return false;
        }
        return true;
    }

    [[nodiscard]] SocketHandle CreateConnectedSocket(const char* host, std::uint16_t port)
    {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* rawAddresses = nullptr;
        const std::string service = std::to_string(port);
        const int lookupResult = ::getaddrinfo(host, service.c_str(), &hints, &rawAddresses);
        if (lookupResult != 0)
        {
            NotifyError(SocketError("getaddrinfo", lookupResult));
            return {};
        }

        const std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> addresses(
            rawAddresses, &::freeaddrinfo);
        int lastError = WSAECONNREFUSED;
        for (const addrinfo* address = addresses.get(); address != nullptr; address = address->ai_next)
        {
            SocketHandle candidate(::socket(
                address->ai_family, address->ai_socktype, address->ai_protocol));
            if (!candidate.IsValid())
            {
                lastError = ::WSAGetLastError();
                continue;
            }

            u_long nonBlocking = 1;
            if (::ioctlsocket(candidate.Get(), FIONBIO, &nonBlocking) == SOCKET_ERROR)
            {
                lastError = ::WSAGetLastError();
                continue;
            }
            const int connectResult = ::connect(candidate.Get(), address->ai_addr,
                static_cast<int>(address->ai_addrlen));
            if (connectResult != 0)
            {
                const int connectError = ::WSAGetLastError();
                if (connectError != WSAEWOULDBLOCK && connectError != WSAEINPROGRESS &&
                    connectError != WSAEINVAL)
                {
                    lastError = connectError;
                    continue;
                }
                fd_set writable{};
                fd_set failed{};
                FD_ZERO(&writable);
                FD_ZERO(&failed);
                FD_SET(candidate.Get(), &writable);
                FD_SET(candidate.Get(), &failed);
                const auto totalMicroseconds =
                    std::chrono::duration_cast<std::chrono::microseconds>(m_connectTimeout).count();
                timeval timeout{
                    .tv_sec = static_cast<long>(totalMicroseconds / 1'000'000),
                    .tv_usec = static_cast<long>(totalMicroseconds % 1'000'000),
                };
                const int selectResult = ::select(0, nullptr, &writable, &failed, &timeout);
                if (selectResult == 0)
                {
                    lastError = WSAETIMEDOUT;
                    continue;
                }
                if (selectResult == SOCKET_ERROR)
                {
                    lastError = ::WSAGetLastError();
                    continue;
                }
                int socketError{};
                int errorSize = sizeof(socketError);
                if (::getsockopt(candidate.Get(), SOL_SOCKET, SO_ERROR,
                        reinterpret_cast<char*>(&socketError), &errorSize) == SOCKET_ERROR ||
                    socketError != 0)
                {
                    lastError = socketError != 0 ? socketError : ::WSAGetLastError();
                    continue;
                }
            }
            u_long blocking = 0;
            if (::ioctlsocket(candidate.Get(), FIONBIO, &blocking) == 0)
            {
                return candidate;
            }
            lastError = ::WSAGetLastError();
        }

        NotifyError(SocketError("connect", lastError));
        return {};
    }

    void ReceiveLoop(SOCKET socket, std::stop_token stopToken) noexcept
    {
        std::array<std::byte, kReceiveBufferSize> buffer{};
        while (!stopToken.stop_requested())
        {
            const int received = ::recv(
                socket, reinterpret_cast<char*>(buffer.data()),
                static_cast<int>(buffer.size()), 0);
            if (received > 0)
            {
                m_receivedByteCount.fetch_add(static_cast<std::uint64_t>(received));
                DeliverBytes(buffer.data(), static_cast<std::size_t>(received));
                continue;
            }
            if (received == SOCKET_ERROR)
            {
                const int error = ::WSAGetLastError();
                if (!stopToken.stop_requested() && error != WSAESHUTDOWN && error != WSAENOTSOCK)
                {
                    NotifyError(SocketError("recv", error));
                }
            }
            else if (!stopToken.stop_requested())
            {
                NotifyError("Connection closed by peer");
            }
            break;
        }

        std::scoped_lock stateLock(m_stateMutex);
        if (m_socket.Get() == socket)
        {
            m_connected.store(false);
            m_socket.Reset();
            StopQueuedSends();
        }
    }

    void SendLoop(SOCKET socket, std::stop_token stopToken) noexcept
    {
        while (!stopToken.stop_requested())
        {
            std::vector<std::byte> bytes;
            {
                std::unique_lock queueLock(m_sendQueueMutex);
                m_sendCondition.wait(queueLock, [this, &stopToken] {
                    return stopToken.stop_requested() || !m_sendQueue.empty() || !m_acceptingSends;
                });
                if (stopToken.stop_requested() || (!m_acceptingSends && m_sendQueue.empty()))
                {
                    return;
                }
                bytes = std::move(m_sendQueue.front());
                m_sendQueue.pop_front();
                m_queuedSendBytes -= bytes.size();
            }

            if (!SendAll(socket, bytes, stopToken))
            {
                MarkConnectionFailed(socket);
                return;
            }
        }
    }

    [[nodiscard]] bool SendAll(
        SOCKET socket, std::span<const std::byte> bytes, std::stop_token stopToken) noexcept
    {
        std::size_t sentBytes = 0;
        while (sentBytes < bytes.size() && !stopToken.stop_requested())
        {
            const std::size_t remaining = bytes.size() - sentBytes;
            const int chunkSize = static_cast<int>((std::min)(
                remaining, static_cast<std::size_t>((std::numeric_limits<int>::max)())));
            const auto* data = reinterpret_cast<const char*>(bytes.data() + sentBytes);
            const int result = ::send(socket, data, chunkSize, 0);
            if (result == SOCKET_ERROR)
            {
                if (!stopToken.stop_requested())
                {
                    NotifyError(SocketError("send", ::WSAGetLastError()));
                }
                return false;
            }
            if (result == 0)
            {
                NotifyError("send returned zero bytes");
                return false;
            }
            sentBytes += static_cast<std::size_t>(result);
            m_sentByteCount.fetch_add(static_cast<std::uint64_t>(result));
        }
        return !stopToken.stop_requested();
    }

    void MarkConnectionFailed(SOCKET socket) noexcept
    {
        std::scoped_lock stateLock(m_stateMutex);
        if (m_socket.Get() == socket)
        {
            m_connected.store(false);
            ::shutdown(socket, SD_BOTH);
            m_socket.Reset();
            StopQueuedSends();
        }
    }

    void StopQueuedSends() noexcept
    {
        {
            std::scoped_lock queueLock(m_sendQueueMutex);
            m_acceptingSends = false;
            m_discardedQueuedSendByteCount.fetch_add(
                static_cast<std::uint64_t>(m_queuedSendBytes));
            m_sendQueue.clear();
            m_queuedSendBytes = 0;
        }
        m_sendThread.request_stop();
        m_sendCondition.notify_all();
    }

    void DeliverBytes(const std::byte* data, std::size_t size) noexcept
    {
        try
        {
            ReceiveHandler receiveHandler;
            {
                std::scoped_lock handlerLock(m_handlerMutex);
                receiveHandler = m_receiveHandler;
            }
            if (receiveHandler)
            {
                receiveHandler(std::vector<std::byte>(data, data + size));
            }
        }
        catch (const std::exception& exception)
        {
            NotifyError(std::string("Receive handler threw: ") + exception.what());
        }
        catch (...)
        {
            NotifyError("Receive handler threw an unknown exception");
        }
    }

    void NotifyError(std::string message) noexcept
    {
        ErrorHandler errorHandler;
        {
            std::scoped_lock handlerLock(m_handlerMutex);
            errorHandler = m_errorHandler;
        }
        if (!errorHandler)
        {
            return;
        }
        try
        {
            errorHandler(std::move(message));
        }
        catch (...)
        {
            // Exceptions must never escape a transport worker thread.
        }
    }

    [[nodiscard]] bool JoinFinishedWorkerThreads()
    {
        if ((m_receiveThread.joinable() &&
             m_receiveThread.get_id() == std::this_thread::get_id()) ||
            (m_sendThread.joinable() &&
             m_sendThread.get_id() == std::this_thread::get_id()))
        {
            NotifyError("Connect cannot be called from a transport worker after disconnect");
            return false;
        }
        if (m_receiveThread.joinable())
        {
            m_receiveThread.join();
        }
        if (m_sendThread.joinable())
        {
            m_sendThread.join();
        }
        return true;
    }

    WinsockSession m_winsock;
    ReceiveHandler m_receiveHandler;
    ErrorHandler m_errorHandler;
    std::mutex m_handlerMutex;
    mutable std::mutex m_stateMutex;
    std::mutex m_operationMutex;
    mutable std::mutex m_sendQueueMutex;
    std::condition_variable m_sendCondition;
    std::deque<std::vector<std::byte>> m_sendQueue;
    std::size_t m_queuedSendBytes{};
    std::size_t m_maximumQueuedSendBytes{};
    std::chrono::milliseconds m_connectTimeout{};
    bool m_acceptingSends{false};
    SocketHandle m_socket;
    std::jthread m_receiveThread;
    std::jthread m_sendThread;
    std::atomic_bool m_connected{false};
    std::atomic<std::uint64_t> m_sentByteCount{};
    std::atomic<std::uint64_t> m_receivedByteCount{};
    std::atomic<std::uint64_t> m_rejectedSendCount{};
    std::atomic<std::uint64_t> m_discardedQueuedSendByteCount{};
};

TcpTransport::TcpTransport(
    ReceiveHandler receiveHandler,
    ErrorHandler errorHandler,
    TcpTransportOptions options)
    : m_impl(std::make_unique<Impl>(
        std::move(receiveHandler), std::move(errorHandler), options))
{
}

TcpTransport::~TcpTransport() = default;

void TcpTransport::SetReceiveHandler(ReceiveHandler receiveHandler)
{
    m_impl->SetReceiveHandler(std::move(receiveHandler));
}

void TcpTransport::SetErrorHandler(ErrorHandler errorHandler)
{
    m_impl->SetErrorHandler(std::move(errorHandler));
}

bool TcpTransport::Connect(const char* host, std::uint16_t port)
{
    return m_impl->Connect(host, port);
}

void TcpTransport::Disconnect() noexcept
{
    m_impl->Disconnect();
}

bool TcpTransport::Send(std::span<const std::byte> bytes)
{
    return m_impl->Send(bytes);
}

bool TcpTransport::IsConnected() const noexcept
{
    return m_impl->IsConnected();
}

TcpTransportStatistics TcpTransport::GetStatistics() const noexcept
{
    return m_impl->GetStatistics();
}

} // namespace DeviceLink::Transport
