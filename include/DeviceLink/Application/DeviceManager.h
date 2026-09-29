#pragma once

#include "DeviceLink/Application/DeviceEventQueue.h"
#include "DeviceLink/Core/ConnectionStateMachine.h"
#include "DeviceLink/Core/DeviceSession.h"

#include <optional>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace DeviceLink::Application
{

class DeviceManager;

class DeviceEventSubscription final
{
public:
    DeviceEventSubscription() = default;
    ~DeviceEventSubscription();
    DeviceEventSubscription(const DeviceEventSubscription&) = delete;
    DeviceEventSubscription& operator=(const DeviceEventSubscription&) = delete;
    DeviceEventSubscription(DeviceEventSubscription&& other) noexcept;
    DeviceEventSubscription& operator=(DeviceEventSubscription&& other) noexcept;
    void Reset() noexcept;

private:
    friend class DeviceManager;
    DeviceEventSubscription(DeviceManager& manager, std::uint64_t observerId) noexcept;
    DeviceManager* m_manager{};
    std::uint64_t m_observerId{};
};

class DeviceManager final
{
public:
    using EventObserver = std::function<void(const DeviceEvent&)>;

    [[nodiscard]] bool RegisterDevice(std::string deviceId);
    [[nodiscard]] bool RemoveDevice(const std::string& deviceId);
    [[nodiscard]] bool ApplyConnectionEvent(
        const std::string& deviceId, Core::ConnectionEvent event);
    [[nodiscard]] bool AttachTransport(
        const std::string& deviceId, std::unique_ptr<Transport::ITransport> transport);
    [[nodiscard]] bool ConnectDevice(
        const std::string& deviceId, const char* host, std::uint16_t port);
    [[nodiscard]] bool DisconnectDevice(const std::string& deviceId) noexcept;
    [[nodiscard]] bool SendFrame(
        const std::string& deviceId,
        const Protocol::PacketFrame& frame,
        bool recordForReplay = true);
    [[nodiscard]] std::optional<Core::ConnectionState> GetConnectionState(
        const std::string& deviceId) const;
    [[nodiscard]] std::size_t DeviceCount() const noexcept;
    [[nodiscard]] DeviceEventSubscription SubscribeToEvents(EventObserver eventObserver);
    void SetEventObserver(EventObserver eventObserver);
    [[nodiscard]] DeviceEventQueue& Events() noexcept;
    [[nodiscard]] const DeviceEventQueue& Events() const noexcept;

private:
    friend class DeviceEventSubscription;
    void RemoveEventObserver(std::uint64_t observerId) noexcept;
    void PublishEvent(DeviceEvent event);

    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, Core::ConnectionStateMachine> m_devices;
    DeviceEventQueue m_events;
    std::unordered_map<std::string, std::shared_ptr<Core::DeviceSession>> m_sessions;
    std::unordered_map<std::uint64_t, EventObserver> m_eventObservers;
    std::uint64_t m_nextObserverId{1};
};

} // namespace DeviceLink::Application
