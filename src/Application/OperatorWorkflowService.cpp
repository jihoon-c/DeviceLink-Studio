#include "DeviceLink/Application/OperatorWorkflowService.h"

#include "DeviceLink/Application/SqliteFrameReplaySource.h"

#include <algorithm>
#include <exception>
#include <chrono>
#include <utility>

namespace DeviceLink::Application
{
OperatorWorkflowService::OperatorWorkflowService(
    DeviceManager& deviceManager,
    Infrastructure::ITextFileStore& fileStore,
    const SqliteFrameReplaySource& replaySource,
    PrepareReplay prepareReplay,
    TimestampProvider timestampProvider)
    : m_scenarioRunner(deviceManager),
      m_replayRunner(deviceManager),
      m_fileService(fileStore),
      m_reportService(fileStore),
      m_replaySource(replaySource),
      m_prepareReplay(std::move(prepareReplay)),
      m_timestampProvider(timestampProvider ? std::move(timestampProvider) : [] {
          return std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch()).count();
      })
{
    m_scenarioRunner.SetProgressObserver(
        [this](const ScenarioProgress& progress) { PublishScenarioProgress(progress); });
    m_replayRunner.SetProgressObserver(
        [this](const ScenarioProgress& progress) { m_progressQueue.Push(progress); });
}

OperatorWorkflowService::~OperatorWorkflowService()
{
    Stop();
    m_progressQueue.SetWakeupHandler({});
}

WorkflowStartResult OperatorWorkflowService::StartScenario(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return {.error = "Select a scenario file"};
    }
    if (IsRunning())
    {
        return {.error = "Another workflow is already running"};
    }
    auto load = m_fileService.Load(path);
    if (!load.Succeeded())
    {
        return {
            .error = std::move(load.error),
            .errorLineNumber = load.errorLineNumber,
        };
    }
    if (load.steps.empty())
    {
        return {.error = "Scenario contains no steps"};
    }
    {
        const std::scoped_lock lock(m_reportMutex);
        m_activeScenarioPath = path;
        m_activeScenarioSteps = load.steps;
        m_scenarioStartedUnixMilliseconds = m_timestampProvider();
        m_lastScenarioReport.reset();
    }
    auto completion = m_scenarioRunner.Start(std::move(load.steps));
    if (!completion)
    {
        const std::scoped_lock lock(m_reportMutex);
        m_activeScenarioPath.clear();
        m_activeScenarioSteps.clear();
        return {.error = "Scenario runner is unavailable"};
    }
    return {.succeeded = true};
}

WorkflowStartResult OperatorWorkflowService::StartReplay(const std::string& deviceId)
{
    if (deviceId.empty())
    {
        return {.error = "Select a device for replay"};
    }
    if (IsRunning())
    {
        return {.error = "Another workflow is already running"};
    }
    try
    {
        if (m_prepareReplay)
        {
            m_prepareReplay();
        }
        auto completion = m_replayRunner.StartFromStoredFrames(deviceId, m_replaySource);
        if (!completion)
        {
            return {.error = "No transmitted frames are stored for this device"};
        }
        return {.succeeded = true};
    }
    catch (const std::exception& exception)
    {
        return {.error = exception.what()};
    }
}

void OperatorWorkflowService::Stop() noexcept
{
    m_scenarioRunner.Stop();
    m_replayRunner.Stop();
}

bool OperatorWorkflowService::IsRunning() const noexcept
{
    return m_scenarioRunner.IsRunning() || m_replayRunner.IsRunning();
}

ScenarioProgressQueue& OperatorWorkflowService::Progress() noexcept
{
    return m_progressQueue;
}

std::optional<ScenarioReportResult> OperatorWorkflowService::LastScenarioReport() const
{
    const std::scoped_lock lock(m_reportMutex);
    return m_lastScenarioReport;
}

void OperatorWorkflowService::PublishScenarioProgress(const ScenarioProgress& progress) noexcept
{
    ScenarioProgress deliveredProgress = progress;
    if (progress.kind == ScenarioProgressKind::Finished)
    {
        try
        {
            ScenarioReportRequest request;
            {
                const std::scoped_lock lock(m_reportMutex);
                const auto finishedUnixMilliseconds = (std::max)(
                    m_timestampProvider(), m_lastReportFinishedUnixMilliseconds + 1);
                m_lastReportFinishedUnixMilliseconds = finishedUnixMilliseconds;
                request = {
                    .scenarioPath = m_activeScenarioPath,
                    .steps = m_activeScenarioSteps,
                    .outcome = progress,
                    .startedUnixMilliseconds = m_scenarioStartedUnixMilliseconds,
                    .finishedUnixMilliseconds = finishedUnixMilliseconds,
                };
            }
            auto result = m_reportService.Write(request);
            if (result.succeeded)
            {
                deliveredProgress.reportPath = result.path;
            }
            else
            {
                deliveredProgress.reportError = result.error;
            }
            {
                const std::scoped_lock lock(m_reportMutex);
                m_lastScenarioReport = std::move(result);
                m_activeScenarioPath.clear();
                m_activeScenarioSteps.clear();
            }
        }
        catch (const std::exception& exception)
        {
            deliveredProgress.reportError = exception.what();
            const std::scoped_lock lock(m_reportMutex);
            m_activeScenarioPath.clear();
            m_activeScenarioSteps.clear();
        }
        catch (...)
        {
            deliveredProgress.reportError = "Unknown report generation failure";
            const std::scoped_lock lock(m_reportMutex);
            m_activeScenarioPath.clear();
            m_activeScenarioSteps.clear();
        }
    }
    m_progressQueue.Push(std::move(deliveredProgress));
}
} // namespace DeviceLink::Application
