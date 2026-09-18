#include "DeviceLink/Application/OperatorSettingsService.h"

#include <charconv>
#include <cctype>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace DeviceLink::Application
{
namespace
{
constexpr char kHexDigits[] = "0123456789ABCDEF";

std::string Encode(std::string_view value)
{
    std::string encoded;
    for (const unsigned char character : value)
    {
        if (std::isalnum(character) != 0 || character == '-' || character == '_' ||
            character == '.' || character == '\\' || character == '/' || character == ':')
        {
            encoded.push_back(static_cast<char>(character));
        }
        else
        {
            encoded.push_back('%');
            encoded.push_back(kHexDigits[character >> 4U]);
            encoded.push_back(kHexDigits[character & 0x0FU]);
        }
    }
    return encoded;
}

std::optional<std::string> Decode(std::string_view value)
{
    std::string decoded;
    for (std::size_t index{}; index < value.size(); ++index)
    {
        if (value[index] != '%')
        {
            decoded.push_back(value[index]);
            continue;
        }
        if (index + 2 >= value.size())
        {
            return std::nullopt;
        }
        unsigned int byte{};
        const auto [end, error] = std::from_chars(
            value.data() + index + 1, value.data() + index + 3, byte, 16);
        if (error != std::errc{} || end != value.data() + index + 3)
        {
            return std::nullopt;
        }
        decoded.push_back(static_cast<char>(byte));
        index += 2;
    }
    return decoded;
}

template <typename Integer>
bool ParseInteger(std::string_view text, Integer& value)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return !text.empty() && error == std::errc{} && end == text.data() + text.size();
}
} // namespace

OperatorSettingsService::OperatorSettingsService(
    Infrastructure::ITextFileStore& fileStore,
    std::filesystem::path settingsPath)
    : m_fileStore(fileStore), m_settingsPath(std::move(settingsPath))
{
}

OperatorSettingsLoadResult OperatorSettingsService::Load() const
{
    const auto read = m_fileStore.Read(m_settingsPath);
    if (!read.Succeeded())
    {
        return {.error = read.error};
    }
    std::unordered_map<std::string, std::string> fields;
    std::istringstream input(*read.contents);
    std::string line;
    while (std::getline(input, line))
    {
        const auto separator = line.find('=');
        if (separator == std::string::npos || separator == 0 ||
            !fields.emplace(line.substr(0, separator), line.substr(separator + 1)).second)
        {
            return {.error = "Settings file contains an invalid or duplicate field"};
        }
    }
    const auto required = [&fields](const char* name) -> const std::string* {
        const auto item = fields.find(name);
        return item == fields.end() ? nullptr : &item->second;
    };
    const auto* version = required("version");
    const auto* hostText = required("host");
    const auto* portText = required("port");
    const auto* reconnectText = required("automatic_reconnect");
    const auto* scenarioText = required("scenario_path");
    const auto* historyDeviceText = required("history_device_id");
    const auto* categoryText = required("history_category_index");
    const auto* searchText = required("history_search_text");
    const auto* heartbeatWarningText = required("heartbeat_warning_milliseconds");
    const auto* heartbeatFaultText = required("heartbeat_fault_milliseconds");
    if (version == nullptr || (*version != "1" && *version != "2") || hostText == nullptr || portText == nullptr ||
        reconnectText == nullptr || scenarioText == nullptr || historyDeviceText == nullptr ||
        categoryText == nullptr || searchText == nullptr ||
        (*version == "2" && (heartbeatWarningText == nullptr || heartbeatFaultText == nullptr)))
    {
        return {.error = "Settings file is incomplete or has an unsupported version"};
    }

    OperatorSettings settings;
    unsigned int port{};
    unsigned int category{};
    unsigned int heartbeatWarning{1500};
    unsigned int heartbeatFault{3000};
    const auto host = Decode(*hostText);
    const auto scenario = Decode(*scenarioText);
    const auto historyDevice = Decode(*historyDeviceText);
    const auto search = Decode(*searchText);
    if (!host || host->empty() || !scenario || !historyDevice || !search ||
        !ParseInteger(*portText, port) || port == 0 || port > 65535 ||
        (*reconnectText != "0" && *reconnectText != "1") ||
        !ParseInteger(*categoryText, category) || category > 6 ||
        (*version == "2" && (!ParseInteger(*heartbeatWarningText, heartbeatWarning) ||
            !ParseInteger(*heartbeatFaultText, heartbeatFault))) ||
        heartbeatWarning < 100 || heartbeatFault <= heartbeatWarning || heartbeatFault > 600000)
    {
        return {.error = "Settings file contains an invalid value"};
    }
    settings.host = *host;
    settings.port = static_cast<std::uint16_t>(port);
    settings.automaticReconnect = *reconnectText == "1";
    settings.scenarioPath = *scenario;
    settings.historyDeviceId = *historyDevice;
    settings.historyCategoryIndex = static_cast<std::uint8_t>(category);
    settings.historySearchText = *search;
    settings.heartbeatWarningMilliseconds = heartbeatWarning;
    settings.heartbeatFaultMilliseconds = heartbeatFault;
    return {.settings = std::move(settings), .loaded = true};
}

OperatorSettingsSaveResult OperatorSettingsService::Save(const OperatorSettings& settings)
{
    if (settings.host.empty() || settings.port == 0 || settings.historyCategoryIndex > 6 ||
        settings.heartbeatWarningMilliseconds < 100 ||
        settings.heartbeatFaultMilliseconds <= settings.heartbeatWarningMilliseconds ||
        settings.heartbeatFaultMilliseconds > 600000)
    {
        return {.error = "Operator settings contain an invalid value"};
    }
    std::ostringstream output;
    output << "version=2\n"
           << "host=" << Encode(settings.host) << '\n'
           << "port=" << settings.port << '\n'
           << "automatic_reconnect=" << (settings.automaticReconnect ? 1 : 0) << '\n'
           << "scenario_path=" << Encode(settings.scenarioPath) << '\n'
           << "history_device_id=" << Encode(settings.historyDeviceId) << '\n'
           << "history_category_index=" << static_cast<unsigned int>(settings.historyCategoryIndex) << '\n'
           << "history_search_text=" << Encode(settings.historySearchText) << '\n'
           << "heartbeat_warning_milliseconds=" << settings.heartbeatWarningMilliseconds << '\n'
           << "heartbeat_fault_milliseconds=" << settings.heartbeatFaultMilliseconds << '\n';
    const auto write = m_fileStore.WriteAtomically(m_settingsPath, output.str());
    return {.succeeded = write.succeeded, .error = write.error};
}
} // namespace DeviceLink::Application
