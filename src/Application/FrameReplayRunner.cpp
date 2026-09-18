#include "DeviceLink/Application/FrameReplayRunner.h"
#include "DeviceLink/Application/SqliteFrameReplaySource.h"

#include <utility>

namespace DeviceLink::Application
{
FrameReplayRunner::FrameReplayRunner(DeviceManager& deviceManager)
    : m_scenarioRunner(deviceManager)
{
}

std::optional<std::future<ScenarioResult>> FrameReplayRunner::Start(
    std::string deviceId,
    std::vector<ReplayFrame> frames)
{
    if (deviceId.empty())
    {
        return std::nullopt;
    }

    std::vector<ScenarioStep> steps;
    steps.reserve(frames.size());
    for (auto& replayFrame : frames)
    {
        steps.push_back({
            .delayBeforeAction = replayFrame.delayBeforeSend,
            .action = SendFrameScenarioAction{
                deviceId,
                std::move(replayFrame.frame),
                false,
            },
        });
    }
    return m_scenarioRunner.Start(std::move(steps));
}

std::optional<std::future<ScenarioResult>> FrameReplayRunner::StartFromStoredFrames(
    std::string deviceId,
    const SqliteFrameReplaySource& source)
{
    auto frames = source.Load(deviceId);
    if (frames.empty())
    {
        return std::nullopt;
    }
    return Start(std::move(deviceId), std::move(frames));
}

void FrameReplayRunner::Stop() noexcept
{
    m_scenarioRunner.Stop();
}

bool FrameReplayRunner::IsRunning() const noexcept
{
    return m_scenarioRunner.IsRunning();
}

void FrameReplayRunner::SetProgressObserver(ScenarioRunner::ProgressObserver observer)
{
    m_scenarioRunner.SetProgressObserver(std::move(observer));
}
} // namespace DeviceLink::Application
