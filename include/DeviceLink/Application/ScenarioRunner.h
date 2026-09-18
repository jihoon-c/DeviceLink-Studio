#pragma once

#include "DeviceLink/Protocol/PacketFrame.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace DeviceLink::Application
{
class DeviceManager;

struct ConnectScenarioAction final
{
    std::string deviceId;
    std::string host;
    std::uint16_t port{};
};

struct SendFrameScenarioAction final
{
    std::string deviceId;
    Protocol::PacketFrame frame;
    bool recordForReplay{true};
};

struct DisconnectScenarioAction final
{
    std::string deviceId;
};

using ScenarioAction =
    std::variant<ConnectScenarioAction, SendFrameScenarioAction, DisconnectScenarioAction>;

struct ScenarioStep final
{
    std::chrono::milliseconds delayBeforeAction{};
    ScenarioAction action;
};

enum class ScenarioStatus
{
    Completed,
    Cancelled,
    Failed,
};

struct ScenarioResult final
{
    ScenarioStatus status;
    std::size_t completedStepCount{};
    std::optional<std::size_t> failedStepIndex;
    std::string failureMessage;
};

enum class ScenarioProgressKind
{
    Started,
    StepCompleted,
    Finished,
};

struct ScenarioProgress final
{
    ScenarioProgressKind kind{ScenarioProgressKind::Started};
    ScenarioStatus status{ScenarioStatus::Completed};
    std::size_t completedStepCount{};
    std::size_t totalStepCount{};
    std::optional<std::size_t> failedStepIndex;
    std::string failureMessage;
    std::optional<std::filesystem::path> reportPath;
    std::string reportError;
};

class ScenarioRunner final
{
public:
    using ProgressObserver = std::function<void(const ScenarioProgress&)>;

    explicit ScenarioRunner(DeviceManager& deviceManager);
    ~ScenarioRunner();

    ScenarioRunner(const ScenarioRunner&) = delete;
    ScenarioRunner& operator=(const ScenarioRunner&) = delete;

    [[nodiscard]] std::optional<std::future<ScenarioResult>> Start(
        std::vector<ScenarioStep> steps);
    void SetProgressObserver(ProgressObserver observer);
    void Stop() noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;

private:
    [[nodiscard]] bool Execute(const ScenarioAction& action);
    void PublishProgress(ScenarioProgress progress) noexcept;
    void Run(
        std::stop_token stopToken,
        std::vector<ScenarioStep> steps,
        std::promise<ScenarioResult> completion);

    DeviceManager& m_deviceManager;
    mutable std::mutex m_mutex;
    std::jthread m_worker;
    bool m_running{};
    ProgressObserver m_progressObserver;
};
} // namespace DeviceLink::Application
