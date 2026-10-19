#include "DeviceLink/Infrastructure/PythonDiagnosticClient.h"
#include "DeviceLink/Infrastructure/AtomicTextFileStore.h"

#include <chrono>
#include <random>
#include <system_error>
#include <vector>
#include <windows.h>

namespace DeviceLink::Infrastructure
{
namespace
{
class Handle final
{
public:
    explicit Handle(HANDLE value) : m_value(value) {}
    ~Handle() { if (m_value && m_value != INVALID_HANDLE_VALUE) ::CloseHandle(m_value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE Get() const noexcept { return m_value; }
private:
    HANDLE m_value{};
};
class ScratchFiles final
{
public:
    std::vector<std::filesystem::path> paths;
    ~ScratchFiles() { for (const auto& path : paths) { std::error_code error; std::filesystem::remove(path, error); } }
};
}

PythonDiagnosticClient::PythonDiagnosticClient(std::filesystem::path projectRoot) : m_root(std::move(projectRoot)) {}
const std::string& PythonDiagnosticClient::LastDiagnosisId() const noexcept { return m_lastDiagnosisId; }

DiagnosticTextResult PythonDiagnosticClient::Process(const std::string& snapshot, std::stop_token stop)
{
    if (snapshot.size() > 65536 || stop.stop_requested()) return {"Diagnostic request cancelled or too large."};
    const auto python = m_root / L"mcp_server/.venv/Scripts/python.exe";
    const auto script = m_root / L"mcp_server/snapshot_diagnostics.py";
    if (!std::filesystem::is_regular_file(python) || !std::filesystem::is_regular_file(script))
        return {"Python diagnostic environment is missing. See LOCAL_LLM_SETUP_KR.md. Observed telemetry is unchanged."};
    const auto directory = m_root / L"artifacts/demo/mfc-diagnostics";
    std::error_code directoryError;
    std::filesystem::create_directories(directory, directoryError);
    if (directoryError) return {"Unable to prepare diagnostic report directory."};
    const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        "-" + std::to_string(std::random_device{}());
    const auto input = directory / (nonce + ".json");
    const auto output = directory / (nonce + ".txt");
    auto identifier = output; identifier.replace_extension(L".report-id");
    ScratchFiles scratch{{input, output, identifier}};
    AtomicTextFileStore files;
    if (!files.WriteAtomically(input, snapshot).succeeded) return {"Unable to write diagnostic observation."};

    const Handle job(::CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.Get() || !::SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return {"Unable to prepare diagnostic process ownership."};
    std::wstring command = L"\"" + python.wstring() + L"\" -X utf8 \"" + script.wstring() +
        L"\" --request \"" + input.wstring() + L"\" --output \"" + output.wstring() + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(python.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, m_root.c_str(), &startup, &process))
        return {"Unable to start Python diagnostic helper."};
    const Handle processHandle(process.hProcess), threadHandle(process.hThread);
    if (!::AssignProcessToJobObject(job.Get(), processHandle.Get()))
    {
        ::TerminateProcess(processHandle.Get(), 1);
        ::WaitForSingleObject(processHandle.Get(), 3000);
        return {"Unable to attach diagnostic process ownership."};
    }
    if (::ResumeThread(threadHandle.Get()) == static_cast<DWORD>(-1)) return {"Unable to resume diagnostic helper."};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
    while (::WaitForSingleObject(processHandle.Get(), 100) == WAIT_TIMEOUT)
    {
        if (stop.stop_requested() || std::chrono::steady_clock::now() >= deadline)
        {
            ::TerminateJobObject(job.Get(), 1);
            ::WaitForSingleObject(processHandle.Get(), 3000);
            return {"Diagnostic explanation cancelled or timed out. The previously saved observation report remains available."};
        }
    }
    DWORD exitCode = 1;
    ::GetExitCodeProcess(processHandle.Get(), &exitCode);
    std::error_code sizeError;
    if (std::filesystem::file_size(output, sizeError) > 200000 || sizeError)
        return {"Diagnostic helper produced no valid output. Check the Python environment."};
    auto text = files.Read(output);
    if (!text.contents) return {"Unable to read diagnostic result."};
    DiagnosticTextResult result{std::move(*text.contents)};
    const auto id = files.Read(identifier);
    if (exitCode == 0 && id.contents && id.contents->size() == 32 &&
        id.contents->find_first_not_of("0123456789abcdef") == std::string::npos)
    {
        auto report = directory / L"reports" / (*id.contents + ".md");
        if (!std::filesystem::is_regular_file(report)) report.replace_extension(L".json");
        if (std::filesystem::is_regular_file(report))
        { m_lastDiagnosisId = *id.contents; result.reportPath = std::move(report); }
    }
    return result;
}
}
