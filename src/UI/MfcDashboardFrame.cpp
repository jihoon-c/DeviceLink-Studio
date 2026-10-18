#include "DeviceLink/UI/MfcDashboardFrame.h"

#include <afxdlgs.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace DeviceLink::UI
{
namespace
{
const wchar_t* ConnectionStateText(Core::ConnectionState state) noexcept
{
    switch (state)
    {
    case Core::ConnectionState::Disconnected: return L"연결 안 됨";
    case Core::ConnectionState::Connecting: return L"연결 중";
    case Core::ConnectionState::Connected: return L"연결됨";
    case Core::ConnectionState::Disconnecting: return L"연결 해제 중";
    case Core::ConnectionState::Faulted: return L"통신 오류";
    }
    return L"알 수 없음";
}

const wchar_t* GimbalStateText(Application::VirtualGimbalState state) noexcept
{
    switch (state)
    {
    case Application::VirtualGimbalState::Offline: return L"Offline";
    case Application::VirtualGimbalState::Ready: return L"Ready";
    case Application::VirtualGimbalState::Initializing: return L"Initializing";
    case Application::VirtualGimbalState::Scanning: return L"Scanning";
    case Application::VirtualGimbalState::Fault: return L"Fault";
    }
    return L"Unknown";
}

const wchar_t* FaultText(Application::VirtualGimbalFault fault) noexcept
{
    switch (fault)
    {
    case Application::VirtualGimbalFault::None: return L"None";
    case Application::VirtualGimbalFault::MotorStall: return L"Motor Stall";
    case Application::VirtualGimbalFault::OverTemperature: return L"Over Temperature";
    case Application::VirtualGimbalFault::SensorFailure: return L"Sensor Failure";
    }
    return L"Unknown";
}

const wchar_t* HeartbeatStateText(Application::TelemetryHeartbeatState state) noexcept
{
    switch (state)
    {
    case Application::TelemetryHeartbeatState::Inactive: return L"Inactive";
    case Application::TelemetryHeartbeatState::Waiting: return L"Waiting";
    case Application::TelemetryHeartbeatState::Healthy: return L"Healthy";
    case Application::TelemetryHeartbeatState::Warning: return L"Warning";
    case Application::TelemetryHeartbeatState::Fault: return L"Fault";
    }
    return L"Unknown";
}

CString PacketDescription(
    const PacketMonitorEntry& packet,
    const Application::VirtualGimbalService& gimbalService)
{
    CString description;
    const wchar_t* direction = packet.direction == PacketDirection::Sent ? L"TX" : L"RX";
    if (const auto acknowledgement =
            gimbalService.DecodeDeviceAcknowledgement(
                packet.messageType, packet.payload))
    {
        description.Format(L"%s ACK  seq=%u  command=0x%04X  %s", direction, packet.sequence,
            acknowledgement->commandType, acknowledgement->succeeded ? L"SUCCESS" : L"FAILED");
    }
    else if (packet.messageType == gimbalService.DeviceTelemetryMessageType())
    {
        description.Format(L"%s TELEMETRY  seq=%u  payload=%zu bytes", direction, packet.sequence,
            packet.payloadSize);
    }
    else
    {
        description.Format(L"%s TYPE=0x%04X  seq=%u  payload=%zu bytes", direction,
            packet.messageType, packet.sequence, packet.payloadSize);
    }
    return description;
}

const char* EventCategoryForSelection(int selection) noexcept
{
    switch (selection)
    {
    case 1: return "connection-state";
    case 2: return "frame-sent";
    case 3: return "frame-replayed";
    case 4: return "frame-received";
    case 5: return "transport-error";
    case 6: return "telemetry-heartbeat";
    default: return "";
    }
}

std::string ToUtf8(const CString& value)
{
    if (value.IsEmpty())
    {
        return {};
    }
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, value.GetString(), value.GetLength(),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return {};
    }
    std::string converted(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, value.GetString(), value.GetLength(), converted.data(),
        size, nullptr, nullptr);
    return converted;
}

CString FromUtf8(const std::string& value)
{
    if (value.empty())
    {
        return {};
    }
    const int size = ::MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
    {
        return {};
    }
    std::wstring converted(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), converted.data(), size);
    return CString(converted.data(), size);
}

void SetControlTextIfChanged(CWnd& control, const CString& text)
{
    CString current;
    control.GetWindowText(current);
    if (current != text)
    {
        control.SetWindowText(text);
    }
}

void SetControlEnabledIfChanged(CWnd& control, bool enabled)
{
    if ((control.IsWindowEnabled() != FALSE) != enabled)
    {
        control.EnableWindow(enabled);
    }
}
} // namespace

