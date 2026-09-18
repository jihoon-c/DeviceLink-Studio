#include "DeviceLink/Application/VirtualGimbalService.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace DeviceLink::Application
{
VirtualGimbalService::VirtualGimbalService(
    DeviceManager& manager, std::string deviceId, ReconnectPolicy reconnectPolicy,
    std::unique_ptr<IGimbalProtocolAdapter> protocolAdapter)
    : m_manager(manager),
      m_deviceId(std::move(deviceId)),
      m_protocolAdapter(protocolAdapter ? std::move(protocolAdapter)
                                        : std::make_unique<DeviceLinkGimbalProtocolAdapter>()),
      m_reconnectPolicy(reconnectPolicy)
{
    if (m_deviceId.empty())
    {
        throw std::invalid_argument("VirtualGimbalService requires a device identifier");
    }
    SetReconnectPolicy(reconnectPolicy);
    m_reconnectThread = std::jthread(
        [this](std::stop_token stopToken) { ReconnectLoop(stopToken); });
}

VirtualGimbalService::~VirtualGimbalService()
{
    {
        const std::scoped_lock lock(m_reconnectMutex);
        m_connectionDesired = false;
    }
    m_reconnectThread.request_stop();
    m_reconnectCondition.notify_all();
    if (m_reconnectThread.joinable())
    {
        m_reconnectThread.join();
    }
    static_cast<void>(m_manager.DisconnectDevice(m_deviceId));
}

bool VirtualGimbalService::Connect(std::string_view host, std::uint16_t port)
{
    if (host.empty() || port == 0)
    {
        return false;
    }
    const std::string nullTerminatedHost(host);
    {
        const std::scoped_lock lock(m_reconnectMutex);
        m_reconnectHost = nullTerminatedHost;
        m_reconnectPort = port;
        m_connectionDesired = true;
        m_reconnectAttemptCount = 1;
        m_reconnectExhausted = false;
    }
    const bool connected = m_manager.ConnectDevice(m_deviceId, nullTerminatedHost.c_str(), port);
    if (connected)
    {
        const std::scoped_lock lock(m_reconnectMutex);
        m_reconnectAttemptCount = 0;
    }
    else
    {
        const std::scoped_lock lock(m_reconnectMutex);
        if (m_reconnectAttemptCount >= m_reconnectPolicy.maximumAttempts)
        {
            m_reconnectExhausted = true;
            m_connectionDesired = false;
        }
    }
    m_reconnectCondition.notify_all();
    return connected;
}

bool VirtualGimbalService::Disconnect() noexcept
{
    {
        const std::scoped_lock lock(m_reconnectMutex);
        m_connectionDesired = false;
        m_reconnectAttemptCount = 0;
        m_reconnectExhausted = false;
    }
    m_reconnectCondition.notify_all();
    return m_manager.DisconnectDevice(m_deviceId);
}

bool VirtualGimbalService::Power(bool enabled)
{
    return Send({.kind = GimbalCommandKind::Power, .powerEnabled = enabled});
}

bool VirtualGimbalService::Initialize()
{
    return Send({.kind = GimbalCommandKind::Initialize});
}

bool VirtualGimbalService::SetPanTilt(double panDegrees, double tiltDegrees)
{
    if (!std::isfinite(panDegrees) || !std::isfinite(tiltDegrees) ||
        panDegrees < kMinimumPanDegrees || panDegrees > kMaximumPanDegrees ||
        tiltDegrees < kMinimumTiltDegrees || tiltDegrees > kMaximumTiltDegrees)
    {
        return false;
    }
    return Send({
        .kind = GimbalCommandKind::SetPanTilt,
        .panDegrees = panDegrees,
        .tiltDegrees = tiltDegrees,
    });
}

bool VirtualGimbalService::StartScan()
{
    return Send({.kind = GimbalCommandKind::StartScan});
}

bool VirtualGimbalService::StopScan()
{
    return Send({.kind = GimbalCommandKind::StopScan});
}

bool VirtualGimbalService::RequestStatus()
{
    return Send({.kind = GimbalCommandKind::RequestStatus});
}

bool VirtualGimbalService::InjectFault(VirtualGimbalFault fault)
{
    if (static_cast<std::uint8_t>(fault) >
        static_cast<std::uint8_t>(VirtualGimbalFault::SensorFailure))
    {
        return false;
    }
    return Send({.kind = GimbalCommandKind::InjectFault, .fault = fault});
}

bool VirtualGimbalService::ConfigureResponse(
    VirtualGimbalResponseMode mode, std::uint16_t delayMilliseconds)
{
    if (static_cast<std::uint8_t>(mode) >
            static_cast<std::uint8_t>(VirtualGimbalResponseMode::NoResponse) ||
        (mode == VirtualGimbalResponseMode::Delayed && delayMilliseconds == 0))
    {
        return false;
    }
    if (mode != VirtualGimbalResponseMode::Delayed)
    {
        delayMilliseconds = 0;
    }
    return Send({
        .kind = GimbalCommandKind::ConfigureResponse,
        .responseMode = mode,
        .responseDelayMilliseconds = delayMilliseconds,
    });
}

bool VirtualGimbalService::SelectEquipmentMode(const VirtualEquipmentMode mode)
{
    return Send({.kind = GimbalCommandKind::SelectEquipmentMode, .equipmentMode = mode});
}

bool VirtualGimbalService::DroneTakeOff() { return Send({.kind = GimbalCommandKind::DroneTakeOff}); }
bool VirtualGimbalService::DroneLand() { return Send({.kind = GimbalCommandKind::DroneLand}); }
bool VirtualGimbalService::DroneMoveTo(double xMeters, double yMeters, double altitudeMeters)
{
    return Send({.kind = GimbalCommandKind::DroneMoveTo, .droneXmeters = xMeters,
        .droneYmeters = yMeters, .droneAltitudeMeters = altitudeMeters});
}
bool VirtualGimbalService::DroneReturnHome() { return Send({.kind = GimbalCommandKind::DroneReturnHome}); }

std::optional<Core::ConnectionState> VirtualGimbalService::ConnectionState() const
{
    return m_manager.GetConnectionState(m_deviceId);
}

const std::string& VirtualGimbalService::DeviceId() const noexcept
{
    return m_deviceId;
}

void VirtualGimbalService::SetAutomaticReconnectEnabled(bool enabled) noexcept
{
    {
        const std::scoped_lock lock(m_reconnectMutex);
        m_reconnectPolicy.enabled = enabled;
        if (enabled)
        {
            m_reconnectExhausted = false;
        }
    }
    m_reconnectCondition.notify_all();
}

void VirtualGimbalService::SetReconnectPolicy(ReconnectPolicy policy) noexcept
{
    policy.retryInterval = (std::max)(policy.retryInterval, std::chrono::milliseconds{10});
    policy.maximumAttempts = (std::max)(policy.maximumAttempts, std::uint32_t{1});
    {
        const std::scoped_lock lock(m_reconnectMutex);
        m_reconnectPolicy = policy;
        m_reconnectExhausted = false;
    }
    m_reconnectCondition.notify_all();
}

ReconnectStatus VirtualGimbalService::GetReconnectStatus() const noexcept
{
    const std::scoped_lock lock(m_reconnectMutex);
    return {
        .enabled = m_reconnectPolicy.enabled,
        .connectionDesired = m_connectionDesired,
        .exhausted = m_reconnectExhausted,
        .attemptCount = m_reconnectAttemptCount,
        .maximumAttempts = m_reconnectPolicy.maximumAttempts,
    };
}

std::optional<VirtualGimbalAcknowledgement> VirtualGimbalService::DecodeAcknowledgement(
    std::uint16_t messageType, std::span<const std::byte> payload) noexcept
{
    return DeviceLinkGimbalProtocolAdapter{}.DecodeAcknowledgement(messageType, payload);
}

std::optional<VirtualGimbalTelemetry> VirtualGimbalService::DecodeTelemetry(
    std::uint16_t messageType, std::span<const std::byte> payload) noexcept
{
    return DeviceLinkGimbalProtocolAdapter{}.DecodeTelemetry(messageType, payload);
}

std::optional<VirtualGimbalAcknowledgement>
VirtualGimbalService::DecodeDeviceAcknowledgement(
    std::uint16_t messageType, std::span<const std::byte> payload) const noexcept
{
    return m_protocolAdapter->DecodeAcknowledgement(messageType, payload);
}

std::optional<VirtualGimbalTelemetry> VirtualGimbalService::DecodeDeviceTelemetry(
    std::uint16_t messageType, std::span<const std::byte> payload) const noexcept
{
    return m_protocolAdapter->DecodeTelemetry(messageType, payload);
}

std::uint16_t VirtualGimbalService::DeviceAcknowledgeMessageType() const noexcept
{
    return m_protocolAdapter->AcknowledgeMessageType();
}

std::uint16_t VirtualGimbalService::DeviceTelemetryMessageType() const noexcept
{
    return m_protocolAdapter->TelemetryMessageType();
}

bool VirtualGimbalService::Send(const GimbalCommand& command)
{
    const auto frame = m_protocolAdapter->EncodeCommand(
        command, m_nextSequence.fetch_add(1, std::memory_order_relaxed));
    return frame && m_manager.SendFrame(m_deviceId, *frame);
}

void VirtualGimbalService::ReconnectLoop(std::stop_token stopToken) noexcept
{
    while (!stopToken.stop_requested())
    {
        std::string host;
        std::uint16_t port{};
        ReconnectPolicy policy;
        {
            std::unique_lock lock(m_reconnectMutex);
            m_reconnectCondition.wait(lock, stopToken, [this] { return m_connectionDesired; });
            if (stopToken.stop_requested())
            {
                return;
            }
            policy = m_reconnectPolicy;
            m_reconnectCondition.wait_for(lock, stopToken, policy.retryInterval,
                [this] { return !m_connectionDesired; });
            if (stopToken.stop_requested())
            {
                return;
            }
            if (!m_connectionDesired)
            {
                continue;
            }
            policy = m_reconnectPolicy;
            if (!policy.enabled)
            {
                continue;
            }
            host = m_reconnectHost;
            port = m_reconnectPort;
        }

        const auto state = m_manager.GetConnectionState(m_deviceId);
        if (!state)
        {
            return;
        }
        if (*state == Core::ConnectionState::Connected)
        {
            const std::scoped_lock lock(m_reconnectMutex);
            m_reconnectAttemptCount = 0;
            m_reconnectExhausted = false;
            continue;
        }
        if (*state == Core::ConnectionState::Connecting ||
            *state == Core::ConnectionState::Disconnecting)
        {
            continue;
        }

        {
            const std::scoped_lock lock(m_reconnectMutex);
            if (m_reconnectAttemptCount >= m_reconnectPolicy.maximumAttempts)
            {
                m_reconnectExhausted = true;
                m_connectionDesired = false;
                continue;
            }
            ++m_reconnectAttemptCount;
        }
        if (*state == Core::ConnectionState::Faulted)
        {
            static_cast<void>(m_manager.DisconnectDevice(m_deviceId));
        }
        {
            const std::scoped_lock lock(m_reconnectMutex);
            if (!m_connectionDesired)
            {
                continue;
            }
        }
        if (m_manager.ConnectDevice(m_deviceId, host.c_str(), port))
        {
            const std::scoped_lock lock(m_reconnectMutex);
            m_reconnectAttemptCount = 0;
            m_reconnectExhausted = false;
        }
        else
        {
            const std::scoped_lock lock(m_reconnectMutex);
            if (m_reconnectAttemptCount >= m_reconnectPolicy.maximumAttempts)
            {
                m_reconnectExhausted = true;
                m_connectionDesired = false;
            }
        }
    }
}

} // namespace DeviceLink::Application
