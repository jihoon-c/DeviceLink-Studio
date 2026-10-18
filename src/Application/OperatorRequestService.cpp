#include "DeviceLink/Application/OperatorRequestService.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace DeviceLink::Application
{
OperatorRequestService::OperatorRequestService(Execute execute) : m_execute(std::move(execute)) {}
OperatorRequestService::~OperatorRequestService() { Stop(); }

OperatorRequestPlan OperatorRequestService::Plan(std::wstring text)
{
    if (text.size() < 2 || text.size() > 1024) return {false, L"요청은 2~1024자로 입력하세요."};
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return std::towlower(c); });
    const auto has = [&text](std::wstring_view word) { return text.find(word) != std::wstring::npos; };
    if (std::regex_search(text, std::wregex(LR"(하지|말고|말아|빼고|제외|대신|지\s*마|않|금지|안\s*(해|하|켜|끄|이동|복구)|\b(no|not|never|without|except)\b|don't)")))
        return {false, L"부정·제외 조건은 자동 실행하지 않습니다. 수행할 동작만 명확히 입력하세요."};
    if (has(L"연결") || has(L"connect")) return {false, L"연결은 상단 연결 버튼을 사용하세요."};
    const bool diagnose = has(L"진단") || has(L"원인") || has(L"방법") || has(L"절차") || has(L"diagnos");
    const bool on = has(L"전원 켜") || has(L"전원 on") || has(L"power on");
    const bool off = has(L"전원 끄") || has(L"전원 off") || has(L"power off");
    const bool initialize = has(L"초기화") || has(L"initialize");
    const bool clear = has(L"고장 해제") || has(L"복구해") || has(L"복구 해") || has(L"clear fault");
    const bool angle = has(L"pan") || has(L"팬") || has(L"tilt") || has(L"틸트");
    if (has(L"주입") || has(L"inject") || has(L"시나리오") || has(L"replay"))
        return {false, L"고장 주입·시나리오·Replay는 시험 탭을 사용하세요."};
    if ((off && (on || initialize || angle || clear)) || (diagnose && (on || off || initialize || angle || (clear && !has(L"방법") && !has(L"절차")))))
        return {false, L"상충 명령이나 진단 질문과 제어 동작은 나누어 요청하세요."};
    if (diagnose) return {true, L"관측 진단: 새 Telemetry의 상태·고장 확인 (장비 변경 없음, LLM 미사용)", {{OperatorRequestAction::Diagnose}}};
    OperatorRequestPlan result{true, L"실행 계획:"};
    if (off) return {true, L"전원 OFF → 새 상태 응답 확인", {{OperatorRequestAction::PowerOff}}};
    if (on) { result.steps.push_back({OperatorRequestAction::PowerOn}); result.description += L"\r\n· 전원 ON 및 응답 확인"; }
    if (initialize) { result.steps.push_back({OperatorRequestAction::Initialize}); result.description += L"\r\n· 초기화 및 Ready 확인"; }
    if (clear) { result.steps.push_back({OperatorRequestAction::ClearFault}); result.description += L"\r\n· 가상 고장 해제 및 상태 확인"; }
    if (angle)
    {
        const auto read = [&text](const wchar_t* names) {
            const std::wregex expression(std::wstring(L"(?:") + names + LR"()\s*(?:각도)?\s*(?:은|는|을|를|=|:)?\s*(-?\d+(?:\.\d+)?(?:e[+-]?\d+)?)(?![\d.e]))");
            std::wsregex_iterator it(text.begin(), text.end(), expression), end;
            if (it == end) throw std::invalid_argument("missing angle");
            const double value = std::stod((*it)[1].str());
            if (++it != end || !std::isfinite(value)) throw std::invalid_argument("ambiguous angle");
            return value;
        };
        try
        {
            const double pan = read(L"pan|팬"), tilt = read(L"tilt|틸트");
            if (pan < -170 || pan > 170 || tilt < -45 || tilt > 80) throw std::out_of_range("angle");
            result.steps.push_back({OperatorRequestAction::Move, pan, tilt});
            std::wostringstream line;
            line << L"\r\n· Pan " << pan << L"° / Tilt " << tilt << L"° 이동 및 도달 확인";
            result.description += line.str();
        }
        catch (const std::exception&) { return {false, L"Pan·Tilt를 각각 한 번씩 입력하세요. Pan -170~170°, Tilt -45~80°."}; }
    }
    if (has(L"상태") || has(L"telemetry") || has(L"status"))
    { result.steps.push_back({OperatorRequestAction::Status}); result.description += L"\r\n· 새 Telemetry 요청"; }
    if (result.steps.empty()) return {false, L"지원 요청: 상태 확인 / 고장 진단 / 전원 켜기·끄기 / 초기화 / Pan 30 Tilt -10 / 고장 해제"};
    return result;
}

