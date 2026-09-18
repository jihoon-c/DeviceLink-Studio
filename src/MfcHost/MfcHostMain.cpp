#include "DeviceLink/Application/DeviceRuntime.h"
#include "DeviceLink/Application/EventLogQueryService.h"
#include "DeviceLink/Application/OperatorWorkflowService.h"
#include "DeviceLink/Application/OperatorSettingsService.h"
#include "DeviceLink/Application/SqliteFrameReplaySource.h"
#include "DeviceLink/Application/VirtualGimbalService.h"
#include "DeviceLink/Infrastructure/AtomicTextFileStore.h"
#include "DeviceLink/Infrastructure/SqliteEventRepository.h"
#include "DeviceLink/Transport/TcpTransport.h"
#include "DeviceLink/UI/MfcDashboardFrame.h"

#include <afxwin.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
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

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int commandShow)
{
    // Per-monitor V2 keeps Win32 non-client areas and child controls sharp when
    // the dashboard moves between displays with different scale factors.
    static_cast<void>(::SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    MfcHostApplication application;

    if (!AfxWinInit(instance, nullptr, GetCommandLine(), 0))
    {
        return 1;
    }

    const std::filesystem::path databasePath(L"DeviceLinkStudio.sqlite");
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
        scenarioFileStore, L"DeviceLinkStudio.settings");

    DeviceLink::UI::MfcDashboardFrame dashboard(
        runtime, gimbalService, workflowService, eventLogQueryService, settingsService);
    if (!dashboard.CreateDashboard())
    {
        return 1;
    }

    application.m_pMainWnd = &dashboard;
    dashboard.ShowWindow(commandShow);
    dashboard.UpdateWindow();
    MSG message{};
    while (GetMessage(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    runtime.FlushEventLog();
    return static_cast<int>(message.wParam);
}
