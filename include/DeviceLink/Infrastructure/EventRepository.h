#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace DeviceLink::Infrastructure
{
struct EventLogEntry final
{
    std::int64_t timestampUnixMilliseconds{};
    std::string deviceId;
    std::string category;
    std::string detail;
    std::vector<std::byte> payload;
};

struct StoredFramePayload final
{
    std::int64_t timestampUnixMilliseconds{};
    std::vector<std::byte> payload;
};

class IEventRepository
{
public:
    virtual ~IEventRepository() = default;
    virtual void Append(const EventLogEntry& entry) = 0;
    [[nodiscard]] virtual std::size_t PruneBefore(
        std::int64_t timestampUnixMilliseconds) = 0;
};
} // namespace DeviceLink::Infrastructure
