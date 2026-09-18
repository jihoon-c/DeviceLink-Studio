#pragma once

#include "DeviceLink/Infrastructure/ILogger.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace DeviceLink::Infrastructure
{
class AsyncLogger final : public ILogger
{
public:
    explicit AsyncLogger(std::unique_ptr<ILogger> sink);
    ~AsyncLogger() override;

    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;

    void Log(LogLevel level, std::string_view message) override;
    void Flush();
    void Shutdown() noexcept;

    [[nodiscard]] std::size_t DroppedEntryCount() const noexcept;
    [[nodiscard]] std::size_t FailedEntryCount() const noexcept;

private:
    struct Entry
    {
        LogLevel level;
        std::string message;
    };

    void Run();

    std::unique_ptr<ILogger> m_sink;
    std::mutex m_mutex;
    std::condition_variable m_workAvailable;
    std::condition_variable m_idle;
    std::deque<Entry> m_entries;
    std::jthread m_worker;
    bool m_accepting{true};
    std::size_t m_activeWrites{};
    std::atomic_size_t m_droppedEntries{};
    std::atomic_size_t m_failedEntries{};
};
} // namespace DeviceLink::Infrastructure
