#include "DeviceLink/Application/ScenarioReportService.h"

#include <algorithm>
#include <exception>
#include <sstream>
#include <type_traits>

namespace DeviceLink::Application
{
namespace
{
[[nodiscard]] const char* StatusText(ScenarioStatus status) noexcept
{
    switch (status)
    {
    case ScenarioStatus::Completed: return "PASS";
    case ScenarioStatus::Cancelled: return "CANCELLED";
    case ScenarioStatus::Failed: return "FAIL";
    }
    return "UNKNOWN";
}

[[nodiscard]] std::string EscapeTableText(std::string text)
{
    for (auto& character : text)
    {
        if (character == '\r' || character == '\n')
        {
            character = ' ';
        }
    }
    std::size_t position{};
    while ((position = text.find('|', position)) != std::string::npos)
    {
        text.insert(position, "\\");
        position += 2;
    }
    return text;
}

[[nodiscard]] std::string DescribeAction(const ScenarioAction& action)
{
    return std::visit([](const auto& value) {
        using Action = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Action, ConnectScenarioAction>)
        {
            return "CONNECT " + value.deviceId + " " + value.host + ":" +
                std::to_string(value.port);
        }
        else if constexpr (std::is_same_v<Action, SendFrameScenarioAction>)
        {
            return "SEND " + value.deviceId + " type=" +
                std::to_string(value.frame.header.messageType) + " sequence=" +
                std::to_string(value.frame.header.sequence) + " bytes=" +
                std::to_string(value.frame.payload.size());
        }
        else
        {
            return "DISCONNECT " + value.deviceId;
        }
    }, action);
}

[[nodiscard]] std::string StepResult(
    std::size_t stepIndex,
    const ScenarioProgress& outcome)
{
    if (stepIndex < outcome.completedStepCount)
    {
        return "PASS";
    }
    if (outcome.failedStepIndex && stepIndex == *outcome.failedStepIndex)
    {
        return "FAIL";
    }
    return "NOT RUN";
}
} // namespace

ScenarioReportService::ScenarioReportService(
    Infrastructure::ITextFileStore& fileStore) noexcept
    : m_fileStore(fileStore)
{
}

ScenarioReportResult ScenarioReportService::Write(const ScenarioReportRequest& request)
{
    if (request.scenarioPath.empty() ||
        request.outcome.kind != ScenarioProgressKind::Finished)
    {
        return {.error = "A finished scenario result is required"};
    }

    const auto reportPath = ReportPathFor(
        request.scenarioPath, request.finishedUnixMilliseconds);
    const auto duration = (std::max)(
        std::int64_t{0}, request.finishedUnixMilliseconds - request.startedUnixMilliseconds);
    std::ostringstream report;
    report << "# DeviceLink Studio Scenario Test Report\n\n"
           << "- Scenario: `" << EscapeTableText(request.scenarioPath.generic_string()) << "`\n"
           << "- Verdict: **" << StatusText(request.outcome.status) << "**\n"
           << "- Started (Unix ms): " << request.startedUnixMilliseconds << "\n"
           << "- Finished (Unix ms): " << request.finishedUnixMilliseconds << "\n"
           << "- Duration: " << duration << " ms\n"
           << "- Completed steps: " << request.outcome.completedStepCount << " / "
           << request.steps.size() << "\n";
    if (request.outcome.failedStepIndex)
    {
        report << "- Failed step: " << (*request.outcome.failedStepIndex + 1) << "\n";
    }
    if (!request.outcome.failureMessage.empty())
    {
        report << "- Failure: " << EscapeTableText(request.outcome.failureMessage) << "\n";
    }

    report << "\n## Step Results\n\n"
           << "| # | Delay (ms) | Action | Result | Detail |\n"
           << "|---:|---:|---|---|---|\n";
    for (std::size_t index{}; index < request.steps.size(); ++index)
    {
        const bool failed = request.outcome.failedStepIndex &&
            index == *request.outcome.failedStepIndex;
        report << "| " << (index + 1) << " | "
               << request.steps[index].delayBeforeAction.count() << " | "
               << EscapeTableText(DescribeAction(request.steps[index].action)) << " | "
               << StepResult(index, request.outcome) << " | "
               << (failed ? EscapeTableText(request.outcome.failureMessage) : "-") << " |\n";
    }
    report << "\nGenerated automatically by DeviceLink Studio.\n";

    try
    {
        const auto write = m_fileStore.WriteAtomically(reportPath, report.str());
        return {
            .succeeded = write.succeeded,
            .path = reportPath,
            .error = write.error,
        };
    }
    catch (const std::exception& exception)
    {
        return {.path = reportPath, .error = exception.what()};
    }
}

std::filesystem::path ScenarioReportService::ReportPathFor(
    const std::filesystem::path& scenarioPath,
    std::int64_t finishedUnixMilliseconds)
{
    return scenarioPath.parent_path() /
        (scenarioPath.stem().wstring() + L".report." +
            std::to_wstring(finishedUnixMilliseconds) + L".md");
}
} // namespace DeviceLink::Application
