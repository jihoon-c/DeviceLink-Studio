#include "DeviceLink/Application/TelemetryHeartbeatWatchdog.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace DeviceLink::Application
{
namespace
{
std::int64_t CurrentSteadyMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

TelemetryHeartbeatWatchdog::TelemetryHeartbeatWatchdog(
    std::vector<std::uint16_t> telemetryMessageTypes,
    TelemetryHeartbeatWatchdogOptions options,
    MonotonicTimestampProvider timestampProvider)
    : m_telemetryMessageTypes(std::move(telemetryMessageTypes)),
      m_options(options),
      m_timestampProvider(timestampProvider ? std::move(timestampProvider)
                                            : MonotonicTimestampProvider{CurrentSteadyMilliseconds})
{
    ValidateOptions(m_options);
    std::sort(m_telemetryMessageTypes.begin(), m_telemetryMessageTypes.end());
    m_telemetryMessageTypes.erase(
        std::unique(m_telemetryMessageTypes.begin(), m_telemetryMessageTypes.end()),
        m_telemetryMessageTypes.end());
    m_worker = std::jthread([this](std::stop_token stopToken) { Run(stopToken); });
}

TelemetryHeartbeatWatchdog::~TelemetryHeartbeatWatchdog()
{
    Stop();
}

void TelemetryHeartbeatWatchdog::Stop() noexcept
{
    m_worker.request_stop();
    m_condition.notify_all();
    if (m_worker.joinable())
    {
        m_worker.join();
    }
}

void TelemetryHeartbeatWatchdog::SetEventHandler(EventHandler eventHandler)
{
    const std::scoped_lock lock(m_mutex);
    m_eventHandler = std::move(eventHandler);
}

void TelemetryHeartbeatWatchdog::SetOptions(TelemetryHeartbeatWatchdogOptions options)
{
    ValidateOptions(options);
    {
        const std::scoped_lock lock(m_mutex);
        m_options = options;
    }
    m_condition.notify_all();
}

void TelemetryHeartbeatWatchdog::ApplyDeviceEvent(const DeviceEvent& event)
{
    std::optional<DeviceEvent> heartbeatEvent;
    const std::int64_t now = m_timestampProvider();
    {
        const std::scoped_lock lock(m_mutex);
        auto& heartbeat = m_devices[event.deviceId];
        if (const auto* connection = std::get_if<ConnectionStateChanged>(&event.payload))
        {
            const bool connected = connection->state == Core::ConnectionState::Connected;
            const auto nextState = connected ? TelemetryHeartbeatState::Waiting
                                             : TelemetryHeartbeatState::Inactive;
            heartbeat.connected = connected;
            heartbeat.lastTelemetryOrConnectionMilliseconds = now;
            if (heartbeat.state != nextState)
            {
                heartbeat.state = nextState;
                heartbeatEvent = DeviceEvent{
                    event.deviceId, TelemetryHeartbeatChanged{nextState, 0}};
            }
        }
        else if (const auto* frame = std::get_if<FrameReceived>(&event.payload);
                 frame != nullptr && heartbeat.connected &&
                 IsTelemetryMessageType(frame->frame.header.messageType))
        {
            heartbeat.lastTelemetryOrConnectionMilliseconds = now;
            if (heartbeat.state != TelemetryHeartbeatState::Healthy)
            {
                heartbeat.state = TelemetryHeartbeatState::Healthy;
                heartbeatEvent = DeviceEvent{
                    event.deviceId, TelemetryHeartbeatChanged{TelemetryHeartbeatState::Healthy, 0}};
            }
        }
    }
    if (heartbeatEvent)
    {
        Publish(std::move(*heartbeatEvent));
    }
}

void TelemetryHeartbeatWatchdog::EvaluateNow()
{
    std::vector<DeviceEvent> events;
    const std::int64_t now = m_timestampProvider();
    {
        const std::scoped_lock lock(m_mutex);
        for (auto& [deviceId, heartbeat] : m_devices)
        {
            if (!heartbeat.connected)
            {
                continue;
            }
            const std::int64_t age = (std::max)(
                std::int64_t{0}, now - heartbeat.lastTelemetryOrConnectionMilliseconds);
            TelemetryHeartbeatState nextState = heartbeat.state;
            if (age >= m_options.faultAfter.count())
            {
                nextState = TelemetryHeartbeatState::Fault;
            }
            else if (age >= m_options.warningAfter.count())
            {
                nextState = TelemetryHeartbeatState::Warning;
            }
            if (nextState != heartbeat.state)
            {
                heartbeat.state = nextState;
                events.push_back({deviceId, TelemetryHeartbeatChanged{nextState, age}});
            }
        }
    }
    for (auto& event : events)
    {
        Publish(std::move(event));
    }
}

TelemetryHeartbeatStatus TelemetryHeartbeatWatchdog::StatusFor(
    const std::string& deviceId) const
{
    const std::int64_t now = m_timestampProvider();
    const std::scoped_lock lock(m_mutex);
    const auto item = m_devices.find(deviceId);
    if (item == m_devices.end() || !item->second.connected)
    {
        return {};
    }
    return {
        .state = item->second.state,
        .ageMilliseconds = (std::max)(
            std::int64_t{0}, now - item->second.lastTelemetryOrConnectionMilliseconds),
    };
}

TelemetryHeartbeatWatchdogOptions TelemetryHeartbeatWatchdog::Options() const
{
    const std::scoped_lock lock(m_mutex);
    return m_options;
}

bool TelemetryHeartbeatWatchdog::IsTelemetryMessageType(
    std::uint16_t messageType) const noexcept
{
    return std::binary_search(
        m_telemetryMessageTypes.begin(), m_telemetryMessageTypes.end(), messageType);
}

void TelemetryHeartbeatWatchdog::ValidateOptions(
    const TelemetryHeartbeatWatchdogOptions& options)
{
    if (options.warningAfter <= std::chrono::milliseconds::zero() ||
        options.faultAfter <= options.warningAfter ||
        options.pollInterval <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument("Invalid telemetry heartbeat watchdog thresholds");
    }
}

void TelemetryHeartbeatWatchdog::Publish(DeviceEvent event) noexcept
{
    EventHandler handler;
    {
        const std::scoped_lock lock(m_mutex);
        handler = m_eventHandler;
    }
    if (!handler)
    {
        return;
    }
    try
    {
        handler(std::move(event));
    }
    catch (...)
    {
        // Monitoring must not terminate its worker when an observer fails.
    }
}

void TelemetryHeartbeatWatchdog::Run(std::stop_token stopToken) noexcept
{
    while (!stopToken.stop_requested())
    {
        std::unique_lock lock(m_mutex);
        m_condition.wait_for(lock, stopToken, m_options.pollInterval, [] { return false; });
        lock.unlock();
        if (!stopToken.stop_requested())
        {
            EvaluateNow();
        }
    }
}
} // namespace DeviceLink::Application
