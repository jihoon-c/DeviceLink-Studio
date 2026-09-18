#include "DeviceLink/Application/ScenarioRunner.h"

#include "DeviceLink/Application/DeviceManager.h"

#include <condition_variable>
#include <type_traits>
#include <utility>

namespace DeviceLink::Application
{
namespace
{
[[nodiscard]] std::string DescribeFailure(const ScenarioAction& action)
{
    return std::visit(
        [](const auto& scenarioAction) {
            using Action = std::decay_t<decltype(scenarioAction)>;
            if constexpr (std::is_same_v<Action, ConnectScenarioAction>)
            {
                return "Connect failed for device '" + scenarioAction.deviceId + "'";
            }
            else if constexpr (std::is_same_v<Action, SendFrameScenarioAction>)
            {
                return "Send failed for device '" + scenarioAction.deviceId + "'";
            }
            else
            {
                return "Disconnect failed for device '" + scenarioAction.deviceId + "'";
            }
        },
        action);
}
} // namespace

ScenarioRunner::ScenarioRunner(DeviceManager& deviceManager)
    : m_deviceManager(deviceManager)
{
}

ScenarioRunner::~ScenarioRunner()
{
    Stop();
}

std::optional<std::future<ScenarioResult>> ScenarioRunner::Start(
    std::vector<ScenarioStep> steps)
{
    if (steps.empty())
    {
        return std::nullopt;
    }

    std::promise<ScenarioResult> completion;
    auto result = completion.get_future();

    const std::scoped_lock lock(m_mutex);
    if (m_running)
    {
        return std::nullopt;
    }

    if (m_worker.joinable())
    {
        m_worker.join();
    }
    m_running = true;
    m_worker = std::jthread(
        [this, steps = std::move(steps), completion = std::move(completion)](
            std::stop_token stopToken) mutable {
            Run(stopToken, std::move(steps), std::move(completion));
        });
    return result;
}

void ScenarioRunner::SetProgressObserver(ProgressObserver observer)
{
    const std::scoped_lock lock(m_mutex);
    m_progressObserver = std::move(observer);
}

void ScenarioRunner::Stop() noexcept
{
    std::jthread worker;
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_worker.joinable())
        {
            return;
        }
        worker = std::move(m_worker);
    }

    worker.request_stop();
    worker.join();
}

bool ScenarioRunner::IsRunning() const noexcept
{
    const std::scoped_lock lock(m_mutex);
    return m_running;
}

bool ScenarioRunner::Execute(const ScenarioAction& action)
{
    return std::visit(
        [this](const auto& scenarioAction) {
            using Action = std::decay_t<decltype(scenarioAction)>;
            if constexpr (std::is_same_v<Action, ConnectScenarioAction>)
            {
                return m_deviceManager.ConnectDevice(
                    scenarioAction.deviceId, scenarioAction.host.c_str(), scenarioAction.port);
            }
            else if constexpr (std::is_same_v<Action, SendFrameScenarioAction>)
            {
                return m_deviceManager.SendFrame(
                    scenarioAction.deviceId, scenarioAction.frame, scenarioAction.recordForReplay);
            }
            else
            {
                return m_deviceManager.DisconnectDevice(scenarioAction.deviceId);
            }
        },
        action);
}

void ScenarioRunner::PublishProgress(ScenarioProgress progress) noexcept
{
    try
    {
        ProgressObserver observer;
        {
            const std::scoped_lock lock(m_mutex);
            observer = m_progressObserver;
        }
        if (observer)
        {
            observer(progress);
        }
    }
    catch (...)
    {
        // UI-facing observers must not be able to terminate the scenario worker.
    }
}

void ScenarioRunner::Run(
    std::stop_token stopToken,
    std::vector<ScenarioStep> steps,
    std::promise<ScenarioResult> completion)
{
    ScenarioResult result{.status = ScenarioStatus::Completed};
    std::condition_variable_any cancellationWait;
    std::mutex cancellationMutex;
    PublishProgress({
        .kind = ScenarioProgressKind::Started,
        .completedStepCount = 0,
        .totalStepCount = steps.size(),
    });

    for (std::size_t stepIndex{}; stepIndex < steps.size(); ++stepIndex)
    {
        const auto& step = steps[stepIndex];
        if (step.delayBeforeAction > std::chrono::milliseconds::zero())
        {
            std::unique_lock lock(cancellationMutex);
            cancellationWait.wait_for(lock, stopToken, step.delayBeforeAction, [] { return false; });
        }

        if (stopToken.stop_requested())
        {
            result.status = ScenarioStatus::Cancelled;
            break;
        }

        if (!Execute(step.action))
        {
            result.status = ScenarioStatus::Failed;
            result.failedStepIndex = stepIndex;
            result.failureMessage = DescribeFailure(step.action);
            PublishProgress({
                .kind = ScenarioProgressKind::Finished,
                .status = result.status,
                .completedStepCount = result.completedStepCount,
                .totalStepCount = steps.size(),
                .failedStepIndex = stepIndex,
                .failureMessage = result.failureMessage,
            });
            break;
        }

        ++result.completedStepCount;
        PublishProgress({
            .kind = ScenarioProgressKind::StepCompleted,
            .completedStepCount = result.completedStepCount,
            .totalStepCount = steps.size(),
        });
    }

    if (result.status != ScenarioStatus::Failed)
    {
        PublishProgress({
            .kind = ScenarioProgressKind::Finished,
            .status = result.status,
            .completedStepCount = result.completedStepCount,
            .totalStepCount = steps.size(),
        });
    }

    {
        const std::scoped_lock lock(m_mutex);
        m_running = false;
    }
    completion.set_value(result);
}
} // namespace DeviceLink::Application
