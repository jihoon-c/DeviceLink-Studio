#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace DeviceLink::Transport
{

class ITransport
{
public:
    using ReceiveHandler = std::function<void(std::vector<std::byte>)>;
    using ErrorHandler = std::function<void(std::string)>;

    virtual ~ITransport() = default;

    virtual void SetReceiveHandler(ReceiveHandler receiveHandler) = 0;
    virtual void SetErrorHandler(ErrorHandler errorHandler) = 0;
    [[nodiscard]] virtual bool Connect(const char* host, std::uint16_t port) = 0;
    virtual void Disconnect() noexcept = 0;
    [[nodiscard]] virtual bool Send(std::span<const std::byte> bytes) = 0;
    [[nodiscard]] virtual bool IsConnected() const noexcept = 0;
};

} // namespace DeviceLink::Transport
