#include "DeviceLink/Application/DeviceEventQueue.h"

#include <utility>

namespace DeviceLink::Application
{

void DeviceEventQueue::SetWakeupHandler(WakeupHandler wakeupHandler)
{
    std::scoped_lock lock(m_mutex);
    m_wakeupHandler = std::move(wakeupHandler);
}

void DeviceEventQueue::Push(DeviceEvent event)
{
    WakeupHandler wakeupHandler;
    {
        std::scoped_lock lock(m_mutex);
        m_events.push_back(std::move(event));
        wakeupHandler = m_wakeupHandler;
    }
    if (wakeupHandler)
    {
        wakeupHandler();
    }
}

std::optional<DeviceEvent> DeviceEventQueue::TryPop()
{
    std::scoped_lock lock(m_mutex);
    if (m_events.empty())
    {
        return std::nullopt;
    }
    DeviceEvent event = std::move(m_events.front());
    m_events.pop_front();
    return event;
}

std::vector<DeviceEvent> DeviceEventQueue::Drain()
{
    std::scoped_lock lock(m_mutex);
    std::vector<DeviceEvent> events;
    events.reserve(m_events.size());
    while (!m_events.empty())
    {
        events.push_back(std::move(m_events.front()));
        m_events.pop_front();
    }
    return events;
}

std::size_t DeviceEventQueue::Size() const noexcept
{
    std::scoped_lock lock(m_mutex);
    return m_events.size();
}

} // namespace DeviceLink::Application
