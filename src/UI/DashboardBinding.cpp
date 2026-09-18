#include "DeviceLink/UI/DashboardBinding.h"

#include <utility>

namespace DeviceLink::UI
{

DashboardBinding::DashboardBinding(
    Application::DeviceEventQueue& eventQueue,
    DashboardModel& model,
    HWND targetWindow,
    UINT notificationMessage,
    RefreshHandler refreshHandler)
    : m_model(model),
      m_refreshHandler(std::move(refreshHandler)),
      m_eventBridge(eventQueue, targetWindow, notificationMessage, [this](
          Application::DeviceEvent event) {
          m_model.ApplyEvent(std::move(event));
          if (m_refreshHandler)
          {
              m_refreshHandler();
          }
      })
{
}

bool DashboardBinding::Attach()
{
    return m_eventBridge.Attach();
}

void DashboardBinding::Detach() noexcept
{
    m_eventBridge.Detach();
}

bool DashboardBinding::HandleMessage(UINT message)
{
    return m_eventBridge.HandleMessage(message);
}

} // namespace DeviceLink::UI
