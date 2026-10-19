#include "DeviceLink/Infrastructure/PythonDiagnosticClient.h"
#include "DeviceLink/Application/DiagnosticSnapshot.h"

#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace DeviceLink;

int wmain(int argc, wchar_t* argv[])
{
    try
    {
        if (argc != 2) throw std::runtime_error("Project root argument required");
        const std::filesystem::path root(argv[1]);
        if (!std::filesystem::is_regular_file(root / L"mcp_server/.venv/Scripts/python.exe"))
        { std::cout << "Skipped: project Python environment not installed\n"; return 77; }
        Infrastructure::PythonDiagnosticClient client(root);
        Application::AgentOperationResult observation{true, "Fixture observation"};
        observation.deviceId = "native-snapshot-test\"\\\n";
        observation.observedAtUnixMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        Application::VirtualGimbalTelemetry telemetry;
        telemetry.state = Application::VirtualGimbalState::Fault;
        telemetry.fault = Application::VirtualGimbalFault::MotorStall;
        telemetry.temperatureCelsius = 25; telemetry.supplyVoltage = 24;
        observation.telemetry = telemetry;
        const std::vector<Application::AgentEventSummary> events{{observation.observedAtUnixMilliseconds,
            "fixture", std::string(511, 'x') + "\xEA\xB0\x80"}};
        auto result = client.Process(Application::SerializeDiagnosticSnapshot(observation, events, false), {});
        if (result.text.find("OBSERVED") == std::string::npos || result.reportPath.empty() || client.LastDiagnosisId().size() != 32)
            throw std::runtime_error("Native/Python diagnosis did not produce cited report");
        const auto baseline = client.LastDiagnosisId();
        observation.telemetry->state = Application::VirtualGimbalState::Ready;
        observation.telemetry->fault = Application::VirtualGimbalFault::None;
        observation.observedAtUnixMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        result = client.Process(Application::SerializeDiagnosticSnapshot(observation, {}, false, baseline), {});
        if (result.text.find("PASS") == std::string::npos || result.reportPath.empty())
            throw std::runtime_error("Same-device recovery verification failed");
        std::stop_source cancelled; cancelled.request_stop();
        if (!client.Process("{}", cancelled.get_token()).reportPath.empty())
            throw std::runtime_error("Cancelled request unexpectedly created report");
        std::cout << "Native/Python snapshot diagnosis and recovery report passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
