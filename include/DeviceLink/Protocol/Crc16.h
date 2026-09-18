#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace DeviceLink::Protocol
{

inline constexpr std::uint16_t kCrc16CcittInitialValue = 0xFFFF;

[[nodiscard]] std::uint16_t CalculateCrc16Ccitt(
    std::span<const std::byte> bytes,
    std::uint16_t initialValue = kCrc16CcittInitialValue) noexcept;

} // namespace DeviceLink::Protocol
