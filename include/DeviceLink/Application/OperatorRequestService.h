#pragma once

#include "DeviceLink/Application/DeviceAgentService.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace DeviceLink::Application
{
enum class OperatorRequestAction { Status, Diagnose, PowerOn, PowerOff, Initialize, Move, ClearFault };
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
    explicit OperatorRequestService(Execute execute);
    ~OperatorRequestService();
    OperatorRequestService(const OperatorRequestService&) = delete;
    OperatorRequestService& operator=(const OperatorRequestService&) = delete;
    [[nodiscard]] static OperatorRequestPlan Plan(std::wstring instruction);
    [[nodiscard]] bool Submit(OperatorRequestPlan plan);
    void Cancel() noexcept;
    void Stop() noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;
    void SetWakeupHandler(Wakeup handler);
    [[nodiscard]] std::vector<std::wstring> Drain();
private:
    void Publish(std::wstring message);
    Execute m_execute;
    std::mutex m_mutex;
    Wakeup m_wakeup;
    std::vector<std::wstring> m_results;
    std::atomic_bool m_running{};
    std::jthread m_worker;
};
}
