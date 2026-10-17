#include "DeviceLink/Application/DeviceAgentService.h"
#include "DeviceLink/Application/DeviceManager.h"
#include "DeviceLink/Application/DeviceRuntime.h"
#include "DeviceLink/Application/EventLogQueryService.h"
#include "DeviceLink/Infrastructure/SqliteEventRepository.h"
#include "DeviceLink/Transport/TcpTransport.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace
{
using DeviceLink::Application::AgentOperationResult;
using DeviceLink::Application::DeviceAgentService;

std::int64_t UnixMillisecondsNow()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string EscapeJson(std::string_view text)
{
    std::ostringstream output;
    for (const unsigned char character : text)
    {
        switch (character)
        {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20)
            {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(character) << std::dec;
            }
            else
            {
                output << character;
            }
        }
    }
    return output.str();
}

std::string SerializeResult(const AgentOperationResult& result)
{
    std::ostringstream output;
    output << std::boolalpha << std::setprecision(8)
           << "{\"ok\":" << result.succeeded
           << ",\"message\":\"" << EscapeJson(result.message) << '"';
    if (!result.deviceId.empty())
        output << ",\"device_id\":\"" << EscapeJson(result.deviceId) << '"';
    if (result.observedAtUnixMilliseconds > 0)
        output << ",\"observed_at_unix_ms\":" << result.observedAtUnixMilliseconds;
    if (result.telemetry)
    {
        const auto& telemetry = *result.telemetry;
        output << ",\"telemetry\":{\"state\":\""
               << DeviceAgentService::StateName(telemetry.state)
               << "\",\"fault\":\"" << DeviceAgentService::FaultName(telemetry.fault)
               << "\",\"pan_degrees\":" << telemetry.panDegrees
               << ",\"tilt_degrees\":" << telemetry.tiltDegrees
               << ",\"target_pan_degrees\":" << telemetry.targetPanDegrees
               << ",\"target_tilt_degrees\":" << telemetry.targetTiltDegrees
               << ",\"temperature_celsius\":" << telemetry.temperatureCelsius
               << ",\"supply_voltage\":" << telemetry.supplyVoltage << '}';
    }
    if (!result.steps.empty())
    {
        output << ",\"steps\":[";
        for (std::size_t index = 0; index < result.steps.size(); ++index)
        {
            if (index != 0) output << ',';
            output << '"' << EscapeJson(result.steps[index]) << '"';
        }
        output << ']';
    }
    output << '}';
    return output.str();
}

std::chrono::milliseconds ReadTimeout(std::istringstream& input, const int fallback)
{
    int value = fallback;
    input >> value;
    return std::chrono::milliseconds((std::max)(1, value));
}
} // namespace

