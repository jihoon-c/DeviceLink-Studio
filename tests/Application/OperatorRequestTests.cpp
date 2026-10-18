#include "DeviceLink/Application/OperatorRequestService.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>

using namespace DeviceLink::Application;
using namespace std::chrono_literals;

namespace
{
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

void RequestsAreValidatedBeforeExecution()
{
    for (const auto* request : {L"전원 끄지마", L"고장 해제하지 말고 상태 확인", L"don't power off",
             L"초기화 빼고 전원 켜줘", L"전원 off Pan 30 Tilt 10", L"Pan 30", L"Pan 30 Pan 40 Tilt 10", L"Pan 1e309 Tilt 0",
             L"Pan 180 Tilt 0", L"진단하고 전원 켜줘", L"MotorStall 주입", L"연결 해제", L"알 수 없는 요청"})
        Require(!OperatorRequestService::Plan(request).valid, "Unsafe or unsupported request accepted");
    const auto move = OperatorRequestService::Plan(L"Pan 30도 Tilt -10도로 이동");
    Require(move.valid && move.steps.size() == 1 && move.steps[0].action == OperatorRequestAction::Move &&
        move.steps[0].pan == 30 && move.steps[0].tilt == -10, "Angle plan differs");
    const auto question = OperatorRequestService::Plan(L"고장 해제 방법 알려줘");
    Require(question.valid && question.steps.size() == 1 && question.steps[0].action == OperatorRequestAction::Diagnose,
        "Recovery question became an actuation request");
}

void FailureStopsCompoundRequestAndPublishesResults()
{
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<OperatorRequestAction> actions;
    OperatorRequestService service([&](const OperatorRequestStep& step) {
        actions.push_back(step.action);
        return AgentOperationResult{false, "fixture failure"};
    });
    service.SetWakeupHandler([&] { std::scoped_lock lock(mutex); condition.notify_all(); });
    Require(service.Submit(OperatorRequestService::Plan(L"전원 켜고 초기화")), "Submit failed");
    { std::unique_lock lock(mutex); Require(condition.wait_for(lock, 3s, [&] { return !service.IsRunning(); }), "Worker did not finish"); }
    service.Stop();
    Require(actions.size() == 1 && actions[0] == OperatorRequestAction::PowerOn, "Execution continued after failure");
    Require(!service.Drain().empty() && service.Drain().empty(), "Result queue did not drain");
}

void RunningRequestRejectsDuplicatesAndCancelsRemainingSteps()
{
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false, release = false;
    std::size_t actions = 0;
    OperatorRequestService service([&](const OperatorRequestStep&) {
        std::unique_lock lock(mutex);
        ++actions; entered = true; condition.notify_all();
        condition.wait(lock, [&] { return release; });
        return AgentOperationResult{true, "fixture complete"};
    });
    Require(service.Submit(OperatorRequestService::Plan(L"전원 켜고 초기화")), "Submit failed");
    bool started;
    { std::unique_lock lock(mutex); started = condition.wait_for(lock, 3s, [&] { return entered; }); }
    const bool duplicate = service.Submit(OperatorRequestService::Plan(L"상태 확인"));
    service.Cancel();
    { std::scoped_lock lock(mutex); release = true; condition.notify_all(); }
    service.Stop();
    Require(started && !duplicate && actions == 1 && !service.IsRunning(), "Duplicate or cancelled step was executed");
}
}

int main()
{
    try
    {
        RequestsAreValidatedBeforeExecution();
        FailureStopsCompoundRequestAndPublishesResults();
        RunningRequestRejectsDuplicatesAndCancelsRemainingSteps();
        std::cout << "Operator request tests passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
