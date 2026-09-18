#include "DeviceLink/UI/MfcDashboardFrame.h"

#include <afxdlgs.h>

#include <cerrno>
#include <algorithm>
#include <chrono>
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
} // namespace

BEGIN_MESSAGE_MAP(MfcDashboardFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_WM_PAINT()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
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
    ON_MESSAGE(WM_APP + 101, &MfcDashboardFrame::OnDeviceEvent)
    ON_MESSAGE(WM_APP + 102, &MfcDashboardFrame::OnWorkflowProgress)
END_MESSAGE_MAP()

MfcDashboardFrame::MfcDashboardFrame(
    Application::DeviceRuntime& runtime,
    Application::VirtualGimbalService& gimbalService,
    Application::OperatorWorkflowService& workflowService,
    Application::EventLogQueryService& eventLogQueryService,
    Application::OperatorSettingsService& settingsService) noexcept
    : m_runtime(runtime),
      m_gimbalService(gimbalService),
      m_workflowService(workflowService),
      m_eventLogQueryService(eventLogQueryService),
      m_settingsService(settingsService)
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
    RECT windowRect{0, 0, ::MulDiv(1100, dpi, 96), ::MulDiv(900, dpi, 96)};
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
    m_dashboardBinding = std::make_unique<DashboardBinding>(
        m_runtime.Devices().Events(), m_dashboardModel, GetSafeHwnd(), kDeviceEventMessage,
        [this] { Invalidate(FALSE); });
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
    return ::MulDiv(logicalX, clientWidth, 1100);
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
    static_cast<void>(m_bodyFont.CreateFontW(-::MulDiv(9, m_dpi, 72), 0, 0, 0,
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
    for (CWnd* control : controls)
    {
        control->SetFont(&m_bodyFont);
    }
    m_connectionStatus.SetFont(&m_sectionFont);
    m_telemetryStatus.SetFont(&m_sectionFont);
}

void MfcDashboardFrame::LayoutControls(int clientWidth, int clientHeight)
{
    if (!::IsWindow(m_hostEdit.GetSafeHwnd()) || clientWidth <= 0 || clientHeight <= 0)
    {
        return;
    }
    const auto x = [this, clientWidth](int value) { return LayoutX(value, clientWidth); };
    const auto y = [this](int value) { return Scale(value); };
    const int extraHeight = std::max(0, clientHeight - y(900));
    const int historyTitleY = y(685) + extraHeight / 2;
    const int historyFilterY = historyTitleY + y(35);
    const int historyListY = historyTitleY + y(75);
    const int packetListBottom = historyTitleY - y(20);
    const int contentBottom = clientHeight - y(35);
    const auto move = [&](CWnd& control, int left, int top, int right, int bottom) {
        control.SetWindowPos(nullptr, x(left), top, x(right) - x(left), bottom - top,
            SWP_NOACTIVATE | SWP_NOZORDER);
    };

    move(m_hostEdit, 35, y(105), 205, y(132));
    move(m_portEdit, 215, y(105), 295, y(132));
    move(m_connectButton, 310, y(103), 405, y(134));
    move(m_disconnectButton, 415, y(103), 525, y(134));
    move(m_connectionStatus, 540, y(105), 655, y(130));
    move(m_automaticReconnectCheck, 310, y(140), 430, y(162));
    move(m_reconnectStatus, 440, y(142), 650, y(162));
    move(m_equipmentModeCombo, 35, y(170), 255, y(285));
    move(m_powerOnButton, 35, y(205), 135, y(238));
    move(m_powerOffButton, 145, y(205), 245, y(238));
    move(m_initializeButton, 255, y(205), 355, y(238));
    move(m_panEdit, 385, y(207), 460, y(234));
    move(m_tiltEdit, 470, y(207), 545, y(234));
    move(m_setTargetButton, 555, y(205), 655, y(238));
    move(m_startScanButton, 35, y(250), 145, y(283));
    move(m_stopScanButton, 155, y(250), 265, y(283));
    move(m_requestStatusButton, 275, y(250), 385, y(283));
    move(m_faultCombo, 680, y(310), 815, y(430));
    move(m_injectFaultButton, 825, y(308), 925, y(341));
    move(m_clearFaultButton, 935, y(308), 1035, y(341));
    move(m_responseModeCombo, 680, y(350), 810, y(470));
    move(m_responseDelayEdit, 820, y(350), 890, y(377));
    move(m_applyResponseModeButton, 930, y(348), 1035, y(380));
    move(m_scenarioPathEdit, 680, y(105), 940, y(132));
    move(m_browseScenarioButton, 950, y(103), 1035, y(134));
    move(m_startScenarioButton, 680, y(145), 805, y(178));
    move(m_stopWorkflowButton, 815, y(145), 920, y(178));
    move(m_workflowStatus, 680, y(183), 1035, y(210));
    move(m_startReplayButton, 680, y(235), 830, y(268));
    move(m_acknowledgeAlarmButton, 840, y(235), 985, y(268));
    move(m_operationStatus, 35, y(295), 650, y(320));
    move(m_telemetryStatus, 35, y(355), 650, y(445));
    move(m_heartbeatStatus, 680, y(390), 1035, y(415));
    move(m_heartbeatWarningEdit, 680, y(420), 750, y(447));
    move(m_heartbeatFaultEdit, 760, y(420), 830, y(447));
    move(m_applyHeartbeatSettingsButton, 840, y(418), 1035, y(450));
    move(m_packetList, 35, y(495), 650, packetListBottom);
    move(m_errorList, 680, y(495), 1035, packetListBottom);
    move(m_historyDeviceEdit, 35, historyFilterY, 210, historyFilterY + y(27));
    move(m_historyCategoryCombo, 220, historyFilterY, 405, historyFilterY + y(180));
    move(m_historySearchEdit, 415, historyFilterY, 690, historyFilterY + y(27));
    move(m_refreshHistoryButton, 700, historyFilterY - y(2), 825, historyFilterY + y(30));
    move(m_historyList, 35, historyListY, 1035, contentBottom);
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
    RECT minimum{0, 0, Scale(1100), Scale(900)};
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
    SaveSettings();
    m_workflowService.Progress().SetWakeupHandler({});
    m_workflowService.Stop();
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
    CRect client;
    GetClientRect(&client);
    dc.FillSolidRect(client, RGB(244, 247, 250));
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(RGB(25, 39, 58));
    auto* previousFont = dc.SelectObject(&m_headingFont);
    const auto x = [this, &client](int value) { return LayoutX(value, client.Width()); };
    const auto y = [this](int value) { return Scale(value); };
    const int extraHeight = std::max(0, client.Height() - y(900));
    const int historyTitleY = y(685) + extraHeight / 2;
    dc.TextOut(x(35), y(24), L"DeviceLink Studio");
    dc.SelectObject(&m_sectionFont);
    dc.TextOut(x(35), y(76), L"TCP 연결");
    dc.TextOut(x(35), y(166), L"운용 모드 / 가상장비 제어");
    dc.TextOut(x(35), y(335), L"실시간 장비 상태");
    dc.TextOut(x(35), y(465), L"수신 패킷");
    dc.TextOut(x(680), y(465), L"통신 오류 / 알람");
    dc.TextOut(x(680), y(76), L"시나리오 자동화");
    dc.TextOut(x(680), y(215), L"Replay / 알람 처리");
    dc.TextOut(x(680), y(280), L"장비 / 통신 장애 시뮬레이션");
    dc.TextOut(x(35), historyTitleY, L"SQLite 이벤트 이력");
    dc.SelectObject(previousFont);
    dc.SetTextColor(RGB(80, 94, 111));
    dc.TextOut(x(35), y(137), L"Host");
    dc.TextOut(x(215), y(137), L"Port");
    const bool droneMode = m_equipmentModeCombo.GetCurSel() == 1;
    dc.TextOut(x(385), y(184), droneMode ? L"X 좌표 m" : L"Pan (−170~170)");
    dc.TextOut(x(500), y(184), droneMode ? L"Y 좌표 m" : L"Tilt (−45~80)");
    dc.TextOut(x(895), y(353), L"ms");
    dc.TextOut(x(680), y(405), L"Warning ms");
    dc.TextOut(x(760), y(405), L"Fault ms");
    dc.TextOut(x(35), historyTitleY + y(15), L"Device ID");
    dc.TextOut(x(220), historyTitleY + y(15), L"이벤트 종류");
    dc.TextOut(x(415), historyTitleY + y(15), L"검색어");
    CString metrics;
    metrics.Format(L"Packets %zu    Errors %zu    Active alarms %zu    Telemetry samples %zu",
        m_dashboardModel.Packets().size(), m_dashboardModel.Errors().size(),
        m_runtime.Alarms().ActiveAlarms().size(), m_runtime.Telemetry().RecentSamples().size());
    dc.TextOut(x(680), y(28), metrics);
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
    const bool disconnected = m_gimbalService.Disconnect();
    SetOperationStatus(disconnected ? L"연결을 안전하게 해제했습니다."
                                    : L"해제할 연결이 없습니다.", disconnected);
    RefreshDashboardViews();
}

void MfcDashboardFrame::OnPowerOn()
{
    const bool sent = m_gimbalService.Power(true);
    SetOperationStatus(L"전원 ON 명령을 전송했습니다.", sent);
}

void MfcDashboardFrame::OnPowerOff()
{
    const bool sent = m_gimbalService.Power(false);
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
    m_connectionStatus.SetWindowText(ConnectionStateText(state));
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
    m_reconnectStatus.SetWindowText(reconnectText);
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
    m_telemetryStatus.SetWindowText(telemetryText);
    const auto heartbeat = m_runtime.HeartbeatWatchdog().StatusFor(m_gimbalService.DeviceId());
    CString heartbeatText;
    heartbeatText.Format(L"Heartbeat: %s    마지막 수신 %lld ms 전",
        HeartbeatStateText(heartbeat.state), heartbeat.ageMilliseconds);
    m_heartbeatStatus.SetWindowText(heartbeatText);
    UpdateCommandAvailability();
    Invalidate(FALSE);
}

void MfcDashboardFrame::UpdateCommandAvailability()
{
    const auto state = m_gimbalService.ConnectionState().value_or(Core::ConnectionState::Disconnected);
    const bool connected = state == Core::ConnectionState::Connected;
    const bool workflowRunning = m_workflowService.IsRunning();
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
    m_startScenarioButton.EnableWindow(!workflowRunning);
    m_startReplayButton.EnableWindow(connected && !workflowRunning);
    m_stopWorkflowButton.EnableWindow(workflowRunning);
    m_acknowledgeAlarmButton.EnableWindow(!m_runtime.Alarms().ActiveAlarms().empty());
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
