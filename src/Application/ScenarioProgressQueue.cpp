#include "DeviceLink/Application/ScenarioProgressQueue.h"

#include <utility>

namespace DeviceLink::Application
{
void ScenarioProgressQueue::SetWakeupHandler(WakeupHandler wakeupHandler)
{
    const std::scoped_lock lock(m_mutex);
    m_wakeupHandler = std::move(wakeupHandler);
}

void ScenarioProgressQueue::Push(ScenarioProgress progress)
{
    WakeupHandler wakeupHandler;
    {
        const std::scoped_lock lock(m_mutex);
        m_progressEvents.push_back(std::move(progress));
        wakeupHandler = m_wakeupHandler;
    }
    if (wakeupHandler)
    {
        wakeupHandler();
    }
}

std::optional<ScenarioProgress> ScenarioProgressQueue::TryPop()
{
    const std::scoped_lock lock(m_mutex);
    if (m_progressEvents.empty())
    {
        return std::nullopt;
    }
    ScenarioProgress progress = std::move(m_progressEvents.front());
    m_progressEvents.pop_front();
    return progress;
}

std::vector<ScenarioProgress> ScenarioProgressQueue::Drain()
{
    const std::scoped_lock lock(m_mutex);
    std::vector<ScenarioProgress> events;
    events.reserve(m_progressEvents.size());
    while (!m_progressEvents.empty())
    {
        events.push_back(std::move(m_progressEvents.front()));
        m_progressEvents.pop_front();
    }
    return events;
}

std::size_t ScenarioProgressQueue::Size() const noexcept
{
    const std::scoped_lock lock(m_mutex);
    return m_progressEvents.size();
}
} // namespace DeviceLink::Application
