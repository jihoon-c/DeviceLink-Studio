#include "DeviceLink/Protocol/Crc16.h"
#include "DeviceLink/Protocol/PacketFrame.h"
#include "DeviceLink/Protocol/PacketHeader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void CrcMatchesReferenceVector()
{
    constexpr std::array<std::byte, 9> kInput{
        std::byte{'1'}, std::byte{'2'}, std::byte{'3'}, std::byte{'4'}, std::byte{'5'},
        std::byte{'6'}, std::byte{'7'}, std::byte{'8'}, std::byte{'9'}};

    Require(DeviceLink::Protocol::CalculateCrc16Ccitt(kInput) == 0x29B1,
            "CRC-16/CCITT-FALSE reference vector differs");
    Require(DeviceLink::Protocol::CalculateCrc16Ccitt(kInput) ==
                DeviceLink::Protocol::CalculateCrc16Ccitt(
                    std::span<const std::byte>(kInput).subspan(4),
                    DeviceLink::Protocol::CalculateCrc16Ccitt(
                        std::span<const std::byte>(kInput).first(4))),
            "CRC incremental calculation differs");
}

void HeaderRoundTripsWithNetworkByteOrder()
{
    const DeviceLink::Protocol::PacketHeader header{
        .flags = 0xA5,
        .messageType = 0x1234,
        .sequence = 0x01020304,
        .payloadSize = 0x00010002,
    };
    const auto bytes = DeviceLink::Protocol::SerializeHeader(header);
    constexpr std::array<std::byte, DeviceLink::Protocol::kPacketHeaderSize> kExpected{
        std::byte{0xD1}, std::byte{0x5C}, std::byte{0x01}, std::byte{0xA5},
        std::byte{0x12}, std::byte{0x34}, std::byte{0x01}, std::byte{0x02},
        std::byte{0x03}, std::byte{0x04}, std::byte{0x00}, std::byte{0x01},
        std::byte{0x00}, std::byte{0x02}};
    Require(bytes == kExpected, "Header wire format differs");

    DeviceLink::Protocol::PacketHeader parsed{};
    Require(DeviceLink::Protocol::TryDeserializeHeader(bytes, parsed), "Header parsing failed");
    Require(parsed.magic == header.magic && parsed.version == header.version &&
                parsed.flags == header.flags && parsed.messageType == header.messageType &&
                parsed.sequence == header.sequence && parsed.payloadSize == header.payloadSize,
            "Round-tripped header differs");
}

void InvalidHeadersAreRejected()
{
    DeviceLink::Protocol::PacketHeader header{};
    header.magic = 0;
    Require(DeviceLink::Protocol::ValidateHeader(header) ==
                DeviceLink::Protocol::HeaderValidationResult::InvalidMagic,
            "Invalid magic accepted");

    header = {};
    header.version = 2;
    Require(DeviceLink::Protocol::ValidateHeader(header) ==
                DeviceLink::Protocol::HeaderValidationResult::UnsupportedVersion,
            "Unsupported version accepted");

    header = {};
    header.payloadSize = DeviceLink::Protocol::kMaximumPayloadSize + 1;
    const auto oversizedBytes = DeviceLink::Protocol::SerializeHeader(header);
    Require(!DeviceLink::Protocol::TryDeserializeHeader(oversizedBytes, header),
            "Oversized payload header accepted");
}

[[nodiscard]] DeviceLink::Protocol::PacketFrame CreateFrame(
    std::uint32_t sequence, std::vector<std::byte> payload)
{
    return {
        .header = {
            .messageType = 0x42,
            .sequence = sequence,
            .payloadSize = static_cast<std::uint32_t>(payload.size()),
        },
        .payload = std::move(payload),
    };
}

void FramesSerializeAndParseAcrossReceiveBoundaries()
{
    const auto frame = CreateFrame(7, {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}});
    const auto bytes = DeviceLink::Protocol::SerializeFrame(frame);
    Require(bytes.has_value(), "Frame serialization failed");

    DeviceLink::Protocol::FrameStreamParser parser;
    Require(parser.Consume(std::span<const std::byte>(*bytes).first(5)).empty(),
            "Partial frame was parsed");
    const auto frames = parser.Consume(std::span<const std::byte>(*bytes).subspan(5));
    Require(frames.size() == 1, "Completed frame was not parsed");
    Require(frames.front().header.sequence == 7 && frames.front().payload == frame.payload,
            "Parsed frame differs");
    Require(parser.BufferedByteCount() == 0, "Parser retained a complete frame");
}

