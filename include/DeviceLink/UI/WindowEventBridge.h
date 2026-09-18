#pragma once

#include "DeviceLink/Application/DeviceEventQueue.h"

#include <Windows.h>

#include <functional>
#include <memory>

namespace DeviceLink::UI
{

class WindowEventBridge final
{
public:
    using EventHandler = std::function<void(Application::DeviceEvent)>;

    WindowEventBridge(
        Application::DeviceEventQueue& eventQueue,
        HWND targetWindow,
        UINT notificationMessage,
        EventHandler eventHandler);
    ~WindowEventBridge();

    [[nodiscard]] bool Attach();
    void Detach() noexcept;
    [[nodiscard]] bool HandleMessage(UINT message);

    WindowEventBridge(const WindowEventBridge&) = delete;
    WindowEventBridge& operator=(const WindowEventBridge&) = delete;

private:
    struct CallbackState;

    Application::DeviceEventQueue& m_eventQueue;
    HWND m_targetWindow{};
    UINT m_notificationMessage{};
    EventHandler m_eventHandler;
    std::shared_ptr<CallbackState> m_callbackState;
    bool m_attached{};
};

} // namespace DeviceLink::UI
