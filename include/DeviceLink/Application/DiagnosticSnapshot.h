#pragma once
#include "DeviceLink/Application/DeviceAgentService.h"

namespace DeviceLink::Application
{
[[nodiscard]] std::string SerializeDiagnosticSnapshot(const AgentOperationResult& observation,
    const std::vector<AgentEventSummary>& events, bool includeLlm, std::string_view baselineId = {});
}
