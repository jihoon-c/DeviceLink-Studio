#include "DeviceLink/Application/DeviceAgentService.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <thread>

namespace DeviceLink::Application
{
namespace
{
constexpr std::uint16_t kSetPanTiltMessageType = 0x1003;
constexpr std::uint16_t kInjectFaultMessageType = 0x1007;

std::string Normalize(std::string_view value)
{
    std::string normalized;
    normalized.reserve(value.size());
    for (const char character : value)
    {
        if (character != '_' && character != '-' && character != ' ')
        {
            normalized.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(character))));
        }
    }
    return normalized;
}
} // namespace

DeviceAgentService::DeviceAgentService(DeviceManager& manager, std::string deviceId)
    : m_manager(manager),
      m_deviceId(std::move(deviceId)),
      m_gimbal(manager, m_deviceId, ReconnectPolicy{.enabled = false})
{
    m_manager.SetEventObserver([this](const DeviceEvent& event) { ApplyDeviceEvent(event); });
}

DeviceAgentService::~DeviceAgentService()
{
    m_manager.SetEventObserver({});
}

AgentOperationResult DeviceAgentService::ConnectDevice(
    std::string_view host, const std::uint16_t port)
{
    if (!m_gimbal.Connect(host, port))
    {
        return {false, "Failed to connect device '" + m_deviceId + "'"};
    }
    return {true, "Device '" + m_deviceId + "' connected to " + std::string(host) + ':' +
        std::to_string(port)};
}

AgentOperationResult DeviceAgentService::SetGimbalAngle(
    const double panDegrees, const double tiltDegrees,
    const std::chrono::milliseconds timeout, const double toleranceDegrees)
{
    if (timeout <= std::chrono::milliseconds::zero() || !std::isfinite(toleranceDegrees) ||
        toleranceDegrees < 0.0)
    {
        return {false, "Timeout and angle tolerance must be positive"};
    }

    std::uint64_t acknowledgementRevision{};
    {
        const std::scoped_lock lock(m_mutex);
        acknowledgementRevision = m_acknowledgementRevision;
    }
    if (!m_gimbal.SetPanTilt(panDegrees, tiltDegrees))
    {
        return {false, "Pan or tilt is outside the supported range"};
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    if (!WaitForAcknowledgement(kSetPanTiltMessageType, acknowledgementRevision,
            (std::min)(timeout, std::chrono::milliseconds(2000))))
    {
        return {false, "Set-angle command was not acknowledged"};
    }

    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        auto result = GetTelemetry((std::min)(remaining, std::chrono::milliseconds(750)));
        if (!result.succeeded || !result.telemetry)
        {
            continue;
        }
        const auto& telemetry = *result.telemetry;
        if (telemetry.fault != VirtualGimbalFault::None)
        {
            return {false, "Gimbal entered fault state while moving", telemetry};
        }
        if (std::abs(telemetry.panDegrees - panDegrees) <= toleranceDegrees &&
            std::abs(telemetry.tiltDegrees - tiltDegrees) <= toleranceDegrees)
        {
            return {true, "Gimbal reached the requested angle", telemetry};
        }
    }
    return {false, "Gimbal did not reach the requested angle before timeout"};
}

AgentOperationResult DeviceAgentService::GetTelemetry(const std::chrono::milliseconds timeout)
{
    if (timeout <= std::chrono::milliseconds::zero())
    {
        return {false, "Telemetry timeout must be positive"};
    }
    std::uint64_t telemetryRevision{};
    {
        const std::scoped_lock lock(m_mutex);
        telemetryRevision = m_telemetryRevision;
    }
    if (!m_gimbal.RequestStatus())
    {
        return {false, "Failed to send telemetry request"};
    }
    auto telemetry = WaitForTelemetry(telemetryRevision, timeout);
    if (!telemetry)
    {
        return {false, "Telemetry response timed out"};
    }
    return {true, "Telemetry received", telemetry};
}

AgentOperationResult DeviceAgentService::InjectFault(
    const VirtualGimbalFault fault, const std::chrono::milliseconds timeout)
{
    if (fault == VirtualGimbalFault::None)
    {
        return {false, "Use clear_fault to clear the active fault"};
    }
    return SetFault(fault, timeout);
}

AgentOperationResult DeviceAgentService::ClearFault(const std::chrono::milliseconds timeout)
{
    return SetFault(VirtualGimbalFault::None, timeout);
}

AgentOperationResult DeviceAgentService::RunTestScenario(
    const std::string_view scenarioName, const std::chrono::milliseconds timeout)
{
    if (Normalize(scenarioName) != "motorstallrecovery")
    {
        return {false, "Unknown scenario. Supported scenario: motor_stall_recovery"};
    }

    AgentOperationResult result{};
    result.steps.push_back("Inject MotorStall fault");
    const auto injected = InjectFault(VirtualGimbalFault::MotorStall, timeout);
    if (!injected.succeeded)
    {
        result.message = "Scenario failed while injecting MotorStall: " + injected.message;
        result.telemetry = injected.telemetry;
        return result;
    }
    result.steps.push_back("Observed Fault state with MotorStall");
    result.steps.push_back("Clear active fault");
    const auto cleared = ClearFault(timeout);
    result.telemetry = cleared.telemetry;
    if (!cleared.succeeded)
    {
        result.message = "Scenario failed while clearing MotorStall: " + cleared.message;
        return result;
    }
    result.steps.push_back("Observed Ready state with no active fault");
    result.succeeded = true;
    result.message = "MotorStall recovery scenario passed";
    return result;
}

