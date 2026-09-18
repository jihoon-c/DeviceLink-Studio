#pragma once

#include "DeviceLink/Infrastructure/ILogger.h"

#include <filesystem>
#include <fstream>
#include <mutex>

namespace DeviceLink::Infrastructure
{

class TextFileLogger final : public ILogger
{
public:
    explicit TextFileLogger(const std::filesystem::path& filePath);
    ~TextFileLogger() override = default;

    void Log(LogLevel level, std::string_view message) override;

    TextFileLogger(const TextFileLogger&) = delete;
    TextFileLogger& operator=(const TextFileLogger&) = delete;

private:
    std::ofstream m_stream;
    std::mutex m_mutex;
};

} // namespace DeviceLink::Infrastructure
