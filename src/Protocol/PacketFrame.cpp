#include "DeviceLink/Protocol/PacketFrame.h"

#include "DeviceLink/Protocol/Crc16.h"

#include <algorithm>
#include <array>

namespace DeviceLink::Protocol
{
namespace
{

[[nodiscard]] std::uint16_t ReadUint16(std::span<const std::byte> bytes, std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset]) << 8) |
           std::to_integer<std::uint8_t>(bytes[offset + 1]);
}

void WriteUint16(std::span<std::byte> bytes, std::size_t offset, std::uint16_t value) noexcept
{
    bytes[offset] = static_cast<std::byte>(value >> 8);
    bytes[offset + 1] = static_cast<std::byte>(value);
}

[[nodiscard]] std::size_t FrameSize(const PacketHeader& header) noexcept
{
    return kPacketHeaderSize + static_cast<std::size_t>(header.payloadSize) + kFrameCrcSize;
}

} // namespace

std::optional<std::vector<std::byte>> SerializeFrame(const PacketFrame& frame)
{
    if (frame.payload.size() > kMaximumPayloadSize ||
        frame.header.payloadSize != frame.payload.size() ||
        ValidateHeader(frame.header) != HeaderValidationResult::Valid)
    {
        return std::nullopt;
    }

    std::vector<std::byte> bytes(FrameSize(frame.header));
    const auto headerBytes = SerializeHeader(frame.header);
    std::copy(headerBytes.begin(), headerBytes.end(), bytes.begin());
    std::copy(frame.payload.begin(), frame.payload.end(), bytes.begin() + kPacketHeaderSize);
    WriteUint16(bytes, bytes.size() - kFrameCrcSize, CalculateCrc16Ccitt(
        std::span<const std::byte>(bytes).first(bytes.size() - kFrameCrcSize)));
    return bytes;
}

std::vector<PacketFrame> FrameStreamParser::Consume(std::span<const std::byte> bytes)
{
    Append(bytes);
    std::vector<PacketFrame> frames;
    while (true)
    {
        SynchronizeToMagic();
        if (m_buffer.size() < kPacketHeaderSize)
        {
            return frames;
        }

        PacketHeader header{};
        if (!TryDeserializeHeader(m_buffer, header))
        {
            m_buffer.erase(m_buffer.begin());
            continue;
        }
        if (!HasCompleteFrame(header))
        {
            return frames;
        }
        if (!HasValidFrameCrc(header))
        {
            m_buffer.erase(m_buffer.begin());
            continue;
        }
        frames.push_back(ExtractFrame(header));
    }
}

void FrameStreamParser::Reset() noexcept
{
    m_buffer.clear();
}

std::size_t FrameStreamParser::BufferedByteCount() const noexcept
{
    return m_buffer.size();
}

void FrameStreamParser::Append(std::span<const std::byte> bytes)
{
    m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
    if (m_buffer.size() > kMaximumFrameSize)
    {
        m_buffer.erase(m_buffer.begin(), m_buffer.end() - kMaximumFrameSize);
    }
}

void FrameStreamParser::SynchronizeToMagic()
{
    constexpr std::array<std::byte, 2> kMagicBytes{
        static_cast<std::byte>(kPacketMagic >> 8), static_cast<std::byte>(kPacketMagic)};
    const auto magic = std::search(
        m_buffer.begin(), m_buffer.end(), kMagicBytes.begin(), kMagicBytes.end());
    if (magic != m_buffer.end())
    {
        m_buffer.erase(m_buffer.begin(), magic);
        return;
    }

    const bool endsWithMagicPrefix = !m_buffer.empty() && m_buffer.back() == kMagicBytes.front();
    m_buffer.clear();
    if (endsWithMagicPrefix)
    {
        m_buffer.push_back(kMagicBytes.front());
    }
}

bool FrameStreamParser::HasCompleteFrame(const PacketHeader& header) const noexcept
{
    return m_buffer.size() >= FrameSize(header);
}

bool FrameStreamParser::HasValidFrameCrc(const PacketHeader& header) const noexcept
{
    const std::size_t frameSize = FrameSize(header);
    const std::uint16_t expectedCrc = ReadUint16(m_buffer, frameSize - kFrameCrcSize);
    const std::uint16_t actualCrc = CalculateCrc16Ccitt(
        std::span<const std::byte>(m_buffer).first(frameSize - kFrameCrcSize));
    return actualCrc == expectedCrc;
}

PacketFrame FrameStreamParser::ExtractFrame(const PacketHeader& header)
{
    const std::size_t frameSize = FrameSize(header);
    PacketFrame frame{header, std::vector<std::byte>(
        m_buffer.begin() + kPacketHeaderSize,
        m_buffer.begin() + kPacketHeaderSize + header.payloadSize)};
    m_buffer.erase(m_buffer.begin(), m_buffer.begin() + frameSize);
    return frame;
}

} // namespace DeviceLink::Protocol
