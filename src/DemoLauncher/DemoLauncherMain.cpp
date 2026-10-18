#include <windows.h>

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace
{
class Handle final
{
public:
    explicit Handle(HANDLE value) noexcept : m_value(value) {}
    ~Handle() { if (m_value != nullptr && m_value != INVALID_HANDLE_VALUE) ::CloseHandle(m_value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    [[nodiscard]] HANDLE Get() const noexcept { return m_value; }
private:
    HANDLE m_value{};
};
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int)
{
    std::array<wchar_t, 32768> module{};
    const DWORD length = ::GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (length == 0 || length >= module.size()) return 1;
    const auto root = std::filesystem::path(module.data()).parent_path();
    const bool check = std::wstring_view(arguments) == L"--check";
    const bool smoke = std::wstring_view(arguments) == L"--smoke";
    const std::wstring mutexName = L"Local\\DeviceLinkDemo-" +
        std::to_wstring(std::hash<std::wstring>{}(root.wstring()));
    const HANDLE mutexValue = ::CreateMutexW(nullptr, FALSE, mutexName.c_str());
    const DWORD mutexError = ::GetLastError();
    const Handle mutex(mutexValue);
    if (!mutex.Get()) return 1;
    if (mutexError == ERROR_ALREADY_EXISTS)
    {
        if (const HWND existing = ::FindWindowW(nullptr, L"DeviceLink 시범운영"))
        {
            ::ShowWindow(existing, SW_RESTORE);
            ::SetForegroundWindow(existing);
        }
        return 0;
    }
    const auto python = root / L"mcp_server" / L".venv" / L"Scripts" / L"pythonw.exe";
    const auto script = root / L"mcp_server" / L"demo_launcher.py";
    if (!std::filesystem::is_regular_file(python) || !std::filesystem::is_regular_file(script))
    {
        ::MessageBoxW(nullptr,
            L"Python 가상환경 또는 운영 스크립트가 없습니다. README의 시범운영 준비 절차를 확인하세요.\n"
            L"이 EXE는 프로젝트 루트에서 실행해야 합니다.", L"DeviceLink 시범운영", MB_OK | MB_ICONERROR);
        return 1;
    }
    std::wstring command = L"\"" + python.wstring() + L"\" \"" + script.wstring() + L"\"";
    if (check) command += L" --check";
    if (smoke) command += L" --smoke";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(python.c_str(), command.data(), nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW, nullptr, root.c_str(), &startup, &process))
    {
        ::MessageBoxW(nullptr, L"시범운영 프로그램을 시작할 수 없습니다.",
                      L"DeviceLink 시범운영", MB_OK | MB_ICONERROR);
        return 1;
    }
    const Handle processHandle(process.hProcess);
    const Handle threadHandle(process.hThread);
    ::WaitForSingleObject(processHandle.Get(), INFINITE);
    DWORD exitCode = 1;
    ::GetExitCodeProcess(processHandle.Get(), &exitCode);
    return static_cast<int>(exitCode);
}
