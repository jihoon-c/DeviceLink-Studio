#pragma once

#include "DeviceLink/Protocol/PacketFrame.h"

#include <cstdint>
#include <optional>
#include <span>

namespace DeviceLink::Application
{
enum class VirtualGimbalState : std::uint8_t
{
    Offline,
    Ready,
    Initializing,
    Scanning,
    Fault,
};

enum class VirtualGimbalFault : std::uint8_t
{
    None,
    MotorStall,
    OverTemperature,
    SensorFailure,
};

enum class VirtualGimbalResponseMode : std::uint8_t
{
    Normal,
    Delayed,
    NoResponse,
};

struct VirtualGimbalTelemetry final
{
    VirtualGimbalState state{};
    VirtualGimbalFault fault{};
    double panDegrees{};
    double tiltDegrees{};
    double targetPanDegrees{};
    double targetTiltDegrees{};
    double temperatureCelsius{};
    double supplyVoltage{};
};

struct VirtualGimbalAcknowledgement final
{
    std::uint16_t commandType{};
    bool succeeded{};
};

enum class GimbalCommandKind : std::uint8_t
{
    Power,
    Initialize,
    SetPanTilt,
    StartScan,
    StopScan,
    RequestStatus,
    InjectFault,
    ConfigureResponse,
    SelectEquipmentMode,
    DroneTakeOff,
    DroneLand,
    DroneMoveTo,
    DroneReturnHome,
};

enum class VirtualEquipmentMode : std::uint8_t { FixedCamera, Drone };

struct GimbalCommand final
{
    GimbalCommandKind kind{};
    bool powerEnabled{};
    double panDegrees{};
    double tiltDegrees{};
    VirtualGimbalFault fault{};
    VirtualGimbalResponseMode responseMode{};
    std::uint16_t responseDelayMilliseconds{};
    VirtualEquipmentMode equipmentMode{VirtualEquipmentMode::FixedCamera};
    double droneXmeters{};
    double droneYmeters{};
    double droneAltitudeMeters{};
};

class IGimbalProtocolAdapter
{
public:
    virtual ~IGimbalProtocolAdapter() = default;

    [[nodiscard]] virtual std::optional<Protocol::PacketFrame> EncodeCommand(
        const GimbalCommand& command, std::uint32_t sequence) const = 0;
    [[nodiscard]] virtual std::optional<VirtualGimbalAcknowledgement> DecodeAcknowledgement(
        std::uint16_t messageType, std::span<const std::byte> payload) const noexcept = 0;
    [[nodiscard]] virtual std::optional<VirtualGimbalTelemetry> DecodeTelemetry(
        std::uint16_t messageType, std::span<const std::byte> payload) const noexcept = 0;
    [[nodiscard]] virtual std::uint16_t AcknowledgeMessageType() const noexcept = 0;
    [[nodiscard]] virtual std::uint16_t TelemetryMessageType() const noexcept = 0;
};

class DeviceLinkGimbalProtocolAdapter final : public IGimbalProtocolAdapter
{
public:
    static constexpr std::uint16_t kAcknowledgeMessageType = 0x6001;
    static constexpr std::uint16_t kTelemetryMessageType = 0x7001;

    [[nodiscard]] std::optional<Protocol::PacketFrame> EncodeCommand(
        const GimbalCommand& command, std::uint32_t sequence) const override;
    [[nodiscard]] std::optional<VirtualGimbalAcknowledgement> DecodeAcknowledgement(
        std::uint16_t messageType,
        std::span<const std::byte> payload) const noexcept override;
    [[nodiscard]] std::optional<VirtualGimbalTelemetry> DecodeTelemetry(
        std::uint16_t messageType,
        std::span<const std::byte> payload) const noexcept override;
    [[nodiscard]] std::uint16_t AcknowledgeMessageType() const noexcept override;
    [[nodiscard]] std::uint16_t TelemetryMessageType() const noexcept override;
};
} // namespace DeviceLink::Application
