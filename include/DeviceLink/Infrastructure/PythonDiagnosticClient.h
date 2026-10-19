#pragma once

#include <filesystem>
#include <stop_token>
#include <string>

namespace DeviceLink::Infrastructure
{
struct DiagnosticTextResult final
{
    std::string text;
    std::filesystem::path reportPath;
};

// Exchanges a bounded observation file with a hidden, cancellable helper process.
// The helper receives no equipment endpoint or device-changing capability.
class PythonDiagnosticClient final
{
public:
    explicit PythonDiagnosticClient(std::filesystem::path projectRoot);
    [[nodiscard]] DiagnosticTextResult Process(const std::string& snapshot, std::stop_token stop);
    [[nodiscard]] const std::string& LastDiagnosisId() const noexcept;
private:
    std::filesystem::path m_root;
    std::string m_lastDiagnosisId;
};
}
