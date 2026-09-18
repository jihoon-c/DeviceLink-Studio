#pragma once

#include "DeviceLink/Application/DeviceRuntime.h"
#include "DeviceLink/Application/EventLogQueryService.h"
#include "DeviceLink/Application/OperatorWorkflowService.h"
#include "DeviceLink/Application/OperatorSettingsService.h"
#include "DeviceLink/Application/VirtualGimbalService.h"
#include "DeviceLink/UI/DashboardBinding.h"
#include "DeviceLink/UI/DashboardModel.h"

#include <afxwin.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace DeviceLink::UI
{
class MfcDashboardFrame final : public CFrameWnd
{
public:
    enum ControlId : UINT
    {
        ConnectButtonId = 2001,
        DisconnectButtonId,
        PowerOnButtonId,
        PowerOffButtonId,
        InitializeButtonId,
        SetTargetButtonId,
        StartScanButtonId,
        StopScanButtonId,
        RequestStatusButtonId,
        InjectFaultButtonId,
        ClearFaultButtonId,
        BrowseScenarioButtonId,
        StartScenarioButtonId,
        StopWorkflowButtonId,
        StartReplayButtonId,
        AcknowledgeAlarmButtonId,
        RefreshHistoryButtonId,
        AutomaticReconnectCheckId,
        ApplyResponseModeButtonId,
        ApplyHeartbeatSettingsButtonId,
        EquipmentModeComboId,
    };

    MfcDashboardFrame(
        Application::DeviceRuntime& runtime,
        Application::VirtualGimbalService& gimbalService,
        Application::OperatorWorkflowService& workflowService,
        Application::EventLogQueryService& eventLogQueryService,
        Application::OperatorSettingsService& settingsService) noexcept;
    ~MfcDashboardFrame() override;

    [[nodiscard]] bool CreateDashboard();

    MfcDashboardFrame(const MfcDashboardFrame&) = delete;
    MfcDashboardFrame& operator=(const MfcDashboardFrame&) = delete;

protected:
    afx_msg int OnCreate(LPCREATESTRUCT createStructure);
    afx_msg void OnDestroy();
    afx_msg void OnPaint();
    afx_msg void OnSize(UINT type, int width, int height);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* minMaxInfo);
    afx_msg LRESULT OnDpiChanged(WPARAM wParam, LPARAM lParam);
    afx_msg void OnConnect();
    afx_msg void OnDisconnect();
    afx_msg void OnPowerOn();
    afx_msg void OnPowerOff();
    afx_msg void OnInitializeDevice();
    afx_msg void OnSetTarget();
    afx_msg void OnStartScan();
    afx_msg void OnStopScan();
    afx_msg void OnRequestStatus();
    afx_msg void OnInjectFault();
    afx_msg void OnClearFault();
    afx_msg void OnApplyResponseMode();
    afx_msg void OnApplyHeartbeatSettings();
    afx_msg void OnBrowseScenario();
    afx_msg void OnStartScenario();
    afx_msg void OnStopWorkflow();
    afx_msg void OnStartReplay();
    afx_msg void OnAcknowledgeAlarm();
    afx_msg void OnRefreshHistory();
    afx_msg void OnToggleAutomaticReconnect();
    afx_msg void OnEquipmentModeChanged();
    afx_msg LRESULT OnDeviceEvent(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnWorkflowProgress(WPARAM wParam, LPARAM lParam);
    DECLARE_MESSAGE_MAP()

private:
    static constexpr UINT kDeviceEventMessage = WM_APP + 101;
    static constexpr UINT kWorkflowProgressMessage = WM_APP + 102;

    [[nodiscard]] bool CreateControls();
    void LayoutControls(int clientWidth, int clientHeight);
    void UpdateFonts();
    [[nodiscard]] int Scale(int logicalPixels) const noexcept;
    [[nodiscard]] int LayoutX(int logicalX, int clientWidth) const noexcept;
    void RefreshDashboardViews();
    void UpdateCommandAvailability();
    void UpdateEquipmentModeControls();
    void RefreshEventHistory();
    void LoadSettings();
    void SaveSettings() noexcept;
    void SetOperationStatus(const wchar_t* message, bool succeeded);
    [[nodiscard]] std::optional<std::uint16_t> ReadPort() const;
    [[nodiscard]] std::optional<double> ReadAngle(const CEdit& edit) const;
    [[nodiscard]] std::optional<std::uint32_t> ReadHeartbeatMilliseconds(const CEdit& edit) const;

    Application::DeviceRuntime& m_runtime;
    Application::VirtualGimbalService& m_gimbalService;
    Application::OperatorWorkflowService& m_workflowService;
    Application::EventLogQueryService& m_eventLogQueryService;
    Application::OperatorSettingsService& m_settingsService;
    DashboardModel m_dashboardModel;
    std::unique_ptr<DashboardBinding> m_dashboardBinding;

    CFont m_headingFont;
    CFont m_sectionFont;
    CFont m_bodyFont;
    UINT m_dpi{96};
    CEdit m_hostEdit;
    CEdit m_portEdit;
    CEdit m_panEdit;
    CEdit m_tiltEdit;
    CEdit m_scenarioPathEdit;
    CEdit m_historyDeviceEdit;
    CEdit m_historySearchEdit;
    CEdit m_responseDelayEdit;
    CEdit m_heartbeatWarningEdit;
    CEdit m_heartbeatFaultEdit;
    CComboBox m_historyCategoryCombo;
    CComboBox m_equipmentModeCombo;
    CComboBox m_faultCombo;
    CComboBox m_responseModeCombo;
    CButton m_connectButton;
    CButton m_disconnectButton;
    CButton m_powerOnButton;
    CButton m_powerOffButton;
    CButton m_initializeButton;
    CButton m_setTargetButton;
    CButton m_startScanButton;
    CButton m_stopScanButton;
    CButton m_requestStatusButton;
    CButton m_injectFaultButton;
    CButton m_clearFaultButton;
    CButton m_browseScenarioButton;
    CButton m_startScenarioButton;
    CButton m_stopWorkflowButton;
    CButton m_startReplayButton;
    CButton m_acknowledgeAlarmButton;
    CButton m_refreshHistoryButton;
    CButton m_automaticReconnectCheck;
    CButton m_applyResponseModeButton;
    CButton m_applyHeartbeatSettingsButton;
    CStatic m_connectionStatus;
    CStatic m_operationStatus;
    CStatic m_telemetryStatus;
    CStatic m_workflowStatus;
    CStatic m_reconnectStatus;
    CStatic m_heartbeatStatus;
    CListBox m_packetList;
    CListBox m_errorList;
    CListBox m_historyList;
    std::vector<std::uint64_t> m_alarmIdsByListIndex;
};
} // namespace DeviceLink::UI
