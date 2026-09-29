#include "DeviceLink/UI/GimbalJoystickControl.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace DeviceLink::UI {
BEGIN_MESSAGE_MAP(GimbalJoystickControl, CWnd)
ON_WM_PAINT()
ON_WM_LBUTTONDOWN()
ON_WM_MOUSEMOVE()
ON_WM_LBUTTONUP()
ON_WM_ENABLE()
ON_WM_CANCELMODE()
ON_WM_CAPTURECHANGED()
END_MESSAGE_MAP()

bool GimbalJoystickControl::Create(CWnd *parent, UINT controlId,
                                   const CRect &bounds,
                                   AxisChangedHandler handler) {
  m_axisChangedHandler = std::move(handler);
  const CString className = AfxRegisterWndClass(
      CS_HREDRAW | CS_VREDRAW, ::LoadCursor(nullptr, IDC_HAND),
      static_cast<HBRUSH>(::GetStockObject(NULL_BRUSH)), nullptr);
  return CWnd::CreateEx(0, className, L"Pan Tilt Joystick",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP, bounds, parent,
                        controlId) != FALSE;
}

void GimbalJoystickControl::SetMode(JoystickMode mode) {
  if (m_mode != mode) {
    m_mode = mode;
    ResetAxes();
  }
}

void GimbalJoystickControl::Reset() { ResetAxes(); }

JoystickMode GimbalJoystickControl::Mode() const noexcept { return m_mode; }

void GimbalJoystickControl::OnPaint() {
  CPaintDC dc(this);
  CRect client;
  GetClientRect(&client);
  dc.FillSolidRect(client, ::GetSysColor(COLOR_3DFACE));

  const int radius =
      (std::max)(12, (std::min)(client.Width(), client.Height()) / 2 - 5);
  const CPoint center(client.CenterPoint());
  const COLORREF ringColor =
      IsWindowEnabled() ? RGB(92, 116, 145) : RGB(160, 160, 160);
  const COLORREF stickColor =
      IsWindowEnabled() ? RGB(47, 104, 171) : RGB(145, 145, 145);

  CPen ringPen(PS_SOLID, 2, ringColor);
  CBrush backgroundBrush(RGB(229, 236, 244));
  CPen *previousPen = dc.SelectObject(&ringPen);
  CBrush *previousBrush = dc.SelectObject(&backgroundBrush);
  dc.Ellipse(center.x - radius, center.y - radius, center.x + radius,
             center.y + radius);
  dc.MoveTo(center.x - radius + 4, center.y);
  dc.LineTo(center.x + radius - 4, center.y);
  dc.MoveTo(center.x, center.y - radius + 4);
  dc.LineTo(center.x, center.y + radius - 4);

  const int thumbRadius = (std::max)(7, radius / 4);
  const CPoint thumb(
      center.x +
          static_cast<int>(std::lround(m_panAxis * (radius - thumbRadius))),
      center.y -
          static_cast<int>(std::lround(m_tiltAxis * (radius - thumbRadius))));
  CBrush thumbBrush(stickColor);
  dc.SelectObject(&thumbBrush);
  dc.Ellipse(thumb.x - thumbRadius, thumb.y - thumbRadius,
             thumb.x + thumbRadius, thumb.y + thumbRadius);
  dc.SelectObject(previousBrush);
  dc.SelectObject(previousPen);
}

void GimbalJoystickControl::OnLButtonDown(UINT flags, CPoint point) {
  SetFocus();
  SetCapture();
  m_dragging = true;
  UpdateAxes(point, true);
  CWnd::OnLButtonDown(flags, point);
}

void GimbalJoystickControl::OnMouseMove(UINT flags, CPoint point) {
  if (m_dragging && (flags & MK_LBUTTON) != 0) {
    UpdateAxes(point, true);
  }
  CWnd::OnMouseMove(flags, point);
}

void GimbalJoystickControl::OnLButtonUp(UINT flags, CPoint point) {
  if (m_dragging) {
    UpdateAxes(point, true);
    m_dragging = false;
    if (GetCapture() == this) {
      ReleaseCapture();
    }
    if (m_axisChangedHandler) {
      m_axisChangedHandler(m_panAxis, m_tiltAxis, JoystickEvent::Released);
    }
    if (m_mode == JoystickMode::Velocity) {
      ResetAxes();
    }
  }
  CWnd::OnLButtonUp(flags, point);
}

void GimbalJoystickControl::OnEnable(BOOL enabled) {
  CWnd::OnEnable(enabled);
  if (!enabled) {
    m_dragging = false;
    if (GetCapture() == this) {
      ReleaseCapture();
    }
    ResetAxes();
  }
  Invalidate(FALSE);
}

void GimbalJoystickControl::OnCancelMode() {
  CancelDrag();
  CWnd::OnCancelMode();
}

void GimbalJoystickControl::OnCaptureChanged(CWnd *window) {
  CancelDrag();
  CWnd::OnCaptureChanged(window);
}

void GimbalJoystickControl::CancelDrag() {
  if (!m_dragging) {
    return;
  }
  m_dragging = false;
  ResetAxes(false);
  if (m_axisChangedHandler) {
    m_axisChangedHandler(0.0, 0.0, JoystickEvent::Released);
  }
}

void GimbalJoystickControl::UpdateAxes(CPoint point, bool notify) {
  CRect client;
  GetClientRect(&client);
  const CPoint center(client.CenterPoint());
  const double radius = static_cast<double>(
      (std::max)(12, (std::min)(client.Width(), client.Height()) / 2 - 5));
  double panAxis = static_cast<double>(point.x - center.x) / radius;
  double tiltAxis = static_cast<double>(center.y - point.y) / radius;
  const double length = std::hypot(panAxis, tiltAxis);
  if (length > 1.0) {
    panAxis /= length;
    tiltAxis /= length;
  }
  if (std::abs(panAxis - m_panAxis) < 0.04 &&
      std::abs(tiltAxis - m_tiltAxis) < 0.04) {
    return;
  }
  m_panAxis = panAxis;
  m_tiltAxis = tiltAxis;
  Invalidate(FALSE);
  if (notify && m_axisChangedHandler) {
    m_axisChangedHandler(m_panAxis, m_tiltAxis, JoystickEvent::Changed);
  }
}

void GimbalJoystickControl::ResetAxes(bool notify) {
  m_panAxis = 0.0;
  m_tiltAxis = 0.0;
  Invalidate(FALSE);
  if (notify && m_axisChangedHandler) {
    m_axisChangedHandler(m_panAxis, m_tiltAxis, JoystickEvent::Changed);
  }
}
} // namespace DeviceLink::UI
