#pragma once

#include "DeviceLink/Infrastructure/EventRepository.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <variant>

namespace DeviceLink::Infrastructure
{
class AsyncEventStore final
{
public:
    explicit AsyncEventStore(std::unique_ptr<IEventRepository> repository);
    ~AsyncEventStore();

    AsyncEventStore(const AsyncEventStore&) = delete;
    AsyncEventStore& operator=(const AsyncEventStore&) = delete;

    void Append(EventLogEntry entry);
    void PruneBefore(std::int64_t timestampUnixMilliseconds);
    void Flush();
    void Shutdown() noexcept;

    [[nodiscard]] std::size_t DroppedEntryCount() const noexcept;
    [[nodiscard]] std::size_t FailedEntryCount() const noexcept;

private:
    struct PruneRequest final
    {
        std::int64_t timestampUnixMilliseconds{};
    };

    using StoreOperation = std::variant<EventLogEntry, PruneRequest>;

    void Run();

    std::unique_ptr<IEventRepository> m_repository;
    std::mutex m_mutex;
    std::condition_variable m_workAvailable;
    std::condition_variable m_idle;
    std::deque<StoreOperation> m_entries;
    std::jthread m_worker;
    bool m_accepting{true};
    std::size_t m_activeWrites{};
    std::atomic_size_t m_droppedEntries{};
    std::atomic_size_t m_failedEntries{};
};
} // namespace DeviceLink::Infrastructure
