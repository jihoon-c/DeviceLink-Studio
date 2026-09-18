#pragma once

#include "DeviceLink/Protocol/PacketHeader.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace DeviceLink::Protocol
{

inline constexpr std::size_t kFrameCrcSize = 2;
inline constexpr std::size_t kMaximumFrameSize =
    kPacketHeaderSize + kMaximumPayloadSize + kFrameCrcSize;

struct PacketFrame final
{
    PacketHeader header{};
    std::vector<std::byte> payload;
};

[[nodiscard]] std::optional<std::vector<std::byte>> SerializeFrame(const PacketFrame& frame);

class FrameStreamParser final
{
public:
    [[nodiscard]] std::vector<PacketFrame> Consume(std::span<const std::byte> bytes);
    void Reset() noexcept;
    [[nodiscard]] std::size_t BufferedByteCount() const noexcept;

private:
    void Append(std::span<const std::byte> bytes);
    void SynchronizeToMagic();
    [[nodiscard]] bool HasCompleteFrame(const PacketHeader& header) const noexcept;
    [[nodiscard]] bool HasValidFrameCrc(const PacketHeader& header) const noexcept;
    [[nodiscard]] PacketFrame ExtractFrame(const PacketHeader& header);

    std::vector<std::byte> m_buffer;
};

} // namespace DeviceLink::Protocol