int main(int argc, char* argv[])
{
    const std::string deviceId = argc > 1 ? argv[1] : "unreal-gimbal-01";
    const char* configuredDatabase = std::getenv("DEVICELINK_AGENT_DB");
    const std::filesystem::path databasePath = configuredDatabase != nullptr && *configuredDatabase != '\0'
        ? std::filesystem::path(configuredDatabase)
        : std::filesystem::path("DeviceLinkStudio.sqlite");
    auto repository = std::make_unique<DeviceLink::Infrastructure::SqliteEventRepository>(databasePath);
    const auto* reader = repository.get();
    DeviceLink::Application::DeviceRuntime runtime(
        std::move(repository),
        {DeviceLink::Application::VirtualGimbalService::kTelemetryMessageType},
        256, UnixMillisecondsNow);
    DeviceLink::Application::EventLogQueryService eventQuery(
        *reader, [&runtime] { runtime.FlushEventLog(); });
    DeviceLink::Application::DeviceManager& manager = runtime.Devices();
    if (!manager.RegisterDevice(deviceId) ||
        !manager.AttachTransport(deviceId, std::make_unique<DeviceLink::Transport::TcpTransport>()))
    {
        std::cerr << "Failed to initialize device runtime\n";
        return 1;
    }
    DeviceAgentService agent(manager, deviceId);

    std::string line;
    while (std::getline(std::cin, line))
    {
        std::istringstream input(line);
        std::string command;
        input >> command;
        AgentOperationResult result{};

        if (command == "CONNECT")
        {
            std::string host;
            unsigned int port{};
            input >> host >> port;
            if (host.empty() || port == 0 || port > 65535)
                result = {false, "CONNECT requires a host and valid port"};
            else
                result = agent.ConnectDevice(host, static_cast<std::uint16_t>(port));
        }
        else if (command == "POWER_ON")
        {
            result = agent.SetPower(true, ReadTimeout(input, 3000));
        }
        else if (command == "POWER_OFF")
        {
            result = agent.SetPower(false, ReadTimeout(input, 3000));
        }
        else if (command == "INITIALIZE")
        {
            result = agent.Initialize(ReadTimeout(input, 5000));
        }
        else if (command == "SET_ANGLE")
        {
            double pan{};
            double tilt{};
            int timeoutMilliseconds = 5000;
            double tolerance = 0.5;
            if (!(input >> pan >> tilt))
                result = {false, "SET_ANGLE requires pan and tilt"};
            else
            {
                input >> timeoutMilliseconds >> tolerance;
                result = agent.SetGimbalAngle(pan, tilt,
                    std::chrono::milliseconds((std::max)(1, timeoutMilliseconds)), tolerance);
            }
        }
        else if (command == "GET_EVENTS")
        {
            int requestedCount = 20;
            input >> requestedCount;
            const auto count = static_cast<std::size_t>((std::clamp)(requestedCount, 1, 100));
            const auto events = agent.RecentEvents(count);
            std::ostringstream output;
            output << "{\"ok\":true,\"device_id\":\"" << EscapeJson(deviceId)
                   << "\",\"events\":[";
            for (std::size_t index = 0; index < events.size(); ++index)
            {
                if (index != 0) output << ',';
                output << "{\"timestamp_unix_ms\":" << events[index].timestampUnixMilliseconds
                       << ",\"category\":\"" << EscapeJson(events[index].category)
                       << "\",\"detail\":\"" << EscapeJson(events[index].detail) << "\"}";
            }
            output << "]}";
            std::cout << output.str() << std::endl;
            continue;
        }
        else if (command == "GET_HISTORY")
        {
            std::int64_t fromUnixMilliseconds{};
            std::int64_t throughUnixMilliseconds{};
            int requestedCount{};
            if (!(input >> fromUnixMilliseconds >> throughUnixMilliseconds >> requestedCount) ||
                fromUnixMilliseconds < 0 || throughUnixMilliseconds < fromUnixMilliseconds ||
                requestedCount < 1 || requestedCount > 100)
            {
                result = {false, "GET_HISTORY requires valid from, through, and 1-100 count"};
            }
            else
            {
                try
                {
                    const auto entries = eventQuery.LoadWindow(deviceId, fromUnixMilliseconds,
                        throughUnixMilliseconds, static_cast<std::size_t>(requestedCount));
                    std::ostringstream output;
                    output << "{\"ok\":true,\"device_id\":\"" << EscapeJson(deviceId)
                           << "\",\"from_unix_ms\":" << fromUnixMilliseconds
                           << ",\"through_unix_ms\":" << throughUnixMilliseconds
                           << ",\"events\":[";
                    for (std::size_t index = 0; index < entries.size(); ++index)
                    {
                        if (index != 0) output << ',';
                        output << "{\"timestamp_unix_ms\":" << entries[index].timestampUnixMilliseconds
                               << ",\"category\":\"" << EscapeJson(entries[index].category)
                               << "\",\"detail\":\"" << EscapeJson(entries[index].detail)
                               << "\",\"payload_byte_count\":" << entries[index].payloadByteCount << '}';
                    }
                    output << "]}";
                    std::cout << output.str() << std::endl;
                    continue;
                }
                catch (const std::exception& exception)
                {
                    result = {false, std::string("Event history query failed: ") + exception.what()};
                }
            }
        }
        else if (command == "GET_TELEMETRY")
        {
            result = agent.GetTelemetry(ReadTimeout(input, 2000));
        }
        else if (command == "INJECT_FAULT")
        {
            std::string faultName;
            input >> faultName;
            const auto fault = DeviceAgentService::ParseFault(faultName);
            if (!fault || *fault == DeviceLink::Application::VirtualGimbalFault::None)
                result = {false, "Unsupported fault. Use MotorStall, OverTemperature, or SensorFailure"};
            else
                result = agent.InjectFault(*fault, ReadTimeout(input, 3000));
        }
        else if (command == "CLEAR_FAULT")
        {
            result = agent.ClearFault(ReadTimeout(input, 3000));
        }
        else if (command == "RUN_SCENARIO")
        {
            std::string scenario;
            input >> scenario;
            result = agent.RunTestScenario(scenario, ReadTimeout(input, 5000));
        }
        else if (command == "QUIT")
        {
            std::cout << "{\"ok\":true,\"message\":\"Bridge stopped\"}" << std::endl;
            break;
        }
        else
        {
            result = {false, "Unknown bridge command"};
        }

        std::cout << SerializeResult(result) << std::endl;
    }
    return 0;
}
