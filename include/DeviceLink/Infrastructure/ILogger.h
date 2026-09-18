#pragma once

#include <string_view>

namespace DeviceLink::Infrastructure
{

enum class LogLevel
{
    Debug,
    Info,
    Warning,
    Error,
};

class ILogger
{
public:
    virtual ~ILogger() = default;

    virtual void Log(LogLevel level, std::string_view message) = 0;
};

} // namespace DeviceLink::Infrastructure
