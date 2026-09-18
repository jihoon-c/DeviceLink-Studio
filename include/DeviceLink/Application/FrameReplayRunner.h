#pragma once

#include "DeviceLink/Application/ScenarioRunner.h"

#include <chrono>
#include <future>
#include <optional>
#include <string>
#include <vector>

namespace DeviceLink::Application
{
class SqliteFrameReplaySource;

struct ReplayFrame final
{
    std::chrono::milliseconds delayBeforeSend{};
    Protocol::PacketFrame frame;
};

class FrameReplayRunner final
{
public:
    explicit FrameReplayRunner(DeviceManager& deviceManager);

    FrameReplayRunner(const FrameReplayRunner&) = delete;
    FrameReplayRunner& operator=(const FrameReplayRunner&) = delete;

    [[nodiscard]] std::optional<std::future<ScenarioResult>> Start(
        std::string deviceId,
        std::vector<ReplayFrame> frames);
    [[nodiscard]] std::optional<std::future<ScenarioResult>> StartFromStoredFrames(
        std::string deviceId,
        const SqliteFrameReplaySource& source);
    void Stop() noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;
    void SetProgressObserver(ScenarioRunner::ProgressObserver observer);

private:
    ScenarioRunner m_scenarioRunner;
};
} // namespace DeviceLink::Application
