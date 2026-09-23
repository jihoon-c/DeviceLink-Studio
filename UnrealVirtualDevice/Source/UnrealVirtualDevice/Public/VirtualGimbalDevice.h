#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VirtualDeviceProtocol.h"

#include "VirtualGimbalDevice.generated.h"

class UPointLightComponent;
class UPoseableMeshComponent;
class USceneComponent;
class UStaticMeshComponent;
class UGimbalDeviceComponent;
class UVirtualDeviceNetworkComponent;

UCLASS(Blueprintable)
class UNREALVIRTUALDEVICE_API AVirtualGimbalDevice final : public AActor
{
	GENERATED_BODY()

public:
	AVirtualGimbalDevice();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
	bool ApplyEndpointConfiguration(int32 ListenPort, bool bAllowLanConnections);

private:
	void HandleMessage(const FVirtualDeviceMessage& Message);
	void SendAcknowledge(uint32 Sequence, uint16 CommandType, bool bSucceeded);
	void SendAcknowledgeImmediately(uint32 Sequence, uint16 CommandType, bool bSucceeded);
	void SendResponse(FVirtualDeviceMessage Message);
	void PublishTelemetry();
	TArray<uint8> CreateTelemetryPayload() const;
	void UpdateVisualState();
	bool IsDroneMode() const;

	UPROPERTY(VisibleAnywhere, Category="DeviceLink|Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category="DeviceLink|Components")
	TObjectPtr<UStaticMeshComponent> DroneBody;

	UPROPERTY(VisibleAnywhere, Category="DeviceLink|Components")
	TObjectPtr<UPoseableMeshComponent> GimbalMesh;

	UPROPERTY(VisibleAnywhere, Category="DeviceLink|Components")
	TObjectPtr<UStaticMeshComponent> BaseMesh;

	UPROPERTY(VisibleAnywhere, Category="DeviceLink|Components")
	TObjectPtr<USceneComponent> PanPivot;

	UPROPERTY(VisibleAnywhere, Category="DeviceLink|Components")
	TObjectPtr<UStaticMeshComponent> PanHousingMesh;

	UPROPERTY(VisibleAnywhere, Category="DeviceLink|Components")
	TObjectPtr<USceneComponent> TiltPivot;

	UPROPERTY(VisibleAnywhere, Category="DeviceLink|Components")
	TObjectPtr<UStaticMeshComponent> SensorMesh;

	UPROPERTY(VisibleAnywhere, Category="DeviceLink|Components")
	TObjectPtr<UPointLightComponent> StatusLight;

	UPROPERTY(VisibleAnywhere, Category="DeviceLink|Components")
	TObjectPtr<UGimbalDeviceComponent> DeviceState;

	UPROPERTY(VisibleAnywhere, Category="DeviceLink|Components")
	TObjectPtr<UVirtualDeviceNetworkComponent> Network;

	UPROPERTY(EditAnywhere, Category="DeviceLink|Gimbal", meta=(ClampMin="-60.0", ClampMax="60.0"))
	float VisualRollDegrees = 0.0F;

	FTimerHandle TelemetryTimer;
	uint32 TelemetrySequence = 1;
	EVirtualDeviceResponseMode ResponseMode = EVirtualDeviceResponseMode::Normal;
	uint16 ResponseDelayMilliseconds = 0;
	bool bDroneMode = false;
	bool bDroneAirborne = false;
	FVector DroneTargetLocation = FVector::ZeroVector;
};