void ParserPreservesFrameOrderAndRecoversAfterBadCrc()
{
    const auto first = DeviceLink::Protocol::SerializeFrame(CreateFrame(1, {std::byte{0x10}}));
    const auto second = DeviceLink::Protocol::SerializeFrame(CreateFrame(2, {std::byte{0x20}, std::byte{0x21}}));
    Require(first.has_value() && second.has_value(), "Frame serialization failed");

    auto corrupted = *first;
    corrupted.back() ^= std::byte{0x01};
    std::vector<std::byte> stream{std::byte{0x00}, std::byte{0xD1}};
    stream.insert(stream.end(), corrupted.begin(), corrupted.end());
    stream.insert(stream.end(), first->begin(), first->end());
    stream.insert(stream.end(), second->begin(), second->end());

    DeviceLink::Protocol::FrameStreamParser parser;
    const auto frames = parser.Consume(stream);
    Require(frames.size() == 2, "Parser did not recover after CRC failure");
    Require(frames[0].header.sequence == 1 && frames[1].header.sequence == 2,
            "Parser did not preserve frame order");
}

void InvalidFramesCannotBeSerialized()
{
    auto frame = CreateFrame(1, {std::byte{0x01}});
    frame.header.payloadSize = 0;
    Require(!DeviceLink::Protocol::SerializeFrame(frame).has_value(),
            "Mismatched payload size serialized");
}

void ParserRecoversAcrossCorruptionCorpusAndArbitraryChunks()
{
    constexpr std::uint32_t kFrameCount = 16;
    std::vector<std::byte> stream;
    std::vector<std::uint32_t> expectedSequences;
    for (std::uint32_t sequence = 1; sequence <= kFrameCount; ++sequence)
    {
        const auto frame = CreateFrame(sequence, {
            static_cast<std::byte>(sequence),
            static_cast<std::byte>(sequence ^ 0x5A),
        });
        const auto bytes = DeviceLink::Protocol::SerializeFrame(frame);
        Require(bytes.has_value(), "Corpus frame serialization failed");
        auto corrupted = *bytes;
        corrupted.back() ^= std::byte{0x80};
        stream.insert(stream.end(), {std::byte{0x00}, std::byte{0xD1}, std::byte{0x00}});
        stream.insert(stream.end(), corrupted.begin(), corrupted.end());
        stream.insert(stream.end(), bytes->begin(), bytes->end());
        expectedSequences.push_back(sequence);
    }

    DeviceLink::Protocol::FrameStreamParser parser;
    std::vector<DeviceLink::Protocol::PacketFrame> parsedFrames;
    std::size_t offset{};
    std::size_t chunkSize = 1;
    while (offset < stream.size())
    {
        const std::size_t count = (std::min)(chunkSize, stream.size() - offset);
        const auto frames = parser.Consume(std::span<const std::byte>(stream).subspan(offset, count));
        parsedFrames.insert(parsedFrames.end(), frames.begin(), frames.end());
        offset += count;
        chunkSize = chunkSize == 7 ? 1 : chunkSize + 1;
    }

    Require(parsedFrames.size() == expectedSequences.size(),
            "Corpus parser frame count differs");
    for (std::size_t index{}; index < parsedFrames.size(); ++index)
    {
        Require(parsedFrames[index].header.sequence == expectedSequences[index],
                "Corpus parser frame sequence differs");
    }
    Require(parser.BufferedByteCount() == 0, "Corpus parser retained bytes");
}

} // namespace

int main()
{
    try
    {
        CrcMatchesReferenceVector();
        HeaderRoundTripsWithNetworkByteOrder();
        InvalidHeadersAreRejected();
        FramesSerializeAndParseAcrossReceiveBoundaries();
        ParserPreservesFrameOrderAndRecoversAfterBadCrc();
        InvalidFramesCannotBeSerialized();
        ParserRecoversAcrossCorruptionCorpusAndArbitraryChunks();
        std::cout << "Protocol tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Protocol test failure: " << exception.what() << '\n';
        return 1;
    }
}
