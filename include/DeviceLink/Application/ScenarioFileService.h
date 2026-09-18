#pragma once

#include "DeviceLink/Application/ScenarioTextCodec.h"
#include "DeviceLink/Infrastructure/ITextFileStore.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace DeviceLink::Application
{
struct ScenarioFileSaveResult final
{
    bool succeeded{};
    std::string error;
};

struct ScenarioFileLoadResult final
{
    std::vector<ScenarioStep> steps;
    std::string error;
    std::optional<std::size_t> errorLineNumber;

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return error.empty();
    }
};

struct ScenarioFileStartResult final
{
    std::optional<std::future<ScenarioResult>> completion;
    std::string error;
    std::optional<std::size_t> errorLineNumber;

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return completion.has_value();
    }
};

class ScenarioFileService final
{
public:
    explicit ScenarioFileService(Infrastructure::ITextFileStore& fileStore) noexcept;

    [[nodiscard]] ScenarioFileSaveResult Save(
        const std::filesystem::path& path,
        const std::vector<ScenarioStep>& steps);
    [[nodiscard]] ScenarioFileLoadResult Load(const std::filesystem::path& path) const;
    [[nodiscard]] ScenarioFileStartResult LoadAndStart(
        const std::filesystem::path& path,
        ScenarioRunner& runner) const;

private:
    Infrastructure::ITextFileStore& m_fileStore;
};
} // namespace DeviceLink::Application
