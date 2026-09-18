#pragma once

#include "DeviceLink/Infrastructure/ITextFileStore.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace DeviceLink::Application
{
struct OperatorSettings final
{
    std::string host{"127.0.0.1"};
    std::uint16_t port{5000};
    bool automaticReconnect{true};
    std::string scenarioPath{"..\\scenarios\\unreal-gimbal-demo.dls"};
    std::string historyDeviceId{"unreal-gimbal-01"};
    std::uint8_t historyCategoryIndex{};
    std::string historySearchText;
    std::uint32_t heartbeatWarningMilliseconds{1500};
    std::uint32_t heartbeatFaultMilliseconds{3000};
};

struct OperatorSettingsLoadResult final
{
    OperatorSettings settings;
    bool loaded{};
    std::string error;
};

struct OperatorSettingsSaveResult final
{
    bool succeeded{};
    std::string error;
};

class OperatorSettingsService final
{
public:
    OperatorSettingsService(
        Infrastructure::ITextFileStore& fileStore,
        std::filesystem::path settingsPath);

    [[nodiscard]] OperatorSettingsLoadResult Load() const;
    [[nodiscard]] OperatorSettingsSaveResult Save(const OperatorSettings& settings);

private:
    Infrastructure::ITextFileStore& m_fileStore;
    std::filesystem::path m_settingsPath;
};
} // namespace DeviceLink::Application
