#pragma once

#include "DeviceLink/Application/VirtualGimbalService.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace DeviceLink::Application
{

struct AgentOperationResult final
{
    bool succeeded{};
    std::string message;
    std::optional<VirtualGimbalTelemetry> telemetry;
    std::vector<std::string> steps;
};

class DeviceAgentService final
{
public:
    DeviceAgentService(DeviceManager& manager, std::string deviceId);
    ~DeviceAgentService();

    DeviceAgentService(const DeviceAgentService&) = delete;
    DeviceAgentService& operator=(const DeviceAgentService&) = delete;

    [[nodiscard]] AgentOperationResult ConnectDevice(
        std::string_view host, std::uint16_t port);
    [[nodiscard]] AgentOperationResult SetGimbalAngle(
        double panDegrees, double tiltDegrees,
        std::chrono::milliseconds timeout = std::chrono::seconds(5),
        double toleranceDegrees = 0.5);
    [[nodiscard]] AgentOperationResult GetTelemetry(
        std::chrono::milliseconds timeout = std::chrono::seconds(2));
    [[nodiscard]] AgentOperationResult InjectFault(
        VirtualGimbalFault fault,
        std::chrono::milliseconds timeout = std::chrono::seconds(3));
    [[nodiscard]] AgentOperationResult ClearFault(
        std::chrono::milliseconds timeout = std::chrono::seconds(3));
    [[nodiscard]] AgentOperationResult RunTestScenario(
        std::string_view scenarioName,
        std::chrono::milliseconds timeout = std::chrono::seconds(5));

    [[nodiscard]] static std::optional<VirtualGimbalFault> ParseFault(
        std::string_view value) noexcept;
    [[nodiscard]] static std::string_view FaultName(VirtualGimbalFault fault) noexcept;
    [[nodiscard]] static std::string_view StateName(VirtualGimbalState state) noexcept;
    [[nodiscard]] static std::string_view ConnectionStateName(Core::ConnectionState state) noexcept;

private:
    void ApplyDeviceEvent(const DeviceEvent& event);
    [[nodiscard]] bool WaitForAcknowledgement(
        std::uint16_t commandType, std::uint64_t previousRevision,
        std::chrono::milliseconds timeout);
    [[nodiscard]] std::optional<VirtualGimbalTelemetry> WaitForTelemetry(
        std::uint64_t previousRevision, std::chrono::milliseconds timeout);
    [[nodiscard]] AgentOperationResult SetFault(
        VirtualGimbalFault fault, std::chrono::milliseconds timeout);

    DeviceManager& m_manager;
    std::string m_deviceId;
    VirtualGimbalService m_gimbal;
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::optional<VirtualGimbalAcknowledgement> m_lastAcknowledgement;
    std::optional<VirtualGimbalTelemetry> m_lastTelemetry;
    std::uint64_t m_acknowledgementRevision{};
    std::uint64_t m_telemetryRevision{};
};

} // namespace DeviceLink::Application
