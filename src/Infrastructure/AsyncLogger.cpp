#include "DeviceLink/Infrastructure/AsyncLogger.h"

#include <stdexcept>
#include <utility>

namespace DeviceLink::Infrastructure
{
AsyncLogger::AsyncLogger(std::unique_ptr<ILogger> sink)
    : m_sink(std::move(sink))
{
    if (!m_sink)
    {
        throw std::invalid_argument("AsyncLogger requires a log sink");
    }

    m_worker = std::jthread([this] { Run(); });
}

AsyncLogger::~AsyncLogger()
{
    Shutdown();
}

void AsyncLogger::Log(LogLevel level, std::string_view message)
{
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_accepting)
        {
            ++m_droppedEntries;
            return;
        }

        m_entries.push_back(Entry{level, std::string(message)});
    }

    m_workAvailable.notify_one();
}

void AsyncLogger::Flush()
{
    if (m_worker.get_id() == std::this_thread::get_id())
    {
        return;
    }

    std::unique_lock lock(m_mutex);
    m_idle.wait(lock, [this] { return m_entries.empty() && m_activeWrites == 0; });
}

void AsyncLogger::Shutdown() noexcept
{
    {
        const std::scoped_lock lock(m_mutex);
        m_accepting = false;
    }

    m_workAvailable.notify_all();

    if (m_worker.joinable() && m_worker.get_id() != std::this_thread::get_id())
    {
        m_worker.join();
    }
}

std::size_t AsyncLogger::DroppedEntryCount() const noexcept
{
    return m_droppedEntries.load();
}

std::size_t AsyncLogger::FailedEntryCount() const noexcept
{
    return m_failedEntries.load();
}

void AsyncLogger::Run()
{
    for (;;)
    {
        Entry entry;
        {
            std::unique_lock lock(m_mutex);
            m_workAvailable.wait(lock, [this] { return !m_entries.empty() || !m_accepting; });

            if (m_entries.empty())
            {
                return;
            }

            entry = std::move(m_entries.front());
            m_entries.pop_front();
            ++m_activeWrites;
        }

        try
        {
            m_sink->Log(entry.level, entry.message);
        }
        catch (...)
        {
            ++m_failedEntries;
        }

        {
            const std::scoped_lock lock(m_mutex);
            --m_activeWrites;
            if (m_entries.empty() && m_activeWrites == 0)
            {
                m_idle.notify_all();
            }
        }
    }
}
} // namespace DeviceLink::Infrastructure
