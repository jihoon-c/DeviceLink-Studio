#include "DeviceLink/Protocol/PacketHeader.h"

namespace DeviceLink::Protocol
{
namespace
{

void WriteUint16(std::span<std::byte> bytes, std::size_t offset, std::uint16_t value) noexcept
{
    bytes[offset] = static_cast<std::byte>(value >> 8);
    bytes[offset + 1] = static_cast<std::byte>(value);
}

void WriteUint32(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) noexcept
{
    bytes[offset] = static_cast<std::byte>(value >> 24);
    bytes[offset + 1] = static_cast<std::byte>(value >> 16);
    bytes[offset + 2] = static_cast<std::byte>(value >> 8);
    bytes[offset + 3] = static_cast<std::byte>(value);
}

[[nodiscard]] std::uint16_t ReadUint16(std::span<const std::byte> bytes, std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset]) << 8) |
           std::to_integer<std::uint8_t>(bytes[offset + 1]);
}

[[nodiscard]] std::uint32_t ReadUint32(std::span<const std::byte> bytes, std::size_t offset) noexcept
{
    return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 24) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 16) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 8) |
           std::to_integer<std::uint8_t>(bytes[offset + 3]);
}

} // namespace

HeaderValidationResult ValidateHeader(const PacketHeader& header) noexcept
{
    if (header.magic != kPacketMagic)
    {
        return HeaderValidationResult::InvalidMagic;
    }
    if (header.version != kProtocolVersion)
    {
        return HeaderValidationResult::UnsupportedVersion;
    }
    if (header.payloadSize > kMaximumPayloadSize)
    {
        return HeaderValidationResult::PayloadTooLarge;
    }
    return HeaderValidationResult::Valid;
}

std::array<std::byte, kPacketHeaderSize> SerializeHeader(const PacketHeader& header) noexcept
{
    std::array<std::byte, kPacketHeaderSize> bytes{};
    WriteUint16(bytes, 0, header.magic);
    bytes[2] = static_cast<std::byte>(header.version);
    bytes[3] = static_cast<std::byte>(header.flags);
    WriteUint16(bytes, 4, header.messageType);
    WriteUint32(bytes, 6, header.sequence);
    WriteUint32(bytes, 10, header.payloadSize);
    return bytes;
}

bool TryDeserializeHeader(std::span<const std::byte> bytes, PacketHeader& header) noexcept
{
    if (bytes.size() < kPacketHeaderSize)
    {
        return false;
    }

    PacketHeader parsedHeader{};
    parsedHeader.magic = ReadUint16(bytes, 0);
    parsedHeader.version = std::to_integer<std::uint8_t>(bytes[2]);
    parsedHeader.flags = std::to_integer<std::uint8_t>(bytes[3]);
    parsedHeader.messageType = ReadUint16(bytes, 4);
    parsedHeader.sequence = ReadUint32(bytes, 6);
    parsedHeader.payloadSize = ReadUint32(bytes, 10);
    if (ValidateHeader(parsedHeader) != HeaderValidationResult::Valid)
    {
        return false;
    }

    header = parsedHeader;
    return true;
}

} // namespace DeviceLink::Protocol
