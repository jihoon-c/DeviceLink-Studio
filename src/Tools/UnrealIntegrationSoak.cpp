#include "DeviceLink/Application/DeviceEventQueue.h"
#include "DeviceLink/Application/DeviceManager.h"
#include "DeviceLink/Application/VirtualGimbalService.h"
#include "DeviceLink/Transport/TcpTransport.h"

#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

namespace
{
using namespace std::chrono_literals;
constexpr auto kDeviceId = "unreal-gimbal-soak";

void Require(bool condition, std::string_view message)
{
    if (!condition)
    {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Integer>
Integer ParsePositiveInteger(std::string_view text, std::string_view name)
{
    Integer value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0)
    {
        throw std::runtime_error(std::string(name) + " must be a positive integer");
    }
    return value;
}

class UnrealGimbalProbe final
{
public:
    UnrealGimbalProbe(std::string host, std::uint16_t port)
        : m_host(std::move(host)), m_port(port),
          m_service(m_manager, kDeviceId, {.enabled = false})
    {
        Require(m_manager.RegisterDevice(kDeviceId), "Could not register soak device");
        Require(m_manager.AttachTransport(kDeviceId,
            std::make_unique<DeviceLink::Transport::TcpTransport>(
                DeviceLink::Transport::TcpTransport::ReceiveHandler{},
                DeviceLink::Transport::TcpTransport::ErrorHandler{},
                DeviceLink::Transport::TcpTransportOptions{.connectTimeout = 500ms})),
            "Could not attach soak transport");
    }

    void WaitForServer(std::chrono::seconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (m_service.Connect(m_host, m_port))
            {
                DrainEvents();
                return;
            }
            static_cast<void>(m_service.Disconnect());
            DrainEvents();
            std::this_thread::sleep_for(250ms);
        }
        throw std::runtime_error("Timed out waiting for the Unreal virtual device");
    }

    void Run(std::size_t cycleCount)
    {
        SendAndWait(0x1001, [this] { return m_service.Power(true); });
        VerifyResponseFaultModes();
        for (std::size_t cycle = 0; cycle < cycleCount; ++cycle)
        {
            const double pan = (cycle % 2 == 0) ? 35.0 : -35.0;
            const double tilt = (cycle % 3 == 0) ? 12.0 : -8.0;
            SendAndWait(0x1003, [this, pan, tilt] { return m_service.SetPanTilt(pan, tilt); });

            const auto telemetryBefore = m_telemetryCount;
            SendAndWait(0x1006, [this] { return m_service.RequestStatus(); });
            WaitForTelemetry(telemetryBefore + 1, 2s);

            if ((cycle + 1) % 25 == 0 && cycle + 1 < cycleCount)
            {
                Require(m_service.Disconnect(), "Scheduled soak disconnect failed");
                DrainEvents();
                WaitForServer(5s);
            }
        }
        SendAndWait(0x1001, [this] { return m_service.Power(false); });
        Require(m_service.Disconnect(), "Final soak disconnect failed");
    }

    [[nodiscard]] std::size_t AcknowledgementCount() const noexcept
    {
        return m_acknowledgementCount;
    }

    [[nodiscard]] std::size_t TelemetryCount() const noexcept
    {
        return m_telemetryCount;
    }

private:
    template <typename SendOperation>
    void SendAndWait(std::uint16_t commandType, SendOperation&& sendOperation)
    {
        Require(sendOperation(), "Unreal command send failed");
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (const auto event = m_manager.Events().TryPop())
            {
                if (const auto* error = std::get_if<DeviceLink::Application::TransportError>(
                        &event->payload))
                {
                    throw std::runtime_error("Transport error: " + error->message);
                }
                const auto* received = std::get_if<DeviceLink::Application::FrameReceived>(
                    &event->payload);
                if (!received)
                {
                    continue;
                }
                if (const auto telemetry =
                        m_service.DecodeDeviceTelemetry(
                            received->frame.header.messageType, received->frame.payload))
                {
                    ValidateTelemetry(*telemetry);
                    ++m_telemetryCount;
                    continue;
                }
                const auto acknowledgement =
                    m_service.DecodeDeviceAcknowledgement(
                        received->frame.header.messageType, received->frame.payload);
                if (acknowledgement && acknowledgement->commandType == commandType)
                {
                    Require(acknowledgement->succeeded, "Unreal rejected a soak command");
                    ++m_acknowledgementCount;
                    return;
                }
            }
            std::this_thread::sleep_for(2ms);
        }
        throw std::runtime_error("Timed out waiting for Unreal acknowledgement");
    }

