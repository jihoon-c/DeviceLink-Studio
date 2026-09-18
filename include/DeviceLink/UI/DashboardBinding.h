#pragma once

#include "DeviceLink/UI/DashboardModel.h"
#include "DeviceLink/UI/WindowEventBridge.h"

#include <functional>

namespace DeviceLink::UI
{

class DashboardBinding final
{
public:
    using RefreshHandler = std::function<void()>;

    DashboardBinding(
        Application::DeviceEventQueue& eventQueue,
        DashboardModel& model,
        HWND targetWindow,
        UINT notificationMessage,
        RefreshHandler refreshHandler);

    [[nodiscard]] bool Attach();
    void Detach() noexcept;
    [[nodiscard]] bool HandleMessage(UINT message);

    DashboardBinding(const DashboardBinding&) = delete;
    DashboardBinding& operator=(const DashboardBinding&) = delete;

private:
    DashboardModel& m_model;
    RefreshHandler m_refreshHandler;
    WindowEventBridge m_eventBridge;
};

} // namespace DeviceLink::UI
