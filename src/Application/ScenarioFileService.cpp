#include "DeviceLink/Application/ScenarioFileService.h"

#include <utility>

namespace DeviceLink::Application
{
ScenarioFileService::ScenarioFileService(Infrastructure::ITextFileStore& fileStore) noexcept
    : m_fileStore(fileStore)
{
}

ScenarioFileSaveResult ScenarioFileService::Save(
    const std::filesystem::path& path,
    const std::vector<ScenarioStep>& steps)
{
    const auto text = ScenarioTextCodec::Serialize(steps);
    if (!text.has_value())
    {
        return {.error = "Scenario contains an invalid action"};
    }

    const auto writeResult = m_fileStore.WriteAtomically(path, *text);
    return {.succeeded = writeResult.succeeded, .error = writeResult.error};
}

ScenarioFileLoadResult ScenarioFileService::Load(const std::filesystem::path& path) const
{
    const auto readResult = m_fileStore.Read(path);
    if (!readResult.Succeeded())
    {
        return {.error = readResult.error};
    }

    auto parseResult = ScenarioTextCodec::Parse(*readResult.contents);
    if (!parseResult.Succeeded())
    {
        return {
            .error = parseResult.error->message,
            .errorLineNumber = parseResult.error->lineNumber,
        };
    }
    return {.steps = std::move(parseResult.steps)};
}

ScenarioFileStartResult ScenarioFileService::LoadAndStart(
    const std::filesystem::path& path,
    ScenarioRunner& runner) const
{
    auto loadResult = Load(path);
    if (!loadResult.Succeeded())
    {
        return {
            .error = std::move(loadResult.error),
            .errorLineNumber = loadResult.errorLineNumber,
        };
    }
    if (loadResult.steps.empty())
    {
        return {.error = "Scenario contains no steps"};
    }

    auto completion = runner.Start(std::move(loadResult.steps));
    if (!completion.has_value())
    {
        return {.error = "Scenario runner is unavailable"};
    }
    return {.completion = std::move(completion)};
}
} // namespace DeviceLink::Application
