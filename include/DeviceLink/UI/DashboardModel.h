#pragma once

#include "DeviceLink/Application/DeviceEventQueue.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace DeviceLink::UI
{

enum class PacketDirection
{
    Received,
    Sent,
};

struct DeviceSummary final
{
    std::string deviceId;
    Core::ConnectionState state{Core::ConnectionState::Disconnected};
};

struct PacketMonitorEntry final
{
    std::string deviceId;
    std::uint16_t messageType{};
    std::uint32_t sequence{};
    std::size_t payloadSize{};
    std::vector<std::byte> payload;
    PacketDirection direction{PacketDirection::Received};
};

struct ErrorEntry final
{
    std::string deviceId;
    std::string message;
};

class DashboardModel final
{
public:
    explicit DashboardModel(std::size_t historyLimit = 500);

    void ApplyEvent(Application::DeviceEvent event);
    void ClearHistory() noexcept;

    [[nodiscard]] std::span<const DeviceSummary> Devices() const noexcept;
    [[nodiscard]] std::span<const PacketMonitorEntry> Packets() const noexcept;
    [[nodiscard]] std::span<const ErrorEntry> Errors() const noexcept;

private:
    void SetDeviceState(const std::string& deviceId, Core::ConnectionState state);
    void TrimHistory() noexcept;

    std::size_t m_historyLimit;
    std::vector<DeviceSummary> m_devices;
    std::vector<PacketMonitorEntry> m_packets;
    std::vector<ErrorEntry> m_errors;
};

} // namespace DeviceLink::UI
