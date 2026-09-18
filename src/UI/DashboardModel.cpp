#include "DeviceLink/UI/DashboardModel.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace DeviceLink::UI
{

DashboardModel::DashboardModel(std::size_t historyLimit)
    : m_historyLimit((std::max)(std::size_t{1}, historyLimit))
{
}

void DashboardModel::ApplyEvent(Application::DeviceEvent event)
{
    std::visit([this, &event](auto&& payload) {
        using Payload = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<Payload, Application::ConnectionStateChanged>)
        {
            SetDeviceState(event.deviceId, payload.state);
        }
        else if constexpr (std::is_same_v<Payload, Application::FrameReceived>)
        {
            m_packets.push_back({
                .deviceId = std::move(event.deviceId),
                .messageType = payload.frame.header.messageType,
                .sequence = payload.frame.header.sequence,
                .payloadSize = payload.frame.payload.size(),
                .payload = std::move(payload.frame.payload),
                .direction = PacketDirection::Received,
            });
            TrimHistory();
        }
        else if constexpr (std::is_same_v<Payload, Application::FrameSent>)
        {
            m_packets.push_back({
                .deviceId = std::move(event.deviceId),
                .messageType = payload.frame.header.messageType,
                .sequence = payload.frame.header.sequence,
                .payloadSize = payload.frame.payload.size(),
                .payload = std::move(payload.frame.payload),
                .direction = PacketDirection::Sent,
            });
            TrimHistory();
        }
        else if constexpr (std::is_same_v<Payload, Application::TransportError>)
        {
            m_errors.push_back({
                .deviceId = std::move(event.deviceId),
                .message = std::move(payload.message),
            });
            TrimHistory();
        }
        else
        {
            // Heartbeat state is queried from DeviceRuntime by the concrete UI.
        }
    }, std::move(event.payload));
}

void DashboardModel::ClearHistory() noexcept
{
    m_packets.clear();
    m_errors.clear();
}

std::span<const DeviceSummary> DashboardModel::Devices() const noexcept
{
    return m_devices;
}

std::span<const PacketMonitorEntry> DashboardModel::Packets() const noexcept
{
    return m_packets;
}

std::span<const ErrorEntry> DashboardModel::Errors() const noexcept
{
    return m_errors;
}

void DashboardModel::SetDeviceState(const std::string& deviceId, Core::ConnectionState state)
{
    const auto found = std::find_if(m_devices.begin(), m_devices.end(), [&deviceId](
        const DeviceSummary& device) { return device.deviceId == deviceId; });
    if (found == m_devices.end())
    {
        m_devices.push_back({deviceId, state});
        return;
    }
    found->state = state;
}

void DashboardModel::TrimHistory() noexcept
{
    if (m_packets.size() > m_historyLimit)
    {
        m_packets.erase(m_packets.begin(), m_packets.end() - m_historyLimit);
    }
    if (m_errors.size() > m_historyLimit)
    {
        m_errors.erase(m_errors.begin(), m_errors.end() - m_historyLimit);
    }
}

} // namespace DeviceLink::UI
