#include "DeviceLink/Application/DeviceEventQueue.h"
#include "DeviceLink/Application/AxisVapixCommandAdapter.h"
#include "DeviceLink/Application/DeviceEventPersistence.h"
#include "DeviceLink/Application/DeviceEventProcessingService.h"
#include "DeviceLink/Application/DeviceManager.h"
#include "DeviceLink/Application/DeviceRuntime.h"
#include "DeviceLink/Application/CommunicationAlarmService.h"
#include "DeviceLink/Application/EventLogQueryService.h"
#include "DeviceLink/Application/FrameReplayRunner.h"
#include "DeviceLink/Application/OperatorWorkflowService.h"
#include "DeviceLink/Application/OperatorSettingsService.h"
#include "DeviceLink/Application/ScenarioRunner.h"
#include "DeviceLink/Application/ScenarioFileService.h"
#include "DeviceLink/Application/ScenarioProgressQueue.h"
#include "DeviceLink/Application/ScenarioReportService.h"
#include "DeviceLink/Application/ScenarioTextCodec.h"
#include "DeviceLink/Application/TelemetryService.h"
#include "DeviceLink/Application/TelemetryQualityService.h"
#include "DeviceLink/Application/TelemetryHeartbeatWatchdog.h"
#include "DeviceLink/Application/VirtualGimbalService.h"
#include "DeviceLink/Infrastructure/AtomicTextFileStore.h"
#include "DeviceLink/Application/SqliteFrameReplaySource.h"
#include "DeviceLink/Infrastructure/AsyncEventStore.h"
#include "DeviceLink/Infrastructure/SqliteEventRepository.h"
#include "DeviceLink/Transport/TcpTransport.h"
#include "DeviceLink/UI/DashboardBinding.h"
#include "DeviceLink/UI/DashboardModel.h"
#include "DeviceLink/UI/WindowEventBridge.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using DeviceLink::Core::ConnectionEvent;
using DeviceLink::Core::ConnectionState;
using namespace std::chrono_literals;

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

class QueueingTransport final : public DeviceLink::Transport::ITransport
{
public:
    void SetReceiveHandler(ReceiveHandler receiveHandler) override
    {
        m_receiveHandler = std::move(receiveHandler);
    }

    void SetErrorHandler(ErrorHandler errorHandler) override
    {
        m_errorHandler = std::move(errorHandler);
    }

    bool Connect(const char*, std::uint16_t) override
    {
        const std::scoped_lock lock(m_mutex);
        m_connected = true;
        return true;
    }

    void Disconnect() noexcept override
    {
        const std::scoped_lock lock(m_mutex);
        m_connected = false;
    }

    bool Send(std::span<const std::byte> bytes) override
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_connected)
        {
            return false;
        }
        m_sentFrames.emplace_back(bytes.begin(), bytes.end());
        return true;
    }

    bool IsConnected() const noexcept override
    {
        const std::scoped_lock lock(m_mutex);
        return m_connected;
    }

    [[nodiscard]] std::vector<std::vector<std::byte>> SentFrames() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_sentFrames;
    }

private:
    ReceiveHandler m_receiveHandler;
    ErrorHandler m_errorHandler;
    mutable std::mutex m_mutex;
    std::vector<std::vector<std::byte>> m_sentFrames;
    bool m_connected{false};
};

class TestGimbalProtocolAdapter final
    : public DeviceLink::Application::IGimbalProtocolAdapter
{
public:
    std::optional<DeviceLink::Protocol::PacketFrame> EncodeCommand(
        const DeviceLink::Application::GimbalCommand& command,
        std::uint32_t sequence) const override
    {
        DeviceLink::Protocol::PacketFrame frame{};
        frame.header.messageType = 0x4321;
        frame.header.sequence = sequence;
        frame.header.payloadSize = 1;
        frame.payload = {static_cast<std::byte>(command.kind)};
        return frame;
    }

    std::optional<DeviceLink::Application::VirtualGimbalAcknowledgement>
    DecodeAcknowledgement(std::uint16_t messageType,
        std::span<const std::byte> payload) const noexcept override
    {
        if (messageType != AcknowledgeMessageType() || payload.size() != 1)
        {
            return std::nullopt;
        }
        return DeviceLink::Application::VirtualGimbalAcknowledgement{
            .commandType = 0x4321,
            .succeeded = payload[0] == std::byte{0},
        };
    }

    std::optional<DeviceLink::Application::VirtualGimbalTelemetry> DecodeTelemetry(
        std::uint16_t, std::span<const std::byte>) const noexcept override
    {
        return std::nullopt;
    }

    std::uint16_t AcknowledgeMessageType() const noexcept override { return 0x4322; }
    std::uint16_t TelemetryMessageType() const noexcept override { return 0x4323; }
};

class FlakyTransport final : public DeviceLink::Transport::ITransport
{
public:
    explicit FlakyTransport(std::uint32_t succeedOnAttempt)
        : m_succeedOnAttempt(succeedOnAttempt)
    {
    }

    void SetReceiveHandler(ReceiveHandler receiveHandler) override
    {
        const std::scoped_lock lock(m_mutex);
        m_receiveHandler = std::move(receiveHandler);
    }

    void SetErrorHandler(ErrorHandler errorHandler) override
    {
        const std::scoped_lock lock(m_mutex);
        m_errorHandler = std::move(errorHandler);
    }

    bool Connect(const char*, std::uint16_t) override
    {
        const std::uint32_t attempt = ++m_connectCount;
        m_connected.store(attempt >= m_succeedOnAttempt);
        return m_connected.load();
    }

    void Disconnect() noexcept override
    {
        m_connected.store(false);
    }

    bool Send(std::span<const std::byte>) override
    {
        return m_connected.load();
    }

    bool IsConnected() const noexcept override
    {
        return m_connected.load();
    }

    void SimulateConnectionLoss()
    {
        ErrorHandler handler;
        {
            const std::scoped_lock lock(m_mutex);
            m_connected.store(false);
            handler = m_errorHandler;
        }
        if (handler)
        {
            handler("Simulated connection loss");
        }
    }

    [[nodiscard]] std::uint32_t ConnectCount() const noexcept
    {
        return m_connectCount.load();
    }

private:
    ReceiveHandler m_receiveHandler;
    ErrorHandler m_errorHandler;
    mutable std::mutex m_mutex;
    std::atomic_bool m_connected{};
    std::atomic<std::uint32_t> m_connectCount{};
    std::uint32_t m_succeedOnAttempt{};
};

class WindowHandle final
{
public:
    explicit WindowHandle(HWND window) noexcept
        : m_window(window)
    {
    }

    ~WindowHandle()
    {
        if (m_window != nullptr)
        {
            ::DestroyWindow(m_window);
        }
    }

    [[nodiscard]] HWND Get() const noexcept
    {
        return m_window;
    }

    WindowHandle(const WindowHandle&) = delete;
    WindowHandle& operator=(const WindowHandle&) = delete;

private:
    HWND m_window{};
};

class TemporaryDatabase final
{
public:
    TemporaryDatabase()
    {
        const auto uniqueSuffix = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        m_path = std::filesystem::temp_directory_path() /
            ("DeviceLinkApplicationTest-" + uniqueSuffix + ".sqlite");
    }

    ~TemporaryDatabase()
    {
        std::error_code error;
        std::filesystem::remove(m_path, error);
    }

