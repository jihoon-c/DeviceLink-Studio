#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace DeviceLink::Infrastructure
{
struct TextFileReadResult final
{
    std::optional<std::string> contents;
    std::string error;

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return contents.has_value();
    }
};

struct TextFileWriteResult final
{
    bool succeeded{};
    std::string error;
};

class ITextFileStore
{
public:
    virtual ~ITextFileStore() = default;

    [[nodiscard]] virtual TextFileReadResult Read(const std::filesystem::path& path) const = 0;
    [[nodiscard]] virtual TextFileWriteResult WriteAtomically(
        const std::filesystem::path& path,
        std::string_view contents) = 0;
};
} // namespace DeviceLink::Infrastructure
