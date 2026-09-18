#include "GimbalDeviceComponent.h"

UGimbalDeviceComponent::UGimbalDeviceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UGimbalDeviceComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateMotion(DeltaTime);
	UpdateTemperature(DeltaTime);
}

bool UGimbalDeviceComponent::SetPower(const bool bPowerOn)
{
	if (!bPowerOn)
	{
		Snapshot.State = EGimbalDeviceState::Offline;
		Snapshot.Fault = EGimbalDeviceFault::None;
		return true;
	}
	if (Snapshot.State == EGimbalDeviceState::Offline)
	{
		Snapshot.State = EGimbalDeviceState::Ready;
		return true;
	}
	return Snapshot.State != EGimbalDeviceState::Fault;
}

bool UGimbalDeviceComponent::InitializeDevice()
{
	if (Snapshot.State == EGimbalDeviceState::Offline)
	{
		return false;
	}
	Snapshot.Fault = EGimbalDeviceFault::None;
	Snapshot.TargetPanDegrees = 0.0F;
	Snapshot.TargetTiltDegrees = 0.0F;
	Snapshot.State = EGimbalDeviceState::Initializing;
	return true;
}

bool UGimbalDeviceComponent::SetTarget(const float PanDegrees, const float TiltDegrees)
{
	if (!CanAcceptMotionCommand())
	{
		return false;
	}
	Snapshot.TargetPanDegrees = FMath::Clamp(PanDegrees, MinimumPanDegrees, MaximumPanDegrees);
	Snapshot.TargetTiltDegrees = FMath::Clamp(TiltDegrees, MinimumTiltDegrees, MaximumTiltDegrees);
	Snapshot.State = EGimbalDeviceState::Ready;
	return true;
}

bool UGimbalDeviceComponent::StartScan()
{
	if (!CanAcceptMotionCommand())
	{
		return false;
	}
	Snapshot.State = EGimbalDeviceState::Scanning;
	return true;
}

bool UGimbalDeviceComponent::StopScan()
{
	if (Snapshot.State != EGimbalDeviceState::Scanning)
	{
		return false;
	}
	Snapshot.TargetPanDegrees = Snapshot.PanDegrees;
	Snapshot.State = EGimbalDeviceState::Ready;
	return true;
}

void UGimbalDeviceComponent::InjectFault(const EGimbalDeviceFault NewFault)
{
	Snapshot.Fault = NewFault;
	if (NewFault == EGimbalDeviceFault::OverTemperature)
	{
		Snapshot.TemperatureCelsius = 95.0F;
	}
	if (NewFault == EGimbalDeviceFault::None)
	{
		Snapshot.State = Snapshot.State == EGimbalDeviceState::Offline
			? EGimbalDeviceState::Offline
			: EGimbalDeviceState::Ready;
		return;
	}
	Snapshot.State = EGimbalDeviceState::Fault;
}

FGimbalDeviceSnapshot UGimbalDeviceComponent::GetSnapshot() const
{
	return Snapshot;
}

void UGimbalDeviceComponent::UpdateMotion(const float DeltaTime)
{
	if (Snapshot.State == EGimbalDeviceState::Scanning)
	{
		Snapshot.TargetPanDegrees += ScanDirection * ScanRateDegreesPerSecond * DeltaTime;
		if (Snapshot.TargetPanDegrees >= MaximumPanDegrees)
		{
			Snapshot.TargetPanDegrees = MaximumPanDegrees;
			ScanDirection = -1.0F;
		}
		else if (Snapshot.TargetPanDegrees <= MinimumPanDegrees)
		{
			Snapshot.TargetPanDegrees = MinimumPanDegrees;
			ScanDirection = 1.0F;
		}
	}

	if (Snapshot.State == EGimbalDeviceState::Offline || Snapshot.State == EGimbalDeviceState::Fault)
	{
		return;
	}

	Snapshot.PanDegrees = FMath::FInterpConstantTo(
		Snapshot.PanDegrees,
		Snapshot.TargetPanDegrees,
		DeltaTime,
		SlewRateDegreesPerSecond);
	Snapshot.TiltDegrees = FMath::FInterpConstantTo(
		Snapshot.TiltDegrees,
		Snapshot.TargetTiltDegrees,
		DeltaTime,
		SlewRateDegreesPerSecond);

	if (Snapshot.State == EGimbalDeviceState::Initializing
		&& FMath::IsNearlyZero(Snapshot.PanDegrees, 0.05F)
		&& FMath::IsNearlyZero(Snapshot.TiltDegrees, 0.05F))
	{
		Snapshot.State = EGimbalDeviceState::Ready;
	}
}

void UGimbalDeviceComponent::UpdateTemperature(const float DeltaTime)
{
	const float TargetTemperature = Snapshot.State == EGimbalDeviceState::Scanning ? 42.0F : 30.0F;
	Snapshot.TemperatureCelsius = FMath::FInterpTo(
		Snapshot.TemperatureCelsius,
		TargetTemperature,
		DeltaTime,
		0.15F);
}

bool UGimbalDeviceComponent::CanAcceptMotionCommand() const
{
	return Snapshot.State != EGimbalDeviceState::Offline
		&& Snapshot.State != EGimbalDeviceState::Fault
		&& Snapshot.State != EGimbalDeviceState::Initializing;
}
