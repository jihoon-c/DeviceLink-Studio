#pragma once

#include "DeviceLink/Application/ScenarioRunner.h"

#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

namespace DeviceLink::Application
{
class ScenarioProgressQueue final
{
public:
    using WakeupHandler = std::function<void()>;

    void SetWakeupHandler(WakeupHandler wakeupHandler);
    void Push(ScenarioProgress progress);
    [[nodiscard]] std::optional<ScenarioProgress> TryPop();
    [[nodiscard]] std::vector<ScenarioProgress> Drain();
    [[nodiscard]] std::size_t Size() const noexcept;

private:
    mutable std::mutex m_mutex;
    WakeupHandler m_wakeupHandler;
    std::deque<ScenarioProgress> m_progressEvents;
};
} // namespace DeviceLink::Application
