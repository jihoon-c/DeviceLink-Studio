#include "DeviceLink/Infrastructure/AsyncEventStore.h"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace DeviceLink::Infrastructure
{
AsyncEventStore::AsyncEventStore(std::unique_ptr<IEventRepository> repository)
    : m_repository(std::move(repository))
{
    if (!m_repository)
    {
        throw std::invalid_argument("AsyncEventStore requires a repository");
    }
    m_worker = std::jthread([this] { Run(); });
}

AsyncEventStore::~AsyncEventStore()
{
    Shutdown();
}

void AsyncEventStore::Append(EventLogEntry entry)
{
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_accepting)
        {
            ++m_droppedEntries;
            return;
        }
        m_entries.push_back(std::move(entry));
    }
    m_workAvailable.notify_one();
}

void AsyncEventStore::PruneBefore(std::int64_t timestampUnixMilliseconds)
{
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_accepting)
        {
            ++m_droppedEntries;
            return;
        }
        m_entries.push_back(PruneRequest{timestampUnixMilliseconds});
    }
    m_workAvailable.notify_one();
}

void AsyncEventStore::Flush()
{
    if (m_worker.get_id() == std::this_thread::get_id())
    {
        return;
    }

    std::unique_lock lock(m_mutex);
    m_idle.wait(lock, [this] { return m_entries.empty() && m_activeWrites == 0; });
}

void AsyncEventStore::Shutdown() noexcept
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

std::size_t AsyncEventStore::DroppedEntryCount() const noexcept
{
    return m_droppedEntries.load();
}

std::size_t AsyncEventStore::FailedEntryCount() const noexcept
{
    return m_failedEntries.load();
}

void AsyncEventStore::Run()
{
    for (;;)
    {
        StoreOperation operation;
        {
            std::unique_lock lock(m_mutex);
            m_workAvailable.wait(lock, [this] { return !m_entries.empty() || !m_accepting; });
            if (m_entries.empty())
            {
                return;
            }

            operation = std::move(m_entries.front());
            m_entries.pop_front();
            ++m_activeWrites;
        }

        try
        {
            std::visit(
                [this](const auto& item) {
                    using Item = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<Item, EventLogEntry>)
                    {
                        m_repository->Append(item);
                    }
                    else
                    {
                        static_cast<void>(m_repository->PruneBefore(item.timestampUnixMilliseconds));
                    }
                },
                operation);
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
