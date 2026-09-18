#include "DeviceLink/Application/GimbalProtocolAdapter.h"

#include <array>
#include <cmath>

namespace DeviceLink::Application
{
namespace
{
constexpr std::uint16_t kPowerMessageType = 0x1001;
constexpr std::uint16_t kInitializeMessageType = 0x1002;
constexpr std::uint16_t kSetPanTiltMessageType = 0x1003;
constexpr std::uint16_t kStartScanMessageType = 0x1004;
constexpr std::uint16_t kStopScanMessageType = 0x1005;
constexpr std::uint16_t kGetStatusMessageType = 0x1006;
constexpr std::uint16_t kInjectFaultMessageType = 0x1007;
constexpr std::uint16_t kConfigureResponseMessageType = 0x1008;
constexpr std::uint16_t kSelectEquipmentModeMessageType = 0x1009;
constexpr std::uint16_t kDroneTakeOffMessageType = 0x1010;
constexpr std::uint16_t kDroneLandMessageType = 0x1011;
constexpr std::uint16_t kDroneMoveToMessageType = 0x1012;
constexpr std::uint16_t kDroneReturnHomeMessageType = 0x1013;

void WriteInt16BigEndian(std::span<std::byte> output, std::size_t offset, std::int16_t value)
{
    const auto bits = static_cast<std::uint16_t>(value);
    output[offset] = static_cast<std::byte>((bits >> 8U) & 0xFFU);
    output[offset + 1] = static_cast<std::byte>(bits & 0xFFU);
}

std::uint16_t ReadUint16BigEndian(std::span<const std::byte> payload, std::size_t offset)
{
    return static_cast<std::uint16_t>(
        (std::to_integer<std::uint16_t>(payload[offset]) << 8U) |
        std::to_integer<std::uint16_t>(payload[offset + 1]));
}

std::int16_t ReadInt16BigEndian(std::span<const std::byte> payload, std::size_t offset)
{
    return static_cast<std::int16_t>(ReadUint16BigEndian(payload, offset));
}

Protocol::PacketFrame MakeFrame(
    std::uint16_t messageType, std::uint32_t sequence, std::span<const std::byte> payload = {})
{
    Protocol::PacketFrame frame{};
    frame.header.messageType = messageType;
    frame.header.sequence = sequence;
    frame.header.payloadSize = static_cast<std::uint32_t>(payload.size());
    frame.payload.assign(payload.begin(), payload.end());
    return frame;
}
} // namespace

std::optional<Protocol::PacketFrame> DeviceLinkGimbalProtocolAdapter::EncodeCommand(
    const GimbalCommand& command, std::uint32_t sequence) const
{
    switch (command.kind)
    {
    case GimbalCommandKind::Power:
    {
        const std::array payload{command.powerEnabled ? std::byte{1} : std::byte{0}};
        return MakeFrame(kPowerMessageType, sequence, payload);
    }
    case GimbalCommandKind::Initialize:
        return MakeFrame(kInitializeMessageType, sequence);
    case GimbalCommandKind::SetPanTilt:
    {
        if (!std::isfinite(command.panDegrees) || !std::isfinite(command.tiltDegrees) ||
            command.panDegrees < -170.0 || command.panDegrees > 170.0 ||
            command.tiltDegrees < -45.0 || command.tiltDegrees > 80.0)
        {
            return std::nullopt;
        }
        const auto pan = static_cast<std::int16_t>(std::lround(command.panDegrees * 100.0));
        const auto tilt = static_cast<std::int16_t>(std::lround(command.tiltDegrees * 100.0));
        std::array<std::byte, 4> payload{};
        WriteInt16BigEndian(payload, 0, pan);
        WriteInt16BigEndian(payload, 2, tilt);
        return MakeFrame(kSetPanTiltMessageType, sequence, payload);
    }
    case GimbalCommandKind::StartScan:
        return MakeFrame(kStartScanMessageType, sequence);
    case GimbalCommandKind::StopScan:
        return MakeFrame(kStopScanMessageType, sequence);
    case GimbalCommandKind::RequestStatus:
        return MakeFrame(kGetStatusMessageType, sequence);
    case GimbalCommandKind::InjectFault:
    {
        const auto fault = static_cast<std::uint8_t>(command.fault);
        if (fault > static_cast<std::uint8_t>(VirtualGimbalFault::SensorFailure))
        {
            return std::nullopt;
        }
        const std::array payload{static_cast<std::byte>(fault)};
        return MakeFrame(kInjectFaultMessageType, sequence, payload);
    }
    case GimbalCommandKind::ConfigureResponse:
    {
        const auto mode = static_cast<std::uint8_t>(command.responseMode);
        if (mode > static_cast<std::uint8_t>(VirtualGimbalResponseMode::NoResponse) ||
            (command.responseMode == VirtualGimbalResponseMode::Delayed &&
                command.responseDelayMilliseconds == 0))
        {
            return std::nullopt;
        }
        const std::array payload{
            static_cast<std::byte>(mode),
            static_cast<std::byte>(command.responseDelayMilliseconds >> 8U),
            static_cast<std::byte>(command.responseDelayMilliseconds & 0xFFU),
        };
        return MakeFrame(kConfigureResponseMessageType, sequence, payload);
    }
    case GimbalCommandKind::SelectEquipmentMode:
        return MakeFrame(kSelectEquipmentModeMessageType, sequence,
            std::array{static_cast<std::byte>(command.equipmentMode)});
    case GimbalCommandKind::DroneTakeOff:
        return MakeFrame(kDroneTakeOffMessageType, sequence);
    case GimbalCommandKind::DroneLand:
        return MakeFrame(kDroneLandMessageType, sequence);
    case GimbalCommandKind::DroneMoveTo:
    {
        if (!std::isfinite(command.droneXmeters) || !std::isfinite(command.droneYmeters) ||
            !std::isfinite(command.droneAltitudeMeters) || command.droneAltitudeMeters < 0.0 ||
            command.droneXmeters < -3276.7 || command.droneXmeters > 3276.7 ||
            command.droneYmeters < -3276.7 || command.droneYmeters > 3276.7 ||
            command.droneAltitudeMeters > 3276.7)
        {
            return std::nullopt;
        }
        std::array<std::byte, 6> payload{};
        WriteInt16BigEndian(payload, 0, static_cast<std::int16_t>(std::lround(command.droneXmeters * 10.0)));
        WriteInt16BigEndian(payload, 2, static_cast<std::int16_t>(std::lround(command.droneYmeters * 10.0)));
        WriteInt16BigEndian(payload, 4, static_cast<std::int16_t>(std::lround(command.droneAltitudeMeters * 10.0)));
        return MakeFrame(kDroneMoveToMessageType, sequence, payload);
    }
    case GimbalCommandKind::DroneReturnHome:
        return MakeFrame(kDroneReturnHomeMessageType, sequence);
    }
    return std::nullopt;
}

std::optional<VirtualGimbalAcknowledgement>
DeviceLinkGimbalProtocolAdapter::DecodeAcknowledgement(
    std::uint16_t messageType, std::span<const std::byte> payload) const noexcept
{
    if (messageType != kAcknowledgeMessageType || payload.size() != 3)
    {
        return std::nullopt;
    }
    return VirtualGimbalAcknowledgement{
        .commandType = ReadUint16BigEndian(payload, 0),
        .succeeded = std::to_integer<std::uint8_t>(payload[2]) == 0,
    };
}

std::optional<VirtualGimbalTelemetry> DeviceLinkGimbalProtocolAdapter::DecodeTelemetry(
    std::uint16_t messageType, std::span<const std::byte> payload) const noexcept
{
    if (messageType != kTelemetryMessageType || payload.size() != 14)
    {
        return std::nullopt;
    }
    const auto state = std::to_integer<std::uint8_t>(payload[0]);
    const auto fault = std::to_integer<std::uint8_t>(payload[1]);
    if (state > static_cast<std::uint8_t>(VirtualGimbalState::Fault) ||
        fault > static_cast<std::uint8_t>(VirtualGimbalFault::SensorFailure))
    {
        return std::nullopt;
    }
    return VirtualGimbalTelemetry{
        .state = static_cast<VirtualGimbalState>(state),
        .fault = static_cast<VirtualGimbalFault>(fault),
        .panDegrees = ReadInt16BigEndian(payload, 2) / 100.0,
        .tiltDegrees = ReadInt16BigEndian(payload, 4) / 100.0,
        .targetPanDegrees = ReadInt16BigEndian(payload, 6) / 100.0,
        .targetTiltDegrees = ReadInt16BigEndian(payload, 8) / 100.0,
        .temperatureCelsius = ReadInt16BigEndian(payload, 10) / 100.0,
        .supplyVoltage = ReadUint16BigEndian(payload, 12) / 1000.0,
    };
}

std::uint16_t DeviceLinkGimbalProtocolAdapter::AcknowledgeMessageType() const noexcept
{
    return kAcknowledgeMessageType;
}

std::uint16_t DeviceLinkGimbalProtocolAdapter::TelemetryMessageType() const noexcept
{
    return kTelemetryMessageType;
}
} // namespace DeviceLink::Application
