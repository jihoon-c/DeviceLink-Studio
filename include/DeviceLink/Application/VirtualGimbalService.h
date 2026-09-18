#pragma once

#include "DeviceLink/Application/DeviceManager.h"
#include "DeviceLink/Application/GimbalProtocolAdapter.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <optional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <mutex>
#include <thread>

namespace DeviceLink::Application
{

struct ReconnectPolicy final
{
    bool enabled{true};
    std::chrono::milliseconds retryInterval{1500};
    std::uint32_t maximumAttempts{5};
};

struct ReconnectStatus final
{
    bool enabled{};
    bool connectionDesired{};
    bool exhausted{};
    std::uint32_t attemptCount{};
    std::uint32_t maximumAttempts{};
};

class VirtualGimbalService final
{
public:
    static constexpr std::uint16_t kAcknowledgeMessageType =
        DeviceLinkGimbalProtocolAdapter::kAcknowledgeMessageType;
    static constexpr std::uint16_t kTelemetryMessageType =
        DeviceLinkGimbalProtocolAdapter::kTelemetryMessageType;
    static constexpr double kMinimumPanDegrees = -170.0;
    static constexpr double kMaximumPanDegrees = 170.0;
    static constexpr double kMinimumTiltDegrees = -45.0;
    static constexpr double kMaximumTiltDegrees = 80.0;

    VirtualGimbalService(
        DeviceManager& manager,
        std::string deviceId,
        ReconnectPolicy reconnectPolicy = {},
        std::unique_ptr<IGimbalProtocolAdapter> protocolAdapter = {});
    ~VirtualGimbalService();

    VirtualGimbalService(const VirtualGimbalService&) = delete;
    VirtualGimbalService& operator=(const VirtualGimbalService&) = delete;

    [[nodiscard]] bool Connect(std::string_view host, std::uint16_t port);
    [[nodiscard]] bool Disconnect() noexcept;
    [[nodiscard]] bool Power(bool enabled);
    [[nodiscard]] bool Initialize();
    [[nodiscard]] bool SetPanTilt(double panDegrees, double tiltDegrees);
    [[nodiscard]] bool StartScan();
    [[nodiscard]] bool StopScan();
    [[nodiscard]] bool RequestStatus();
    [[nodiscard]] bool InjectFault(VirtualGimbalFault fault);
    [[nodiscard]] bool ConfigureResponse(
        VirtualGimbalResponseMode mode, std::uint16_t delayMilliseconds = 0);
    [[nodiscard]] bool SelectEquipmentMode(VirtualEquipmentMode mode);
    [[nodiscard]] bool DroneTakeOff();
    [[nodiscard]] bool DroneLand();
    [[nodiscard]] bool DroneMoveTo(double xMeters, double yMeters, double altitudeMeters);
    [[nodiscard]] bool DroneReturnHome();
    [[nodiscard]] std::optional<Core::ConnectionState> ConnectionState() const;
    [[nodiscard]] const std::string& DeviceId() const noexcept;
    void SetAutomaticReconnectEnabled(bool enabled) noexcept;
    void SetReconnectPolicy(ReconnectPolicy policy) noexcept;
    [[nodiscard]] ReconnectStatus GetReconnectStatus() const noexcept;

    [[nodiscard]] static std::optional<VirtualGimbalAcknowledgement> DecodeAcknowledgement(
        std::uint16_t messageType, std::span<const std::byte> payload) noexcept;
    [[nodiscard]] static std::optional<VirtualGimbalTelemetry> DecodeTelemetry(
        std::uint16_t messageType, std::span<const std::byte> payload) noexcept;
    [[nodiscard]] std::optional<VirtualGimbalAcknowledgement> DecodeDeviceAcknowledgement(
        std::uint16_t messageType, std::span<const std::byte> payload) const noexcept;
    [[nodiscard]] std::optional<VirtualGimbalTelemetry> DecodeDeviceTelemetry(
        std::uint16_t messageType, std::span<const std::byte> payload) const noexcept;
    [[nodiscard]] std::uint16_t DeviceAcknowledgeMessageType() const noexcept;
    [[nodiscard]] std::uint16_t DeviceTelemetryMessageType() const noexcept;

private:
    [[nodiscard]] bool Send(const GimbalCommand& command);
    void ReconnectLoop(std::stop_token stopToken) noexcept;

    DeviceManager& m_manager;
    std::string m_deviceId;
    std::unique_ptr<IGimbalProtocolAdapter> m_protocolAdapter;
    std::atomic<std::uint32_t> m_nextSequence{1};
    mutable std::mutex m_reconnectMutex;
    std::condition_variable_any m_reconnectCondition;
    ReconnectPolicy m_reconnectPolicy;
    std::string m_reconnectHost;
    std::uint16_t m_reconnectPort{};
    std::uint32_t m_reconnectAttemptCount{};
    bool m_connectionDesired{};
    bool m_reconnectExhausted{};
    std::jthread m_reconnectThread;
};

} // namespace DeviceLink::Application
