#include "DeviceLink/Infrastructure/TextFileLogger.h"

#include <chrono>
#include <iomanip>
#include <stdexcept>

namespace DeviceLink::Infrastructure
{
namespace
{

[[nodiscard]] const char* ToString(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARNING";
    case LogLevel::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

} // namespace

TextFileLogger::TextFileLogger(const std::filesystem::path& filePath)
    : m_stream(filePath, std::ios::out | std::ios::app)
{
    if (!m_stream.is_open())
    {
        throw std::runtime_error("Unable to open log file");
    }
}

void TextFileLogger::Log(LogLevel level, std::string_view message)
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    if (::localtime_s(&localTime, &timestamp) != 0)
    {
        throw std::runtime_error("Unable to obtain local time for log entry");
    }

    std::scoped_lock lock(m_mutex);
    m_stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S")
             << " [" << ToString(level) << "] " << message << '\n';
    m_stream.flush();
    if (!m_stream)
    {
        throw std::runtime_error("Unable to write log entry");
    }
}

} // namespace DeviceLink::Infrastructure