std::optional<VirtualGimbalFault> DeviceAgentService::ParseFault(
    const std::string_view value) noexcept
{
    const std::string normalized = Normalize(value);
    if (normalized == "none") return VirtualGimbalFault::None;
    if (normalized == "motorstall") return VirtualGimbalFault::MotorStall;
    if (normalized == "overtemperature") return VirtualGimbalFault::OverTemperature;
    if (normalized == "sensorfailure") return VirtualGimbalFault::SensorFailure;
    return std::nullopt;
}

std::string_view DeviceAgentService::FaultName(const VirtualGimbalFault fault) noexcept
{
    switch (fault)
    {
    case VirtualGimbalFault::None: return "None";
    case VirtualGimbalFault::MotorStall: return "MotorStall";
    case VirtualGimbalFault::OverTemperature: return "OverTemperature";
    case VirtualGimbalFault::SensorFailure: return "SensorFailure";
    }
    return "Unknown";
}

std::string_view DeviceAgentService::StateName(const VirtualGimbalState state) noexcept
{
    switch (state)
    {
    case VirtualGimbalState::Offline: return "Offline";
    case VirtualGimbalState::Ready: return "Ready";
    case VirtualGimbalState::Initializing: return "Initializing";
    case VirtualGimbalState::Scanning: return "Scanning";
    case VirtualGimbalState::Fault: return "Fault";
    }
    return "Unknown";
}

std::string_view DeviceAgentService::ConnectionStateName(const Core::ConnectionState state) noexcept
{
    switch (state)
    {
    case Core::ConnectionState::Disconnected: return "Disconnected";
    case Core::ConnectionState::Connecting: return "Connecting";
    case Core::ConnectionState::Connected: return "Connected";
    case Core::ConnectionState::Disconnecting: return "Disconnecting";
    case Core::ConnectionState::Faulted: return "Faulted";
    }
    return "Unknown";
}

void DeviceAgentService::ApplyDeviceEvent(const DeviceEvent& event)
{
    if (event.deviceId != m_deviceId)
    {
        return;
    }
    const auto* received = std::get_if<FrameReceived>(&event.payload);
    if (!received)
    {
        return;
    }

    bool changed{};
    const auto& frame = received->frame;
    if (const auto acknowledgement = m_gimbal.DecodeDeviceAcknowledgement(
            frame.header.messageType, frame.payload))
    {
        const std::scoped_lock lock(m_mutex);
        m_lastAcknowledgement = acknowledgement;
        ++m_acknowledgementRevision;
        changed = true;
    }
    if (const auto telemetry = m_gimbal.DecodeDeviceTelemetry(
            frame.header.messageType, frame.payload))
    {
        const std::scoped_lock lock(m_mutex);
        m_lastTelemetry = telemetry;
        ++m_telemetryRevision;
        changed = true;
    }
    if (changed)
    {
        m_condition.notify_all();
    }
}

bool DeviceAgentService::WaitForAcknowledgement(
    const std::uint16_t commandType, const std::uint64_t previousRevision,
    const std::chrono::milliseconds timeout)
{
    std::unique_lock lock(m_mutex);
    const bool received = m_condition.wait_for(lock, timeout, [&] {
        return m_acknowledgementRevision > previousRevision && m_lastAcknowledgement &&
            m_lastAcknowledgement->commandType == commandType;
    });
    return received && m_lastAcknowledgement->succeeded;
}

std::optional<VirtualGimbalTelemetry> DeviceAgentService::WaitForTelemetry(
    const std::uint64_t previousRevision, const std::chrono::milliseconds timeout)
{
    std::unique_lock lock(m_mutex);
    if (!m_condition.wait_for(lock, timeout,
            [&] { return m_telemetryRevision > previousRevision && m_lastTelemetry.has_value(); }))
    {
        return std::nullopt;
    }
    return m_lastTelemetry;
}

AgentOperationResult DeviceAgentService::SetFault(
    const VirtualGimbalFault fault, const std::chrono::milliseconds timeout)
{
    if (timeout <= std::chrono::milliseconds::zero())
    {
        return {false, "Fault operation timeout must be positive"};
    }
    std::uint64_t acknowledgementRevision{};
    {
        const std::scoped_lock lock(m_mutex);
        acknowledgementRevision = m_acknowledgementRevision;
    }
    if (!m_gimbal.InjectFault(fault))
    {
        return {false, "Failed to send fault command"};
    }
    if (!WaitForAcknowledgement(kInjectFaultMessageType, acknowledgementRevision,
            (std::min)(timeout, std::chrono::milliseconds(2000))))
    {
        return {false, "Fault command was not acknowledged"};
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        auto telemetry = GetTelemetry((std::min)(remaining, std::chrono::milliseconds(750)));
        if (!telemetry.succeeded || !telemetry.telemetry)
        {
            continue;
        }
        const bool expectedState = fault == VirtualGimbalFault::None
            ? telemetry.telemetry->state == VirtualGimbalState::Ready
            : telemetry.telemetry->state == VirtualGimbalState::Fault;
        if (telemetry.telemetry->fault == fault && expectedState)
        {
            telemetry.message = fault == VirtualGimbalFault::None
                ? "Fault cleared and device returned to Ready"
                : "Fault injected and observed in telemetry";
            return telemetry;
        }
    }
    return {false, "Telemetry did not confirm the requested fault state"};
}

} // namespace DeviceLink::Application
