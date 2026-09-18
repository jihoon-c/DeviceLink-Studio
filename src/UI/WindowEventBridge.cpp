#include "DeviceLink/UI/WindowEventBridge.h"

#include <atomic>
#include <utility>

namespace DeviceLink::UI
{

struct WindowEventBridge::CallbackState final
{
    std::atomic<HWND> targetWindow{nullptr};
    UINT notificationMessage{};
};

WindowEventBridge::WindowEventBridge(
    Application::DeviceEventQueue& eventQueue,
    HWND targetWindow,
    UINT notificationMessage,
    EventHandler eventHandler)
    : m_eventQueue(eventQueue),
      m_targetWindow(targetWindow),
      m_notificationMessage(notificationMessage),
      m_eventHandler(std::move(eventHandler))
{
}

WindowEventBridge::~WindowEventBridge()
{
    Detach();
}

bool WindowEventBridge::Attach()
{
    if (m_attached || m_targetWindow == nullptr || m_notificationMessage < WM_APP)
    {
        return false;
    }

    m_callbackState = std::make_shared<CallbackState>();
    m_callbackState->targetWindow.store(m_targetWindow);
    m_callbackState->notificationMessage = m_notificationMessage;
    const std::weak_ptr<CallbackState> callbackState = m_callbackState;
    m_eventQueue.SetWakeupHandler([callbackState] {
        const auto state = callbackState.lock();
        if (!state)
        {
            return;
        }
        const HWND targetWindow = state->targetWindow.load();
        if (targetWindow != nullptr)
        {
            static_cast<void>(::PostMessageW(targetWindow, state->notificationMessage, 0, 0));
        }
    });
    m_attached = true;
    return true;
}

void WindowEventBridge::Detach() noexcept
{
    if (!m_attached)
    {
        return;
    }

    m_callbackState->targetWindow.store(nullptr);
    m_eventQueue.SetWakeupHandler({});
    m_callbackState.reset();
    m_attached = false;
}

bool WindowEventBridge::HandleMessage(UINT message)
{
    if (!m_attached || message != m_notificationMessage)
    {
        return false;
    }

    for (auto& event : m_eventQueue.Drain())
    {
        if (m_eventHandler)
        {
            m_eventHandler(std::move(event));
        }
    }
    return true;
}

} // namespace DeviceLink::UI
