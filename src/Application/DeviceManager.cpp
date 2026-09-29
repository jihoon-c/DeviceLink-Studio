#include "DeviceLink/Application/DeviceManager.h"

#include <mutex>
#include <utility>
#include <vector>

namespace DeviceLink::Application
{

DeviceEventSubscription::DeviceEventSubscription(
    DeviceManager& manager, const std::uint64_t observerId) noexcept
    : m_manager(&manager), m_observerId(observerId)
{
}

DeviceEventSubscription::~DeviceEventSubscription()
{
    Reset();
}

DeviceEventSubscription::DeviceEventSubscription(DeviceEventSubscription&& other) noexcept
    : m_manager(std::exchange(other.m_manager, nullptr)),
      m_observerId(std::exchange(other.m_observerId, 0))
{
}

DeviceEventSubscription& DeviceEventSubscription::operator=(
    DeviceEventSubscription&& other) noexcept
{
    if (this != &other)
    {
        Reset();
        m_manager = std::exchange(other.m_manager, nullptr);
        m_observerId = std::exchange(other.m_observerId, 0);
    }
    return *this;
}

void DeviceEventSubscription::Reset() noexcept
{
    if (m_manager != nullptr)
    {
        m_manager->RemoveEventObserver(m_observerId);
        m_manager = nullptr;
        m_observerId = 0;
    }
}

bool DeviceManager::RegisterDevice(std::string deviceId)
{
    if (deviceId.empty())
    {
        return false;
    }

    std::unique_lock lock(m_mutex);
    return m_devices.try_emplace(std::move(deviceId)).second;
}

bool DeviceManager::RemoveDevice(const std::string& deviceId)
{
    std::unique_lock lock(m_mutex);
    m_sessions.erase(deviceId);
    return m_devices.erase(deviceId) != 0;
}

bool DeviceManager::ApplyConnectionEvent(
    const std::string& deviceId, Core::ConnectionEvent event)
{
    std::unique_lock lock(m_mutex);
    const auto device = m_devices.find(deviceId);
    return device != m_devices.end() && device->second.Apply(event);
}

bool DeviceManager::AttachTransport(
    const std::string& deviceId, std::unique_ptr<Transport::ITransport> transport)
{
    if (!transport)
    {
        return false;
    }

    std::unique_lock lock(m_mutex);
    if (!m_devices.contains(deviceId) || m_sessions.contains(deviceId))
    {
        return false;
    }
    m_sessions.emplace(deviceId, std::make_shared<Core::DeviceSession>(
        std::move(transport),
        [this, deviceId](Core::ConnectionState state) {
            PublishEvent({deviceId, ConnectionStateChanged{state}});
        },
        [this, deviceId](Protocol::PacketFrame frame) {
            PublishEvent({deviceId, FrameReceived{std::move(frame)}});
        },
        [this, deviceId](std::string error) {
            PublishEvent({deviceId, TransportError{std::move(error)}});
        }));
    return true;
}

bool DeviceManager::ConnectDevice(
    const std::string& deviceId, const char* host, std::uint16_t port)
{
    std::shared_ptr<Core::DeviceSession> session;
    {
        std::shared_lock lock(m_mutex);
        const auto item = m_sessions.find(deviceId);
        if (item == m_sessions.end())
        {
            return false;
        }
        session = item->second;
    }
    return session->Connect(host, port);
}

bool DeviceManager::DisconnectDevice(const std::string& deviceId) noexcept
{
    std::shared_ptr<Core::DeviceSession> session;
    {
        std::shared_lock lock(m_mutex);
        const auto item = m_sessions.find(deviceId);
        if (item == m_sessions.end())
        {
            return false;
        }
        session = item->second;
    }
    session->Disconnect();
    return true;
}

bool DeviceManager::SendFrame(
    const std::string& deviceId, const Protocol::PacketFrame& frame, bool recordForReplay)
{
    std::shared_ptr<Core::DeviceSession> session;
    {
        std::shared_lock lock(m_mutex);
        const auto item = m_sessions.find(deviceId);
        if (item == m_sessions.end())
        {
            return false;
        }
        session = item->second;
    }
    if (!session->SendFrame(frame))
    {
        return false;
    }
    PublishEvent({deviceId, FrameSent{frame, recordForReplay}});
    return true;
}

std::optional<Core::ConnectionState> DeviceManager::GetConnectionState(
    const std::string& deviceId) const
{
    std::shared_ptr<Core::DeviceSession> session;
    std::shared_lock lock(m_mutex);
    const auto device = m_devices.find(deviceId);
    if (device == m_devices.end())
    {
        return std::nullopt;
    }
    const Core::ConnectionState registeredState = device->second.State();
    const auto sessionItem = m_sessions.find(deviceId);
    if (sessionItem != m_sessions.end())
    {
        session = sessionItem->second;
    }
    lock.unlock();
    return session ? session->State() : registeredState;
}

std::size_t DeviceManager::DeviceCount() const noexcept
{
    std::shared_lock lock(m_mutex);
    return m_devices.size();
}

void DeviceManager::SetEventObserver(EventObserver eventObserver)
{
    std::unique_lock lock(m_mutex);
    if (eventObserver)
    {
        m_eventObservers[0] = std::move(eventObserver);
    }
    else
    {
        m_eventObservers.erase(0);
    }
}

DeviceEventSubscription DeviceManager::SubscribeToEvents(EventObserver eventObserver)
{
    if (!eventObserver)
    {
        return {};
    }
    std::unique_lock lock(m_mutex);
    const auto observerId = m_nextObserverId++;
    m_eventObservers.emplace(observerId, std::move(eventObserver));
    return DeviceEventSubscription(*this, observerId);
}

void DeviceManager::RemoveEventObserver(const std::uint64_t observerId) noexcept
{
    std::unique_lock lock(m_mutex);
    m_eventObservers.erase(observerId);
}

void DeviceManager::PublishEvent(DeviceEvent event)
{
    std::vector<EventObserver> eventObservers;
    {
        std::shared_lock lock(m_mutex);
        eventObservers.reserve(m_eventObservers.size());
        for (const auto& [observerId, observer] : m_eventObservers)
        {
            static_cast<void>(observerId);
            eventObservers.push_back(observer);
        }
    }

    m_events.Push(event);
    for (const auto& eventObserver : eventObservers)
    {
        eventObserver(event);
    }
}

DeviceEventQueue& DeviceManager::Events() noexcept
{
    return m_events;
}

const DeviceEventQueue& DeviceManager::Events() const noexcept
{
    return m_events;
}

} // namespace DeviceLink::Application