BEGIN_MESSAGE_MAP(MfcDashboardFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_WM_PAINT()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_WM_TIMER()
    ON_MESSAGE(WM_DPICHANGED, &MfcDashboardFrame::OnDpiChanged)
    ON_BN_CLICKED(MfcDashboardFrame::ConnectButtonId, &MfcDashboardFrame::OnConnect)
    ON_BN_CLICKED(MfcDashboardFrame::DisconnectButtonId, &MfcDashboardFrame::OnDisconnect)
    ON_BN_CLICKED(MfcDashboardFrame::PowerOnButtonId, &MfcDashboardFrame::OnPowerOn)
    ON_BN_CLICKED(MfcDashboardFrame::PowerOffButtonId, &MfcDashboardFrame::OnPowerOff)
    ON_BN_CLICKED(MfcDashboardFrame::InitializeButtonId, &MfcDashboardFrame::OnInitializeDevice)
    ON_BN_CLICKED(MfcDashboardFrame::SetTargetButtonId, &MfcDashboardFrame::OnSetTarget)
    ON_BN_CLICKED(MfcDashboardFrame::StartScanButtonId, &MfcDashboardFrame::OnStartScan)
    ON_BN_CLICKED(MfcDashboardFrame::StopScanButtonId, &MfcDashboardFrame::OnStopScan)
    ON_BN_CLICKED(MfcDashboardFrame::RequestStatusButtonId, &MfcDashboardFrame::OnRequestStatus)
    ON_BN_CLICKED(MfcDashboardFrame::InjectFaultButtonId, &MfcDashboardFrame::OnInjectFault)
    ON_BN_CLICKED(MfcDashboardFrame::ClearFaultButtonId, &MfcDashboardFrame::OnClearFault)
    ON_BN_CLICKED(MfcDashboardFrame::ApplyResponseModeButtonId,
        &MfcDashboardFrame::OnApplyResponseMode)
    ON_BN_CLICKED(MfcDashboardFrame::ApplyHeartbeatSettingsButtonId,
        &MfcDashboardFrame::OnApplyHeartbeatSettings)
    ON_BN_CLICKED(MfcDashboardFrame::BrowseScenarioButtonId, &MfcDashboardFrame::OnBrowseScenario)
    ON_BN_CLICKED(MfcDashboardFrame::StartScenarioButtonId, &MfcDashboardFrame::OnStartScenario)
    ON_BN_CLICKED(MfcDashboardFrame::StopWorkflowButtonId, &MfcDashboardFrame::OnStopWorkflow)
    ON_BN_CLICKED(MfcDashboardFrame::StartReplayButtonId, &MfcDashboardFrame::OnStartReplay)
    ON_BN_CLICKED(MfcDashboardFrame::AcknowledgeAlarmButtonId, &MfcDashboardFrame::OnAcknowledgeAlarm)
    ON_BN_CLICKED(MfcDashboardFrame::RefreshHistoryButtonId, &MfcDashboardFrame::OnRefreshHistory)
    ON_BN_CLICKED(MfcDashboardFrame::AutomaticReconnectCheckId,
        &MfcDashboardFrame::OnToggleAutomaticReconnect)
    ON_CBN_SELCHANGE(MfcDashboardFrame::EquipmentModeComboId,
        &MfcDashboardFrame::OnEquipmentModeChanged)
    ON_BN_CLICKED(MfcDashboardFrame::VelocityModeButtonId,
        &MfcDashboardFrame::OnVelocityMode)
    ON_BN_CLICKED(MfcDashboardFrame::PositionModeButtonId,
        &MfcDashboardFrame::OnPositionMode)
    ON_BN_CLICKED(MfcDashboardFrame::CenterGimbalButtonId,
        &MfcDashboardFrame::OnCenterGimbal)
    ON_BN_CLICKED(MfcDashboardFrame::CameraViewCheckId,
        &MfcDashboardFrame::OnToggleCameraView)
    ON_MESSAGE(WM_APP + 101, &MfcDashboardFrame::OnDeviceEvent)
    ON_MESSAGE(WM_APP + 102, &MfcDashboardFrame::OnWorkflowProgress)
    ON_MESSAGE(WM_APP + 103, &MfcDashboardFrame::OnRequestProgress)
    ON_NOTIFY(TCN_SELCHANGE, MfcDashboardFrame::WorkspaceTabId, &MfcDashboardFrame::OnWorkspaceChanged)
    ON_BN_CLICKED(MfcDashboardFrame::PreviewRequestButtonId, &MfcDashboardFrame::OnPreviewRequest)
    ON_BN_CLICKED(MfcDashboardFrame::ExecuteRequestButtonId, &MfcDashboardFrame::OnExecuteRequest)
    ON_BN_CLICKED(MfcDashboardFrame::CancelRequestButtonId, &MfcDashboardFrame::OnCancelRequest)
    ON_BN_CLICKED(MfcDashboardFrame::DiagnoseRequestButtonId, &MfcDashboardFrame::OnDiagnoseRequest)
END_MESSAGE_MAP()

MfcDashboardFrame::MfcDashboardFrame(
    Application::DeviceRuntime& runtime,
    Application::VirtualGimbalService& gimbalService,
    Application::OperatorWorkflowService& workflowService,
    Application::EventLogQueryService& eventLogQueryService,
    Application::OperatorSettingsService& settingsService,
    Application::OperatorRequestService& requestService) noexcept
    : m_runtime(runtime),
      m_gimbalService(gimbalService),
      m_workflowService(workflowService),
      m_eventLogQueryService(eventLogQueryService),
      m_settingsService(settingsService), m_requestService(requestService)
{
}

MfcDashboardFrame::~MfcDashboardFrame()
{
    if (m_dashboardBinding)
    {
        m_dashboardBinding->Detach();
    }
}

bool MfcDashboardFrame::CreateDashboard()
{
    const UINT dpi = ::GetDpiForSystem();
    RECT windowRect{0, 0, ::MulDiv(1280, dpi, 96), ::MulDiv(780, dpi, 96)};
    constexpr DWORD style = WS_OVERLAPPEDWINDOW;
    ::AdjustWindowRectExForDpi(&windowRect, style, FALSE, 0, dpi);
    return Create(nullptr, L"DeviceLink Studio - Unreal Virtual Gimbal",
        style, CRect(::MulDiv(80, dpi, 96), ::MulDiv(30, dpi, 96),
            ::MulDiv(80, dpi, 96) + windowRect.right - windowRect.left,
            ::MulDiv(30, dpi, 96) + windowRect.bottom - windowRect.top)) != FALSE;
}

int MfcDashboardFrame::OnCreate(LPCREATESTRUCT createStructure)
{
    if (CFrameWnd::OnCreate(createStructure) == -1 || !CreateControls())
    {
        return -1;
    }
    m_dpi = ::GetDpiForWindow(GetSafeHwnd());
    UpdateFonts();
    CRect client;
    GetClientRect(&client);
    LayoutControls(client.Width(), client.Height());
    LoadSettings();
    SetTimer(kJoystickTimerId, 50, nullptr);
    m_dashboardBinding = std::make_unique<DashboardBinding>(
        m_runtime.Devices().Events(), m_dashboardModel, GetSafeHwnd(), kDeviceEventMessage,
        [] {});
    if (!m_dashboardBinding->Attach())
    {
        return -1;
    }
    const HWND window = GetSafeHwnd();
    m_workflowService.Progress().SetWakeupHandler([window] {
        if (::IsWindow(window))
        {
            ::PostMessageW(window, kWorkflowProgressMessage, 0, 0);
        }
    });
    m_requestService.SetWakeupHandler([window] { ::PostMessageW(window, WM_APP + 103, 0, 0); });
    RefreshDashboardViews();
    RefreshEventHistory();
    return 0;
}

bool MfcDashboardFrame::CreateControls()
{
    constexpr DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;
    constexpr DWORD buttonStyle = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;
    if (!m_hostEdit.Create(editStyle, CRect(35, 105, 205, 132), this, 2101) ||
        !m_equipmentModeCombo.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
                CBS_DROPDOWNLIST, CRect(35, 170, 255, 285), this, EquipmentModeComboId) ||
        !m_portEdit.Create(editStyle | ES_NUMBER, CRect(215, 105, 295, 132), this, 2102) ||
        !m_connectButton.Create(L"연결", buttonStyle, CRect(310, 103, 405, 134), this,
            ConnectButtonId) ||
        !m_disconnectButton.Create(L"연결 해제", buttonStyle, CRect(415, 103, 525, 134), this,
            DisconnectButtonId) ||
        !m_connectionStatus.Create(L"연결 안 됨", WS_CHILD | WS_VISIBLE,
            CRect(540, 105, 655, 130), this) ||
        !m_automaticReconnectCheck.Create(L"자동 재접속", WS_CHILD | WS_VISIBLE |
                BS_AUTOCHECKBOX, CRect(310, 140, 430, 162), this, AutomaticReconnectCheckId) ||
        !m_reconnectStatus.Create(L"재접속 0 / 5", WS_CHILD | WS_VISIBLE,
            CRect(440, 142, 650, 162), this) ||
        !m_powerOnButton.Create(L"전원 ON", buttonStyle, CRect(35, 205, 135, 238), this,
            PowerOnButtonId) ||
        !m_powerOffButton.Create(L"전원 OFF", buttonStyle, CRect(145, 205, 245, 238), this,
            PowerOffButtonId) ||
        !m_initializeButton.Create(L"초기화", buttonStyle, CRect(255, 205, 355, 238), this,
            InitializeButtonId) ||
        !m_panEdit.Create(editStyle, CRect(385, 207, 460, 234), this, 2103) ||
        !m_tiltEdit.Create(editStyle, CRect(470, 207, 545, 234), this, 2104) ||
        !m_setTargetButton.Create(L"각도 적용", buttonStyle, CRect(555, 205, 655, 238), this,
            SetTargetButtonId) ||
        !m_startScanButton.Create(L"스캔 시작", buttonStyle, CRect(35, 250, 145, 283), this,
            StartScanButtonId) ||
        !m_stopScanButton.Create(L"스캔 정지", buttonStyle, CRect(155, 250, 265, 283), this,
            StopScanButtonId) ||
        !m_requestStatusButton.Create(L"상태 요청", buttonStyle, CRect(275, 250, 385, 283), this,
            RequestStatusButtonId) ||
        !m_injectFaultButton.Create(L"고장 주입", buttonStyle, CRect(395, 250, 505, 283), this,
            InjectFaultButtonId) ||
        !m_clearFaultButton.Create(L"고장 해제", buttonStyle, CRect(515, 250, 625, 283), this,
            ClearFaultButtonId) ||
        !m_faultCombo.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
                CBS_DROPDOWNLIST, CRect(680, 310, 815, 430), this, 2302) ||
        !m_responseModeCombo.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
                CBS_DROPDOWNLIST, CRect(680, 350, 810, 470), this, 2303) ||
        !m_responseDelayEdit.Create(editStyle | ES_NUMBER,
            CRect(820, 350, 890, 377), this, 2108) ||
        !m_applyResponseModeButton.Create(L"응답 설정", buttonStyle,
            CRect(930, 348, 1035, 380), this, ApplyResponseModeButtonId) ||
        !m_heartbeatWarningEdit.Create(editStyle | ES_NUMBER,
            CRect(680, 420, 750, 447), this, 2109) ||
        !m_heartbeatFaultEdit.Create(editStyle | ES_NUMBER,
            CRect(760, 420, 830, 447), this, 2110) ||
        !m_applyHeartbeatSettingsButton.Create(L"Heartbeat 적용", buttonStyle,
            CRect(840, 418, 1035, 450), this, ApplyHeartbeatSettingsButtonId) ||
        !m_cameraViewCheck.Create(L"카메라 뷰 보기", WS_CHILD | WS_VISIBLE |
                BS_AUTOCHECKBOX, CRect(680, 275, 830, 300), this, CameraViewCheckId) ||
        !m_scenarioPathEdit.Create(editStyle, CRect(680, 105, 940, 132), this, 2105) ||
        !m_browseScenarioButton.Create(L"찾기", buttonStyle, CRect(950, 103, 1035, 134), this,
            BrowseScenarioButtonId) ||
        !m_startScenarioButton.Create(L"시나리오 실행", buttonStyle,
            CRect(680, 145, 805, 178), this, StartScenarioButtonId) ||
        !m_stopWorkflowButton.Create(L"작업 중지", buttonStyle,
            CRect(815, 145, 920, 178), this, StopWorkflowButtonId) ||
        !m_startReplayButton.Create(L"송신 기록 Replay", buttonStyle,
            CRect(680, 235, 830, 268), this, StartReplayButtonId) ||
        !m_acknowledgeAlarmButton.Create(L"선택 알람 확인", buttonStyle,
            CRect(840, 235, 985, 268), this, AcknowledgeAlarmButtonId) ||
        !m_workflowStatus.Create(L"자동화 작업 대기", WS_CHILD | WS_VISIBLE,
            CRect(680, 183, 1035, 210), this) ||
        !m_historyDeviceEdit.Create(editStyle, CRect(35, 720, 210, 747), this, 2106) ||
        !m_historyCategoryCombo.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
                CBS_DROPDOWNLIST, CRect(220, 720, 405, 900), this, 2301) ||
        !m_historySearchEdit.Create(editStyle, CRect(415, 720, 690, 747), this, 2107) ||
        !m_refreshHistoryButton.Create(L"이력 새로고침", buttonStyle,
            CRect(700, 718, 825, 750), this, RefreshHistoryButtonId) ||
        !m_operationStatus.Create(L"Unreal 실행 후 연결하세요.", WS_CHILD | WS_VISIBLE,
            CRect(35, 295, 760, 320), this) ||
        !m_telemetryStatus.Create(L"Telemetry: 수신 대기", WS_CHILD | WS_VISIBLE | SS_LEFT,
            CRect(35, 355, 650, 445), this) ||
        !m_heartbeatStatus.Create(L"Heartbeat: Inactive", WS_CHILD | WS_VISIBLE | SS_LEFT,
            CRect(680, 390, 1035, 415), this) ||
        !m_packetList.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
            CRect(35, 495, 650, 665), this, 2201) ||
        !m_errorList.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
            CRect(680, 495, 1035, 665), this, 2202) ||
        !m_historyList.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL |
                LBS_NOINTEGRALHEIGHT, CRect(35, 760, 1035, 865), this, 2203))
    {
        return false;
    }
    if (!m_velocityModeButton.Create(L"속도 모드",
            WS_CHILD | WS_VISIBLE | WS_GROUP | BS_AUTORADIOBUTTON,
            CRect(395, 250, 475, 274), this, VelocityModeButtonId) ||
        !m_positionModeButton.Create(L"위치 모드",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            CRect(480, 250, 560, 274), this, PositionModeButtonId) ||
        !m_centerGimbalButton.Create(L"중앙 정렬", buttonStyle,
            CRect(395, 280, 475, 312), this, CenterGimbalButtonId) ||
        !m_joystickSpeedStatus.Create(L"20 Hz / 20 deg/s",
            WS_CHILD | WS_VISIBLE, CRect(480, 285, 565, 307), this) ||
        !m_joystick.Create(this, 2401, CRect(570, 245, 650, 325),
            [this](double panAxis, double tiltAxis, JoystickEvent event) {
                OnJoystickAxes(panAxis, tiltAxis, event);
            }))
    {
        return false;
    }
    if (!m_workspaceTabs.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP, CRect(0, 0, 10, 10), this, WorkspaceTabId) ||
        !m_requestEdit.Create(editStyle | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP,
            CRect(0, 0, 10, 10), this, 2502) ||
        !m_requestOutput.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            CRect(0, 0, 10, 10), this, 2503) ||
        !m_requestHelp.Create(L"예: 현재 상태 확인 / 고장 진단 / Pan 30 Tilt -10\n미리보기 후 실행하세요. 연결은 상단 버튼을 사용합니다.", WS_CHILD | WS_VISIBLE,
            CRect(0, 0, 10, 10), this) ||
        !m_previewRequestButton.Create(L"미리보기", buttonStyle | WS_TABSTOP, CRect(0, 0, 10, 10), this, PreviewRequestButtonId) ||
        !m_executeRequestButton.Create(L"실행", buttonStyle | WS_TABSTOP, CRect(0, 0, 10, 10), this, ExecuteRequestButtonId) ||
        !m_cancelRequestButton.Create(L"요청 중지", buttonStyle | WS_TABSTOP, CRect(0, 0, 10, 10), this, CancelRequestButtonId) ||
        !m_diagnoseRequestButton.Create(L"현재 상태 진단", buttonStyle | WS_TABSTOP, CRect(0, 0, 10, 10), this, DiagnoseRequestButtonId)) return false;
    m_workspaceTabs.InsertItem(0, L"운용");
    m_workspaceTabs.InsertItem(1, L"시험 / 설정");
    m_workspaceTabs.InsertItem(2, L"이력 / 패킷");
    m_workspaceTabs.SetCurSel(0);
    m_requestEdit.SetLimitText(1024);
    m_requestEdit.SetWindowText(L"현재 상태 확인");
    m_requestOutput.SetLimitText(50000);
    m_requestOutput.SetWindowText(L"규칙 기반 관측 진단 · LLM 미사용\r\n\r\n자연어 요청은 같은 장비 연결과 Application 서비스를 사용합니다.");
    m_velocityModeButton.SetCheck(BST_CHECKED);
    m_joystick.SetMode(JoystickMode::Velocity);
    m_hostEdit.SetWindowText(L"127.0.0.1");
    m_portEdit.SetWindowText(L"5000");
    m_panEdit.SetWindowText(L"45.0");
    m_tiltEdit.SetWindowText(L"10.0");
    m_scenarioPathEdit.SetWindowText(L"..\\scenarios\\unreal-gimbal-demo.dls");
    m_historyDeviceEdit.SetWindowText(L"unreal-gimbal-01");
    m_historyCategoryCombo.AddString(L"전체 종류");
    m_equipmentModeCombo.AddString(L"고정카메라 관제 모드");
    m_equipmentModeCombo.AddString(L"드론 관제 모드");
    m_equipmentModeCombo.SetCurSel(0);
    m_historyCategoryCombo.AddString(L"연결 상태");
    m_historyCategoryCombo.AddString(L"송신 프레임");
    m_historyCategoryCombo.AddString(L"Replay 프레임");
    m_historyCategoryCombo.AddString(L"수신 프레임");
    m_historyCategoryCombo.AddString(L"통신 오류");
    m_historyCategoryCombo.AddString(L"Telemetry Heartbeat");
    m_historyCategoryCombo.SetCurSel(0);
    m_faultCombo.AddString(L"Motor Stall");
    m_faultCombo.AddString(L"Over Temperature");
    m_faultCombo.AddString(L"Sensor Failure");
    m_faultCombo.SetCurSel(0);
    m_responseModeCombo.AddString(L"정상 응답");
    m_responseModeCombo.AddString(L"응답 지연");
    m_responseModeCombo.AddString(L"무응답");
    m_responseModeCombo.SetCurSel(0);
    m_responseDelayEdit.SetWindowText(L"1000");
    m_heartbeatWarningEdit.SetWindowText(L"1500");
    m_heartbeatFaultEdit.SetWindowText(L"3000");
    m_automaticReconnectCheck.SetCheck(BST_CHECKED);
    UpdateEquipmentModeControls();
    m_historyList.SetHorizontalExtent(1400);
    UpdateCommandAvailability();
    return true;
}

