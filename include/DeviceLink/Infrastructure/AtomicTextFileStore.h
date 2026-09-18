#pragma once

#include "DeviceLink/Infrastructure/ITextFileStore.h"

namespace DeviceLink::Infrastructure
{
class AtomicTextFileStore final : public ITextFileStore
{
public:
    [[nodiscard]] TextFileReadResult Read(const std::filesystem::path& path) const override;
    [[nodiscard]] TextFileWriteResult WriteAtomically(
        const std::filesystem::path& path,
        std::string_view contents) override;
};
} // namespace DeviceLink::Infrastructure
