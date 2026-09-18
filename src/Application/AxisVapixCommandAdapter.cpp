#include "DeviceLink/Application/AxisVapixCommandAdapter.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace DeviceLink::Application
{
namespace
{
std::string Base64Encode(std::string_view input)
{
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve((input.size() + 2U) / 3U * 4U);
    for (std::size_t index = 0; index < input.size(); index += 3U)
    {
        const auto first = static_cast<unsigned char>(input[index]);
        const auto second = index + 1U < input.size()
            ? static_cast<unsigned char>(input[index + 1U]) : 0U;
        const auto third = index + 2U < input.size()
            ? static_cast<unsigned char>(input[index + 2U]) : 0U;
        const auto packed = (static_cast<unsigned int>(first) << 16U) |
            (static_cast<unsigned int>(second) << 8U) | static_cast<unsigned int>(third);
        encoded += alphabet[(packed >> 18U) & 0x3FU];
        encoded += alphabet[(packed >> 12U) & 0x3FU];
        encoded += index + 1U < input.size() ? alphabet[(packed >> 6U) & 0x3FU] : '=';
        encoded += index + 2U < input.size() ? alphabet[packed & 0x3FU] : '=';
    }
    return encoded;
}

std::string FormatNumber(double value)
{
    auto text = std::to_string(value);
    const auto lastNonZero = text.find_last_not_of('0');
    if (lastNonZero != std::string::npos)
    {
        text.erase(lastNonZero + 1U);
    }
    if (!text.empty() && text.back() == '.')
    {
        text.pop_back();
    }
    return text;
}

std::optional<double> ReadKeyValue(std::string_view responseBody, std::string_view key) noexcept
{
    const std::string prefix = std::string(key) + "=";
    const auto start = responseBody.find(prefix);
    if (start == std::string_view::npos)
    {
        return std::nullopt;
    }
    const auto valueStart = start + prefix.size();
    const auto valueEnd = responseBody.find_first_of("\r\n", valueStart);
    try
    {
        const std::string value(responseBody.substr(valueStart, valueEnd - valueStart));
        std::size_t consumed{};
        const double parsed = std::stod(value, &consumed);
        return consumed == value.size() && std::isfinite(parsed)
            ? std::optional<double>{parsed} : std::nullopt;
    }
    catch (...)
    {
        return std::nullopt;
    }
}
} // namespace

AxisVapixCommandAdapter::AxisVapixCommandAdapter(AxisVapixOptions options)
    : m_options(std::move(options))
{
    if (m_options.hostName.empty() || m_options.cameraChannel == 0 ||
        m_options.scanPanSpeed < -100 || m_options.scanPanSpeed > 100 ||
        m_options.scanPanSpeed == 0)
    {
        throw std::invalid_argument("Axis VAPIX endpoint options are invalid");
    }
    if (m_options.basicCredentials && m_options.basicCredentials->userName.empty())
    {
        throw std::invalid_argument("Axis VAPIX Basic authentication requires a user name");
    }
}

std::optional<AxisVapixRequest> AxisVapixCommandAdapter::EncodeCommand(
    const GimbalCommand& command) const
{
    const std::string camera = std::to_string(m_options.cameraChannel);
    switch (command.kind)
    {
    case GimbalCommandKind::Initialize:
        return MakeGetRequest("/axis-cgi/com/ptz.cgi?move=home&camera=" + camera);
    case GimbalCommandKind::SetPanTilt:
        if (!std::isfinite(command.panDegrees) || !std::isfinite(command.tiltDegrees))
        {
            return std::nullopt;
        }
        return MakeGetRequest("/axis-cgi/com/ptz.cgi?pan=" + FormatNumber(command.panDegrees) +
            "&tilt=" + FormatNumber(command.tiltDegrees) + "&camera=" + camera);
    case GimbalCommandKind::StartScan:
        return MakeGetRequest("/axis-cgi/com/ptz.cgi?continuouspantiltmove=" +
            std::to_string(m_options.scanPanSpeed) + ",0&camera=" + camera);
    case GimbalCommandKind::StopScan:
        return MakeGetRequest("/axis-cgi/com/ptz.cgi?move=stop&camera=" + camera);
    case GimbalCommandKind::RequestStatus:
        return MakeGetRequest("/axis-cgi/com/ptz.cgi?query=position&camera=" + camera);
    case GimbalCommandKind::Power:
    case GimbalCommandKind::InjectFault:
    case GimbalCommandKind::ConfigureResponse:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<AxisVapixPosition> AxisVapixCommandAdapter::DecodePositionResponse(
    std::int32_t httpStatusCode, std::string_view responseBody) noexcept
{
    if (httpStatusCode < 200 || httpStatusCode >= 300)
    {
        return std::nullopt;
    }
    const auto pan = ReadKeyValue(responseBody, "pan");
    const auto tilt = ReadKeyValue(responseBody, "tilt");
    if (!pan || !tilt)
    {
        return std::nullopt;
    }
    return AxisVapixPosition{.panDegrees = *pan, .tiltDegrees = *tilt,
        .zoom = ReadKeyValue(responseBody, "zoom")};
}

AxisVapixRequest AxisVapixCommandAdapter::MakeGetRequest(std::string target) const
{
    AxisVapixRequest request{.target = std::move(target)};
    request.serializedRequest = "GET " + request.target + " HTTP/1.1\r\nHost: " +
        m_options.hostName + "\r\nAccept: text/plain\r\nConnection: keep-alive\r\n";
    if (m_options.basicCredentials)
    {
        const auto& credentials = *m_options.basicCredentials;
        request.serializedRequest += "Authorization: Basic " +
            Base64Encode(credentials.userName + ":" + credentials.password) + "\r\n";
    }
    request.serializedRequest += "\r\n";
    return request;
}
} // namespace DeviceLink::Application
