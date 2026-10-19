#include "DeviceLink/Application/DiagnosticSnapshot.h"

#include <iomanip>
#include <algorithm>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace DeviceLink::Application
{
namespace
{
std::string_view Utf8Prefix(std::string_view text, std::size_t limit)
{
    if (text.size() <= limit) return text;
    while (limit > 0 && (static_cast<unsigned char>(text[limit]) & 0xC0U) == 0x80U) --limit;
    return text.substr(0, limit);
}

std::string Quote(std::string_view text)
{
    std::ostringstream output;
    output << '"';
    for (const unsigned char character : text)
    {
        if (character == '"' || character == '\\') output << '\\' << character;
        else if (character < 32) output << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(character);
        else output << character;
    }
    output << '"';
    return output.str();
}
}
std::string SerializeDiagnosticSnapshot(const AgentOperationResult& observation,
    const std::vector<AgentEventSummary>& events, bool includeLlm, std::string_view baselineId)
{
    if (!observation.succeeded || !observation.telemetry) throw std::invalid_argument("Fresh telemetry is required");
    const auto& telemetry = *observation.telemetry;
    std::ostringstream output; output.imbue(std::locale::classic()); output << std::setprecision(15);
    output << "{\"device_id\":" << Quote(observation.deviceId) << ",\"observed_at_unix_ms\":" << observation.observedAtUnixMilliseconds
        << ",\"include_llm\":" << (includeLlm ? "true" : "false") << ",\"telemetry\":{\"state\":" << Quote(DeviceAgentService::StateName(telemetry.state))
        << ",\"fault\":" << Quote(DeviceAgentService::FaultName(telemetry.fault)) << ",\"pan_degrees\":" << telemetry.panDegrees
        << ",\"tilt_degrees\":" << telemetry.tiltDegrees << ",\"temperature_celsius\":" << telemetry.temperatureCelsius
        << ",\"supply_voltage\":" << telemetry.supplyVoltage << "},\"events\":[";
    const auto count = (std::min)(events.size(), std::size_t{20});
    for (std::size_t index = events.size() - count; index < events.size(); ++index)
    {
        if (index != events.size()-count) output << ',';
        const auto& event = events[index];
        output << "{\"timestamp_unix_ms\":" << event.timestampUnixMilliseconds << ",\"category\":" << Quote(Utf8Prefix(event.category,100))
            << ",\"detail\":" << Quote(Utf8Prefix(event.detail,512)) << '}';
    }
    output << ']';
    if (!baselineId.empty()) output << ",\"baseline_id\":" << Quote(baselineId);
    output << '}';
    return output.str();
}
}
