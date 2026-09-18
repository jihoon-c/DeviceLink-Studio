#include "DeviceLink/Application/ScenarioTextCodec.h"

#include <charconv>
#include <cctype>
#include <limits>
#include <sstream>
#include <span>
#include <string>
#include <type_traits>

namespace DeviceLink::Application
{
namespace
{
[[nodiscard]] bool ParseUnsigned(std::string_view text, std::uint64_t maximum,
    std::uint64_t& value)
{
    if (text.empty())
    {
        return false;
    }

    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() && value <= maximum;
}

[[nodiscard]] std::optional<std::vector<std::byte>> DecodePayload(std::string_view text)
{
    if (text == "-")
    {
        return std::vector<std::byte>{};
    }
    if (text.empty() || text.size() % 2 != 0)
    {
        return std::nullopt;
    }

    std::vector<std::byte> payload;
    payload.reserve(text.size() / 2);
    for (std::size_t offset{}; offset < text.size(); offset += 2)
    {
        unsigned int value{};
        const auto [end, error] = std::from_chars(
            text.data() + offset, text.data() + offset + 2, value, 16);
        if (error != std::errc{} || end != text.data() + offset + 2 || value > 0xFF)
        {
            return std::nullopt;
        }
        payload.push_back(static_cast<std::byte>(value));
    }
    return payload;
}

void AppendPayload(std::ostringstream& output, std::span<const std::byte> payload)
{
    static constexpr char hexDigits[] = "0123456789ABCDEF";
    if (payload.empty())
    {
        output << '-';
        return;
    }

    for (const std::byte value : payload)
    {
        const auto integerValue = std::to_integer<unsigned int>(value);
        output << hexDigits[integerValue >> 4] << hexDigits[integerValue & 0x0F];
    }
}

[[nodiscard]] ScenarioParseResult Failure(std::size_t lineNumber, std::string message)
{
    return {.error = ScenarioParseError{.lineNumber = lineNumber, .message = std::move(message)}};
}
} // namespace

std::optional<std::string> ScenarioTextCodec::Serialize(const std::vector<ScenarioStep>& steps)
{
    std::ostringstream output;
    for (const auto& step : steps)
    {
        output << step.delayBeforeAction.count() << ' ';
        const bool valid = std::visit(
            [&output](const auto& action) {
                using Action = std::decay_t<decltype(action)>;
                if constexpr (std::is_same_v<Action, ConnectScenarioAction>)
                {
                    if (action.deviceId.empty() || action.host.empty() || action.port == 0)
                    {
                        return false;
                    }
                    output << "CONNECT " << action.deviceId << ' ' << action.host << ' ' << action.port;
                }
                else if constexpr (std::is_same_v<Action, SendFrameScenarioAction>)
                {
                    if (action.deviceId.empty() ||
                        action.frame.header.payloadSize != action.frame.payload.size())
                    {
                        return false;
                    }
                    output << "SEND " << action.deviceId << ' ' << action.frame.header.messageType << ' '
                           << action.frame.header.sequence << ' ';
                    AppendPayload(output, action.frame.payload);
                }
                else
                {
                    if (action.deviceId.empty())
                    {
                        return false;
                    }
                    output << "DISCONNECT " << action.deviceId;
                }
                return true;
            },
            step.action);
        if (!valid)
        {
            return std::nullopt;
        }
        output << '\n';
    }
    return output.str();
}

ScenarioParseResult ScenarioTextCodec::Parse(std::string_view text)
{
    std::istringstream input{std::string(text)};
    std::string line;
    std::size_t lineNumber{};
    ScenarioParseResult result;
    while (std::getline(input, line))
    {
        ++lineNumber;
        const auto firstNonSpace = line.find_first_not_of(" \t\r");
        if (firstNonSpace == std::string::npos || line[firstNonSpace] == '#')
        {
            continue;
        }

        std::istringstream fields(line.substr(firstNonSpace));
        std::string delayText;
        std::string command;
        if (!(fields >> delayText >> command))
        {
            return Failure(lineNumber, "Expected delay and command");
        }
        std::uint64_t delay{};
        if (!ParseUnsigned(delayText, static_cast<std::uint64_t>(
                std::chrono::milliseconds::max().count()), delay))
        {
            return Failure(lineNumber, "Invalid delay");
        }

        ScenarioStep step{.delayBeforeAction = std::chrono::milliseconds(delay)};
        if (command == "CONNECT")
        {
            std::string deviceId;
            std::string host;
            std::string portText;
            std::string extra;
            std::uint64_t port{};
            if (!(fields >> deviceId >> host >> portText) || fields >> extra || deviceId.empty() || host.empty() ||
                !ParseUnsigned(portText, std::numeric_limits<std::uint16_t>::max(), port) || port == 0)
            {
                return Failure(lineNumber, "Invalid CONNECT command");
            }
            step.action = ConnectScenarioAction{std::move(deviceId), std::move(host),
                static_cast<std::uint16_t>(port)};
        }
        else if (command == "SEND")
        {
            std::string deviceId;
            std::string messageTypeText;
            std::string sequenceText;
            std::string payloadText;
            std::string extra;
            std::uint64_t messageType{};
            std::uint64_t sequence{};
            if (!(fields >> deviceId >> messageTypeText >> sequenceText >> payloadText) || fields >> extra ||
                deviceId.empty() || !ParseUnsigned(messageTypeText,
                    std::numeric_limits<std::uint16_t>::max(), messageType) || !ParseUnsigned(sequenceText,
                    std::numeric_limits<std::uint32_t>::max(), sequence))
            {
                return Failure(lineNumber, "Invalid SEND command");
            }
            const auto payload = DecodePayload(payloadText);
            if (!payload.has_value())
            {
                return Failure(lineNumber, "Invalid SEND payload");
            }
            Protocol::PacketFrame frame{};
            frame.header.messageType = static_cast<std::uint16_t>(messageType);
            frame.header.sequence = static_cast<std::uint32_t>(sequence);
            frame.header.payloadSize = static_cast<std::uint32_t>(payload->size());
            frame.payload = *payload;
            step.action = SendFrameScenarioAction{std::move(deviceId), std::move(frame)};
        }
        else if (command == "DISCONNECT")
        {
            std::string deviceId;
            std::string extra;
            if (!(fields >> deviceId) || fields >> extra || deviceId.empty())
            {
                return Failure(lineNumber, "Invalid DISCONNECT command");
            }
            step.action = DisconnectScenarioAction{std::move(deviceId)};
        }
        else
        {
            return Failure(lineNumber, "Unknown command");
        }
        result.steps.push_back(std::move(step));
    }
    return result;
}
} // namespace DeviceLink::Application
