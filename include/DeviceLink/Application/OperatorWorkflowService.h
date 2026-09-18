#pragma once

#include "DeviceLink/Application/FrameReplayRunner.h"
#include "DeviceLink/Application/ScenarioFileService.h"
#include "DeviceLink/Application/ScenarioProgressQueue.h"
#include "DeviceLink/Application/ScenarioReportService.h"

#include <filesystem>
#include <functional>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace DeviceLink::Application
{
class DeviceManager;
class SqliteFrameReplaySource;
}

namespace DeviceLink::Infrastructure
{
class ITextFileStore;
}

namespace DeviceLink::Application
{
struct WorkflowStartResult final
{
    bool succeeded{};
    std::string error;
    std::optional<std::size_t> errorLineNumber;
};

class OperatorWorkflowService final
{
public:
    using PrepareReplay = std::function<void()>;
    using TimestampProvider = std::function<std::int64_t()>;

    OperatorWorkflowService(
        DeviceManager& deviceManager,
        Infrastructure::ITextFileStore& fileStore,
        const SqliteFrameReplaySource& replaySource,
        PrepareReplay prepareReplay = {},
        TimestampProvider timestampProvider = {});
    ~OperatorWorkflowService();

    OperatorWorkflowService(const OperatorWorkflowService&) = delete;
    OperatorWorkflowService& operator=(const OperatorWorkflowService&) = delete;

    [[nodiscard]] WorkflowStartResult StartScenario(const std::filesystem::path& path);
    [[nodiscard]] WorkflowStartResult StartReplay(const std::string& deviceId);
    void Stop() noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] ScenarioProgressQueue& Progress() noexcept;
    [[nodiscard]] std::optional<ScenarioReportResult> LastScenarioReport() const;

private:
    void PublishScenarioProgress(const ScenarioProgress& progress) noexcept;

    ScenarioProgressQueue m_progressQueue;
    ScenarioRunner m_scenarioRunner;
    FrameReplayRunner m_replayRunner;
    ScenarioFileService m_fileService;
    ScenarioReportService m_reportService;
    const SqliteFrameReplaySource& m_replaySource;
    PrepareReplay m_prepareReplay;
    TimestampProvider m_timestampProvider;
    mutable std::mutex m_reportMutex;
    std::filesystem::path m_activeScenarioPath;
    std::vector<ScenarioStep> m_activeScenarioSteps;
    std::int64_t m_scenarioStartedUnixMilliseconds{};
    std::int64_t m_lastReportFinishedUnixMilliseconds{};
    std::optional<ScenarioReportResult> m_lastScenarioReport;
};
} // namespace DeviceLink::Application
