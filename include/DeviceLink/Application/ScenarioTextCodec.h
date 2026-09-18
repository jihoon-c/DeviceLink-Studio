#pragma once

#include "DeviceLink/Application/ScenarioRunner.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace DeviceLink::Application
{
struct ScenarioParseError final
{
    std::size_t lineNumber{};
    std::string message;
};

struct ScenarioParseResult final
{
    std::vector<ScenarioStep> steps;
    std::optional<ScenarioParseError> error;

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return !error.has_value();
    }
};

class ScenarioTextCodec final
{
public:
    [[nodiscard]] static std::optional<std::string> Serialize(
        const std::vector<ScenarioStep>& steps);
    [[nodiscard]] static ScenarioParseResult Parse(std::string_view text);
};
} // namespace DeviceLink::Application
