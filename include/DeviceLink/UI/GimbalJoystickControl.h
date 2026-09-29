#pragma once

#include <afxwin.h>

#include <cstdint>
#include <functional>

namespace DeviceLink::UI {
enum class JoystickMode : std::uint8_t { Velocity, Position };
enum class JoystickEvent : std::uint8_t { Changed, Released };

// Mouse-driven, presentation-only UI control. It emits normalized axes and
// deliberately keeps command interpretation in MfcDashboardFrame.
class GimbalJoystickControl final : public CWnd {
public:
  GimbalJoystickControl() = default;
  using AxisChangedHandler =
      std::function<void(double panAxis, double tiltAxis, JoystickEvent event)>;

  [[nodiscard]] bool Create(CWnd *parent, UINT controlId, const CRect &bounds,
                            AxisChangedHandler handler);

  GimbalJoystickControl(const GimbalJoystickControl &) = delete;
  GimbalJoystickControl &operator=(const GimbalJoystickControl &) = delete;

  void SetMode(JoystickMode mode);
  void Reset();
  [[nodiscard]] JoystickMode Mode() const noexcept;

protected:
  afx_msg void OnPaint();
  afx_msg void OnLButtonDown(UINT flags, CPoint point);
  afx_msg void OnMouseMove(UINT flags, CPoint point);
  afx_msg void OnLButtonUp(UINT flags, CPoint point);
  afx_msg void OnEnable(BOOL enabled);
  afx_msg void OnCancelMode();
  afx_msg void OnCaptureChanged(CWnd *window);
  DECLARE_MESSAGE_MAP()

private:
  void UpdateAxes(CPoint point, bool notify);
  void ResetAxes(bool notify = true);
  void CancelDrag();

  AxisChangedHandler m_axisChangedHandler;
  double m_panAxis{};
  double m_tiltAxis{};
  bool m_dragging{};
  JoystickMode m_mode{JoystickMode::Velocity};
};
} // namespace DeviceLink::UI
