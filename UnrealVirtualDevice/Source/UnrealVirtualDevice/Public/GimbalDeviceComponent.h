#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "GimbalDeviceComponent.generated.h"

UENUM(BlueprintType)
enum class EGimbalDeviceState : uint8
{
	Offline,
	Ready,
	Initializing,
	Scanning,
	Fault,
};

UENUM(BlueprintType)
enum class EGimbalDeviceFault : uint8
{
	None,
	MotorStall,
	OverTemperature,
	SensorFailure,
};

USTRUCT(BlueprintType)
struct FGimbalDeviceSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EGimbalDeviceState State = EGimbalDeviceState::Offline;

	UPROPERTY(BlueprintReadOnly)
	EGimbalDeviceFault Fault = EGimbalDeviceFault::None;

	UPROPERTY(BlueprintReadOnly)
	float PanDegrees = 0.0F;

	UPROPERTY(BlueprintReadOnly)
	float TiltDegrees = 0.0F;

	UPROPERTY(BlueprintReadOnly)
	float TargetPanDegrees = 0.0F;

	UPROPERTY(BlueprintReadOnly)
	float TargetTiltDegrees = 0.0F;

	UPROPERTY(BlueprintReadOnly)
	float TemperatureCelsius = 24.0F;

	UPROPERTY(BlueprintReadOnly)
	float SupplyVoltage = 24.0F;
};

UCLASS(ClassGroup=(DeviceLink), BlueprintType, meta=(BlueprintSpawnableComponent))
class UNREALVIRTUALDEVICE_API UGimbalDeviceComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	UGimbalDeviceComponent();

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category="DeviceLink|Gimbal")
	bool SetPower(bool bPowerOn);

	UFUNCTION(BlueprintCallable, Category="DeviceLink|Gimbal")
	bool InitializeDevice();

	UFUNCTION(BlueprintCallable, Category="DeviceLink|Gimbal")
	bool SetTarget(float PanDegrees, float TiltDegrees);

	UFUNCTION(BlueprintCallable, Category="DeviceLink|Gimbal")
	bool StartScan();

	UFUNCTION(BlueprintCallable, Category="DeviceLink|Gimbal")
	bool StopScan();

	UFUNCTION(BlueprintCallable, Category="DeviceLink|Gimbal")
	void InjectFault(EGimbalDeviceFault NewFault);

	UFUNCTION(BlueprintPure, Category="DeviceLink|Gimbal")
	FGimbalDeviceSnapshot GetSnapshot() const;

private:
	void UpdateMotion(float DeltaTime);
	void UpdateTemperature(float DeltaTime);
	bool CanAcceptMotionCommand() const;

	UPROPERTY(EditAnywhere, Category="DeviceLink|Limits")
	float MinimumPanDegrees = -150.0F;

	UPROPERTY(EditAnywhere, Category="DeviceLink|Limits")
	float MaximumPanDegrees = 150.0F;

	UPROPERTY(EditAnywhere, Category="DeviceLink|Limits")
	float MinimumTiltDegrees = -60.0F;

	UPROPERTY(EditAnywhere, Category="DeviceLink|Limits")
	float MaximumTiltDegrees = 120.0F;

	UPROPERTY(EditAnywhere, Category="DeviceLink|Motion", meta=(ClampMin="1.0"))
	float SlewRateDegreesPerSecond = 60.0F;

	UPROPERTY(EditAnywhere, Category="DeviceLink|Motion", meta=(ClampMin="1.0"))
	float ScanRateDegreesPerSecond = 30.0F;

	FGimbalDeviceSnapshot Snapshot;
	float ScanDirection = 1.0F;
};
