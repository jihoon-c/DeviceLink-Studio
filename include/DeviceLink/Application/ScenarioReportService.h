#pragma once

#include "DeviceLink/Application/ScenarioRunner.h"
#include "DeviceLink/Infrastructure/ITextFileStore.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace DeviceLink::Application
{
struct ScenarioReportRequest final
{
    std::filesystem::path scenarioPath;
    std::vector<ScenarioStep> steps;
    ScenarioProgress outcome;
    std::int64_t startedUnixMilliseconds{};
    std::int64_t finishedUnixMilliseconds{};
};

struct ScenarioReportResult final
{
    bool succeeded{};
    std::filesystem::path path;
    std::string error;
};

class ScenarioReportService final
{
public:
    explicit ScenarioReportService(Infrastructure::ITextFileStore& fileStore) noexcept;

    [[nodiscard]] ScenarioReportResult Write(const ScenarioReportRequest& request);
    [[nodiscard]] static std::filesystem::path ReportPathFor(
        const std::filesystem::path& scenarioPath,
        std::int64_t finishedUnixMilliseconds);

private:
    Infrastructure::ITextFileStore& m_fileStore;
};
} // namespace DeviceLink::Application
