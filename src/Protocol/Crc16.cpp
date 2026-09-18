#include "DeviceLink/Protocol/Crc16.h"

namespace DeviceLink::Protocol
{

std::uint16_t CalculateCrc16Ccitt(
    std::span<const std::byte> bytes, std::uint16_t initialValue) noexcept
{
    std::uint16_t crc = initialValue;
    for (const std::byte byte : bytes)
    {
        crc ^= static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(byte)) << 8;
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 0x8000U) != 0U
                ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021U)
                : static_cast<std::uint16_t>(crc << 1);
        }
    }
    return crc;
}

} // namespace DeviceLink::Protocol