    [[nodiscard]] const std::filesystem::path& Path() const noexcept
    {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

enum class ServerBehavior
{
    Echo,
    CloseAfterFrame,
    CorruptThenEcho,
    FragmentedEcho,
    SendTelemetry,
};

class FrameEchoServer final
{
public:
    explicit FrameEchoServer(ServerBehavior behavior = ServerBehavior::Echo)
        : m_behavior(behavior)
    {
        if (::WSAStartup(MAKEWORD(2, 2), &m_winsockData) != 0)
        {
            throw std::runtime_error("Test WSAStartup failed");
        }
        m_winsockStarted = true;

        m_listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listener == INVALID_SOCKET)
        {
            throw std::runtime_error("Test socket creation failed");
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(m_listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) ==
                SOCKET_ERROR ||
            ::listen(m_listener, 1) == SOCKET_ERROR)
        {
            throw std::runtime_error("Test listener setup failed");
        }

        int addressSize = sizeof(address);
        if (::getsockname(m_listener, reinterpret_cast<sockaddr*>(&address), &addressSize) ==
            SOCKET_ERROR)
        {
            throw std::runtime_error("Test listener query failed");
        }
        m_port = ntohs(address.sin_port);
        m_thread = std::jthread([this] { Run(); });
    }

    ~FrameEchoServer()
    {
        if (m_listener != INVALID_SOCKET)
        {
            ::shutdown(m_listener, SD_BOTH);
            ::closesocket(m_listener);
        }
        if (m_thread.joinable())
        {
            m_thread.join();
        }
        if (m_winsockStarted)
        {
            ::WSACleanup();
        }
    }

    [[nodiscard]] std::uint16_t Port() const noexcept
    {
        return m_port;
    }

private:
    static bool SendAll(SOCKET socket, std::span<const std::byte> bytes)
    {
        std::size_t offset{};
        while (offset < bytes.size())
        {
            const int sent = ::send(socket, reinterpret_cast<const char*>(bytes.data() + offset),
                static_cast<int>(bytes.size() - offset), 0);
            if (sent <= 0)
            {
                return false;
            }
            offset += static_cast<std::size_t>(sent);
        }
        return true;
    }

    static bool SendFragmented(SOCKET socket, std::span<const std::byte> bytes)
    {
        if (bytes.size() < 2)
        {
            return SendAll(socket, bytes);
        }

        const std::size_t splitOffset = bytes.size() / 2;
        return SendAll(socket, bytes.first(splitOffset)) &&
            (std::this_thread::sleep_for(20ms), SendAll(socket, bytes.subspan(splitOffset)));
    }

    void Run()
    {
        const SOCKET client = ::accept(m_listener, nullptr, nullptr);
        if (client == INVALID_SOCKET)
        {
            return;
        }

        if (m_behavior == ServerBehavior::SendTelemetry)
        {
            for (std::uint32_t sequence = 0; sequence < 2; ++sequence)
            {
                DeviceLink::Protocol::PacketFrame telemetry{};
                telemetry.header.messageType = 0x7001;
                telemetry.header.sequence = sequence;
                telemetry.header.payloadSize = 1;
                telemetry.payload = {static_cast<std::byte>(sequence)};
                const auto bytes = DeviceLink::Protocol::SerializeFrame(telemetry);
                if (!bytes || !SendAll(client, *bytes))
                {
                    ::closesocket(client);
                    return;
                }
                std::this_thread::sleep_for(50ms);
            }
        }

        DeviceLink::Protocol::FrameStreamParser parser;
        std::array<std::byte, 256> buffer{};
        while (true)
        {
            const int received = ::recv(client, reinterpret_cast<char*>(buffer.data()),
                static_cast<int>(buffer.size()), 0);
            if (received <= 0)
            {
                ::closesocket(client);
                return;
            }

            for (const auto& frame : parser.Consume(std::span<const std::byte>(buffer).first(
                     static_cast<std::size_t>(received))))
            {
                if (m_behavior == ServerBehavior::CloseAfterFrame)
                {
                    ::closesocket(client);
                    return;
                }
                const auto bytes = DeviceLink::Protocol::SerializeFrame(frame);
                if (!bytes)
                {
                    ::closesocket(client);
                    return;
                }
                if (m_behavior == ServerBehavior::CorruptThenEcho)
                {
                    auto corruptBytes = *bytes;
                    corruptBytes.back() ^= std::byte{0xFF};
                    if (!SendAll(client, corruptBytes))
                    {
                        ::closesocket(client);
                        return;
                    }
                }
                const bool sent = m_behavior == ServerBehavior::FragmentedEcho
                    ? SendFragmented(client, *bytes)
                    : SendAll(client, *bytes);
                if (!sent)
                {
                    ::closesocket(client);
                    return;
                }
            }
        }
    }

    WSADATA m_winsockData{};
    SOCKET m_listener{INVALID_SOCKET};
    std::uint16_t m_port{};
    std::jthread m_thread;
    bool m_winsockStarted{};
    ServerBehavior m_behavior;
};

void DeviceLifecycleTransitionsAreValidated()
{
    DeviceLink::Application::DeviceManager manager;
    Require(!manager.RegisterDevice(""), "Empty device ID accepted");
    Require(manager.RegisterDevice("simulator-1"), "Device registration failed");
    Require(!manager.RegisterDevice("simulator-1"), "Duplicate device accepted");

    Require(manager.ApplyConnectionEvent("simulator-1", ConnectionEvent::ConnectRequested),
            "Connect request rejected");
    Require(manager.ApplyConnectionEvent("simulator-1", ConnectionEvent::ConnectSucceeded),
            "Connect success rejected");
    Require(manager.GetConnectionState("simulator-1") == ConnectionState::Connected,
            "Device is not connected");
    Require(!manager.ApplyConnectionEvent("simulator-1", ConnectionEvent::ConnectSucceeded),
            "Invalid connected transition accepted");

    Require(manager.ApplyConnectionEvent("simulator-1", ConnectionEvent::DisconnectRequested),
            "Disconnect request rejected");
    Require(manager.ApplyConnectionEvent("simulator-1", ConnectionEvent::DisconnectCompleted),
            "Disconnect completion rejected");
    Require(manager.GetConnectionState("simulator-1") == ConnectionState::Disconnected,
            "Device is not disconnected");
}

void FaultRecoveryAndRemovalAreHandled()
{
    DeviceLink::Application::DeviceManager manager;
    Require(manager.RegisterDevice("simulator-2"), "Device registration failed");
    Require(manager.ApplyConnectionEvent("simulator-2", ConnectionEvent::ConnectRequested),
            "Connect request rejected");
    Require(manager.ApplyConnectionEvent("simulator-2", ConnectionEvent::ConnectFailed),
            "Connect failure rejected");
    Require(manager.GetConnectionState("simulator-2") == ConnectionState::Faulted,
            "Failed connection is not faulted");
    Require(manager.ApplyConnectionEvent("simulator-2", ConnectionEvent::ResetFault),
            "Fault reset rejected");
    Require(manager.RemoveDevice("simulator-2"), "Device removal failed");
    Require(!manager.GetConnectionState("simulator-2").has_value(),
            "Removed device remains visible");
}

void WorkerEventsAreQueuedInFifoOrder()
{
    DeviceLink::Application::DeviceEventQueue queue;
    std::size_t wakeupCount = 0;
    queue.SetWakeupHandler([&] { ++wakeupCount; });

    queue.Push({
        .deviceId = "simulator-1",
        .payload = DeviceLink::Application::ConnectionStateChanged{ConnectionState::Connected},
    });
    queue.Push({
        .deviceId = "simulator-1",
        .payload = DeviceLink::Application::TransportError{"Connection reset"},
    });

    Require(wakeupCount == 2, "Wakeup handler was not called for each event");
    Require(queue.Size() == 2, "Queued event count differs");
    const auto events = queue.Drain();
    Require(events.size() == 2 && events[0].deviceId == "simulator-1",
            "Queued event order differs");
    Require(std::holds_alternative<DeviceLink::Application::ConnectionStateChanged>(
                events[0].payload),
            "First event payload differs");
    Require(std::holds_alternative<DeviceLink::Application::TransportError>(events[1].payload),
            "Second event payload differs");
    Require(queue.Drain().empty(), "Drain did not empty the queue");
}

void WindowBridgeDispatchesQueuedEventsOnDemand()
{
    DeviceLink::Application::DeviceEventQueue queue;
    std::vector<DeviceLink::Application::DeviceEvent> handledEvents;
    WindowHandle window(::CreateWindowExW(
        0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr));
    Require(window.Get() != nullptr, "Message-only window creation failed");
    DeviceLink::UI::WindowEventBridge bridge(
        queue, window.Get(), WM_APP + 1,
        [&](DeviceLink::Application::DeviceEvent event) {
            handledEvents.push_back(std::move(event));
        });
    Require(bridge.Attach(), "Window bridge attachment failed");

    queue.Push({
        .deviceId = "ui-device",
        .payload = DeviceLink::Application::ConnectionStateChanged{ConnectionState::Connected},
    });
    MSG message{};
    Require(::PeekMessageW(&message, window.Get(), WM_APP + 1, WM_APP + 1, PM_REMOVE) != FALSE,
            "Window bridge did not post a notification");
    Require(bridge.HandleMessage(message.message), "Window bridge rejected its notification");
    Require(handledEvents.size() == 1 && handledEvents.front().deviceId == "ui-device",
            "Window bridge event differs");
    Require(queue.Size() == 0, "Window bridge did not drain the queue");
    bridge.Detach();
    Require(!bridge.HandleMessage(WM_APP + 1), "Detached bridge accepted a message");
}

void DashboardModelRetainsDisplayHistory()
{
    DeviceLink::UI::DashboardModel model(2);
    model.ApplyEvent({
        .deviceId = "dashboard-device",
        .payload = DeviceLink::Application::ConnectionStateChanged{ConnectionState::Connected},
    });

    for (std::uint32_t sequence = 1; sequence <= 3; ++sequence)
    {
        DeviceLink::Protocol::PacketFrame frame{};
        frame.header.messageType = 0x7001;
        frame.header.sequence = sequence;
        frame.header.payloadSize = 1;
        frame.payload = {static_cast<std::byte>(sequence)};
        model.ApplyEvent({
            .deviceId = "dashboard-device",
            .payload = DeviceLink::Application::FrameReceived{std::move(frame)},
        });
    }
    model.ApplyEvent({
        .deviceId = "dashboard-device",
        .payload = DeviceLink::Application::TransportError{"Connection reset"},
    });

    Require(model.Devices().size() == 1 &&
                model.Devices().front().state == ConnectionState::Connected,
            "Dashboard device summary differs");
    Require(model.Packets().size() == 2 && model.Packets().front().sequence == 2 &&
                model.Packets().back().sequence == 3,
            "Dashboard packet history differs");
    Require(model.Errors().size() == 1 &&
                model.Errors().front().message == "Connection reset",
            "Dashboard error history differs");
    model.ClearHistory();
    Require(model.Packets().empty() && model.Errors().empty(), "Dashboard history was not cleared");
}

void DashboardBindingAppliesEventsAndRequestsRefresh()
{
    DeviceLink::Application::DeviceEventQueue queue;
    DeviceLink::UI::DashboardModel model;
    WindowHandle window(::CreateWindowExW(
        0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr));
    Require(window.Get() != nullptr, "Message-only window creation failed");
    std::size_t refreshCount{};
    DeviceLink::UI::DashboardBinding binding(
        queue, model, window.Get(), WM_APP + 2, [&] { ++refreshCount; });
    Require(binding.Attach(), "Dashboard binding attachment failed");

    queue.Push({
        .deviceId = "bound-device",
        .payload = DeviceLink::Application::ConnectionStateChanged{ConnectionState::Connected},
    });
    MSG message{};
    Require(::PeekMessageW(&message, window.Get(), WM_APP + 2, WM_APP + 2, PM_REMOVE) != FALSE,
            "Dashboard binding did not post a notification");
    Require(binding.HandleMessage(message.message), "Dashboard binding rejected its notification");
    Require(model.Devices().size() == 1 && model.Devices().front().deviceId == "bound-device" &&
                model.Devices().front().state == ConnectionState::Connected,
            "Dashboard binding did not update the model");
    Require(refreshCount == 1, "Dashboard binding did not request a refresh");
    binding.Detach();
}

void ManagerConnectsSessionsAndPublishesEvents()
{
    DeviceLink::Application::DeviceManager manager;
    Require(manager.RegisterDevice("simulator-3"), "Device registration failed");
    Require(manager.AttachTransport("simulator-3", std::make_unique<QueueingTransport>()),
            "Transport attachment failed");
    Require(manager.ConnectDevice("simulator-3", "loopback", 5000),
            "Managed device connection failed");
    Require(manager.GetConnectionState("simulator-3") == ConnectionState::Connected,
            "Managed device is not connected");

    const auto events = manager.Events().Drain();
    Require(events.size() == 2, "Connection state events differ");
    Require(std::holds_alternative<DeviceLink::Application::ConnectionStateChanged>(
                events[0].payload) &&
                std::holds_alternative<DeviceLink::Application::ConnectionStateChanged>(
                events[1].payload),
            "Connection events were not queued");
    Require(manager.DisconnectDevice("simulator-3"), "Managed disconnection failed");
}

void ManagerPersistsEventsThroughAsyncStore()
{
    TemporaryDatabase database;
    {
        auto repository = std::make_unique<DeviceLink::Infrastructure::SqliteEventRepository>(
            database.Path());
        DeviceLink::Infrastructure::AsyncEventStore eventStore(std::move(repository));
        DeviceLink::Application::DeviceEventPersistence persistence(
            eventStore, [] { return std::int64_t{123456}; });
        DeviceLink::Application::DeviceManager manager;
        manager.SetEventObserver([&persistence](const DeviceLink::Application::DeviceEvent& event) {
            persistence.Persist(event);
        });

        Require(manager.RegisterDevice("persisted-device"), "Persistent device registration failed");
        Require(manager.AttachTransport("persisted-device", std::make_unique<QueueingTransport>()),
                "Persistent transport attachment failed");
        Require(manager.ConnectDevice("persisted-device", "loopback", 5000),
                "Persistent device connection failed");
        eventStore.Flush();
        Require(manager.DisconnectDevice("persisted-device"), "Persistent device disconnection failed");
        eventStore.Flush();
    }

    DeviceLink::Infrastructure::SqliteEventRepository reader(database.Path());
    const auto entries = reader.ReadAll();
    Require(entries.size() == 4, "Persisted device event count differs");
    Require(entries[0].timestampUnixMilliseconds == 123456 &&
                entries[0].deviceId == "persisted-device" &&
                entries[0].category == "connection-state" &&
                entries[0].detail == "connecting",
            "Connecting event was not persisted");
    Require(entries[1].category == "connection-state" && entries[1].detail == "connected" &&
                entries[2].detail == "disconnecting" && entries[3].detail == "disconnected",
            "Persisted state transition sequence differs");
}

void EventLogQueryServiceMapsRecentStorageEntries()
{
    TemporaryDatabase database;
    DeviceLink::Infrastructure::SqliteEventRepository repository(database.Path());
    repository.Append({
        .timestampUnixMilliseconds = 1000,
        .deviceId = "query-device",
        .category = "connection-state",
        .detail = "connected",
    });
    repository.Append({
        .timestampUnixMilliseconds = 2000,
        .deviceId = "query-device",
        .category = "frame-received",
        .detail = "type=42",
        .payload = {std::byte{0x10}, std::byte{0x20}},
    });

    DeviceLink::Application::EventLogQueryService queryService(repository);
    const auto summaries = queryService.LoadRecent(1);
    Require(summaries.size() == 1 && summaries.front().timestampUnixMilliseconds == 2000 &&
                summaries.front().deviceId == "query-device" &&
                summaries.front().category == "frame-received" &&
                summaries.front().payloadByteCount == 2,
            "Event log query summary differs");

    repository.Append({
        .timestampUnixMilliseconds = 3000,
        .deviceId = "other-device",
        .category = "transport-error",
        .detail = "Connection CLOSED by peer",
    });
    std::size_t prepareCount{};
    DeviceLink::Application::EventLogQueryService filteredQuery(
        repository, [&prepareCount] { ++prepareCount; });
    const auto filtered = filteredQuery.LoadRecent({
        .deviceId = "other-device",
        .category = "transport-error",
        .containsText = "closed BY",
        .maximumEntryCount = 20,
    });
    Require(prepareCount == 1 && filtered.size() == 1 &&
                filtered.front().timestampUnixMilliseconds == 3000 &&
                filtered.front().detail == "Connection CLOSED by peer",
            "Event log filters or prepare callback differ");
    Require(filteredQuery.LoadRecent({.containsText = "missing", .maximumEntryCount = 20}).empty(),
            "Event log text filter returned a non-matching entry");
}

void CommunicationAlarmServiceTracksAndAcknowledgesTransportErrors()
{
    std::int64_t timestamp = 1000;
    DeviceLink::Application::CommunicationAlarmService alarmService(
        [&timestamp] { return timestamp += 10; });
    alarmService.ApplyDeviceEvent({
        .deviceId = "alarm-device",
        .payload = DeviceLink::Application::ConnectionStateChanged{ConnectionState::Connected},
    });
    alarmService.ApplyDeviceEvent({
        .deviceId = "alarm-device",
        .payload = DeviceLink::Application::TransportError{"Connection closed by peer"},
    });
    alarmService.ApplyDeviceEvent({
        .deviceId = "alarm-device",
        .payload = DeviceLink::Application::TransportError{"Connection closed by peer"},
    });

    const auto alarms = alarmService.ActiveAlarms();
    Require(alarms.size() == 1 && alarms.front().id == 1 &&
                alarms.front().timestampUnixMilliseconds == 1020 &&
                alarms.front().occurrenceCount == 2,
            "Communication alarm aggregation differs");
    Require(alarmService.Acknowledge(alarms.front().id), "Alarm acknowledgement failed");
    Require(!alarmService.Acknowledge(alarms.front().id) && alarmService.ActiveAlarms().empty(),
            "Acknowledged alarm remained active");
}

void CommunicationAlarmServiceTracksTelemetryHeartbeatHealth()
{
    std::int64_t timestamp = 2000;
    DeviceLink::Application::CommunicationAlarmService alarmService(
        [&timestamp] { return timestamp += 10; });
    alarmService.ApplyDeviceEvent({
        .deviceId = "heartbeat-device",
        .payload = DeviceLink::Application::TelemetryHeartbeatChanged{
            DeviceLink::Application::TelemetryHeartbeatState::Warning, 1500},
    });
    alarmService.ApplyDeviceEvent({
        .deviceId = "heartbeat-device",
        .payload = DeviceLink::Application::TelemetryHeartbeatChanged{
            DeviceLink::Application::TelemetryHeartbeatState::Fault, 3000},
    });

    const auto alarms = alarmService.ActiveAlarms();
    Require(alarms.size() == 1 &&
                alarms.front().kind ==
                    DeviceLink::Application::CommunicationAlarmKind::TelemetryHeartbeat &&
                alarms.front().severity == DeviceLink::Application::AlarmSeverity::Critical &&
                alarms.front().occurrenceCount == 2 &&
                alarms.front().message.find("3000 ms") != std::string::npos,
            "Telemetry heartbeat alarm escalation differs");

    alarmService.ApplyDeviceEvent({
        .deviceId = "heartbeat-device",
        .payload = DeviceLink::Application::TelemetryHeartbeatChanged{
            DeviceLink::Application::TelemetryHeartbeatState::Healthy, 0},
    });
    Require(alarmService.ActiveAlarms().empty(),
            "Recovered telemetry heartbeat alarm remained active");
}

void DeviceEventProcessingServicePersistsAndRaisesCommunicationAlarm()
{
    TemporaryDatabase database;
    auto repository = std::make_unique<DeviceLink::Infrastructure::SqliteEventRepository>(
        database.Path());
    DeviceLink::Infrastructure::AsyncEventStore eventStore(std::move(repository));
    DeviceLink::Application::DeviceEventPersistence persistence(
        eventStore, [] { return std::int64_t{5000}; });
    DeviceLink::Application::CommunicationAlarmService alarmService(
        [] { return std::int64_t{6000}; });
    DeviceLink::Application::TelemetryService telemetryService(
        {0x7001}, 10, [] { return std::int64_t{7000}; });
    DeviceLink::Application::TelemetryQualityService telemetryQualityService(
        {0x7001}, [] { return std::int64_t{7100}; });
    DeviceLink::Application::TelemetryHeartbeatWatchdog heartbeatWatchdog(
        {0x7001},
        {.warningAfter = 1h, .faultAfter = 2h, .pollInterval = 1h});
    DeviceLink::Application::DeviceEventProcessingService processor(
        persistence, alarmService, telemetryService, telemetryQualityService, heartbeatWatchdog);
    processor.Process({
        .deviceId = "processing-device",
        .payload = DeviceLink::Application::TransportError{"Connection closed by peer"},
    });
    DeviceLink::Protocol::PacketFrame telemetryFrame{};
    telemetryFrame.header.messageType = 0x7001;
    telemetryFrame.header.sequence = 9;
    telemetryFrame.header.payloadSize = 1;
    telemetryFrame.payload = {std::byte{0x42}};
    processor.Process({
        .deviceId = "processing-device",
        .payload = DeviceLink::Application::FrameReceived{std::move(telemetryFrame)},
    });
    eventStore.Flush();

    DeviceLink::Infrastructure::SqliteEventRepository reader(database.Path());
    const auto entries = reader.ReadAll();
    const auto alarms = alarmService.ActiveAlarms();
    const auto telemetrySamples = telemetryService.RecentSamples();
    const auto telemetryQuality = telemetryQualityService.MetricsFor("processing-device");
    Require(entries.size() == 2 && entries.front().category == "transport-error" &&
                entries.front().detail == "Connection closed by peer" &&
                entries.back().category == "frame-received" && alarms.size() == 1 &&
                alarms.front().deviceId == "processing-device" && telemetrySamples.size() == 1 &&
                telemetrySamples.front().sequence == 9 && telemetryQuality &&
                telemetryQuality->receivedFrameCount == 1,
            "Device event processing result differs");
}

void DeviceRuntimeRoutesManagerEventsToEventProcessingPipeline()
{
    TemporaryDatabase database;
    DeviceLink::Application::DeviceRuntime runtime(
        std::make_unique<DeviceLink::Infrastructure::SqliteEventRepository>(database.Path()),
        {0x7001}, 10, [] { return std::int64_t{8000}; });
    Require(runtime.Devices().RegisterDevice("runtime-device"),
            "Runtime device registration failed");
    Require(runtime.Devices().AttachTransport("runtime-device", std::make_unique<QueueingTransport>()),
            "Runtime transport attachment failed");
    Require(runtime.Devices().ConnectDevice("runtime-device", "loopback", 5000),
            "Runtime device connection failed");
    Require(runtime.Devices().DisconnectDevice("runtime-device"),
            "Runtime device disconnection failed");
    runtime.FlushEventLog();

    DeviceLink::Infrastructure::SqliteEventRepository reader(database.Path());
    const auto entries = reader.ReadAll();
    Require(entries.size() == 6 && entries.front().category == "connection-state" &&
                entries.front().detail == "connecting" && entries.back().detail == "disconnected" &&
                entries[2].category == "telemetry-heartbeat" &&
                entries[2].detail == "state=waiting;age-ms=0" &&
                entries[4].category == "telemetry-heartbeat" &&
                entries[4].detail == "state=inactive;age-ms=0",
            "Runtime event processing pipeline differs");
}

void TelemetryServiceFiltersFramesAndLimitsHistory()
{
    std::int64_t timestamp{};
    DeviceLink::Application::TelemetryService telemetryService(
        {0x7001, 0x7001}, 2, [&timestamp] { return timestamp += 10; });
    DeviceLink::Protocol::PacketFrame nonTelemetry{};
    nonTelemetry.header.messageType = 0x1001;
    Require(!telemetryService.ApplyDeviceEvent({
                .deviceId = "telemetry-device",
                .payload = DeviceLink::Application::FrameReceived{nonTelemetry},
            }),
            "Non-telemetry frame was accepted");

    for (std::uint32_t sequence = 1; sequence <= 3; ++sequence)
    {
        DeviceLink::Protocol::PacketFrame telemetry{};
        telemetry.header.messageType = 0x7001;
        telemetry.header.sequence = sequence;
        telemetry.header.payloadSize = 1;
        telemetry.payload = {static_cast<std::byte>(sequence)};
        Require(telemetryService.ApplyDeviceEvent({
                    .deviceId = "telemetry-device",
                    .payload = DeviceLink::Application::FrameReceived{std::move(telemetry)},
                }),
                "Telemetry frame was rejected");
    }

    const auto samples = telemetryService.RecentSamples();
    Require(samples.size() == 2 && samples[0].timestampUnixMilliseconds == 20 &&
                samples[0].sequence == 2 && samples[1].sequence == 3 &&
                samples[1].payloadByteCount == 1,
            "Telemetry sample history differs");
}

void AxisVapixCommandAdapterMapsSupportedCommandsAndPositionResponse()
{
    using namespace DeviceLink::Application;
    AxisVapixCommandAdapter adapter({
        .hostName = "axis-lab.local",
        .cameraChannel = 2,
        .scanPanSpeed = 30,
        .basicCredentials = AxisVapixCredentials{.userName = "operator", .password = "secret"},
    });

    const auto setPosition = adapter.EncodeCommand({
        .kind = GimbalCommandKind::SetPanTilt,
        .panDegrees = 45.5,
        .tiltDegrees = -10.25,
    });
    Require(setPosition &&
                setPosition->target == "/axis-cgi/com/ptz.cgi?pan=45.5&tilt=-10.25&camera=2" &&
                setPosition->serializedRequest.find("Host: axis-lab.local\r\n") != std::string::npos &&
                setPosition->serializedRequest.find("Authorization: Basic b3BlcmF0b3I6c2VjcmV0\r\n") !=
                    std::string::npos,
            "Axis VAPIX absolute PTZ request differs");

    const auto startScan = adapter.EncodeCommand({.kind = GimbalCommandKind::StartScan});
    const auto stopScan = adapter.EncodeCommand({.kind = GimbalCommandKind::StopScan});
    const auto status = adapter.EncodeCommand({.kind = GimbalCommandKind::RequestStatus});
    Require(startScan && startScan->target.find("continuouspantiltmove=30,0") != std::string::npos &&
                stopScan && stopScan->target.find("move=stop") != std::string::npos &&
                status && status->target.find("query=position") != std::string::npos &&
                !adapter.EncodeCommand({.kind = GimbalCommandKind::Power}),
            "Axis VAPIX command support boundary differs");

    const auto position = AxisVapixCommandAdapter::DecodePositionResponse(
        200, "pan=45.5\r\ntilt=-10.25\r\nzoom=1250\r\n");
    Require(position && position->panDegrees == 45.5 && position->tiltDegrees == -10.25 &&
                position->zoom && *position->zoom == 1250.0 &&
                !AxisVapixCommandAdapter::DecodePositionResponse(401, "pan=0\r\ntilt=0\r\n") &&
                !AxisVapixCommandAdapter::DecodePositionResponse(200, "pan=0\r\n"),
            "Axis VAPIX position response differs");
}

void TelemetryQualityServiceMeasuresIntervalJitterAndLoss()
{
    std::int64_t timestamp{};
    DeviceLink::Application::TelemetryQualityService qualityService(
        {0x7001}, [&timestamp] { return timestamp; });
    const auto publishTelemetry = [&qualityService](std::uint32_t sequence) {
        DeviceLink::Protocol::PacketFrame frame{};
        frame.header.messageType = 0x7001;
        frame.header.sequence = sequence;
        return qualityService.ApplyDeviceEvent({
            .deviceId = "quality-device",
            .payload = DeviceLink::Application::FrameReceived{std::move(frame)},
        });
    };

    timestamp = 100;
    Require(publishTelemetry(10), "First telemetry quality sample was rejected");
    timestamp = 200;
    Require(publishTelemetry(11), "Second telemetry quality sample was rejected");
    timestamp = 350;
    Require(publishTelemetry(14), "Loss telemetry quality sample was rejected");
    timestamp = 450;
    Require(publishTelemetry(13), "Out-of-order telemetry quality sample was rejected");

    const auto metrics = qualityService.MetricsFor("quality-device");
    Require(metrics && metrics->receivedFrameCount == 4 &&
                metrics->estimatedMissingFrameCount == 2 && metrics->outOfOrderFrameCount == 1 &&
                metrics->lastIntervalMilliseconds == 100 &&
                metrics->averageIntervalMilliseconds == 116 &&
                metrics->averageJitterMilliseconds == 50 &&
                metrics->deliveryRatePercent > 66.6 && metrics->deliveryRatePercent < 66.7 &&
                !qualityService.MetricsFor("missing-device"),
            "Telemetry quality metrics differ");
}

void TelemetryHeartbeatWatchdogTransitionsAndRecovers()
{
    std::atomic<std::int64_t> monotonicMilliseconds{0};
    DeviceLink::Application::TelemetryHeartbeatWatchdog watchdog(
        {0x7001, 0x7001},
        {.warningAfter = 150ms, .faultAfter = 300ms, .pollInterval = 1h},
        [&monotonicMilliseconds] { return monotonicMilliseconds.load(); });
    watchdog.SetOptions({.warningAfter = 100ms, .faultAfter = 200ms, .pollInterval = 1h});
    const auto configuredOptions = watchdog.Options();
    bool rejectedInvalidOptions{};
    try
    {
        watchdog.SetOptions({.warningAfter = 200ms, .faultAfter = 200ms, .pollInterval = 1h});
    }
    catch (const std::invalid_argument&)
    {
        rejectedInvalidOptions = true;
    }
    std::vector<DeviceLink::Application::TelemetryHeartbeatChanged> changes;
    watchdog.SetEventHandler([&changes](DeviceLink::Application::DeviceEvent event) {
        if (const auto* change =
                std::get_if<DeviceLink::Application::TelemetryHeartbeatChanged>(&event.payload))
        {
            changes.push_back(*change);
        }
    });

    watchdog.ApplyDeviceEvent({
        .deviceId = "heartbeat-device",
        .payload = DeviceLink::Application::ConnectionStateChanged{ConnectionState::Connected},
    });
    monotonicMilliseconds = 99;
    watchdog.EvaluateNow();
    monotonicMilliseconds = 100;
    watchdog.EvaluateNow();
    monotonicMilliseconds = 200;
    watchdog.EvaluateNow();

    DeviceLink::Protocol::PacketFrame telemetry{};
    telemetry.header.messageType = 0x7001;
    watchdog.ApplyDeviceEvent({
        .deviceId = "heartbeat-device",
        .payload = DeviceLink::Application::FrameReceived{std::move(telemetry)},
    });
    const auto healthy = watchdog.StatusFor("heartbeat-device");
    watchdog.ApplyDeviceEvent({
        .deviceId = "heartbeat-device",
        .payload = DeviceLink::Application::ConnectionStateChanged{ConnectionState::Disconnected},
    });

    using DeviceLink::Application::TelemetryHeartbeatState;
    Require(changes.size() == 5 && changes[0].state == TelemetryHeartbeatState::Waiting &&
                changes[1].state == TelemetryHeartbeatState::Warning &&
                changes[1].ageMilliseconds == 100 &&
                changes[2].state == TelemetryHeartbeatState::Fault &&
                changes[2].ageMilliseconds == 200 &&
                changes[3].state == TelemetryHeartbeatState::Healthy &&
                changes[4].state == TelemetryHeartbeatState::Inactive &&
                healthy.state == TelemetryHeartbeatState::Healthy &&
                healthy.ageMilliseconds == 0 && configuredOptions.warningAfter == 100ms &&
                configuredOptions.faultAfter == 200ms && rejectedInvalidOptions,
            "Telemetry heartbeat watchdog transition sequence differs");
}

void ScenarioRunnerExecutesManagedDeviceActions()
{
    DeviceLink::Application::DeviceManager manager;
    Require(manager.RegisterDevice("scenario-device"), "Scenario device registration failed");
    Require(manager.AttachTransport("scenario-device", std::make_unique<QueueingTransport>()),
            "Scenario transport attachment failed");

    DeviceLink::Protocol::PacketFrame frame{};
    frame.header.messageType = 0x6100;
    frame.header.sequence = 5;
    frame.header.payloadSize = 1;
    frame.payload = {std::byte{0x45}};

    DeviceLink::Application::ScenarioRunner runner(manager);
    std::mutex progressMutex;
    std::vector<DeviceLink::Application::ScenarioProgress> progressEvents;
    runner.SetProgressObserver([&progressMutex, &progressEvents](
        const DeviceLink::Application::ScenarioProgress& progress) {
        const std::scoped_lock lock(progressMutex);
        progressEvents.push_back(progress);
    });
    auto resultFuture = runner.Start({
        {.delayBeforeAction = 0ms,
            .action = DeviceLink::Application::ConnectScenarioAction{
                "scenario-device", "loopback", 5000}},
        {.delayBeforeAction = 0ms,
            .action = DeviceLink::Application::SendFrameScenarioAction{"scenario-device", frame}},
        {.delayBeforeAction = 0ms,
            .action = DeviceLink::Application::DisconnectScenarioAction{"scenario-device"}},
    });
    Require(resultFuture.has_value(), "Scenario did not start");
    Require(resultFuture->wait_for(2s) == std::future_status::ready,
            "Scenario did not complete");
    const auto result = resultFuture->get();
    Require(result.status == DeviceLink::Application::ScenarioStatus::Completed &&
                result.completedStepCount == 3,
            "Scenario result differs");
    Require(!runner.IsRunning(), "Completed scenario remains marked as running");
    {
        const std::scoped_lock lock(progressMutex);
        Require(progressEvents.size() == 5 &&
                    progressEvents[0].kind == DeviceLink::Application::ScenarioProgressKind::Started &&
                    progressEvents[1].kind == DeviceLink::Application::ScenarioProgressKind::StepCompleted &&
                    progressEvents[3].completedStepCount == 3 &&
                    progressEvents[4].kind == DeviceLink::Application::ScenarioProgressKind::Finished &&
                    progressEvents[4].status == DeviceLink::Application::ScenarioStatus::Completed,
                "Scenario progress sequence differs");
    }
    Require(manager.GetConnectionState("scenario-device") == ConnectionState::Disconnected,
            "Scenario did not disconnect the device");
    Require(!runner.Start({}).has_value(), "Scenario runner accepted an empty scenario");
    auto secondResultFuture = runner.Start({
        {.delayBeforeAction = 0ms,
            .action = DeviceLink::Application::ConnectScenarioAction{
                "scenario-device", "loopback", 5000}},
        {.delayBeforeAction = 0ms,
            .action = DeviceLink::Application::DisconnectScenarioAction{"scenario-device"}},
    });
    Require(secondResultFuture.has_value() &&
                secondResultFuture->wait_for(2s) == std::future_status::ready,
            "Scenario runner did not restart");
    const auto secondResult = secondResultFuture->get();
    Require(secondResult.status == DeviceLink::Application::ScenarioStatus::Completed &&
                secondResult.completedStepCount == 2,
            "Restarted scenario result differs");
}

void ScenarioRunnerCancelsDuringDelay()
{
    DeviceLink::Application::DeviceManager manager;
    DeviceLink::Application::ScenarioRunner runner(manager);
    auto resultFuture = runner.Start({
        {.delayBeforeAction = 5s,
            .action = DeviceLink::Application::DisconnectScenarioAction{"not-executed"}},
    });
    Require(resultFuture.has_value(), "Cancellable scenario did not start");

    runner.Stop();
    Require(resultFuture->wait_for(100ms) == std::future_status::ready,
            "Cancelled scenario did not finish promptly");
    const auto result = resultFuture->get();
    Require(result.status == DeviceLink::Application::ScenarioStatus::Cancelled &&
                result.completedStepCount == 0,
            "Cancelled scenario result differs");
    Require(!runner.IsRunning(), "Cancelled scenario remains marked as running");
}

void ScenarioRunnerReportsFailureDetails()
{
    DeviceLink::Application::DeviceManager manager;
    DeviceLink::Application::ScenarioRunner runner(manager);
    std::mutex progressMutex;
    std::vector<DeviceLink::Application::ScenarioProgress> progressEvents;
    runner.SetProgressObserver([&progressMutex, &progressEvents](
        const DeviceLink::Application::ScenarioProgress& progress) {
        const std::scoped_lock lock(progressMutex);
        progressEvents.push_back(progress);
    });
    auto resultFuture = runner.Start({
        {.delayBeforeAction = 0ms,
            .action = DeviceLink::Application::SendFrameScenarioAction{"missing-device", {}}},
    });
    Require(resultFuture.has_value() && resultFuture->wait_for(2s) == std::future_status::ready,
            "Failing scenario did not complete");
    const auto result = resultFuture->get();
    Require(result.status == DeviceLink::Application::ScenarioStatus::Failed &&
                result.failedStepIndex == 0 &&
                result.failureMessage == "Send failed for device 'missing-device'",
            "Scenario failure details differ");
    const std::scoped_lock lock(progressMutex);
    Require(progressEvents.size() == 2 &&
                progressEvents.back().kind == DeviceLink::Application::ScenarioProgressKind::Finished &&
                progressEvents.back().failedStepIndex == 0 &&
                progressEvents.back().failureMessage == result.failureMessage,
            "Scenario failure progress differs");
}

void ScenarioProgressIsQueuedFromWorker()
{
    DeviceLink::Application::DeviceManager manager;
    Require(manager.RegisterDevice("progress-device"), "Progress device registration failed");
    Require(manager.AttachTransport("progress-device", std::make_unique<QueueingTransport>()),
            "Progress transport attachment failed");
    DeviceLink::Application::ScenarioRunner runner(manager);
    DeviceLink::Application::ScenarioProgressQueue progressQueue;
    std::mutex wakeupMutex;
    std::condition_variable wakeupCondition;
    progressQueue.SetWakeupHandler([&] { wakeupCondition.notify_one(); });
    runner.SetProgressObserver([&progressQueue](
        const DeviceLink::Application::ScenarioProgress& progress) {
        progressQueue.Push(progress);
    });

    auto resultFuture = runner.Start({
        {.delayBeforeAction = 0ms,
            .action = DeviceLink::Application::ConnectScenarioAction{
                "progress-device", "loopback", 5000}},
        {.delayBeforeAction = 0ms,
            .action = DeviceLink::Application::DisconnectScenarioAction{"progress-device"}},
    });
    Require(resultFuture.has_value() && resultFuture->wait_for(2s) == std::future_status::ready,
            "Progress scenario did not complete");
    {
        std::unique_lock lock(wakeupMutex);
        Require(wakeupCondition.wait_for(lock, 2s, [&] { return progressQueue.Size() == 4; }),
                "Progress events were not queued");
    }
    const auto progressEvents = progressQueue.Drain();
    Require(progressEvents.size() == 4 &&
                progressEvents.front().kind == DeviceLink::Application::ScenarioProgressKind::Started &&
                progressEvents[2].completedStepCount == 2 &&
                progressEvents.back().kind == DeviceLink::Application::ScenarioProgressKind::Finished,
            "Queued progress sequence differs");
}

void ScenarioTextCodecRoundTripsAndRejectsInvalidInput()
{
    DeviceLink::Protocol::PacketFrame frame{};
    frame.header.messageType = 0x8100;
    frame.header.sequence = 33;
    frame.header.payloadSize = 2;
    frame.payload = {std::byte{0xAB}, std::byte{0xCD}};
    const std::vector<DeviceLink::Application::ScenarioStep> steps{
        {.delayBeforeAction = 10ms,
            .action = DeviceLink::Application::ConnectScenarioAction{
                "codec-device", "127.0.0.1", 5000}},
        {.delayBeforeAction = 20ms,
            .action = DeviceLink::Application::SendFrameScenarioAction{"codec-device", frame}},
        {.delayBeforeAction = 0ms,
            .action = DeviceLink::Application::DisconnectScenarioAction{"codec-device"}},
    };

    const auto text = DeviceLink::Application::ScenarioTextCodec::Serialize(steps);
    Require(text.has_value(), "Scenario serialization failed");
    const auto parsed = DeviceLink::Application::ScenarioTextCodec::Parse(*text);
    Require(parsed.Succeeded() && parsed.steps.size() == 3,
            "Scenario round-trip parsing failed");
    const auto* connect = std::get_if<DeviceLink::Application::ConnectScenarioAction>(
        &parsed.steps[0].action);
    const auto* send = std::get_if<DeviceLink::Application::SendFrameScenarioAction>(
        &parsed.steps[1].action);
    const auto* disconnect = std::get_if<DeviceLink::Application::DisconnectScenarioAction>(
        &parsed.steps[2].action);
    Require(connect != nullptr && connect->host == "127.0.0.1" && connect->port == 5000 &&
                send != nullptr && send->frame.header.messageType == frame.header.messageType &&
                send->frame.header.sequence == frame.header.sequence && send->frame.payload == frame.payload &&
                disconnect != nullptr && disconnect->deviceId == "codec-device",
            "Scenario round-trip values differ");

    const auto invalidPayload = DeviceLink::Application::ScenarioTextCodec::Parse(
        "0 SEND codec-device 1 2 A\n");
    Require(!invalidPayload.Succeeded() && invalidPayload.error->lineNumber == 1,
            "Odd-length payload was accepted");
    const auto invalidPort = DeviceLink::Application::ScenarioTextCodec::Parse(
        "0 CONNECT codec-device localhost 70000\n");
    Require(!invalidPort.Succeeded() && invalidPort.error->lineNumber == 1,
            "Out-of-range port was accepted");
}

void ScenarioFileServiceSavesAndLoadsScenario()
{
    TemporaryDatabase file;
    DeviceLink::Infrastructure::AtomicTextFileStore fileStore;
    DeviceLink::Application::ScenarioFileService service(fileStore);
    DeviceLink::Protocol::PacketFrame frame{};
    frame.header.messageType = 0x9001;
    frame.header.sequence = 4;
    frame.header.payloadSize = 1;
    frame.payload = {std::byte{0x7F}};
    const std::vector<DeviceLink::Application::ScenarioStep> steps{
        {.delayBeforeAction = 3ms,
            .action = DeviceLink::Application::ConnectScenarioAction{
                "file-device", "127.0.0.1", 5000}},
        {.delayBeforeAction = 5ms,
            .action = DeviceLink::Application::SendFrameScenarioAction{"file-device", frame}},
    };

    const auto saved = service.Save(file.Path(), steps);
    Require(saved.succeeded, "Scenario file save failed");
    const auto loaded = service.Load(file.Path());
    Require(loaded.Succeeded() && loaded.steps.size() == 2,
            "Scenario file load failed");
    const auto* action = std::get_if<DeviceLink::Application::SendFrameScenarioAction>(
        &loaded.steps[1].action);
    Require(action != nullptr && action->frame.header.sequence == 4 &&
                action->frame.payload == frame.payload,
            "Scenario file contents differ");

    DeviceLink::Application::DeviceManager manager;
    Require(manager.RegisterDevice("file-device"), "Scenario file device registration failed");
    auto transport = std::make_unique<QueueingTransport>();
    auto* const recordingTransport = transport.get();
    Require(manager.AttachTransport("file-device", std::move(transport)),
            "Scenario file transport attachment failed");
    DeviceLink::Application::ScenarioRunner runner(manager);
    auto started = service.LoadAndStart(file.Path(), runner);
    Require(started.Succeeded() && started.completion->wait_for(2s) == std::future_status::ready,
            "Scenario file execution did not complete");
    const auto execution = started.completion->get();
    Require(execution.status == DeviceLink::Application::ScenarioStatus::Completed &&
                execution.completedStepCount == 2,
            "Scenario file execution result differs");
    const auto expectedFrame = DeviceLink::Protocol::SerializeFrame(frame);
    const auto sentFrames = recordingTransport->SentFrames();
    Require(expectedFrame.has_value() && sentFrames.size() == 1 && sentFrames.front() == *expectedFrame,
            "Scenario file execution bytes differ");

    Require(fileStore.WriteAtomically(file.Path(), "invalid scenario\n").succeeded,
            "Invalid scenario setup failed");
    const auto invalid = service.Load(file.Path());
    Require(!invalid.Succeeded() && invalid.errorLineNumber == 1,
            "Invalid scenario file was accepted");
    auto invalidStart = service.LoadAndStart(file.Path(), runner);
    Require(!invalidStart.Succeeded() && invalidStart.errorLineNumber == 1,
            "Invalid scenario file was started");
}

void FrameReplayRunnerReplaysValidatedFrames()
{
    DeviceLink::Application::DeviceManager manager;
    Require(manager.RegisterDevice("replay-device"), "Replay device registration failed");
    auto transport = std::make_unique<QueueingTransport>();
    auto* const recordingTransport = transport.get();
    Require(manager.AttachTransport("replay-device", std::move(transport)),
            "Replay transport attachment failed");
    Require(manager.ConnectDevice("replay-device", "loopback", 5000),
            "Replay device connection failed");

    DeviceLink::Protocol::PacketFrame first{};
    first.header.messageType = 0x7101;
    first.header.sequence = 1;
    first.header.payloadSize = 1;
    first.payload = {std::byte{0x01}};
    DeviceLink::Protocol::PacketFrame second{};
    second.header.messageType = 0x7102;
    second.header.sequence = 2;
    second.header.payloadSize = 2;
    second.payload = {std::byte{0x02}, std::byte{0x03}};

    DeviceLink::Application::FrameReplayRunner replay(manager);
    auto resultFuture = replay.Start("replay-device", {
        {.delayBeforeSend = 0ms, .frame = first},
        {.delayBeforeSend = 10ms, .frame = second},
    });
    Require(resultFuture.has_value(), "Replay did not start");
    Require(resultFuture->wait_for(2s) == std::future_status::ready,
            "Replay did not complete");
    const auto result = resultFuture->get();
    Require(result.status == DeviceLink::Application::ScenarioStatus::Completed &&
                result.completedStepCount == 2,
            "Replay result differs");

    const auto sentFrames = recordingTransport->SentFrames();
    Require(sentFrames.size() == 2, "Replay frame count differs");
    const auto firstBytes = DeviceLink::Protocol::SerializeFrame(first);
    const auto secondBytes = DeviceLink::Protocol::SerializeFrame(second);
    Require(firstBytes.has_value() && secondBytes.has_value() &&
                sentFrames[0] == *firstBytes && sentFrames[1] == *secondBytes,
            "Replay bytes differ");
    Require(!replay.Start("", {}).has_value(), "Replay accepted an empty device ID");
    Require(manager.DisconnectDevice("replay-device"), "Replay device disconnection failed");
}

void SqliteFrameReplaySourceLoadsPersistedFrames()
{
    TemporaryDatabase database;
    DeviceLink::Protocol::PacketFrame first{};
    first.header.messageType = 0x7201;
    first.header.sequence = 11;
    first.header.payloadSize = 1;
    first.payload = {std::byte{0x04}};
    DeviceLink::Protocol::PacketFrame second{};
    second.header.messageType = 0x7202;
    second.header.sequence = 12;
    second.header.payloadSize = 2;
    second.payload = {std::byte{0x05}, std::byte{0x06}};

    {
        auto repository = std::make_unique<DeviceLink::Infrastructure::SqliteEventRepository>(
            database.Path());
        DeviceLink::Infrastructure::AsyncEventStore eventStore(std::move(repository));
        std::size_t timestampIndex{};
        DeviceLink::Application::DeviceEventPersistence persistence(
            eventStore, [&timestampIndex] {
                return timestampIndex++ == 0 ? std::int64_t{1000} : std::int64_t{1025};
            });
        persistence.Persist({
            .deviceId = "replay-source-device",
            .payload = DeviceLink::Application::FrameSent{first},
        });
        persistence.Persist({
            .deviceId = "replay-source-device",
            .payload = DeviceLink::Application::FrameSent{second},
        });
        eventStore.Flush();
    }

    DeviceLink::Infrastructure::SqliteEventRepository reader(database.Path());
    DeviceLink::Application::SqliteFrameReplaySource source(reader);
    const auto replayFrames = source.Load("replay-source-device");
    Require(replayFrames.size() == 2, "Persisted replay frame count differs");
    Require(replayFrames[0].delayBeforeSend == 0ms &&
                replayFrames[0].frame.header.sequence == first.header.sequence &&
                replayFrames[0].frame.payload == first.payload,
            "First persisted replay frame differs");
    Require(replayFrames[1].delayBeforeSend == 25ms &&
                replayFrames[1].frame.header.sequence == second.header.sequence &&
                replayFrames[1].frame.payload == second.payload,
            "Second persisted replay frame differs");

    DeviceLink::Application::DeviceManager manager;
    Require(manager.RegisterDevice("replay-source-device"),
            "Stored replay device registration failed");
    auto transport = std::make_unique<QueueingTransport>();
    auto* const recordingTransport = transport.get();
    Require(manager.AttachTransport("replay-source-device", std::move(transport)),
            "Stored replay transport attachment failed");
    Require(manager.ConnectDevice("replay-source-device", "loopback", 5000),
            "Stored replay device connection failed");

    DeviceLink::Application::FrameReplayRunner runner(manager);
    auto resultFuture = runner.StartFromStoredFrames("replay-source-device", source);
    Require(resultFuture.has_value() && resultFuture->wait_for(2s) == std::future_status::ready,
            "Stored replay did not complete");
    const auto result = resultFuture->get();
    Require(result.status == DeviceLink::Application::ScenarioStatus::Completed &&
                result.completedStepCount == 2,
            "Stored replay result differs");
    const auto sentFrames = recordingTransport->SentFrames();
    const auto firstBytes = DeviceLink::Protocol::SerializeFrame(first);
    const auto secondBytes = DeviceLink::Protocol::SerializeFrame(second);
    Require(firstBytes.has_value() && secondBytes.has_value() && sentFrames.size() == 2 &&
                sentFrames[0] == *firstBytes && sentFrames[1] == *secondBytes,
            "Stored replay bytes differ");
    Require(!runner.StartFromStoredFrames("unknown-device", source).has_value(),
            "Stored replay accepted an empty source");
    Require(manager.DisconnectDevice("replay-source-device"),
            "Stored replay device disconnection failed");
}

void ManagerRoundTripsFramesOverTcp()
{
    FrameEchoServer server;
    DeviceLink::Application::DeviceManager manager;
    std::mutex mutex;
    std::condition_variable condition;
    manager.Events().SetWakeupHandler([&] { condition.notify_one(); });

    Require(manager.RegisterDevice("loopback-device"), "Device registration failed");
    Require(manager.AttachTransport("loopback-device",
                std::make_unique<DeviceLink::Transport::TcpTransport>()),
            "TCP transport attachment failed");
    Require(manager.ConnectDevice("loopback-device", "127.0.0.1", server.Port()),
            "TCP device connection failed");
    Require(manager.GetConnectionState("loopback-device") == ConnectionState::Connected,
            "TCP device is not connected");
    const auto connectionEvents = manager.Events().Drain();
    Require(connectionEvents.size() == 2, "TCP connection events differ");

    DeviceLink::Protocol::PacketFrame outgoing{};
    outgoing.header.messageType = 42;
    outgoing.header.sequence = 73;
    outgoing.payload = {std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
    outgoing.header.payloadSize = static_cast<std::uint32_t>(outgoing.payload.size());
    Require(manager.SendFrame("loopback-device", outgoing), "TCP frame send failed");

    {
        std::unique_lock lock(mutex);
        Require(condition.wait_for(lock, 2s, [&] { return manager.Events().Size() >= 2; }),
                "Timed out waiting for echoed frame event");
    }

    const auto events = manager.Events().Drain();
    bool receivedEcho{};
    for (const auto& event : events)
    {
        if (event.deviceId != "loopback-device")
        {
            continue;
        }
        const auto* frameEvent = std::get_if<DeviceLink::Application::FrameReceived>(&event.payload);
        if (frameEvent != nullptr && frameEvent->frame.header.messageType == outgoing.header.messageType &&
            frameEvent->frame.header.sequence == outgoing.header.sequence &&
            frameEvent->frame.payload == outgoing.payload)
        {
            receivedEcho = true;
        }
    }
    Require(receivedEcho, "Echoed frame event differs");
    Require(manager.DisconnectDevice("loopback-device"), "TCP device disconnection failed");
}

void ManagerOperatesMultipleTcpDevicesIndependently()
{
    FrameEchoServer firstServer;
    FrameEchoServer secondServer;
    DeviceLink::Application::DeviceManager manager;
    std::mutex mutex;
    std::condition_variable condition;
    manager.Events().SetWakeupHandler([&] { condition.notify_one(); });

    Require(manager.RegisterDevice("multi-first") && manager.RegisterDevice("multi-second"),
            "Multi-device registration failed");
    Require(manager.AttachTransport("multi-first",
                std::make_unique<DeviceLink::Transport::TcpTransport>()) &&
                manager.AttachTransport("multi-second",
                std::make_unique<DeviceLink::Transport::TcpTransport>()),
            "Multi-device transport attachment failed");
    Require(manager.ConnectDevice("multi-first", "127.0.0.1", firstServer.Port()) &&
                manager.ConnectDevice("multi-second", "127.0.0.1", secondServer.Port()),
            "Multi-device connection failed");
    Require(manager.Events().Drain().size() == 4,
            "Multi-device connection event count differs");

    DeviceLink::Protocol::PacketFrame firstFrame{};
    firstFrame.header.messageType = 0x1001;
    firstFrame.header.sequence = 1;
    firstFrame.header.payloadSize = 1;
    firstFrame.payload = {std::byte{0x11}};
    DeviceLink::Protocol::PacketFrame secondFrame{};
    secondFrame.header.messageType = 0x1002;
    secondFrame.header.sequence = 2;
    secondFrame.header.payloadSize = 2;
    secondFrame.payload = {std::byte{0x22}, std::byte{0x33}};
    Require(manager.SendFrame("multi-first", firstFrame) &&
                manager.SendFrame("multi-second", secondFrame),
            "Multi-device send failed");

    {
        std::unique_lock lock(mutex);
        Require(condition.wait_for(lock, 2s, [&] { return manager.Events().Size() >= 4; }),
                "Timed out waiting for multi-device echo events");
    }

    bool receivedFirst{};
    bool receivedSecond{};
    for (const auto& event : manager.Events().Drain())
    {
        const auto* const frameEvent = std::get_if<DeviceLink::Application::FrameReceived>(
            &event.payload);
        if (frameEvent == nullptr)
        {
            continue;
        }
        if (event.deviceId == "multi-first" && frameEvent->frame.payload == firstFrame.payload &&
            frameEvent->frame.header.sequence == firstFrame.header.sequence)
        {
            receivedFirst = true;
        }
        if (event.deviceId == "multi-second" && frameEvent->frame.payload == secondFrame.payload &&
            frameEvent->frame.header.sequence == secondFrame.header.sequence)
        {
            receivedSecond = true;
        }
    }
    Require(receivedFirst && receivedSecond, "Multi-device frame isolation failed");
    Require(manager.DisconnectDevice("multi-first") && manager.DisconnectDevice("multi-second"),
            "Multi-device disconnection failed");
}

void ManagerReconnectsTcpDeviceAcrossMultipleServerInstances()
{
    constexpr std::uint32_t kCycleCount = 5;
    DeviceLink::Application::DeviceManager manager;
    std::mutex mutex;
    std::condition_variable condition;
    manager.Events().SetWakeupHandler([&] { condition.notify_one(); });
    Require(manager.RegisterDevice("reconnect-peer"), "Reconnect device registration failed");
    Require(manager.AttachTransport("reconnect-peer",
                std::make_unique<DeviceLink::Transport::TcpTransport>()),
            "Reconnect transport attachment failed");

    for (std::uint32_t cycle{}; cycle < kCycleCount; ++cycle)
    {
        FrameEchoServer server;
        Require(manager.ConnectDevice("reconnect-peer", "127.0.0.1", server.Port()),
                "Reconnect cycle connection failed");
        Require(manager.Events().Drain().size() == 2,
                "Reconnect cycle connection events differ");

        DeviceLink::Protocol::PacketFrame outgoing{};
        outgoing.header.messageType = 0x3001;
        outgoing.header.sequence = cycle;
        outgoing.header.payloadSize = 1;
        outgoing.payload = {static_cast<std::byte>(cycle)};
        Require(manager.SendFrame("reconnect-peer", outgoing), "Reconnect cycle send failed");
        {
            std::unique_lock lock(mutex);
            Require(condition.wait_for(lock, 2s, [&] { return manager.Events().Size() >= 2; }),
                    "Reconnect cycle echo timed out");
        }

        bool receivedEcho{};
        for (const auto& event : manager.Events().Drain())
        {
            if (const auto* const frameEvent = std::get_if<DeviceLink::Application::FrameReceived>(
                    &event.payload); frameEvent != nullptr &&
                frameEvent->frame.header.sequence == cycle &&
                frameEvent->frame.payload == outgoing.payload)
            {
                receivedEcho = true;
            }
        }
        Require(receivedEcho, "Reconnect cycle echo differs");
        Require(manager.DisconnectDevice("reconnect-peer"),
                "Reconnect cycle disconnection failed");
        Require(manager.GetConnectionState("reconnect-peer") == ConnectionState::Disconnected,
                "Reconnect cycle state differs");
        (void)manager.Events().Drain();
    }
}

void ManagerParsesFragmentedTcpFrames()
{
    FrameEchoServer server(ServerBehavior::FragmentedEcho);
    DeviceLink::Application::DeviceManager manager;
    std::mutex mutex;
    std::condition_variable condition;
    manager.Events().SetWakeupHandler([&] { condition.notify_one(); });

    Require(manager.RegisterDevice("fragmented-peer"), "Device registration failed");
    Require(manager.AttachTransport("fragmented-peer",
                std::make_unique<DeviceLink::Transport::TcpTransport>()),
            "TCP transport attachment failed");
    Require(manager.ConnectDevice("fragmented-peer", "127.0.0.1", server.Port()),
            "TCP device connection failed");
    (void)manager.Events().Drain();

    DeviceLink::Protocol::PacketFrame outgoing{};
    outgoing.header.messageType = 0x42;
    outgoing.header.sequence = 8;
    outgoing.header.payloadSize = 3;
    outgoing.payload = {std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
    Require(manager.SendFrame("fragmented-peer", outgoing), "TCP frame send failed");

    {
        std::unique_lock lock(mutex);
        Require(condition.wait_for(lock, 2s, [&] { return manager.Events().Size() >= 2; }),
                "Timed out waiting for fragmented echoed frame");
    }

    const auto events = manager.Events().Drain();
    bool receivedEcho{};
    for (const auto& event : events)
    {
        if (const auto* frameEvent = std::get_if<DeviceLink::Application::FrameReceived>(
                &event.payload); frameEvent != nullptr && event.deviceId == "fragmented-peer" &&
            frameEvent->frame.header.sequence == outgoing.header.sequence &&
            frameEvent->frame.payload == outgoing.payload)
        {
            receivedEcho = true;
        }
    }
    Require(receivedEcho, "Fragmented TCP frame was not reconstructed");
    Require(manager.DisconnectDevice("fragmented-peer"), "TCP device disconnection failed");
}

void ManagerFaultsWhenPeerClosesConnection()
{
    FrameEchoServer server(ServerBehavior::CloseAfterFrame);
    DeviceLink::Application::DeviceManager manager;
    std::mutex mutex;
    std::condition_variable condition;
    manager.Events().SetWakeupHandler([&] { condition.notify_one(); });

    Require(manager.RegisterDevice("closing-peer"), "Device registration failed");
    Require(manager.AttachTransport("closing-peer",
                std::make_unique<DeviceLink::Transport::TcpTransport>()),
            "TCP transport attachment failed");
    Require(manager.ConnectDevice("closing-peer", "127.0.0.1", server.Port()),
            "TCP device connection failed");
    const auto connectionEvents = manager.Events().Drain();
    Require(connectionEvents.size() == 2, "TCP connection events differ");

    DeviceLink::Protocol::PacketFrame outgoing{};
    outgoing.header.messageType = 99;
    outgoing.header.payloadSize = 1;
    outgoing.payload = {std::byte{0x01}};
    Require(manager.SendFrame("closing-peer", outgoing), "TCP frame send failed");

    {
        std::unique_lock lock(mutex);
        Require(condition.wait_for(lock, 2s, [&] {
                    return manager.GetConnectionState("closing-peer") == ConnectionState::Faulted &&
                        manager.Events().Size() >= 3;
                }),
                "Peer close did not publish the complete fault event sequence");
    }

    const auto events = manager.Events().Drain();
    bool hasFaultedState{};
    bool hasTransportError{};
    for (const auto& event : events)
    {
        if (event.deviceId != "closing-peer")
        {
            continue;
        }
        if (const auto* state = std::get_if<DeviceLink::Application::ConnectionStateChanged>(
                &event.payload); state != nullptr && state->state == ConnectionState::Faulted)
        {
            hasFaultedState = true;
        }
        if (std::holds_alternative<DeviceLink::Application::TransportError>(event.payload))
        {
            hasTransportError = true;
        }
    }
    Require(hasFaultedState, "Faulted state event was not published");
    Require(hasTransportError, "Peer close error was not published");
    Require(manager.DisconnectDevice("closing-peer"), "Faulted device disconnection failed");
}

void ManagerRecoversAfterCorruptFrame()
{
    FrameEchoServer server(ServerBehavior::CorruptThenEcho);
    DeviceLink::Application::DeviceManager manager;
    std::mutex mutex;
    std::condition_variable condition;
    manager.Events().SetWakeupHandler([&] { condition.notify_one(); });

    Require(manager.RegisterDevice("corrupt-frame-peer"), "Device registration failed");
    Require(manager.AttachTransport("corrupt-frame-peer",
                std::make_unique<DeviceLink::Transport::TcpTransport>()),
            "TCP transport attachment failed");
    Require(manager.ConnectDevice("corrupt-frame-peer", "127.0.0.1", server.Port()),
            "TCP device connection failed");
    const auto connectionEvents = manager.Events().Drain();
    Require(connectionEvents.size() == 2, "TCP connection events differ");

    DeviceLink::Protocol::PacketFrame outgoing{};
    outgoing.header.messageType = 100;
    outgoing.header.sequence = 7;
    outgoing.header.payloadSize = 1;
    outgoing.payload = {std::byte{0xAB}};
    Require(manager.SendFrame("corrupt-frame-peer", outgoing), "TCP frame send failed");

    {
        std::unique_lock lock(mutex);
        Require(condition.wait_for(lock, 2s, [&] { return manager.Events().Size() >= 2; }),
                "Timed out waiting for recovered frame event");
    }

    const auto events = manager.Events().Drain();
    bool receivedValidFrame{};
    for (const auto& event : events)
    {
        if (const auto* frameEvent = std::get_if<DeviceLink::Application::FrameReceived>(
                &event.payload); frameEvent != nullptr && event.deviceId == "corrupt-frame-peer" &&
                frameEvent->frame.header.sequence == outgoing.header.sequence &&
                frameEvent->frame.payload == outgoing.payload)
        {
            receivedValidFrame = true;
        }
        Require(!std::holds_alternative<DeviceLink::Application::TransportError>(event.payload),
                "CRC corruption caused a transport error");
    }
    Require(receivedValidFrame, "Parser did not recover after corrupt frame");
    Require(manager.GetConnectionState("corrupt-frame-peer") == ConnectionState::Connected,
            "CRC corruption changed connection state");
    Require(manager.DisconnectDevice("corrupt-frame-peer"), "TCP device disconnection failed");
}

void ManagerPublishesSimulatorTelemetry()
{
    FrameEchoServer server(ServerBehavior::SendTelemetry);
    DeviceLink::Application::DeviceManager manager;
    std::mutex mutex;
    std::condition_variable condition;
    manager.Events().SetWakeupHandler([&] { condition.notify_one(); });

    Require(manager.RegisterDevice("telemetry-peer"), "Device registration failed");
    Require(manager.AttachTransport("telemetry-peer",
                std::make_unique<DeviceLink::Transport::TcpTransport>()),
            "TCP transport attachment failed");
    Require(manager.ConnectDevice("telemetry-peer", "127.0.0.1", server.Port()),
            "TCP device connection failed");

    {
        std::unique_lock lock(mutex);
        Require(condition.wait_for(lock, 2s, [&] { return manager.Events().Size() >= 4; }),
                "Timed out waiting for telemetry events");
    }

    const auto events = manager.Events().Drain();
    std::size_t telemetryCount{};
    for (const auto& event : events)
    {
        const auto* frameEvent = std::get_if<DeviceLink::Application::FrameReceived>(&event.payload);
        if (event.deviceId == "telemetry-peer" && frameEvent != nullptr &&
            frameEvent->frame.header.messageType == 0x7001)
        {
            ++telemetryCount;
        }
    }
    Require(telemetryCount == 2, "Telemetry frame count differs");
    Require(manager.DisconnectDevice("telemetry-peer"), "TCP device disconnection failed");
}

void VirtualGimbalServiceBuildsCommandsAndDecodesResponses()
{
    DeviceLink::Application::DeviceManager manager;
    Require(manager.RegisterDevice("unreal-gimbal"), "Gimbal registration failed");
    auto transport = std::make_unique<QueueingTransport>();
    auto* transportView = transport.get();
    Require(manager.AttachTransport("unreal-gimbal", std::move(transport)),
            "Gimbal transport attachment failed");

    DeviceLink::Application::VirtualGimbalService service(manager, "unreal-gimbal");
    Require(!service.Connect("", 5000), "Empty host was accepted");
    Require(!service.Connect("127.0.0.1", 0), "Zero port was accepted");
    Require(service.Connect("127.0.0.1", 5000), "Gimbal connection failed");
    Require(service.Power(true), "Power command failed");
    Require(service.Initialize(), "Initialize command failed");
    Require(service.SetPanTilt(-12.34, 56.78), "SetPanTilt command failed");
    Require(!service.SetPanTilt(-171.0, 0.0), "Out-of-range pan was accepted");
    Require(!service.SetPanTilt(0.0, 81.0), "Out-of-range tilt was accepted");
    Require(service.StartScan(), "Start scan command failed");
    Require(service.StopScan(), "Stop scan command failed");
    Require(service.RequestStatus(), "Status command failed");
    Require(service.InjectFault(DeviceLink::Application::VirtualGimbalFault::MotorStall),
            "Fault command failed");
    Require(!service.ConfigureResponse(
                DeviceLink::Application::VirtualGimbalResponseMode::Delayed, 0),
            "Zero response delay was accepted");
    Require(service.ConfigureResponse(
                DeviceLink::Application::VirtualGimbalResponseMode::Delayed, 750),
            "Response delay command failed");
    Require(service.SelectEquipmentMode(
                DeviceLink::Application::VirtualEquipmentMode::Drone),
            "Drone mode command failed");
    Require(service.DroneTakeOff(), "Drone take-off command failed");
    Require(service.DroneMoveTo(125.4, -32.1, 10.0), "Drone move command failed");
    Require(service.DroneLand(), "Drone land command failed");
    Require(service.DroneReturnHome(), "Drone return-home command failed");

    const auto serializedFrames = transportView->SentFrames();
    Require(serializedFrames.size() == 13, "Unexpected gimbal command count");
    DeviceLink::Protocol::FrameStreamParser parser;
    std::vector<DeviceLink::Protocol::PacketFrame> frames;
    for (const auto& bytes : serializedFrames)
    {
        auto parsed = parser.Consume(bytes);
        Require(parsed.size() == 1, "Gimbal command did not produce one valid frame");
        frames.push_back(std::move(parsed.front()));
    }
    Require(frames[0].header.messageType == 0x1001 &&
            frames[0].payload == std::vector<std::byte>{std::byte{1}},
            "Power payload differs");
    Require(frames[2].header.messageType == 0x1003 && frames[2].payload ==
            std::vector<std::byte>{std::byte{0xFB}, std::byte{0x2E},
                                   std::byte{0x16}, std::byte{0x2E}},
            "SetPanTilt big-endian payload differs");
    Require(frames[7].header.messageType == 0x1008 && frames[7].payload ==
            std::vector<std::byte>{std::byte{1}, std::byte{0x02}, std::byte{0xEE}},
            "Response delay payload differs");
    Require(frames[8].header.messageType == 0x1009 && frames[8].payload ==
            std::vector<std::byte>{std::byte{1}}, "Equipment mode payload differs");
    Require(frames[10].header.messageType == 0x1012 && frames[10].payload ==
            std::vector<std::byte>{std::byte{0x04}, std::byte{0xE6},
                                   std::byte{0xFE}, std::byte{0xBF},
                                   std::byte{0x00}, std::byte{0x64}},
            "Drone coordinate payload differs");
    for (std::size_t index = 0; index < frames.size(); ++index)
    {
        Require(frames[index].header.sequence == index + 1,
                "Gimbal command sequence is not monotonic");
    }

    const std::array acknowledgement{
        std::byte{0x10}, std::byte{0x03}, std::byte{0x00}};
    const auto decodedAcknowledgement =
        DeviceLink::Application::VirtualGimbalService::DecodeAcknowledgement(
            DeviceLink::Application::VirtualGimbalService::kAcknowledgeMessageType,
            acknowledgement);
    Require(decodedAcknowledgement && decodedAcknowledgement->commandType == 0x1003 &&
            decodedAcknowledgement->succeeded, "Acknowledgement decoding failed");

    const std::array telemetry{
        std::byte{0x03}, std::byte{0x00},
        std::byte{0x04}, std::byte{0xD2},
        std::byte{0xFD}, std::byte{0xD3},
        std::byte{0x09}, std::byte{0xA4},
        std::byte{0xEE}, std::byte{0x94},
        std::byte{0x0B}, std::byte{0xB8},
        std::byte{0x5D}, std::byte{0xC0}};
    const auto decodedTelemetry =
        DeviceLink::Application::VirtualGimbalService::DecodeTelemetry(
            DeviceLink::Application::VirtualGimbalService::kTelemetryMessageType, telemetry);
    Require(decodedTelemetry &&
            decodedTelemetry->state == DeviceLink::Application::VirtualGimbalState::Scanning &&
            decodedTelemetry->panDegrees == 12.34 && decodedTelemetry->tiltDegrees == -5.57 &&
            decodedTelemetry->targetPanDegrees == 24.68 &&
            decodedTelemetry->targetTiltDegrees == -44.60 &&
            decodedTelemetry->temperatureCelsius == 30.0 &&
            decodedTelemetry->supplyVoltage == 24.0,
            "Telemetry decoding failed");
    Require(service.Disconnect(), "Gimbal disconnect failed");
}

void VirtualGimbalServiceUsesInjectedProtocolAdapter()
{
    DeviceLink::Application::DeviceManager manager;
    Require(manager.RegisterDevice("adapter-gimbal"), "Adapter device registration failed");
    auto transport = std::make_unique<QueueingTransport>();
    auto* transportView = transport.get();
    Require(manager.AttachTransport("adapter-gimbal", std::move(transport)),
        "Adapter transport attachment failed");
    DeviceLink::Application::VirtualGimbalService service(
        manager, "adapter-gimbal", {}, std::make_unique<TestGimbalProtocolAdapter>());

    Require(service.Connect("equipment.local", 6100), "Adapter service connection failed");
    Require(service.Power(true), "Injected adapter command failed");
    const auto bytes = transportView->SentFrames();
    Require(bytes.size() == 1, "Injected adapter command count differs");
    DeviceLink::Protocol::FrameStreamParser parser;
    const auto frames = parser.Consume(bytes.front());
    Require(frames.size() == 1 && frames.front().header.messageType == 0x4321 &&
            frames.front().header.sequence == 1 && frames.front().payload ==
                std::vector<std::byte>{static_cast<std::byte>(
                    DeviceLink::Application::GimbalCommandKind::Power)},
        "Injected adapter did not control frame encoding");

    const std::array successPayload{std::byte{0}};
    const auto acknowledgement = service.DecodeDeviceAcknowledgement(0x4322, successPayload);
    Require(acknowledgement && acknowledgement->succeeded &&
            service.DeviceAcknowledgeMessageType() == 0x4322 &&
            service.DeviceTelemetryMessageType() == 0x4323,
        "Injected adapter did not control response decoding or message types");
    Require(!service.DecodeDeviceAcknowledgement(0x6001, successPayload),
        "Injected adapter unexpectedly used the default response decoder");
    Require(service.Disconnect(), "Adapter service disconnection failed");
}

void VirtualGimbalServiceRetriesConnectionsWithinPolicy()
{
    const auto waitUntil = [](const auto& predicate) {
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate())
            {
                return true;
            }
            std::this_thread::sleep_for(5ms);
        }
        return predicate();
    };

    DeviceLink::Application::DeviceManager manager;
    Require(manager.RegisterDevice("retry-device"), "Retry device registration failed");
    auto transport = std::make_unique<FlakyTransport>(3);
    auto* transportView = transport.get();
    Require(manager.AttachTransport("retry-device", std::move(transport)),
            "Retry transport attachment failed");
    DeviceLink::Application::VirtualGimbalService service(
        manager, "retry-device",
        {.enabled = true, .retryInterval = 10ms, .maximumAttempts = 5});
    Require(!service.Connect("127.0.0.1", 5000),
            "Flaky transport unexpectedly connected on its first attempt");
    Require(waitUntil([&] {
                return manager.GetConnectionState("retry-device") == ConnectionState::Connected;
            }),
            "Automatic reconnect did not recover the connection");
    Require(transportView->ConnectCount() == 3 &&
                service.GetReconnectStatus().attemptCount == 0,
            "Automatic reconnect attempt state differs");

    service.SetAutomaticReconnectEnabled(false);
    transportView->SimulateConnectionLoss();
    Require(waitUntil([&] {
                return manager.GetConnectionState("retry-device") == ConnectionState::Faulted;
            }),
            "Simulated connection loss did not fault the session");
    const auto attemptsAfterLoss = transportView->ConnectCount();
    std::this_thread::sleep_for(50ms);
    Require(transportView->ConnectCount() == attemptsAfterLoss,
            "Disabled automatic reconnect still attempted a connection");
    Require(service.Disconnect(), "Retry device disconnection failed");

    DeviceLink::Application::DeviceManager exhaustedManager;
    Require(exhaustedManager.RegisterDevice("exhausted-device"),
            "Exhausted device registration failed");
    auto exhaustedTransport = std::make_unique<FlakyTransport>(100);
    auto* exhaustedView = exhaustedTransport.get();
    Require(exhaustedManager.AttachTransport("exhausted-device", std::move(exhaustedTransport)),
            "Exhausted transport attachment failed");
    DeviceLink::Application::VirtualGimbalService exhaustedService(
        exhaustedManager, "exhausted-device",
        {.enabled = true, .retryInterval = 10ms, .maximumAttempts = 3});
    Require(!exhaustedService.Connect("127.0.0.1", 5000),
            "Always-failing transport unexpectedly connected");
    Require(waitUntil([&] { return exhaustedService.GetReconnectStatus().exhausted; }),
            "Reconnect policy did not report exhaustion");
    Require(exhaustedView->ConnectCount() == 3,
            "Reconnect policy exceeded its maximum attempt count");
}

void OutboundReplayStorageDoesNotAmplifyReplayedFrames()
{
    TemporaryDatabase database;
    DeviceLink::Application::DeviceRuntime runtime(
        std::make_unique<DeviceLink::Infrastructure::SqliteEventRepository>(database.Path()),
        std::vector<std::uint16_t>{}, 10, [] { return std::int64_t{1234}; });
    Require(runtime.Devices().RegisterDevice("recording-device"),
            "Recording device registration failed");
    auto transport = std::make_unique<QueueingTransport>();
    auto* transportView = transport.get();
    Require(runtime.Devices().AttachTransport("recording-device", std::move(transport)),
            "Recording transport attachment failed");
    Require(runtime.Devices().ConnectDevice("recording-device", "loopback", 5000),
            "Recording device connection failed");

    DeviceLink::Protocol::PacketFrame command{};
    command.header.messageType = 0x1001;
    command.header.sequence = 44;
    command.header.payloadSize = 1;
    command.payload = {std::byte{1}};
    Require(runtime.Devices().SendFrame("recording-device", command),
            "Original command send failed");
    runtime.FlushEventLog();

    DeviceLink::Infrastructure::SqliteEventRepository reader(database.Path());
    DeviceLink::Application::SqliteFrameReplaySource source(reader);
    Require(source.Load("recording-device").size() == 1,
            "Original transmitted frame was not available for replay");

    DeviceLink::Application::FrameReplayRunner replay(runtime.Devices());
    auto completion = replay.StartFromStoredFrames("recording-device", source);
    Require(completion && completion->wait_for(2s) == std::future_status::ready &&
                completion->get().status == DeviceLink::Application::ScenarioStatus::Completed,
            "Stored outbound frame replay failed");
    runtime.FlushEventLog();
    Require(transportView->SentFrames().size() == 2, "Replay did not resend the command once");
    Require(source.Load("recording-device").size() == 1,
            "Replayed frame was incorrectly added back to the replay source");
    Require(runtime.Devices().DisconnectDevice("recording-device"),
            "Recording device disconnection failed");
}

void OperatorWorkflowCoordinatesScenarioProgressAndCancellation()
{
    TemporaryDatabase database;
    const auto scenarioPath = std::filesystem::path(database.Path().wstring() + L".dls");
    DeviceLink::Infrastructure::AtomicTextFileStore fileStore;
    const auto writeResult = fileStore.WriteAtomically(
        scenarioPath, "200 SEND workflow-device 4097 1 01\n");
    Require(writeResult.succeeded, "Workflow scenario fixture write failed");

    DeviceLink::Application::DeviceManager manager;
    Require(manager.RegisterDevice("workflow-device"), "Workflow device registration failed");
    Require(manager.AttachTransport("workflow-device", std::make_unique<QueueingTransport>()),
            "Workflow transport attachment failed");
    Require(manager.ConnectDevice("workflow-device", "loopback", 5000),
            "Workflow device connection failed");
    DeviceLink::Infrastructure::SqliteEventRepository reader(database.Path());
    DeviceLink::Application::SqliteFrameReplaySource source(reader);
    DeviceLink::Application::OperatorWorkflowService workflow(manager, fileStore, source);

    Require(!workflow.StartScenario({}).succeeded, "Workflow accepted an empty scenario path");
    Require(workflow.StartScenario(scenarioPath).succeeded, "Workflow scenario did not start");
    Require(!workflow.StartScenario(scenarioPath).succeeded,
            "Workflow allowed two concurrent operations");
    workflow.Stop();
    const auto progress = workflow.Progress().Drain();
    Require(!progress.empty() && progress.back().kind ==
                DeviceLink::Application::ScenarioProgressKind::Finished &&
                progress.back().status == DeviceLink::Application::ScenarioStatus::Cancelled &&
                progress.back().reportPath.has_value(),
            "Workflow cancellation progress differs");
    const auto report = workflow.LastScenarioReport();
    Require(report && report->succeeded && report->path == *progress.back().reportPath,
            "Workflow did not retain its generated scenario report");
    const auto reportText = fileStore.Read(report->path);
    Require(reportText.Succeeded() && reportText.contents->find("Verdict: **CANCELLED**") !=
                std::string::npos &&
                reportText.contents->find("NOT RUN") != std::string::npos,
            "Workflow scenario report contents differ");
    Require(!workflow.StartReplay("workflow-device").succeeded,
            "Workflow replay accepted an empty outbound history");
    Require(manager.DisconnectDevice("workflow-device"), "Workflow device disconnection failed");
    std::error_code removeError;
    std::filesystem::remove(scenarioPath, removeError);
    std::filesystem::remove(report->path, removeError);
}

void ScenarioReportServiceWritesFailedStepDetails()
{
    TemporaryDatabase database;
    DeviceLink::Infrastructure::AtomicTextFileStore fileStore;
    DeviceLink::Application::ScenarioReportService reportService(fileStore);
    const auto scenarioPath = std::filesystem::path(database.Path().wstring() + L".dls");
    DeviceLink::Protocol::PacketFrame frame{};
    frame.header.messageType = 0x1003;
    frame.header.sequence = 7;
    frame.payload = {std::byte{0x01}, std::byte{0x02}};
    const auto result = reportService.Write({
        .scenarioPath = scenarioPath,
        .steps = {
            {.delayBeforeAction = 0ms,
                .action = DeviceLink::Application::ConnectScenarioAction{
                    "report-device", "127.0.0.1", 5000}},
            {.delayBeforeAction = 50ms,
                .action = DeviceLink::Application::SendFrameScenarioAction{"report-device", frame}},
            {.delayBeforeAction = 0ms,
                .action = DeviceLink::Application::DisconnectScenarioAction{"report-device"}},
        },
        .outcome = {
            .kind = DeviceLink::Application::ScenarioProgressKind::Finished,
            .status = DeviceLink::Application::ScenarioStatus::Failed,
            .completedStepCount = 1,
            .totalStepCount = 3,
            .failedStepIndex = 1,
            .failureMessage = "Connection | refused",
        },
        .startedUnixMilliseconds = 1000,
        .finishedUnixMilliseconds = 1250,
    });
    const auto text = fileStore.Read(result.path);
    Require(result.succeeded && text.Succeeded() &&
                result.path ==
                    DeviceLink::Application::ScenarioReportService::ReportPathFor(scenarioPath, 1250) &&
                text.contents->find("Verdict: **FAIL**") != std::string::npos &&
                text.contents->find("Duration: 250 ms") != std::string::npos &&
                text.contents->find("CONNECT report-device 127.0.0.1:5000") != std::string::npos &&
                text.contents->find("NOT RUN") != std::string::npos &&
                text.contents->find("Connection \\| refused") != std::string::npos,
            "Scenario report formatting differs");
    const auto archivedResult = reportService.Write({
        .scenarioPath = scenarioPath,
        .outcome = {
            .kind = DeviceLink::Application::ScenarioProgressKind::Finished,
            .status = DeviceLink::Application::ScenarioStatus::Completed,
        },
        .startedUnixMilliseconds = 1300,
        .finishedUnixMilliseconds = 1301,
    });
    Require(archivedResult.succeeded && archivedResult.path != result.path &&
                fileStore.Read(result.path).Succeeded() && fileStore.Read(archivedResult.path).Succeeded(),
            "Scenario report archive overwrote a prior result");
    std::error_code removeError;
    std::filesystem::remove(result.path, removeError);
    std::filesystem::remove(archivedResult.path, removeError);
}

void OperatorSettingsRoundTripAndRejectCorruption()
{
    TemporaryDatabase uniquePath;
    const auto settingsPath = std::filesystem::path(uniquePath.Path().wstring() + L".settings");
    DeviceLink::Infrastructure::AtomicTextFileStore fileStore;
    DeviceLink::Application::OperatorSettingsService settingsService(fileStore, settingsPath);
    const DeviceLink::Application::OperatorSettings expected{
        .host = "device.example.local",
        .port = 6123,
        .automaticReconnect = false,
        .scenarioPath = "C:\\Scenario Folder\\scenario=1.dls",
        .historyDeviceId = "gimbal-special",
        .historyCategoryIndex = 4,
        .historySearchText = "Fault=Motor%\nnext",
        .heartbeatWarningMilliseconds = 1200,
        .heartbeatFaultMilliseconds = 4800,
    };
    const auto saved = settingsService.Save(expected);
    Require(saved.succeeded, "Operator settings save failed");
    const auto loaded = settingsService.Load();
    Require(loaded.loaded && loaded.settings.host == expected.host &&
                loaded.settings.port == expected.port &&
                loaded.settings.automaticReconnect == expected.automaticReconnect &&
                loaded.settings.scenarioPath == expected.scenarioPath &&
                loaded.settings.historyDeviceId == expected.historyDeviceId &&
                loaded.settings.historyCategoryIndex == expected.historyCategoryIndex &&
                loaded.settings.historySearchText == expected.historySearchText &&
                loaded.settings.heartbeatWarningMilliseconds == expected.heartbeatWarningMilliseconds &&
                loaded.settings.heartbeatFaultMilliseconds == expected.heartbeatFaultMilliseconds,
            "Operator settings round trip differs");
    Require(!settingsService.Save({.host = "", .port = 5000}).succeeded,
            "Operator settings accepted an empty host");

    const auto corruptWrite = fileStore.WriteAtomically(settingsPath, "version=1\nhost=x\n");
    Require(corruptWrite.succeeded, "Corrupt settings fixture write failed");
    const auto corrupt = settingsService.Load();
    Require(!corrupt.loaded && !corrupt.error.empty(),
            "Incomplete operator settings were accepted");
    const auto legacyWrite = fileStore.WriteAtomically(settingsPath,
        "version=1\nhost=127.0.0.1\nport=5000\nautomatic_reconnect=1\n"
        "scenario_path=legacy.dls\nhistory_device_id=device\nhistory_category_index=0\n"
        "history_search_text=\n");
    Require(legacyWrite.succeeded, "Legacy settings fixture write failed");
    const auto legacy = settingsService.Load();
    Require(legacy.loaded && legacy.settings.heartbeatWarningMilliseconds == 1500 &&
                legacy.settings.heartbeatFaultMilliseconds == 3000,
            "Legacy settings did not receive safe heartbeat defaults");
    std::error_code removeError;
    std::filesystem::remove(settingsPath, removeError);
}

} // namespace

