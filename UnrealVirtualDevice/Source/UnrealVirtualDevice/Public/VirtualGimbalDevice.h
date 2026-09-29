#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VirtualDeviceProtocol.h"

#include "VirtualGimbalDevice.generated.h"

class UPointLightComponent;
class UCameraComponent;
class UPoseableMeshComponent;
class USceneCaptureComponent2D;
class USceneComponent;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
class UGimbalDeviceComponent;
class UVirtualDeviceNetworkComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnVirtualDeviceCameraViewRequested, bool);

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
	UTextureRenderTarget2D* GetSensorRenderTarget() const;
	FOnVirtualDeviceCameraViewRequested& OnCameraViewRequested();

private:
	struct FClientResponseConfiguration final
	{
		EVirtualDeviceResponseMode Mode = EVirtualDeviceResponseMode::Normal;
		uint16 DelayMilliseconds = 0;
	};

	void HandleMessage(uint64 ClientId, const FVirtualDeviceMessage& Message);
	void HandleClientDisconnected(uint64 ClientId);
	void SendAcknowledge(uint64 ClientId, uint32 Sequence, uint16 CommandType, bool bSucceeded);
	void SendAcknowledgeImmediately(
		uint64 ClientId, uint32 Sequence, uint16 CommandType, bool bSucceeded);
	void SendResponse(uint64 ClientId, FVirtualDeviceMessage Message);
	void PublishTelemetry();
	void PublishTelemetryToClient(uint64 ClientId);
	TArray<uint8> CreateTelemetryPayload() const;
	void UpdateVisualState();
	void LoadConnectedArticulatedMeshes();
	void InitializeSensorCamera();
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
	TObjectPtr<USceneCaptureComponent2D> SensorCapture;

	UPROPERTY(VisibleAnywhere, Category="DeviceLink|Components")
	TObjectPtr<UCameraComponent> SensorViewCamera;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> SensorRenderTarget;

	UPROPERTY(VisibleAnywhere, Category="DeviceLink|Components")
	TObjectPtr<UGimbalDeviceComponent> DeviceState;

	UPROPERTY(VisibleAnywhere, Category="DeviceLink|Components")
	TObjectPtr<UVirtualDeviceNetworkComponent> Network;

	UPROPERTY(EditAnywhere, Category="DeviceLink|Gimbal", meta=(ClampMin="-60.0", ClampMax="60.0"))
	float VisualRollDegrees = 0.0F;

	FTimerHandle TelemetryTimer;
	uint32 TelemetrySequence = 1;
	TMap<uint64, FClientResponseConfiguration> ClientResponseConfigurations;
	bool bDroneMode = false;
	bool bDroneAirborne = false;
	FVector DroneTargetLocation = FVector::ZeroVector;
	FOnVirtualDeviceCameraViewRequested CameraViewRequestedDelegate;
};
