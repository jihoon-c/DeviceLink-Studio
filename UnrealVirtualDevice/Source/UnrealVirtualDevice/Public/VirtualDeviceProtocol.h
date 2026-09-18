#pragma once

#include "CoreMinimal.h"

enum class EVirtualDeviceMessageType : uint16
{
	Power = 0x1001,
	Initialize = 0x1002,
	SetPanTilt = 0x1003,
	StartScan = 0x1004,
	StopScan = 0x1005,
	GetStatus = 0x1006,
	InjectFault = 0x1007,
	ConfigureResponse = 0x1008,
	SelectEquipmentMode = 0x1009,
	DroneTakeOff = 0x1010,
	DroneLand = 0x1011,
	DroneMoveTo = 0x1012,
	DroneReturnHome = 0x1013,
	Acknowledge = 0x6001,
	Telemetry = 0x7001,
};

enum class EVirtualDeviceResponseMode : uint8
{
	Normal,
	Delayed,
	NoResponse,
};

struct FVirtualDeviceMessage final
{
	uint16 MessageType = 0;
	uint32 Sequence = 0;
	TArray<uint8> Payload;
};

namespace VirtualDeviceProtocol
{
inline constexpr uint16 PacketMagic = 0xD15C;
inline constexpr uint8 ProtocolVersion = 1;
inline constexpr int32 HeaderSize = 14;
inline constexpr int32 CrcSize = 2;
inline constexpr uint32 MaximumPayloadSize = 1024 * 1024;

UNREALVIRTUALDEVICE_API uint16 CalculateCrc16(const TArrayView<const uint8> Bytes);
UNREALVIRTUALDEVICE_API bool Serialize(const FVirtualDeviceMessage& Message, TArray<uint8>& OutBytes);
UNREALVIRTUALDEVICE_API void Consume(
	TArray<uint8>& InOutBuffer,
	const TArrayView<const uint8> Bytes,
	TArray<FVirtualDeviceMessage>& OutMessages);

UNREALVIRTUALDEVICE_API void WriteInt16BigEndian(TArray<uint8>& Bytes, int16 Value);
UNREALVIRTUALDEVICE_API void WriteUint16BigEndian(TArray<uint8>& Bytes, uint16 Value);
UNREALVIRTUALDEVICE_API bool TryReadInt16BigEndian(
	const TArrayView<const uint8> Bytes,
	int32 Offset,
	int16& OutValue);
UNREALVIRTUALDEVICE_API bool TryReadUint16BigEndian(
	const TArrayView<const uint8> Bytes,
	int32 Offset,
	uint16& OutValue);
}
