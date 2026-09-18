#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "GimbalDeviceComponent.h"
#include "VirtualDeviceProtocol.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVirtualDeviceProtocolRoundTripTest,
	"DeviceLink.VirtualDevice.Protocol.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVirtualDeviceProtocolRoundTripTest::RunTest(const FString& Parameters)
{
	FVirtualDeviceMessage Original;
	Original.MessageType = static_cast<uint16>(EVirtualDeviceMessageType::SetPanTilt);
	Original.Sequence = 42;
	Original.Payload = {0x04, 0xD2, 0xFE, 0x0C};

	TArray<uint8> Serialized;
	TestTrue(TEXT("Frame serializes"), VirtualDeviceProtocol::Serialize(Original, Serialized));

	TArray<uint8> Buffer;
	TArray<FVirtualDeviceMessage> Parsed;
	VirtualDeviceProtocol::Consume(Buffer, MakeArrayView(Serialized).Left(7), Parsed);
	TestEqual(TEXT("Partial frame is buffered"), Parsed.Num(), 0);
	VirtualDeviceProtocol::Consume(Buffer, MakeArrayView(Serialized).RightChop(7), Parsed);

	TestEqual(TEXT("One frame is parsed"), Parsed.Num(), 1);
	if (Parsed.Num() == 1)
	{
		TestEqual(TEXT("Message type round-trips"), Parsed[0].MessageType, Original.MessageType);
		TestEqual(TEXT("Sequence round-trips"), Parsed[0].Sequence, Original.Sequence);
		TestTrue(TEXT("Payload round-trips"), Parsed[0].Payload == Original.Payload);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVirtualDeviceFaultStateTest,
	"DeviceLink.VirtualDevice.Gimbal.FaultStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVirtualDeviceFaultStateTest::RunTest(const FString& Parameters)
{
	UGimbalDeviceComponent* Device = NewObject<UGimbalDeviceComponent>();
	TestNotNull(TEXT("Gimbal state component is created"), Device);
	if (Device == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("Power on succeeds"), Device->SetPower(true));
	Device->InjectFault(EGimbalDeviceFault::OverTemperature);
	const FGimbalDeviceSnapshot OverTemperature = Device->GetSnapshot();
	TestEqual(TEXT("Over-temperature enters fault state"),
		OverTemperature.State, EGimbalDeviceState::Fault);
	TestEqual(TEXT("Over-temperature fault is published"),
		OverTemperature.Fault, EGimbalDeviceFault::OverTemperature);
	TestTrue(TEXT("Over-temperature raises simulated temperature"),
		OverTemperature.TemperatureCelsius >= 90.0F);

	Device->InjectFault(EGimbalDeviceFault::None);
	TestEqual(TEXT("Fault clear restores ready state"),
		Device->GetSnapshot().State, EGimbalDeviceState::Ready);
	Device->InjectFault(EGimbalDeviceFault::SensorFailure);
	TestEqual(TEXT("Sensor failure is published"),
		Device->GetSnapshot().Fault, EGimbalDeviceFault::SensorFailure);
	TestFalse(TEXT("Motion command is blocked during sensor failure"),
		Device->SetTarget(10.0F, 5.0F));
	return true;
}

#endif
