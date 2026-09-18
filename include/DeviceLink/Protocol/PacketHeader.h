#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace DeviceLink::Protocol
{

inline constexpr std::uint16_t kPacketMagic = 0xD15C;
inline constexpr std::uint8_t kProtocolVersion = 1;
inline constexpr std::size_t kPacketHeaderSize = 14;
inline constexpr std::uint32_t kMaximumPayloadSize = 1024 * 1024;

struct PacketHeader final
{
    std::uint16_t magic{kPacketMagic};
    std::uint8_t version{kProtocolVersion};
    std::uint8_t flags{};
    std::uint16_t messageType{};
    std::uint32_t sequence{};
    std::uint32_t payloadSize{};
};

enum class HeaderValidationResult
{
    Valid,
    InvalidMagic,
    UnsupportedVersion,
    PayloadTooLarge,
};

[[nodiscard]] HeaderValidationResult ValidateHeader(const PacketHeader& header) noexcept;
[[nodiscard]] std::array<std::byte, kPacketHeaderSize> SerializeHeader(
    const PacketHeader& header) noexcept;
[[nodiscard]] bool TryDeserializeHeader(
    std::span<const std::byte> bytes, PacketHeader& header) noexcept;

} // namespace DeviceLink::Protocol