bool OperatorRequestService::Submit(OperatorRequestPlan plan)
{
    if (!plan.valid || plan.steps.empty() || plan.steps.size() > 8) return false;
    for (const auto& step : plan.steps)
        if (static_cast<int>(step.action) < 0 || step.action > OperatorRequestAction::ClearFault ||
            (step.action == OperatorRequestAction::Move && (!std::isfinite(step.pan) || !std::isfinite(step.tilt) ||
            step.pan < -170 || step.pan > 170 || step.tilt < -45 || step.tilt > 80))) return false;
    if (m_running.exchange(true)) return false;
    try { m_worker = std::jthread([this, plan = std::move(plan)](std::stop_token stop) {
        try
        {
            for (const auto& step : plan.steps)
            {
                if (stop.stop_requested()) { Publish(L"요청을 중지했습니다. 이미 전송한 동작은 취소되지 않습니다."); break; }
                const auto result = m_execute(step);
                std::wstring message = result.succeeded ? L"[확인 완료] " : L"[실패] ";
                message.append(result.message.begin(), result.message.end());
                if (result.telemetry)
                {
                    const auto& telemetry = *result.telemetry;
                    const auto state = DeviceAgentService::StateName(telemetry.state);
                    const auto fault = DeviceAgentService::FaultName(telemetry.fault);
                    std::wostringstream details;
                    details << L"\r\n관측: " << std::wstring(state.begin(), state.end()) << L" / " << std::wstring(fault.begin(), fault.end())
                            << L"\r\nPan " << telemetry.panDegrees << L"° / Tilt " << telemetry.tiltDegrees
                            << L"° / 온도 " << telemetry.temperatureCelsius << L"°C / 전압 " << telemetry.supplyVoltage << L"V";
                    message += details.str();
                    if (step.action == OperatorRequestAction::Diagnose)
                        message += L"\r\n관측된 고장 코드이며 물리적 원인을 확정하지 않습니다. 수리 성공 판정은 별도 시험이 필요합니다.";
                }
                Publish(std::move(message));
                if (!result.succeeded) break;
            }
        }
        catch (const std::exception&) { Publish(L"[실패] 요청 처리 중 오류가 발생했습니다."); }
        m_running.store(false);
        Publish(L"요청 처리 종료");
    }); }
    catch (const std::exception&) { m_running.store(false); return false; }
    return true;
}
void OperatorRequestService::Cancel() noexcept { m_worker.request_stop(); }
void OperatorRequestService::Stop() noexcept { Cancel(); if (m_worker.joinable()) m_worker.join(); }
bool OperatorRequestService::IsRunning() const noexcept { return m_running.load(); }
void OperatorRequestService::SetWakeupHandler(Wakeup handler) { std::scoped_lock lock(m_mutex); m_wakeup = std::move(handler); }
std::vector<std::wstring> OperatorRequestService::Drain() { std::scoped_lock lock(m_mutex); auto result = std::move(m_results); m_results.clear(); return result; }
void OperatorRequestService::Publish(std::wstring message)
{
    Wakeup wakeup;
    { std::scoped_lock lock(m_mutex); if (m_results.size() == 64) m_results.erase(m_results.begin()); m_results.push_back(std::move(message)); wakeup = m_wakeup; }
    if (wakeup) wakeup();
}
}