int MfcDashboardFrame::Scale(int logicalPixels) const noexcept
{
    return ::MulDiv(logicalPixels, static_cast<int>(m_dpi), 96);
}

int MfcDashboardFrame::LayoutX(int logicalX, int clientWidth) const noexcept
{
    return ::MulDiv(logicalX, clientWidth, 1280);
}

void MfcDashboardFrame::UpdateFonts()
{
    m_headingFont.DeleteObject();
    m_sectionFont.DeleteObject();
    m_bodyFont.DeleteObject();
    static_cast<void>(m_headingFont.CreateFontW(-::MulDiv(18, m_dpi, 72), 0, 0, 0,
        FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"));
    static_cast<void>(m_sectionFont.CreateFontW(-::MulDiv(11, m_dpi, 72), 0, 0, 0,
        FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"));
    static_cast<void>(m_bodyFont.CreateFontW(-::MulDiv(10, m_dpi, 72), 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"));

    std::vector<CWnd*> controls{&m_hostEdit, &m_portEdit, &m_panEdit,
             &m_tiltEdit, &m_scenarioPathEdit, &m_historyDeviceEdit, &m_historySearchEdit,
             &m_responseDelayEdit, &m_heartbeatWarningEdit, &m_heartbeatFaultEdit,
             &m_historyCategoryCombo, &m_equipmentModeCombo, &m_faultCombo,
             &m_responseModeCombo, &m_connectButton, &m_disconnectButton, &m_powerOnButton,
             &m_powerOffButton, &m_initializeButton, &m_setTargetButton, &m_startScanButton,
             &m_stopScanButton, &m_requestStatusButton, &m_injectFaultButton,
             &m_clearFaultButton, &m_browseScenarioButton, &m_startScenarioButton,
             &m_stopWorkflowButton, &m_startReplayButton, &m_acknowledgeAlarmButton,
             &m_refreshHistoryButton, &m_automaticReconnectCheck, &m_operationStatus,
             &m_workflowStatus, &m_reconnectStatus, &m_heartbeatStatus,
             &m_packetList, &m_errorList,
             &m_historyList, &m_applyResponseModeButton};
    controls.push_back(&m_applyHeartbeatSettingsButton);
    controls.push_back(&m_velocityModeButton);
    controls.push_back(&m_positionModeButton);
    controls.push_back(&m_centerGimbalButton);
    controls.push_back(&m_cameraViewCheck);
    controls.insert(controls.end(), {&m_workspaceTabs, &m_requestEdit, &m_requestOutput, &m_requestHelp,
        &m_previewRequestButton, &m_executeRequestButton, &m_cancelRequestButton, &m_diagnoseRequestButton});
    controls.push_back(&m_joystickSpeedStatus);
    for (CWnd* control : controls)
    {
        control->SetFont(&m_bodyFont);
    }
    m_connectionStatus.SetFont(&m_sectionFont);
    m_telemetryStatus.SetFont(&m_sectionFont);
}

void MfcDashboardFrame::LayoutControls(int width, int height)
{
    if (!::IsWindow(m_hostEdit.GetSafeHwnd()) || width <= 0 || height <= 0) return;
    const auto x = [this, width](int value) { return LayoutX(value, width); };
    const auto y = [this](int value) { return Scale(value); };
    const auto move = [&](CWnd& control, int left, int top, int right, int bottom) {
        control.SetWindowPos(nullptr, x(left), y(top), x(right)-x(left), y(bottom)-y(top), SWP_NOACTIVATE | SWP_NOZORDER);
    };
    const int logicalHeight = ::MulDiv(height, 96, static_cast<int>(m_dpi));
    const int tab = m_workspaceTabs.GetCurSel();
    const auto show = [](std::initializer_list<CWnd*> controls, bool visible) {
        for (auto* control : controls) control->ShowWindow(visible ? SW_SHOWNA : SW_HIDE);
    };
    show({&m_equipmentModeCombo, &m_powerOnButton, &m_powerOffButton, &m_initializeButton,
        &m_panEdit, &m_tiltEdit, &m_setTargetButton, &m_startScanButton, &m_stopScanButton,
        &m_requestStatusButton, &m_velocityModeButton, &m_positionModeButton, &m_centerGimbalButton,
        &m_joystick, &m_joystickSpeedStatus, &m_cameraViewCheck}, tab == 0);
    show({&m_scenarioPathEdit, &m_browseScenarioButton, &m_startScenarioButton, &m_stopWorkflowButton,
        &m_startReplayButton, &m_acknowledgeAlarmButton, &m_workflowStatus, &m_faultCombo,
        &m_injectFaultButton, &m_clearFaultButton, &m_responseModeCombo, &m_responseDelayEdit,
        &m_applyResponseModeButton, &m_heartbeatWarningEdit, &m_heartbeatFaultEdit, &m_applyHeartbeatSettingsButton}, tab == 1);
    show({&m_historyDeviceEdit, &m_historyCategoryCombo, &m_historySearchEdit, &m_refreshHistoryButton,
        &m_historyList, &m_packetList, &m_errorList}, tab == 2);
    move(m_hostEdit, 35, 82, 230, 110); move(m_portEdit, 240, 82, 310, 110);
    move(m_connectButton, 325, 80, 415, 112); move(m_disconnectButton, 425, 80, 525, 112);
    move(m_automaticReconnectCheck, 550, 84, 670, 110); move(m_reconnectStatus, 690, 85, 840, 110);
    move(m_connectionStatus, 380, 30, 680, 58);
    move(m_telemetryStatus, 35, 132, 1235, 176); move(m_heartbeatStatus, 35, 181, 1235, 203);
    move(m_workspaceTabs, 28, 216, 845, 247);
    move(m_operationStatus, 40, logicalHeight-65, 825, logicalHeight-20);
    move(m_requestEdit, 880, 263, 1245, 340);
    move(m_previewRequestButton, 880, 350, 990, 385); move(m_executeRequestButton, 1000, 350, 1100, 385);
    move(m_cancelRequestButton, 1110, 350, 1245, 385);
    move(m_diagnoseRequestButton, 880, 398, 1245, 433);
    move(m_requestHelp, 880, 446, 1245, 505);
    move(m_requestOutput, 880, 516, 1245, logicalHeight-25);
    move(m_equipmentModeCombo, 50, 290, 350, 420);
    move(m_powerOnButton, 50, 345, 165, 382); move(m_powerOffButton, 175, 345, 290, 382);
    move(m_initializeButton, 300, 345, 415, 382);
    move(m_panEdit, 50, 430, 160, 462); move(m_tiltEdit, 180, 430, 290, 462);
    move(m_setTargetButton, 305, 429, 420, 464);
    move(m_velocityModeButton, 460, 291, 570, 319); move(m_positionModeButton, 580, 291, 690, 319);
    move(m_joystick, 535, 335, 710, 510); move(m_centerGimbalButton, 460, 530, 575, 565);
    move(m_joystickSpeedStatus, 585, 536, 805, 565);
    move(m_startScanButton, 50, 505, 165, 542); move(m_stopScanButton, 175, 505, 290, 542);
    move(m_requestStatusButton, 305, 505, 420, 542); move(m_cameraViewCheck, 50, 565, 360, 595);
    move(m_scenarioPathEdit, 50, 288, 675, 318); move(m_browseScenarioButton, 690, 286, 805, 320);
    move(m_startScenarioButton, 50, 333, 205, 369); move(m_stopWorkflowButton, 215, 333, 355, 369);
    move(m_startReplayButton, 365, 333, 580, 369); move(m_acknowledgeAlarmButton, 590, 333, 805, 369);
    move(m_workflowStatus, 50, 379, 805, 407);
    move(m_faultCombo, 50, 453, 325, 580); move(m_injectFaultButton, 340, 452, 505, 488);
    move(m_clearFaultButton, 515, 452, 680, 488);
    move(m_responseModeCombo, 50, 528, 325, 655); move(m_responseDelayEdit, 340, 528, 440, 558);
    move(m_applyResponseModeButton, 515, 526, 680, 562);
    move(m_heartbeatWarningEdit, 50, 608, 170, 638); move(m_heartbeatFaultEdit, 185, 608, 305, 638);
    move(m_applyHeartbeatSettingsButton, 340, 606, 590, 642);
    move(m_historyDeviceEdit, 50, 292, 270, 322); move(m_historyCategoryCombo, 285, 292, 465, 455);
    move(m_historySearchEdit, 480, 292, 675, 322); move(m_refreshHistoryButton, 690, 290, 805, 324);
    const int middle = 345 + (logicalHeight-100-345)*55/100;
    move(m_historyList, 50, 345, 805, middle);
    move(m_packetList, 50, middle+35, 420, logicalHeight-90);
    move(m_errorList, 435, middle+35, 805, logicalHeight-90);
    m_historyList.SetHorizontalExtent(x(1400));
}

void MfcDashboardFrame::OnSize(UINT type, int width, int height)
{
    CFrameWnd::OnSize(type, width, height);
    if (type != SIZE_MINIMIZED)
    {
        LayoutControls(width, height);
        Invalidate(FALSE);
    }
}

void MfcDashboardFrame::OnGetMinMaxInfo(MINMAXINFO* minMaxInfo)
{
    CFrameWnd::OnGetMinMaxInfo(minMaxInfo);
    RECT minimum{0, 0, Scale(1120), Scale(720)};
    ::AdjustWindowRectExForDpi(&minimum, GetStyle(), FALSE, GetExStyle(), m_dpi);
    minMaxInfo->ptMinTrackSize.x = minimum.right - minimum.left;
    minMaxInfo->ptMinTrackSize.y = minimum.bottom - minimum.top;
}

LRESULT MfcDashboardFrame::OnDpiChanged(WPARAM wParam, LPARAM lParam)
{
    m_dpi = HIWORD(wParam);
    UpdateFonts();
    const auto* suggested = reinterpret_cast<const RECT*>(lParam);
    SetWindowPos(nullptr, suggested->left, suggested->top,
        suggested->right - suggested->left, suggested->bottom - suggested->top,
        SWP_NOACTIVATE | SWP_NOZORDER);
    CRect client;
    GetClientRect(&client);
    LayoutControls(client.Width(), client.Height());
    Invalidate(FALSE);
    return 0;
}

void MfcDashboardFrame::OnDestroy()
{
    KillTimer(kJoystickTimerId);
    m_requestService.SetWakeupHandler({});
    m_requestService.Stop();
    SaveSettings();
    m_workflowService.Progress().SetWakeupHandler({});
    m_workflowService.Stop();
    if (m_cameraViewCheck.GetCheck() == BST_CHECKED)
    {
        static_cast<void>(m_gimbalService.SetCameraView(false));
    }
    static_cast<void>(m_gimbalService.Disconnect());
    if (m_dashboardBinding)
    {
        m_dashboardBinding->Detach();
        m_dashboardBinding.reset();
    }
    CFrameWnd::OnDestroy();
    PostQuitMessage(0);
}

void MfcDashboardFrame::OnPaint()
{
    CPaintDC dc(this);
    CRect client; GetClientRect(&client);
    const auto x = [this, &client](int value) { return LayoutX(value, client.Width()); };
    const auto y = [this](int value) { return Scale(value); };
    dc.FillSolidRect(client, RGB(242, 246, 250));
    dc.FillSolidRect(CRect(x(25), y(250), x(845), client.bottom-y(80)), RGB(255,255,255));
    dc.FillSolidRect(CRect(x(865), y(216), x(1260), client.bottom-y(15)), RGB(255,255,255));
    dc.SetBkMode(TRANSPARENT); dc.SetTextColor(RGB(25,39,58));
    auto* previous = dc.SelectObject(&m_headingFont);
    dc.TextOut(x(35), y(24), L"DeviceLink Studio");
    dc.SelectObject(&m_sectionFont);
    dc.TextOut(x(880), y(226), L"자연어 요청 / 관측 진단");
    dc.SelectObject(&m_bodyFont); dc.SetTextColor(RGB(75,90,108));
    dc.TextOut(x(35), y(60), L"장비 주소"); dc.TextOut(x(240), y(60), L"Port");
    const int tab = m_workspaceTabs.GetCurSel();
    if (tab == 0)
    {
        dc.TextOut(x(50), y(263), L"운용 모드"); dc.TextOut(x(460), y(263), L"짐벌 조이스틱");
        dc.TextOut(x(50), y(403), L"Pan / X 좌표"); dc.TextOut(x(180), y(403), L"Tilt / Y 좌표");
        dc.TextOut(x(50), y(610), L"명령 전송과 실제 도달 확인은 요청 패널에서 구분합니다.");
    }
    else if (tab == 1)
    {
        dc.TextOut(x(50), y(263), L"시나리오 / Record & Replay");
        dc.TextOut(x(50), y(425), L"가상 고장 주입 / 해제");
        dc.TextOut(x(50), y(503), L"응답 모드 / 지연 시간 (ms)");
        dc.TextOut(x(50), y(581), L"Heartbeat Warning / Fault (ms)");
    }
    else
    {
        dc.TextOut(x(50), y(263), L"Device ID"); dc.TextOut(x(285), y(263), L"분류"); dc.TextOut(x(480), y(263), L"검색");
        const int logicalHeight = ::MulDiv(client.Height(),96,static_cast<int>(m_dpi));
        const int middle = 345+(logicalHeight-100-345)*55/100;
        dc.TextOut(x(50), y(middle+10), L"패킷 상세"); dc.TextOut(x(435), y(middle+10), L"오류 / 알람");
    }
    dc.SelectObject(previous);
}

void MfcDashboardFrame::OnWorkspaceChanged(NMHDR*, LRESULT* result)
{
    CRect client; GetClientRect(&client);
    LayoutControls(client.Width(), client.Height());
    Invalidate(FALSE);
    *result = 0;
}

void MfcDashboardFrame::OnPreviewRequest()
{
    CString input; m_requestEdit.GetWindowText(input);
    const auto plan = Application::OperatorRequestService::Plan(std::wstring(input.GetString()));
    m_requestOutput.SetWindowText(plan.description.c_str());
    m_previewedInstruction = plan.valid ? input : CString();
}

void MfcDashboardFrame::OnExecuteRequest()
{
    if (m_requestService.IsRunning() || m_workflowService.IsRunning()) return;
    if (m_equipmentModeCombo.GetCurSel() == 1)
    { m_requestOutput.SetWindowText(L"자연어 운용은 현재 고정형 짐벌 모드에서 지원합니다."); return; }
    CString input; m_requestEdit.GetWindowText(input);
    if (input != m_previewedInstruction)
    {
        OnPreviewRequest();
        if (!m_previewedInstruction.IsEmpty())
        { m_requestOutput.SetSel(-1, -1); m_requestOutput.ReplaceSel(L"\r\n\r\n계획을 확인한 뒤 실행을 누르세요."); }
        return;
    }
    const auto plan = Application::OperatorRequestService::Plan(std::wstring(input.GetString()));
    m_joystick.Reset(); m_joystickPanAxis = 0; m_joystickTiltAxis = 0; m_positionTargetDirty = false;
    if (m_requestService.Submit(plan))
    {
        m_requestOutput.SetWindowText((plan.description + L"\r\n\r\n[처리 중] 실제 장비 응답을 기다립니다…\r\n").c_str());
        m_previewedInstruction.Empty();
    }
    UpdateCommandAvailability();
}

void MfcDashboardFrame::OnCancelRequest()
{
    m_requestService.Cancel();
    m_requestOutput.SetSel(-1,-1);
    m_requestOutput.ReplaceSel(L"\r\n중지를 요청했습니다. 현재 전송한 동작은 응답/시간 초과 후 종료합니다.\r\n");
}

void MfcDashboardFrame::OnDiagnoseRequest()
{
    if (m_requestService.IsRunning() || m_workflowService.IsRunning()) return;
    m_requestEdit.SetWindowText(L"고장 진단");
    OnPreviewRequest();
    OnExecuteRequest();
}

LRESULT MfcDashboardFrame::OnRequestProgress(WPARAM, LPARAM)
{
    for (const auto& message : m_requestService.Drain())
    {
        if (m_requestOutput.GetWindowTextLength() > 45000) m_requestOutput.SetWindowText(L"이전 표시를 정리했습니다.\r\n");
        m_requestOutput.SetSel(-1,-1);
        m_requestOutput.ReplaceSel((L"\r\n" + message + L"\r\n").c_str());
    }
    RefreshDashboardViews();
    return 0;
}

void MfcDashboardFrame::OnConnect()
{
    CString hostText;
    m_hostEdit.GetWindowText(hostText);
    const auto port = ReadPort();
    if (hostText.IsEmpty() || !port)
    {
        SetOperationStatus(L"Host와 1~65535 범위의 Port를 확인하세요.", false);
        return;
    }
    const CStringA hostNarrow(hostText);
    const bool connected = m_gimbalService.Connect(hostNarrow.GetString(), *port);
    if (connected)
    {
        const bool droneMode = m_equipmentModeCombo.GetCurSel() == 1;
        static_cast<void>(m_gimbalService.SelectEquipmentMode(
            droneMode ? Application::VirtualEquipmentMode::Drone
                      : Application::VirtualEquipmentMode::FixedCamera));
    }
    const auto reconnect = m_gimbalService.GetReconnectStatus();
    SetOperationStatus(connected ? L"Unreal 가상장비에 연결했습니다."
        : reconnect.enabled && reconnect.connectionDesired
            ? L"최초 연결 실패: 설정된 정책으로 자동 재접속을 시도합니다."
            : L"연결 실패: Unreal에서 Play 후 시뮬레이션을 시작했는지 확인하세요.", connected);
    RefreshDashboardViews();
}

void MfcDashboardFrame::OnDisconnect()
{
    if (m_cameraViewCheck.GetCheck() == BST_CHECKED)
    {
        static_cast<void>(m_gimbalService.SetCameraView(false));
        m_cameraViewCheck.SetCheck(BST_UNCHECKED);
    }
    const bool disconnected = m_gimbalService.Disconnect();
    if (disconnected)
    {
        m_powerEnabled = false;
        m_joystick.Reset();
        UpdateEquipmentModeControls();
    }
    SetOperationStatus(disconnected ? L"연결을 안전하게 해제했습니다."
                                    : L"해제할 연결이 없습니다.", disconnected);
    RefreshDashboardViews();
}

void MfcDashboardFrame::OnPowerOn()
{
    const bool sent = m_gimbalService.Power(true);
    SetOperationStatus(sent
            ? L"전원 ON 명령을 전송했습니다. Telemetry 확인을 기다립니다."
            : L"전원 ON 명령 전송에 실패했습니다.", sent);
}

void MfcDashboardFrame::OnPowerOff()
{
    const bool sent = m_gimbalService.Power(false);
    if (sent)
    {
        m_powerEnabled = false;
        m_joystick.Reset();
        UpdateEquipmentModeControls();
    }
    SetOperationStatus(L"전원 OFF 명령을 전송했습니다.", sent);
}

void MfcDashboardFrame::OnInitializeDevice()
{
    const bool sent = m_gimbalService.Initialize();
    SetOperationStatus(L"초기화 명령을 전송했습니다.", sent);
}

void MfcDashboardFrame::OnSetTarget()
{
    const auto pan = ReadAngle(m_panEdit);
    const auto tilt = ReadAngle(m_tiltEdit);
    if (!pan || !tilt)
    {
        SetOperationStatus(L"Pan/Tilt에 숫자를 입력하세요.", false);
        return;
    }
    const bool droneMode = m_equipmentModeCombo.GetCurSel() == 1;
    const bool sent = droneMode ? m_gimbalService.DroneMoveTo(*pan, *tilt, 10.0)
                                : m_gimbalService.SetPanTilt(*pan, *tilt);
    SetOperationStatus(sent ? (droneMode ? L"드론 목표 좌표(X/Y, 고도 10 m)를 전송했습니다."
                                         : L"목표 각도 명령을 전송했습니다.")
        : (droneMode ? L"드론 좌표는 X/Y 숫자 범위에서 입력하세요."
                     : L"각도 범위: Pan −170~170, Tilt −45~80"), sent);
}

void MfcDashboardFrame::OnVelocityMode()
{
    m_velocityMode = true;
    m_positionTargetDirty = false;
    m_joystickPanAxis = 0.0;
    m_joystickTiltAxis = 0.0;
    m_lastJoystickTick = std::chrono::steady_clock::now();
    m_joystick.SetMode(JoystickMode::Velocity);
    UpdateJoystickModeControls();
}

void MfcDashboardFrame::OnPositionMode()
{
    m_velocityMode = false;
    m_positionTargetDirty = false;
    m_joystick.SetMode(JoystickMode::Position);
    UpdateJoystickModeControls();
}

void MfcDashboardFrame::OnCenterGimbal()
{
    m_panEdit.SetWindowText(L"0.0");
    m_tiltEdit.SetWindowText(L"0.0");
    m_positionTargetDirty = false;
    const bool sent = m_gimbalService.SetPanTilt(0.0, 0.0);
    SetOperationStatus(L"짐벌 중앙 위치를 전송했습니다.", sent);
}

void MfcDashboardFrame::OnJoystickAxes(
    double panAxis, double tiltAxis, JoystickEvent event)
{
    m_joystickPanAxis = panAxis;
    m_joystickTiltAxis = tiltAxis;

    if (!m_velocityMode)
    {
        const double pan = (std::clamp)(panAxis * 170.0,
            Application::VirtualGimbalService::kMinimumPanDegrees,
            Application::VirtualGimbalService::kMaximumPanDegrees);
        const double tilt = tiltAxis >= 0.0 ? tiltAxis * 80.0 : tiltAxis * 45.0;
        CString panText;
        CString tiltText;
        panText.Format(L"%.1f", pan);
        tiltText.Format(L"%.1f", tilt);
        m_panEdit.SetWindowText(panText);
        m_tiltEdit.SetWindowText(tiltText);
        m_positionTargetDirty = true;
    }

    if (event == JoystickEvent::Released)
    {
        const auto pan = ReadAngle(m_panEdit);
        const auto tilt = ReadAngle(m_tiltEdit);
        if (pan && tilt)
        {
            const bool sent = m_gimbalService.SetPanTilt(*pan, *tilt);
            SetOperationStatus(L"조이스틱의 마지막 목표 위치를 확정했습니다.", sent);
        }
        m_positionTargetDirty = false;
    }
}

void MfcDashboardFrame::UpdateJoystickModeControls()
{
    m_velocityModeButton.SetCheck(m_velocityMode ? BST_CHECKED : BST_UNCHECKED);
    m_positionModeButton.SetCheck(m_velocityMode ? BST_UNCHECKED : BST_CHECKED);
    m_joystickSpeedStatus.SetWindowText(
        m_velocityMode ? L"20 Hz / 20 deg/s" : L"20 Hz / 절대 위치");
}

void MfcDashboardFrame::OnStartScan()
{
    const bool droneMode = m_equipmentModeCombo.GetCurSel() == 1;
    const bool sent = droneMode ? m_gimbalService.DroneTakeOff() : m_gimbalService.StartScan();
    SetOperationStatus(droneMode ? L"드론 이륙 명령을 전송했습니다." : L"스캔 시작 명령을 전송했습니다.", sent);
}

void MfcDashboardFrame::OnStopScan()
{
    const bool droneMode = m_equipmentModeCombo.GetCurSel() == 1;
    const bool sent = droneMode ? m_gimbalService.DroneLand() : m_gimbalService.StopScan();
    SetOperationStatus(droneMode ? L"드론 착륙 명령을 전송했습니다." : L"스캔 정지 명령을 전송했습니다.", sent);
}

void MfcDashboardFrame::OnRequestStatus()
{
    const bool droneMode = m_equipmentModeCombo.GetCurSel() == 1;
    const bool sent = droneMode ? m_gimbalService.DroneReturnHome() : m_gimbalService.RequestStatus();
    SetOperationStatus(droneMode ? L"드론 귀환 명령을 전송했습니다." : L"상태 요청 명령을 전송했습니다.", sent);
}

void MfcDashboardFrame::OnEquipmentModeChanged()
{
    const bool droneMode = m_equipmentModeCombo.GetCurSel() == 1;
    const bool sent = m_gimbalService.SelectEquipmentMode(
        droneMode ? Application::VirtualEquipmentMode::Drone
                  : Application::VirtualEquipmentMode::FixedCamera);
    UpdateEquipmentModeControls();
    SetOperationStatus(sent ? (droneMode ? L"드론 관제 모드를 요청했습니다."
                                          : L"고정카메라 관제 모드를 요청했습니다.")
                            : L"연결 후 운용 모드를 적용하세요.", sent);
}

void MfcDashboardFrame::OnToggleCameraView()
{
    const bool enabled = m_cameraViewCheck.GetCheck() == BST_CHECKED;
    const bool sent = m_gimbalService.SetCameraView(enabled);
    if (!sent)
    {
        m_cameraViewCheck.SetCheck(enabled ? BST_UNCHECKED : BST_CHECKED);
    }
    SetOperationStatus(sent
            ? (enabled ? L"Unreal 시점을 장비 카메라로 전환했습니다."
                       : L"Unreal 시점을 현장 카메라로 복귀했습니다.")
            : L"연결 후 카메라 뷰를 전환하세요.",
        sent);
}

void MfcDashboardFrame::UpdateEquipmentModeControls()
{
    const bool droneMode = m_equipmentModeCombo.GetCurSel() == 1;
    m_initializeButton.SetWindowText(droneMode ? L"페이로드 초기화" : L"초기화");
    m_setTargetButton.SetWindowText(droneMode ? L"좌표 이동" : L"각도 적용");
    m_startScanButton.SetWindowText(droneMode ? L"이륙" : L"스캔 시작");
    m_stopScanButton.SetWindowText(droneMode ? L"착륙" : L"스캔 정지");
    m_requestStatusButton.SetWindowText(droneMode ? L"귀환" : L"상태 요청");
    m_panEdit.SetWindowText(droneMode ? L"0" : L"45.0");
    m_tiltEdit.SetWindowText(droneMode ? L"0" : L"10.0");
    const bool connected = m_gimbalService.ConnectionState().value_or(
        Core::ConnectionState::Disconnected) == Core::ConnectionState::Connected;
    const BOOL joystickEnabled = !droneMode && connected && m_powerEnabled;
    m_joystick.EnableWindow(joystickEnabled);
    m_velocityModeButton.EnableWindow(!droneMode);
    m_positionModeButton.EnableWindow(!droneMode);
    m_centerGimbalButton.EnableWindow(joystickEnabled);
    UpdateJoystickModeControls();
}

void MfcDashboardFrame::OnInjectFault()
{
    const int selection = m_faultCombo.GetCurSel();
    if (selection == CB_ERR || selection > 2)
    {
        SetOperationStatus(L"주입할 장비 고장을 선택하세요.", false);
        return;
    }
    const auto fault = static_cast<Application::VirtualGimbalFault>(selection + 1);
    const bool sent = m_gimbalService.InjectFault(fault);
    const wchar_t* names[] = {L"Motor Stall", L"Over Temperature", L"Sensor Failure"};
    CString message;
    message.Format(L"%s 고장을 주입했습니다.", names[selection]);
    SetOperationStatus(message, sent);
}

void MfcDashboardFrame::OnClearFault()
{
    const bool sent = m_gimbalService.InjectFault(Application::VirtualGimbalFault::None);
    SetOperationStatus(L"장비 고장을 해제했습니다.", sent);
}

void MfcDashboardFrame::OnApplyResponseMode()
{
    const int selection = m_responseModeCombo.GetCurSel();
    if (selection == CB_ERR || selection > 2)
    {
        SetOperationStatus(L"통신 응답 모드를 선택하세요.", false);
        return;
    }
    std::uint16_t delay{};
    if (selection == 1)
    {
        CString text;
        m_responseDelayEdit.GetWindowText(text);
        wchar_t* end{};
        errno = 0;
        const auto value = std::wcstoul(text.GetString(), &end, 10);
        if (errno != 0 || end == text.GetString() || *end != L'\0' || value == 0 || value > 65535)
        {
            SetOperationStatus(L"응답 지연은 1~65535 ms로 입력하세요.", false);
            return;
        }
        delay = static_cast<std::uint16_t>(value);
    }
    const auto mode = static_cast<Application::VirtualGimbalResponseMode>(selection);
    const bool sent = m_gimbalService.ConfigureResponse(mode, delay);
    const wchar_t* messages[] = {
        L"정상 응답 모드로 복구했습니다.",
        L"응답 지연 모드를 적용했습니다.",
        L"무응답 모드를 적용했습니다. 복구 명령은 항상 처리됩니다.",
    };
    SetOperationStatus(messages[selection], sent);
}

void MfcDashboardFrame::OnApplyHeartbeatSettings()
{
    const auto warningMilliseconds = ReadHeartbeatMilliseconds(m_heartbeatWarningEdit);
    const auto faultMilliseconds = ReadHeartbeatMilliseconds(m_heartbeatFaultEdit);
    if (!warningMilliseconds || !faultMilliseconds || *warningMilliseconds < 100 ||
        *faultMilliseconds <= *warningMilliseconds)
    {
        SetOperationStatus(L"Heartbeat은 Warning 100 ms 이상, Fault는 Warning보다 크게 입력하세요.", false);
        return;
    }

    try
    {
        auto options = m_runtime.HeartbeatWatchdog().Options();
        options.warningAfter = std::chrono::milliseconds(*warningMilliseconds);
        options.faultAfter = std::chrono::milliseconds(*faultMilliseconds);
        m_runtime.SetHeartbeatOptions(options);
        SetOperationStatus(L"Heartbeat 경고/장애 임계값을 적용했습니다.", true);
    }
    catch (const std::exception& exception)
    {
        CString message;
        message.Format(L"Heartbeat 설정 실패: %S", exception.what());
        SetOperationStatus(message, false);
    }
}

void MfcDashboardFrame::OnBrowseScenario()
{
    CFileDialog dialog(TRUE, L"dls", nullptr,
        OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
        L"DeviceLink Scenario (*.dls)|*.dls|Text Files (*.txt)|*.txt|All Files (*.*)|*.*||",
        this);
    if (dialog.DoModal() == IDOK)
    {
        m_scenarioPathEdit.SetWindowText(dialog.GetPathName());
    }
}

void MfcDashboardFrame::OnStartScenario()
{
    CString path;
    m_scenarioPathEdit.GetWindowText(path);
    const auto result = m_workflowService.StartScenario(std::filesystem::path(path.GetString()));
    CString status;
    if (result.succeeded)
    {
        status = L"[실행 중] 시나리오를 시작했습니다.";
    }
    else if (result.errorLineNumber)
    {
        status.Format(L"[실패] Line %zu: %S", *result.errorLineNumber, result.error.c_str());
    }
    else
    {
        status.Format(L"[실패] %S", result.error.c_str());
    }
    m_workflowStatus.SetWindowText(status);
    UpdateCommandAvailability();
}

void MfcDashboardFrame::OnStopWorkflow()
{
    m_workflowService.Stop();
    m_workflowStatus.SetWindowText(L"[중지] 자동화 작업을 중지했습니다.");
    UpdateCommandAvailability();
}

void MfcDashboardFrame::OnStartReplay()
{
    const auto result = m_workflowService.StartReplay(m_gimbalService.DeviceId());
    CString status;
    if (result.succeeded)
    {
        status = L"[실행 중] 저장된 송신 명령을 Replay합니다.";
    }
    else
    {
        status.Format(L"[실패] %S", result.error.c_str());
    }
    m_workflowStatus.SetWindowText(status);
    UpdateCommandAvailability();
}

void MfcDashboardFrame::OnAcknowledgeAlarm()
{
    const int selection = m_errorList.GetCurSel();
    if (selection == LB_ERR || static_cast<std::size_t>(selection) >= m_alarmIdsByListIndex.size() ||
        m_alarmIdsByListIndex[selection] == 0)
    {
        SetOperationStatus(L"확인 처리할 ALARM 항목을 선택하세요.", false);
        return;
    }
    const bool acknowledged = m_runtime.Alarms().Acknowledge(m_alarmIdsByListIndex[selection]);
    SetOperationStatus(L"선택한 알람을 확인 처리했습니다.", acknowledged);
    RefreshDashboardViews();
}

void MfcDashboardFrame::OnRefreshHistory()
{
    RefreshEventHistory();
}

void MfcDashboardFrame::OnToggleAutomaticReconnect()
{
    const bool enabled = m_automaticReconnectCheck.GetCheck() == BST_CHECKED;
    m_gimbalService.SetAutomaticReconnectEnabled(enabled);
    SetOperationStatus(enabled ? L"자동 재접속을 활성화했습니다."
                               : L"자동 재접속을 비활성화했습니다.", true);
    RefreshDashboardViews();
}

void MfcDashboardFrame::OnTimer(UINT_PTR timerId)
{
    const auto now = std::chrono::steady_clock::now();
    double joystickTickSeconds = 0.05;
    if (m_lastJoystickTick != std::chrono::steady_clock::time_point{})
    {
        joystickTickSeconds = (std::clamp)(
            std::chrono::duration<double>(now - m_lastJoystickTick).count(), 0.0, 0.25);
    }
    m_lastJoystickTick = now;

    if (timerId == kJoystickTimerId && m_powerEnabled && !m_requestService.IsRunning() &&
        m_equipmentModeCombo.GetCurSel() != 1)
    {
        if (m_velocityMode &&
            (std::abs(m_joystickPanAxis) > 0.08 ||
             std::abs(m_joystickTiltAxis) > 0.08))
        {
            constexpr double speedDegreesPerSecond = 20.0;
            const double pan = ReadAngle(m_panEdit).value_or(0.0);
            const double tilt = ReadAngle(m_tiltEdit).value_or(0.0);
            const double targetPan = (std::clamp)(
                pan + m_joystickPanAxis * speedDegreesPerSecond * joystickTickSeconds,
                Application::VirtualGimbalService::kMinimumPanDegrees,
                Application::VirtualGimbalService::kMaximumPanDegrees);
            const double targetTilt = (std::clamp)(
                tilt + m_joystickTiltAxis * speedDegreesPerSecond * joystickTickSeconds,
                Application::VirtualGimbalService::kMinimumTiltDegrees,
                Application::VirtualGimbalService::kMaximumTiltDegrees);
            CString panText;
            CString tiltText;
            panText.Format(L"%.1f", targetPan);
            tiltText.Format(L"%.1f", targetTilt);
            m_panEdit.SetWindowText(panText);
            m_tiltEdit.SetWindowText(tiltText);
            if (!m_gimbalService.SetPanTilt(targetPan, targetTilt))
            {
                m_joystickPanAxis = 0.0;
                m_joystickTiltAxis = 0.0;
                m_joystick.Reset();
                SetOperationStatus(L"조이스틱 명령 전송에 실패했습니다.", false);
            }
        }
        else if (!m_velocityMode && m_positionTargetDirty)
        {
            const auto pan = ReadAngle(m_panEdit);
            const auto tilt = ReadAngle(m_tiltEdit);
            if (pan && tilt)
            {
                if (!m_gimbalService.SetPanTilt(*pan, *tilt))
                {
                    m_joystickPanAxis = 0.0;
                    m_joystickTiltAxis = 0.0;
                    m_joystick.Reset();
                    SetOperationStatus(L"조이스틱 명령 전송에 실패했습니다.", false);
                }
            }
            m_positionTargetDirty = false;
        }
    }
    CFrameWnd::OnTimer(timerId);
}

LRESULT MfcDashboardFrame::OnDeviceEvent(WPARAM, LPARAM)
{
    if (!m_dashboardBinding || !m_dashboardBinding->HandleMessage(kDeviceEventMessage))
    {
        return 1;
    }
    RefreshDashboardViews();
    return 0;
}

LRESULT MfcDashboardFrame::OnWorkflowProgress(WPARAM, LPARAM)
{
    const auto events = m_workflowService.Progress().Drain();
    for (const auto& progress : events)
    {
        CString status;
        if (progress.kind == Application::ScenarioProgressKind::Started)
        {
            status.Format(L"[실행 중] 0 / %zu 단계", progress.totalStepCount);
        }
        else if (progress.kind == Application::ScenarioProgressKind::StepCompleted)
        {
            status.Format(L"[실행 중] %zu / %zu 단계", progress.completedStepCount,
                progress.totalStepCount);
        }
        else if (progress.status == Application::ScenarioStatus::Completed)
        {
            status.Format(L"[완료] %zu / %zu 단계", progress.completedStepCount,
                progress.totalStepCount);
        }
        else if (progress.status == Application::ScenarioStatus::Cancelled)
        {
            status.Format(L"[중지] %zu / %zu 단계", progress.completedStepCount,
                progress.totalStepCount);
        }
        else
        {
            status.Format(L"[실패] Step %zu: %S", progress.failedStepIndex.value_or(0) + 1,
                progress.failureMessage.c_str());
        }
        if (progress.reportPath)
        {
            status += L"\r\n[리포트] ";
            status += progress.reportPath->c_str();
        }
        else if (!progress.reportError.empty())
        {
            status += L"\r\n[리포트 실패] ";
            CString reportError;
            reportError.Format(L"%S", progress.reportError.c_str());
            status += reportError;
        }
        m_workflowStatus.SetWindowText(status);
    }
    UpdateCommandAvailability();
    return 0;
}

void MfcDashboardFrame::RefreshDashboardViews()
{
    const auto state = m_gimbalService.ConnectionState().value_or(Core::ConnectionState::Disconnected);
    SetControlTextIfChanged(m_connectionStatus, ConnectionStateText(state));
    const auto reconnect = m_gimbalService.GetReconnectStatus();
    CString reconnectText;
    if (!reconnect.enabled)
    {
        reconnectText = L"재접속 꺼짐";
    }
    else if (reconnect.exhausted)
    {
        reconnectText.Format(L"재접속 실패 %u / %u", reconnect.attemptCount,
            reconnect.maximumAttempts);
    }
    else
    {
        reconnectText.Format(L"재접속 %u / %u", reconnect.attemptCount,
            reconnect.maximumAttempts);
    }
    SetControlTextIfChanged(m_reconnectStatus, reconnectText);
    m_packetList.SetRedraw(FALSE);
    m_packetList.ResetContent();
    const auto packets = m_dashboardModel.Packets();
    const auto firstPacket = packets.size() > 100 ? packets.size() - 100 : 0;
    for (std::size_t index = firstPacket; index < packets.size(); ++index)
    {
        m_packetList.AddString(PacketDescription(packets[index], m_gimbalService));
    }
    if (m_packetList.GetCount() > 0)
    {
        m_packetList.SetCurSel(m_packetList.GetCount() - 1);
    }
    m_packetList.SetRedraw(TRUE);
    m_packetList.Invalidate(FALSE);
    m_errorList.SetRedraw(FALSE);
    m_errorList.ResetContent();
    m_alarmIdsByListIndex.clear();
    for (const auto& error : m_dashboardModel.Errors())
    {
        CString text;
        text.Format(L"%S: %S", error.deviceId.c_str(), error.message.c_str());
        m_errorList.AddString(text);
        m_alarmIdsByListIndex.push_back(0);
    }
    for (const auto& alarm : m_runtime.Alarms().ActiveAlarms())
    {
        CString text;
        text.Format(alarm.severity == Application::AlarmSeverity::Critical
                ? L"CRITICAL: %S" : L"WARNING: %S",
            alarm.message.c_str());
        m_errorList.AddString(text);
        m_alarmIdsByListIndex.push_back(alarm.id);
    }
    m_errorList.SetRedraw(TRUE);
    m_errorList.Invalidate(FALSE);
    CString telemetryText(L"Telemetry: 수신 대기");
    for (auto item = packets.rbegin(); item != packets.rend(); ++item)
    {
        if (item->direction != PacketDirection::Received)
        {
            continue;
        }
        const auto telemetry = m_gimbalService.DecodeDeviceTelemetry(
            item->messageType, item->payload);
        if (!telemetry)
        {
            continue;
        }
        m_powerEnabled = telemetry->state != Application::VirtualGimbalState::Offline;
        telemetryText.Format(L"State: %s    Fault: %s    Pan/Tilt: %.2f° / %.2f°    Target: %.2f° / %.2f°\r\nTemp: %.2f°C    Voltage: %.3f V",
            GimbalStateText(telemetry->state), FaultText(telemetry->fault),
            telemetry->panDegrees, telemetry->tiltDegrees,
            telemetry->targetPanDegrees, telemetry->targetTiltDegrees,
            telemetry->temperatureCelsius, telemetry->supplyVoltage);
        break;
    }
    if (const auto quality = m_runtime.TelemetryQuality().MetricsFor(m_gimbalService.DeviceId()))
    {
        CString qualityText;
        qualityText.Format(
            L"\r\nQuality: RX %llu    Delivery %.1f%%    Interval %lld ms / Avg %lld ms    Jitter %lld ms    Missing %llu    Out-of-order %llu",
            static_cast<unsigned long long>(quality->receivedFrameCount), quality->deliveryRatePercent,
            quality->lastIntervalMilliseconds, quality->averageIntervalMilliseconds,
            quality->averageJitterMilliseconds,
            static_cast<unsigned long long>(quality->estimatedMissingFrameCount),
            static_cast<unsigned long long>(quality->outOfOrderFrameCount));
        telemetryText += qualityText;
    }
    SetControlTextIfChanged(m_telemetryStatus, telemetryText);
    const auto heartbeat = m_runtime.HeartbeatWatchdog().StatusFor(m_gimbalService.DeviceId());
    CString heartbeatText;
    heartbeatText.Format(L"Heartbeat: %s    마지막 수신 %lld ms 전",
        HeartbeatStateText(heartbeat.state), heartbeat.ageMilliseconds);
    SetControlTextIfChanged(m_heartbeatStatus, heartbeatText);
    UpdateCommandAvailability();
    InvalidateMetrics();
}

void MfcDashboardFrame::InvalidateMetrics()
{
    CRect client;
    GetClientRect(&client);
    const auto x = [this, &client](int value) { return LayoutX(value, client.Width()); };
    const auto y = [this](int value) { return Scale(value); };
    InvalidateRect(CRect(x(675), y(18), x(1070), y(55)), FALSE);
}

void MfcDashboardFrame::UpdateCommandAvailability()
{
    const auto state = m_gimbalService.ConnectionState().value_or(Core::ConnectionState::Disconnected);
    const bool connected = state == Core::ConnectionState::Connected;
    const bool workflowRunning = m_workflowService.IsRunning();
    const bool requestRunning = m_requestService.IsRunning();
    m_connectButton.EnableWindow(state == Core::ConnectionState::Disconnected);
    m_disconnectButton.EnableWindow(state != Core::ConnectionState::Disconnected);
    for (CButton* button : {&m_powerOnButton, &m_powerOffButton, &m_initializeButton,
             &m_setTargetButton, &m_startScanButton, &m_stopScanButton,
             &m_requestStatusButton, &m_injectFaultButton, &m_clearFaultButton,
             &m_applyResponseModeButton})
    {
        button->EnableWindow(connected);
    }
    m_faultCombo.EnableWindow(connected);
    m_responseModeCombo.EnableWindow(connected);
    m_responseDelayEdit.EnableWindow(connected);
    m_cameraViewCheck.EnableWindow(connected);
    if (!connected)
    {
        m_cameraViewCheck.SetCheck(BST_UNCHECKED);
    }
    m_startScenarioButton.EnableWindow(!workflowRunning);
    m_startReplayButton.EnableWindow(connected && !workflowRunning);
    m_stopWorkflowButton.EnableWindow(workflowRunning);
    m_acknowledgeAlarmButton.EnableWindow(!m_runtime.Alarms().ActiveAlarms().empty());
    if (!connected)
    {
        m_powerEnabled = false;
    }
    const bool fixedCameraMode = m_equipmentModeCombo.GetCurSel() != 1;
    SetControlEnabledIfChanged(m_joystick,
        connected && fixedCameraMode && m_powerEnabled);
    SetControlEnabledIfChanged(m_centerGimbalButton,
        connected && fixedCameraMode && m_powerEnabled);
    m_previewRequestButton.EnableWindow(!requestRunning);
    m_requestEdit.EnableWindow(!requestRunning);
    m_executeRequestButton.EnableWindow(connected && fixedCameraMode && !requestRunning && !workflowRunning);
    m_diagnoseRequestButton.EnableWindow(connected && fixedCameraMode && !requestRunning && !workflowRunning);
    m_cancelRequestButton.EnableWindow(requestRunning);
    m_equipmentModeCombo.EnableWindow(!requestRunning && !workflowRunning);
    if (requestRunning)
    {
        for (CWnd* control : std::initializer_list<CWnd*>{&m_connectButton, &m_disconnectButton,
                 &m_powerOnButton, &m_powerOffButton, &m_initializeButton, &m_setTargetButton,
                 &m_startScanButton, &m_stopScanButton, &m_requestStatusButton, &m_injectFaultButton,
                 &m_clearFaultButton, &m_applyResponseModeButton, &m_cameraViewCheck,
                 &m_startScenarioButton, &m_startReplayButton, &m_joystick, &m_centerGimbalButton,
                 &m_velocityModeButton, &m_positionModeButton}) control->EnableWindow(FALSE);
    }
}

void MfcDashboardFrame::RefreshEventHistory()
{
    CString deviceText;
    CString searchText;
    m_historyDeviceEdit.GetWindowText(deviceText);
    m_historySearchEdit.GetWindowText(searchText);
    const CStringA deviceNarrow(deviceText);
    const CStringA searchNarrow(searchText);

    try
    {
        const auto entries = m_eventLogQueryService.LoadRecent({
            .deviceId = deviceNarrow.GetString(),
            .category = EventCategoryForSelection(m_historyCategoryCombo.GetCurSel()),
            .containsText = searchNarrow.GetString(),
            .maximumEntryCount = 200,
        });
        m_historyList.SetRedraw(FALSE);
        m_historyList.ResetContent();
        for (const auto& entry : entries)
        {
            const CTime timestamp(static_cast<__time64_t>(entry.timestampUnixMilliseconds / 1000));
            const CString timeText = timestamp.Format(L"%Y-%m-%d %H:%M:%S");
            CString row;
            row.Format(L"%s | %S | %S | %S | %zu B", timeText.GetString(),
                entry.deviceId.c_str(), entry.category.c_str(), entry.detail.c_str(),
                entry.payloadByteCount);
            m_historyList.AddString(row);
        }
        m_historyList.SetRedraw(TRUE);
        m_historyList.Invalidate(FALSE);
        CString status;
        status.Format(L"[OK] 이벤트 이력 %zu건을 조회했습니다.", entries.size());
        m_operationStatus.SetWindowText(status);
    }
    catch (const std::exception& exception)
    {
        m_historyList.SetRedraw(TRUE);
        CString status;
        status.Format(L"[실패] 이벤트 이력 조회: %S", exception.what());
        m_operationStatus.SetWindowText(status);
    }
}

void MfcDashboardFrame::LoadSettings()
{
    const auto loaded = m_settingsService.Load();
    if (!loaded.loaded)
    {
        return;
    }
    m_hostEdit.SetWindowText(FromUtf8(loaded.settings.host));
    CString port;
    port.Format(L"%u", loaded.settings.port);
    m_portEdit.SetWindowText(port);
    m_automaticReconnectCheck.SetCheck(
        loaded.settings.automaticReconnect ? BST_CHECKED : BST_UNCHECKED);
    m_gimbalService.SetAutomaticReconnectEnabled(loaded.settings.automaticReconnect);
    m_scenarioPathEdit.SetWindowText(FromUtf8(loaded.settings.scenarioPath));
    m_historyDeviceEdit.SetWindowText(FromUtf8(loaded.settings.historyDeviceId));
    m_historyCategoryCombo.SetCurSel(loaded.settings.historyCategoryIndex);
    m_historySearchEdit.SetWindowText(FromUtf8(loaded.settings.historySearchText));
    CString warning;
    CString fault;
    warning.Format(L"%u", loaded.settings.heartbeatWarningMilliseconds);
    fault.Format(L"%u", loaded.settings.heartbeatFaultMilliseconds);
    m_heartbeatWarningEdit.SetWindowText(warning);
    m_heartbeatFaultEdit.SetWindowText(fault);
    auto options = m_runtime.HeartbeatWatchdog().Options();
    options.warningAfter = std::chrono::milliseconds(loaded.settings.heartbeatWarningMilliseconds);
    options.faultAfter = std::chrono::milliseconds(loaded.settings.heartbeatFaultMilliseconds);
    m_runtime.SetHeartbeatOptions(options);
}

void MfcDashboardFrame::SaveSettings() noexcept
{
    try
    {
        CString host;
        CString scenarioPath;
        CString historyDevice;
        CString historySearch;
        const auto runtimeOptions = m_runtime.HeartbeatWatchdog().Options();
        m_hostEdit.GetWindowText(host);
        m_scenarioPathEdit.GetWindowText(scenarioPath);
        m_historyDeviceEdit.GetWindowText(historyDevice);
        m_historySearchEdit.GetWindowText(historySearch);
        const int category = m_historyCategoryCombo.GetCurSel();
        static_cast<void>(m_settingsService.Save({
            .host = ToUtf8(host),
            .port = ReadPort().value_or(5000),
            .automaticReconnect = m_automaticReconnectCheck.GetCheck() == BST_CHECKED,
            .scenarioPath = ToUtf8(scenarioPath),
            .historyDeviceId = ToUtf8(historyDevice),
            .historyCategoryIndex = static_cast<std::uint8_t>(category == CB_ERR ? 0 : category),
            .historySearchText = ToUtf8(historySearch),
            .heartbeatWarningMilliseconds = static_cast<std::uint32_t>(
                runtimeOptions.warningAfter.count()),
            .heartbeatFaultMilliseconds = static_cast<std::uint32_t>(
                runtimeOptions.faultAfter.count()),
        }));
    }
    catch (...)
    {
        // Window shutdown must continue even when preferences cannot be stored.
    }
}

void MfcDashboardFrame::SetOperationStatus(const wchar_t* message, bool succeeded)
{
    CString text;
    text.Format(L"%s %s", succeeded ? L"[OK]" : L"[실패]", message);
    m_operationStatus.SetWindowText(text);
}

std::optional<std::uint16_t> MfcDashboardFrame::ReadPort() const
{
    CString text;
    m_portEdit.GetWindowText(text);
    wchar_t* end{};
    errno = 0;
    const auto value = std::wcstoul(text.GetString(), &end, 10);
    if (errno != 0 || end == text.GetString() || *end != L'\0' || value == 0 || value > 65535)
    {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(value);
}

std::optional<double> MfcDashboardFrame::ReadAngle(const CEdit& edit) const
{
    CString text;
    edit.GetWindowText(text);
    wchar_t* end{};
    errno = 0;
    const double value = std::wcstod(text.GetString(), &end);
    if (errno != 0 || end == text.GetString() || *end != L'\0')
    {
        return std::nullopt;
    }
    return value;
}

std::optional<std::uint32_t> MfcDashboardFrame::ReadHeartbeatMilliseconds(
    const CEdit& edit) const
{
    CString text;
    edit.GetWindowText(text);
    wchar_t* end{};
    errno = 0;
    const auto value = std::wcstoul(text.GetString(), &end, 10);
    if (errno != 0 || end == text.GetString() || *end != L'\0' || value > 600000)
    {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}
} // namespace DeviceLink::UI