void RunDeviceSessionTests();

int main()
{
    try
    {
        DeviceLifecycleTransitionsAreValidated();
        FaultRecoveryAndRemovalAreHandled();
        WorkerEventsAreQueuedInFifoOrder();
        WindowBridgeDispatchesQueuedEventsOnDemand();
        DashboardModelRetainsDisplayHistory();
        DashboardBindingAppliesEventsAndRequestsRefresh();
        ManagerConnectsSessionsAndPublishesEvents();
        ManagerPersistsEventsThroughAsyncStore();
        EventLogQueryServiceMapsRecentStorageEntries();
        CommunicationAlarmServiceTracksAndAcknowledgesTransportErrors();
        CommunicationAlarmServiceTracksTelemetryHeartbeatHealth();
        DeviceEventProcessingServicePersistsAndRaisesCommunicationAlarm();
        DeviceRuntimeRoutesManagerEventsToEventProcessingPipeline();
        TelemetryServiceFiltersFramesAndLimitsHistory();
        AxisVapixCommandAdapterMapsSupportedCommandsAndPositionResponse();
        TelemetryQualityServiceMeasuresIntervalJitterAndLoss();
        TelemetryHeartbeatWatchdogTransitionsAndRecovers();
        ScenarioRunnerExecutesManagedDeviceActions();
        ScenarioRunnerCancelsDuringDelay();
        ScenarioRunnerReportsFailureDetails();
        ScenarioProgressIsQueuedFromWorker();
        ScenarioTextCodecRoundTripsAndRejectsInvalidInput();
        ScenarioFileServiceSavesAndLoadsScenario();
        FrameReplayRunnerReplaysValidatedFrames();
        SqliteFrameReplaySourceLoadsPersistedFrames();
        ManagerRoundTripsFramesOverTcp();
        ManagerOperatesMultipleTcpDevicesIndependently();
        ManagerReconnectsTcpDeviceAcrossMultipleServerInstances();
        ManagerParsesFragmentedTcpFrames();
        ManagerFaultsWhenPeerClosesConnection();
        ManagerRecoversAfterCorruptFrame();
        ManagerPublishesSimulatorTelemetry();
        VirtualGimbalServiceBuildsCommandsAndDecodesResponses();
        VirtualGimbalServiceUsesInjectedProtocolAdapter();
        VirtualGimbalServiceRetriesConnectionsWithinPolicy();
        OutboundReplayStorageDoesNotAmplifyReplayedFrames();
        OperatorWorkflowCoordinatesScenarioProgressAndCancellation();
        ScenarioReportServiceWritesFailedStepDetails();
        OperatorSettingsRoundTripAndRejectCorruption();
        RunDeviceSessionTests();
        std::cout << "Application tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Application test failure: " << exception.what() << '\n';
        return 1;
    }
}
