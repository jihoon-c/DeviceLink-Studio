#include "VirtualDeviceProtocol.h"

namespace
{
uint16 ReadUint16(const TArrayView<const uint8> Bytes, const int32 Offset)
{
	return static_cast<uint16>((static_cast<uint16>(Bytes[Offset]) << 8) | Bytes[Offset + 1]);
}

uint32 ReadUint32(const TArrayView<const uint8> Bytes, const int32 Offset)
{
	return (static_cast<uint32>(Bytes[Offset]) << 24)
		| (static_cast<uint32>(Bytes[Offset + 1]) << 16)
		| (static_cast<uint32>(Bytes[Offset + 2]) << 8)
		| static_cast<uint32>(Bytes[Offset + 3]);
}

void WriteUint32(TArray<uint8>& Bytes, const uint32 Value)
{
	Bytes.Add(static_cast<uint8>(Value >> 24));
	Bytes.Add(static_cast<uint8>(Value >> 16));
	Bytes.Add(static_cast<uint8>(Value >> 8));
	Bytes.Add(static_cast<uint8>(Value));
}

int32 FindMagic(const TArray<uint8>& Buffer)
{
	for (int32 Index = 0; Index + 1 < Buffer.Num(); ++Index)
	{
		if (Buffer[Index] == static_cast<uint8>(VirtualDeviceProtocol::PacketMagic >> 8)
			&& Buffer[Index + 1] == static_cast<uint8>(VirtualDeviceProtocol::PacketMagic))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}
}

uint16 VirtualDeviceProtocol::CalculateCrc16(const TArrayView<const uint8> Bytes)
{
	uint16 Crc = 0xFFFF;
	for (const uint8 Byte : Bytes)
	{
		Crc ^= static_cast<uint16>(Byte) << 8;
		for (int32 Bit = 0; Bit < 8; ++Bit)
		{
			Crc = (Crc & 0x8000U) != 0U
				? static_cast<uint16>((Crc << 1) ^ 0x1021U)
				: static_cast<uint16>(Crc << 1);
		}
	}
	return Crc;
}

bool VirtualDeviceProtocol::Serialize(
	const FVirtualDeviceMessage& Message,
	TArray<uint8>& OutBytes)
{
	if (Message.Payload.Num() > static_cast<int32>(MaximumPayloadSize))
	{
		return false;
	}

	OutBytes.Reset(HeaderSize + Message.Payload.Num() + CrcSize);
	WriteUint16BigEndian(OutBytes, PacketMagic);
	OutBytes.Add(ProtocolVersion);
	OutBytes.Add(0);
	WriteUint16BigEndian(OutBytes, Message.MessageType);
	WriteUint32(OutBytes, Message.Sequence);
	WriteUint32(OutBytes, static_cast<uint32>(Message.Payload.Num()));
	OutBytes.Append(Message.Payload);
	WriteUint16BigEndian(OutBytes, CalculateCrc16(OutBytes));
	return true;
}

void VirtualDeviceProtocol::Consume(
	TArray<uint8>& InOutBuffer,
	const TArrayView<const uint8> Bytes,
	TArray<FVirtualDeviceMessage>& OutMessages)
{
	InOutBuffer.Append(Bytes.GetData(), Bytes.Num());

	while (true)
	{
		const int32 MagicIndex = FindMagic(InOutBuffer);
		if (MagicIndex == INDEX_NONE)
		{
			const bool bKeepPrefix = InOutBuffer.Num() > 0
				&& InOutBuffer.Last() == static_cast<uint8>(PacketMagic >> 8);
			InOutBuffer.Reset();
			if (bKeepPrefix)
			{
				InOutBuffer.Add(static_cast<uint8>(PacketMagic >> 8));
			}
			return;
		}

		if (MagicIndex > 0)
		{
			InOutBuffer.RemoveAt(0, MagicIndex, EAllowShrinking::No);
		}
		if (InOutBuffer.Num() < HeaderSize)
		{
			return;
		}

		const TArrayView<const uint8> HeaderView(InOutBuffer.GetData(), HeaderSize);
		const uint8 Version = HeaderView[2];
		const uint32 PayloadSize = ReadUint32(HeaderView, 10);
		if (Version != ProtocolVersion || PayloadSize > MaximumPayloadSize)
		{
			InOutBuffer.RemoveAt(0, 1, EAllowShrinking::No);
			continue;
		}

		const uint64 FrameSize64 = static_cast<uint64>(HeaderSize) + PayloadSize + CrcSize;
		if (FrameSize64 > static_cast<uint64>(MAX_int32))
		{
			InOutBuffer.RemoveAt(0, 1, EAllowShrinking::No);
			continue;
		}
		const int32 FrameSize = static_cast<int32>(FrameSize64);
		if (InOutBuffer.Num() < FrameSize)
		{
			return;
		}

		const uint16 ExpectedCrc = ReadUint16(InOutBuffer, FrameSize - CrcSize);
		const uint16 ActualCrc = CalculateCrc16(
			TArrayView<const uint8>(InOutBuffer.GetData(), FrameSize - CrcSize));
		if (ExpectedCrc != ActualCrc)
		{
			InOutBuffer.RemoveAt(0, 1, EAllowShrinking::No);
			continue;
		}

		FVirtualDeviceMessage& Message = OutMessages.Emplace_GetRef();
		Message.MessageType = ReadUint16(HeaderView, 4);
		Message.Sequence = ReadUint32(HeaderView, 6);
		Message.Payload.Append(InOutBuffer.GetData() + HeaderSize, static_cast<int32>(PayloadSize));
		InOutBuffer.RemoveAt(0, FrameSize, EAllowShrinking::No);
	}
}

void VirtualDeviceProtocol::WriteInt16BigEndian(TArray<uint8>& Bytes, const int16 Value)
{
	WriteUint16BigEndian(Bytes, static_cast<uint16>(Value));
}

void VirtualDeviceProtocol::WriteUint16BigEndian(TArray<uint8>& Bytes, const uint16 Value)
{
	Bytes.Add(static_cast<uint8>(Value >> 8));
	Bytes.Add(static_cast<uint8>(Value));
}

bool VirtualDeviceProtocol::TryReadInt16BigEndian(
	const TArrayView<const uint8> Bytes,
	const int32 Offset,
	int16& OutValue)
{
	if (Offset < 0 || Offset + 1 >= Bytes.Num())
	{
		return false;
	}
	OutValue = static_cast<int16>(ReadUint16(Bytes, Offset));
	return true;
}

bool VirtualDeviceProtocol::TryReadUint16BigEndian(
	const TArrayView<const uint8> Bytes,
	const int32 Offset,
	uint16& OutValue)
{
	if (Offset < 0 || Offset + 1 >= Bytes.Num())
	{
		return false;
	}
	OutValue = ReadUint16(Bytes, Offset);
	return true;
}
