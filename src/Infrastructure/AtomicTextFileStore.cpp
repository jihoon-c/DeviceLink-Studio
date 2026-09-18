#include "DeviceLink/Infrastructure/AtomicTextFileStore.h"

#include <Windows.h>

#include <array>
#include <fstream>
#include <iterator>
#include <system_error>

namespace DeviceLink::Infrastructure
{
namespace
{
class TemporaryFile final
{
public:
    explicit TemporaryFile(std::filesystem::path path)
        : m_path(std::move(path))
    {
    }

    ~TemporaryFile()
    {
        if (!m_released)
        {
            std::error_code error;
            std::filesystem::remove(m_path, error);
        }
    }

    [[nodiscard]] const std::filesystem::path& Path() const noexcept
    {
        return m_path;
    }

    void Release() noexcept
    {
        m_released = true;
    }

private:
    std::filesystem::path m_path;
    bool m_released{};
};

[[nodiscard]] std::string LastErrorMessage()
{
    return std::system_category().message(static_cast<int>(::GetLastError()));
}
} // namespace

TextFileReadResult AtomicTextFileStore::Read(const std::filesystem::path& path) const
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        return {.error = "Unable to open text file for reading"};
    }

    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad())
    {
        return {.error = "Unable to read text file"};
    }
    return {.contents = std::move(contents)};
}

TextFileWriteResult AtomicTextFileStore::WriteAtomically(
    const std::filesystem::path& path,
    std::string_view contents)
{
    if (path.empty())
    {
        return {.error = "A target path is required"};
    }

    const auto directory = path.has_parent_path() ? path.parent_path() :
        std::filesystem::current_path();
    std::error_code directoryError;
    if (!std::filesystem::is_directory(directory, directoryError))
    {
        return {.error = "Target directory does not exist"};
    }

    std::array<wchar_t, MAX_PATH> temporaryPath{};
    const auto directoryText = directory.wstring();
    if (::GetTempFileNameW(directoryText.c_str(), L"DLS", 0, temporaryPath.data()) == 0)
    {
        return {.error = "Unable to create temporary text file: " + LastErrorMessage()};
    }
    TemporaryFile temporaryFile(temporaryPath.data());

    {
        std::ofstream output(temporaryFile.Path(), std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            return {.error = "Unable to open temporary text file"};
        }
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.close();
        if (!output)
        {
            return {.error = "Unable to write temporary text file"};
        }
    }

    if (!::MoveFileExW(temporaryFile.Path().c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        return {.error = "Unable to replace target text file: " + LastErrorMessage()};
    }
    temporaryFile.Release();
    return {.succeeded = true};
}
} // namespace DeviceLink::Infrastructure
