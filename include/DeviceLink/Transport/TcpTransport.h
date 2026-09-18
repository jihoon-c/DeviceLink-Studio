#pragma once

#include "DeviceLink/Transport/ITransport.h"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <memory>

namespace DeviceLink::Transport
{
struct TcpTransportOptions final
{
    std::size_t maximumQueuedSendBytes{1024 * 1024};
    std::chrono::milliseconds connectTimeout{3000};
};

struct TcpTransportStatistics final
{
    std::uint64_t sentByteCount{};
    std::uint64_t receivedByteCount{};
    std::uint64_t rejectedSendCount{};
    std::uint64_t discardedQueuedSendByteCount{};
    std::size_t queuedSendByteCount{};
};

class TcpTransport final : public ITransport
{
public:
    using ReceiveHandler = ITransport::ReceiveHandler;
    using ErrorHandler = ITransport::ErrorHandler;

    explicit TcpTransport(
        ReceiveHandler receiveHandler = {},
        ErrorHandler errorHandler = {},
        TcpTransportOptions options = {});
    ~TcpTransport() override;

    TcpTransport(const TcpTransport&) = delete;
    TcpTransport& operator=(const TcpTransport&) = delete;
    TcpTransport(TcpTransport&&) = delete;
    TcpTransport& operator=(TcpTransport&&) = delete;

    void SetReceiveHandler(ReceiveHandler receiveHandler) override;
    void SetErrorHandler(ErrorHandler errorHandler) override;
    [[nodiscard]] bool Connect(const char* host, std::uint16_t port) override;
    void Disconnect() noexcept override;
    [[nodiscard]] bool Send(std::span<const std::byte> bytes) override;
    [[nodiscard]] bool IsConnected() const noexcept override;
    [[nodiscard]] TcpTransportStatistics GetStatistics() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace DeviceLink::Transport
