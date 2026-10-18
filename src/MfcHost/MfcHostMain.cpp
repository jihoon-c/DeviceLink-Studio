#include "DeviceLink/Application/DeviceRuntime.h"
#include "DeviceLink/Application/EventLogQueryService.h"
#include "DeviceLink/Application/OperatorWorkflowService.h"
#include "DeviceLink/Application/OperatorSettingsService.h"
#include "DeviceLink/Application/SqliteFrameReplaySource.h"
#include "DeviceLink/Application/VirtualGimbalService.h"
#include "DeviceLink/Application/DeviceAgentService.h"
#include "DeviceLink/Application/OperatorRequestService.h"
#include "DeviceLink/Infrastructure/AtomicTextFileStore.h"
#include "DeviceLink/Infrastructure/SqliteEventRepository.h"
#include "DeviceLink/Transport/TcpTransport.h"
#include "DeviceLink/UI/MfcDashboardFrame.h"

#include <afxwin.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <string>
#include <vector>

namespace
{
class MfcHostApplication final : public CWinApp
{
};

std::int64_t CurrentUnixMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int commandShow)
{
    const std::wstring_view arguments(commandLine);
    const bool demoHidden = arguments == L"--demo-hidden";
    const bool demo = demoHidden || arguments == L"--demo";
    // Per-monitor V2 keeps Win32 non-client areas and child controls sharp when
    // the dashboard moves between displays with different scale factors.
    static_cast<void>(::SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    MfcHostApplication application;

    if (!AfxWinInit(instance, nullptr, GetCommandLine(), 0))
    {
        return 1;
    }
    INITCOMMONCONTROLSEX commonControls{sizeof(INITCOMMONCONTROLSEX), ICC_TAB_CLASSES};
    if (!::InitCommonControlsEx(&commonControls)) return 1;

    if (demo) std::filesystem::create_directories(L"artifacts/demo");
    const std::filesystem::path databasePath(demo ? L"artifacts/demo/mfc.sqlite" : L"DeviceLinkStudio.sqlite");
    DeviceLink::Application::DeviceRuntime runtime(
        std::make_unique<DeviceLink::Infrastructure::SqliteEventRepository>(
            databasePath),
        std::vector<std::uint16_t>{
            DeviceLink::Application::VirtualGimbalService::kTelemetryMessageType},
        500, CurrentUnixMilliseconds);

    constexpr auto deviceId = "unreal-gimbal-01";
    if (!runtime.Devices().RegisterDevice(deviceId) ||
        !runtime.Devices().AttachTransport(
            deviceId, std::make_unique<DeviceLink::Transport::TcpTransport>()))
    {
        return 1;
    }
    DeviceLink::Application::VirtualGimbalService gimbalService(runtime.Devices(), deviceId);
    DeviceLink::Infrastructure::AtomicTextFileStore scenarioFileStore;
    DeviceLink::Infrastructure::SqliteEventRepository replayRepository(databasePath);
    DeviceLink::Application::SqliteFrameReplaySource replaySource(replayRepository);
    DeviceLink::Application::OperatorWorkflowService workflowService(
        runtime.Devices(), scenarioFileStore, replaySource, [&runtime] { runtime.FlushEventLog(); });
    DeviceLink::Application::EventLogQueryService eventLogQueryService(
        replayRepository, [&runtime] { runtime.FlushEventLog(); });
    DeviceLink::Application::OperatorSettingsService settingsService(
        scenarioFileStore, demo ? L"artifacts/demo/mfc.settings" : L"DeviceLinkStudio.settings");
    if (demo)
    {
        DeviceLink::Application::OperatorSettings initial;
        initial.scenarioPath = (std::filesystem::current_path() / L"scenarios/unreal-gimbal-demo.dls").string();
        if (!settingsService.Save(initial).succeeded) return 1;
    }

    DeviceLink::Application::DeviceAgentService requestAgent(runtime.Devices(), gimbalService);
    DeviceLink::Application::OperatorRequestService requestService([&requestAgent](const auto& step) {
        using Action = DeviceLink::Application::OperatorRequestAction;
        switch (step.action)
        {
        case Action::Status:
        case Action::Diagnose: return requestAgent.GetTelemetry();
        case Action::PowerOn: return requestAgent.SetPower(true);
        case Action::PowerOff: return requestAgent.SetPower(false);
        case Action::Initialize: return requestAgent.Initialize();
        case Action::Move: return requestAgent.SetGimbalAngle(step.pan, step.tilt);
        case Action::ClearFault: return requestAgent.ClearFault();
        }
        return DeviceLink::Application::AgentOperationResult{false, "Unsupported action"};
    });
    DeviceLink::UI::MfcDashboardFrame dashboard(
        runtime, gimbalService, workflowService, eventLogQueryService, settingsService, requestService);
    if (!dashboard.CreateDashboard())
    {
        return 1;
    }

    application.m_pMainWnd = &dashboard;
    const int showCommand = commandShow == SW_HIDE ? SW_SHOWNORMAL : commandShow;
    if (!demoHidden)
    {
        dashboard.ShowWindow(showCommand);
        dashboard.UpdateWindow();
        dashboard.SetForegroundWindow();
    }
    if (demo)
    {
        // Dispatch the existing operator action on the UI thread. The action uses
        // Application services; the launcher never accesses MFC controls.
        dashboard.SendMessage(WM_COMMAND, DeviceLink::UI::MfcDashboardFrame::ConnectButtonId);
        const bool connected = gimbalService.ConnectionState().value_or(
            DeviceLink::Core::ConnectionState::Disconnected) == DeviceLink::Core::ConnectionState::Connected;
        const std::string ready = "{\"pid\":" + std::to_string(::GetCurrentProcessId()) +
            ",\"connected\":" + (connected ? "true}" : "false}");
        static_cast<void>(scenarioFileStore.WriteAtomically(L"artifacts/demo/mfc-ready.json", ready));
    }
    MSG message{};
    while (GetMessage(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    runtime.FlushEventLog();
    return static_cast<int>(message.wParam);
}
