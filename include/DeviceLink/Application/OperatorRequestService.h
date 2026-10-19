#pragma once

#include "DeviceLink/Application/DeviceAgentService.h"

#include <atomic>
#include <functional>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace DeviceLink::Application
{
enum class OperatorRequestAction { Status, Diagnose, PowerOn, PowerOff, Initialize, Move, ClearFault, Verify };
struct OperatorDiagnosticDetails final
{
    std::wstring text;
    std::filesystem::path reportPath;
};
struct OperatorRequestStep final
{
    OperatorRequestAction action{OperatorRequestAction::Status};
    double pan{};
    double tilt{};
};
struct OperatorRequestPlan final
{
    bool valid{};
    std::wstring description;
    std::vector<OperatorRequestStep> steps;
};

class OperatorRequestService final
{
public:
    using Execute = std::function<AgentOperationResult(const OperatorRequestStep&)>;
    using Wakeup = std::function<void()>;
    using Explain = std::function<OperatorDiagnosticDetails(const AgentOperationResult&, bool, bool, std::stop_token)>;
    explicit OperatorRequestService(Execute execute, Explain explain = {}, std::filesystem::path reportDirectory = {});
    ~OperatorRequestService();
    OperatorRequestService(const OperatorRequestService&) = delete;
    OperatorRequestService& operator=(const OperatorRequestService&) = delete;
    [[nodiscard]] static OperatorRequestPlan Plan(std::wstring instruction);
    [[nodiscard]] bool Submit(OperatorRequestPlan plan, bool includeLlm = false);
    void Cancel() noexcept;
    void Stop() noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;
    void SetWakeupHandler(Wakeup handler);
    [[nodiscard]] std::vector<std::wstring> Drain();
    [[nodiscard]] std::filesystem::path LastReportPath() const;
    [[nodiscard]] const std::filesystem::path& ReportDirectory() const noexcept;
private:
    void Publish(std::wstring message);
    Execute m_execute;
    Explain m_explain;
    mutable std::mutex m_mutex;
    std::filesystem::path m_lastReportPath;
    std::filesystem::path m_reportDirectory;
    Wakeup m_wakeup;
    std::vector<std::wstring> m_results;
    std::atomic_bool m_running{};
    std::jthread m_worker;
};
}