    void WaitForTelemetry(std::size_t expectedCount, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (m_telemetryCount < expectedCount && std::chrono::steady_clock::now() < deadline)
        {
            DrainOneEvent();
            std::this_thread::sleep_for(2ms);
        }
        Require(m_telemetryCount >= expectedCount, "Timed out waiting for Unreal telemetry");
    }

    void DrainOneEvent()
    {
        const auto event = m_manager.Events().TryPop();
        if (!event)
        {
            return;
        }
        if (const auto* received = std::get_if<DeviceLink::Application::FrameReceived>(
                &event->payload))
        {
            if (const auto telemetry =
                    m_service.DecodeDeviceTelemetry(
                        received->frame.header.messageType, received->frame.payload))
            {
                ValidateTelemetry(*telemetry);
                ++m_telemetryCount;
            }
        }
    }

    void DrainEvents()
    {
        while (m_manager.Events().TryPop())
        {
        }
    }

    void VerifyResponseFaultModes()
    {
        SendAndWait(0x1008, [this] {
            return m_service.ConfigureResponse(
                DeviceLink::Application::VirtualGimbalResponseMode::Delayed, 150);
        });
        const auto delayedStarted = std::chrono::steady_clock::now();
        SendAndWait(0x1006, [this] { return m_service.RequestStatus(); });
        Require(std::chrono::steady_clock::now() - delayedStarted >= 100ms,
            "Unreal response delay was not applied");

        SendAndWait(0x1008, [this] {
            return m_service.ConfigureResponse(
                DeviceLink::Application::VirtualGimbalResponseMode::NoResponse);
        });
        Require(m_service.RequestStatus(), "No-response status command could not be sent");
        bool receivedStatusAcknowledgement{};
        const auto deadline = std::chrono::steady_clock::now() + 400ms;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (const auto event = m_manager.Events().TryPop())
            {
                if (const auto* received = std::get_if<DeviceLink::Application::FrameReceived>(
                        &event->payload))
                {
                    if (const auto acknowledgement =
                            m_service.DecodeDeviceAcknowledgement(
                                received->frame.header.messageType, received->frame.payload);
                        acknowledgement && acknowledgement->commandType == 0x1006)
                    {
                        receivedStatusAcknowledgement = true;
                    }
                    if (const auto telemetry = m_service.DecodeDeviceTelemetry(
                            received->frame.header.messageType, received->frame.payload))
                    {
                        ValidateTelemetry(*telemetry);
                        ++m_telemetryCount;
                    }
                }
            }
            std::this_thread::sleep_for(2ms);
        }
        Require(!receivedStatusAcknowledgement,
            "Unreal no-response mode still returned an acknowledgement");

        SendAndWait(0x1008, [this] {
            return m_service.ConfigureResponse(
                DeviceLink::Application::VirtualGimbalResponseMode::Normal);
        });
    }

    static void ValidateTelemetry(
        const DeviceLink::Application::VirtualGimbalTelemetry& telemetry)
    {
        Require(std::isfinite(telemetry.panDegrees) && std::isfinite(telemetry.tiltDegrees),
            "Unreal telemetry contains a non-finite angle");
        Require(telemetry.panDegrees >= -170.0 && telemetry.panDegrees <= 170.0,
            "Unreal telemetry pan is outside device limits");
        Require(telemetry.tiltDegrees >= -45.0 && telemetry.tiltDegrees <= 80.0,
            "Unreal telemetry tilt is outside device limits");
        Require(telemetry.supplyVoltage > 0.0 && telemetry.supplyVoltage < 100.0,
            "Unreal telemetry voltage is implausible");
    }

    std::string m_host;
    std::uint16_t m_port{};
    DeviceLink::Application::DeviceManager m_manager;
    DeviceLink::Application::VirtualGimbalService m_service;
    std::size_t m_acknowledgementCount{};
    std::size_t m_telemetryCount{};
};
} // namespace

int main(int argumentCount, char* arguments[])
{
    try
    {
        const std::string host = argumentCount >= 2 ? arguments[1] : "127.0.0.1";
        const auto portValue = argumentCount >= 3
            ? ParsePositiveInteger<std::uint32_t>(arguments[2], "Port") : 5000U;
        Require(portValue <= 65535, "Port must be in the range 1-65535");
        const auto cycleCount = argumentCount >= 4
            ? ParsePositiveInteger<std::size_t>(arguments[3], "Cycle count") : 250U;

        UnrealGimbalProbe probe(host, static_cast<std::uint16_t>(portValue));
        probe.WaitForServer(60s);
        probe.Run(cycleCount);
        std::cout << "Unreal integration soak passed: " << cycleCount
                  << " cycles, " << probe.AcknowledgementCount() << " ACK, "
                  << probe.TelemetryCount() << " telemetry frames\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Unreal integration soak failed: " << exception.what() << '\n';
        return 1;
    }
}
