#include "VirtualGimbalDevice.h"

#include "CameraViewStatusComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GimbalDeviceComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "VirtualDeviceNetworkComponent.h"

namespace
{
constexpr TCHAR YawBoneName[] = TEXT("gimbal_yaw");
constexpr TCHAR RollBoneName[] = TEXT("gimbal_roll");
constexpr TCHAR PitchBoneName[] = TEXT("gimbal_pitch");

void ApplyComponentSpaceRotation(
	UPoseableMeshComponent& Mesh,
	const FName BoneName,
	const FQuat& DeltaRotation)
{
	FTransform Transform = Mesh.GetBoneTransformByName(BoneName, EBoneSpaces::ComponentSpace);
	Transform.SetRotation(DeltaRotation * Transform.GetRotation());
	Mesh.SetBoneTransformByName(BoneName, Transform, EBoneSpaces::ComponentSpace);
}
}

AVirtualGimbalDevice::AVirtualGimbalDevice()
{
	PrimaryActorTick.bCanEverTick = true;

#if WITH_EDITORONLY_DATA
	// A device endpoint is infrastructure, not spatial scenery. Keep it loaded in
	// partitioned worlds even when no player-controlled streaming source exists.
	bIsSpatiallyLoaded = false;
#endif

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	DroneBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DroneBody"));
	DroneBody->SetupAttachment(SceneRoot);
	DroneBody->SetRelativeScale3D(FVector(1.2F, 1.2F, 0.22F));
	DroneBody->SetVisibility(false);

	GimbalMesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("GimbalMesh"));
	GimbalMesh->SetupAttachment(SceneRoot);
	GimbalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	BaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
	BaseMesh->SetupAttachment(DroneBody);
	BaseMesh->SetRelativeScale3D(FVector(1.5F, 1.5F, 0.35F));

	PanPivot = CreateDefaultSubobject<USceneComponent>(TEXT("PanPivot"));
	PanPivot->SetupAttachment(DroneBody);
	PanPivot->SetRelativeLocation(FVector(0.0F, 0.0F, 60.0F));

	PanHousingMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PanHousingMesh"));
	PanHousingMesh->SetupAttachment(PanPivot);
	PanHousingMesh->SetRelativeScale3D(FVector(0.9F, 0.9F, 0.5F));

	TiltPivot = CreateDefaultSubobject<USceneComponent>(TEXT("TiltPivot"));
	TiltPivot->SetupAttachment(PanPivot);
	TiltPivot->SetRelativeLocation(FVector(0.0F, 0.0F, 45.0F));

	SensorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SensorMesh"));
	SensorMesh->SetupAttachment(TiltPivot);
	SensorMesh->SetRelativeLocation(FVector(35.0F, 0.0F, 0.0F));
	SensorMesh->SetRelativeScale3D(FVector(0.75F, 0.55F, 0.55F));

	StatusLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("StatusLight"));
	StatusLight->SetupAttachment(GimbalMesh);
	StatusLight->SetRelativeLocation(FVector(1.85F, -2.3F, 9.6F));
	StatusLight->SetIntensity(2500.0F);
	StatusLight->SetAttenuationRadius(220.0F);

	SensorCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SensorCapture"));
	SensorCapture->SetupAttachment(TiltPivot);
	SensorCapture->SetRelativeLocation(FVector(170.0F, 0.0F, 25.0F));
	SensorCapture->SetRelativeRotation(FRotator::ZeroRotator);
	SensorCapture->FOVAngle = 45.0F;
	SensorCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	SensorCapture->bCaptureEveryFrame = true;
	SensorCapture->bCaptureOnMovement = true;
    SensorViewCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("SensorViewCamera"));
    CreateDefaultSubobject<UCameraViewStatusComponent>(TEXT("CameraViewStatus"));
	SensorViewCamera->SetupAttachment(TiltPivot);
	SensorViewCamera->SetRelativeLocation(FVector(170.0F, 0.0F, 25.0F));
	SensorViewCamera->SetRelativeRotation(FRotator::ZeroRotator);
	SensorViewCamera->FieldOfView = 45.0F;
	SensorViewCamera->SetActive(true);

	DeviceState = CreateDefaultSubobject<UGimbalDeviceComponent>(TEXT("DeviceState"));
	Network = CreateDefaultSubobject<UVirtualDeviceNetworkComponent>(TEXT("Network"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> VirtualEOMesh(
		TEXT("/Game/VirtualEO/SK_DLS_VirtualEO.SK_DLS_VirtualEO"));
	if (CylinderMesh.Succeeded())
	{
		BaseMesh->SetStaticMesh(CylinderMesh.Object);
		PanHousingMesh->SetStaticMesh(CylinderMesh.Object);
	}
	if (CubeMesh.Succeeded())
	{
		DroneBody->SetStaticMesh(CubeMesh.Object);
		SensorMesh->SetStaticMesh(CubeMesh.Object);
	}
	if (VirtualEOMesh.Succeeded())
	{
		GimbalMesh->SetSkinnedAssetAndUpdate(VirtualEOMesh.Object, true);
		BaseMesh->SetVisibility(false, true);
		PanHousingMesh->SetVisibility(false, true);
		SensorMesh->SetVisibility(false, true);
	}
	else
	{
		GimbalMesh->SetVisibility(false, true);
	}
}

void AVirtualGimbalDevice::BeginPlay()
{
	Super::BeginPlay();
	LoadConnectedArticulatedMeshes();
	InitializeSensorCamera();
	Network->OnMessageReceived().AddUObject(this, &AVirtualGimbalDevice::HandleMessage);
	Network->OnClientDisconnected().AddUObject(
		this, &AVirtualGimbalDevice::HandleClientDisconnected);
	GetWorldTimerManager().SetTimer(
		TelemetryTimer,
		this,
		&AVirtualGimbalDevice::PublishTelemetry,
		0.5F,
		true);
}

void AVirtualGimbalDevice::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearAllTimersForObject(this);
	Network->OnMessageReceived().RemoveAll(this);
	Network->OnClientDisconnected().RemoveAll(this);
	ClientResponseConfigurations.Reset();
	SensorCapture->TextureTarget = nullptr;
	SensorRenderTarget = nullptr;
	Super::EndPlay(EndPlayReason);
}

void AVirtualGimbalDevice::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bDroneMode)
	{
		const FVector NewLocation = FMath::VInterpConstantTo(
			DroneBody->GetRelativeLocation(), DroneTargetLocation, DeltaTime, 600.0F);
		DroneBody->SetRelativeLocation(NewLocation);
		GimbalMesh->SetRelativeLocation(NewLocation);
	}
	UpdateVisualState();
}

bool AVirtualGimbalDevice::ApplyEndpointConfiguration(
	const int32 ListenPort, const bool bAllowLanConnections)
{
	return Network->StartListening(ListenPort, bAllowLanConnections);
}

UTextureRenderTarget2D* AVirtualGimbalDevice::GetSensorRenderTarget() const
{
	return SensorRenderTarget;
}

FOnVirtualDeviceCameraViewRequested& AVirtualGimbalDevice::OnCameraViewRequested()
{
	return CameraViewRequestedDelegate;
}

void AVirtualGimbalDevice::LoadConnectedArticulatedMeshes()
{
	if (GimbalMesh->GetSkinnedAsset() != nullptr)
	{
		return;
	}

	UStaticMesh* ConnectedBase = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Game/DeviceLink/Equipment/Articulated/SM_DLS_FixedDualSensor_Base_Connected.SM_DLS_FixedDualSensor_Base_Connected"));
	UStaticMesh* ConnectedPan = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Game/DeviceLink/Equipment/Articulated/SM_DLS_FixedDualSensor_Pan_Connected.SM_DLS_FixedDualSensor_Pan_Connected"));
	UStaticMesh* ConnectedTilt = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Game/DeviceLink/Equipment/Articulated/SM_DLS_FixedDualSensor_Tilt_Connected.SM_DLS_FixedDualSensor_Tilt_Connected"));
	if (!IsValid(ConnectedBase) || !IsValid(ConnectedPan) || !IsValid(ConnectedTilt))
	{
		UE_LOG(LogTemp, Warning, TEXT("Connected fixed-camera meshes are not available."));
		return;
	}

	BaseMesh->SetStaticMesh(ConnectedBase);
	PanHousingMesh->SetStaticMesh(ConnectedPan);
	SensorMesh->SetStaticMesh(ConnectedTilt);
}

void AVirtualGimbalDevice::InitializeSensorCamera()
{
	FVector CameraLocation;
	FRotator CameraRotation;
	if (GimbalMesh->GetSkinnedAsset() != nullptr)
	{
		CameraLocation = FVector(170.0F, 0.0F, 25.0F);
		CameraRotation = FRotator::ZeroRotator;
	}
	else
	{
		// The articulated fixed-camera lenses face local -Y and tilt about X.
		CameraLocation = FVector(0.0F, -170.0F, 25.0F);
		CameraRotation = FRotator(0.0F, -90.0F, 0.0F);
	}
	SensorCapture->SetRelativeLocation(CameraLocation);
	SensorCapture->SetRelativeRotation(CameraRotation);
	SensorViewCamera->SetRelativeLocation(CameraLocation);
	SensorViewCamera->SetRelativeRotation(CameraRotation);

	SensorRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("SensorRenderTarget"));
	SensorRenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	SensorRenderTarget->ClearColor = FLinearColor::Black;
	SensorRenderTarget->bAutoGenerateMips = false;
	SensorRenderTarget->InitAutoFormat(512, 288);
	SensorRenderTarget->UpdateResourceImmediate(true);
	SensorCapture->TextureTarget = SensorRenderTarget;
}

void AVirtualGimbalDevice::HandleMessage(
	const uint64 ClientId, const FVirtualDeviceMessage& Message)
{
	bool bSucceeded = false;
	switch (static_cast<EVirtualDeviceMessageType>(Message.MessageType))
	{
	case EVirtualDeviceMessageType::Power:
		bSucceeded = Message.Payload.Num() == 1 && DeviceState->SetPower(Message.Payload[0] != 0);
		break;
	case EVirtualDeviceMessageType::Initialize:
		bSucceeded = Message.Payload.IsEmpty() && DeviceState->InitializeDevice();
		break;
	case EVirtualDeviceMessageType::SetPanTilt:
		{
			int16 PanCentidegrees = 0;
			int16 TiltCentidegrees = 0;
			bSucceeded = Message.Payload.Num() == 4
				&& VirtualDeviceProtocol::TryReadInt16BigEndian(Message.Payload, 0, PanCentidegrees)
				&& VirtualDeviceProtocol::TryReadInt16BigEndian(Message.Payload, 2, TiltCentidegrees)
				&& DeviceState->SetTarget(PanCentidegrees / 100.0F, TiltCentidegrees / 100.0F);
		}
		break;
	case EVirtualDeviceMessageType::StartScan:
		bSucceeded = Message.Payload.IsEmpty() && DeviceState->StartScan();
		break;
	case EVirtualDeviceMessageType::StopScan:
		bSucceeded = Message.Payload.IsEmpty() && DeviceState->StopScan();
		break;
	case EVirtualDeviceMessageType::GetStatus:
		bSucceeded = Message.Payload.IsEmpty();
		if (bSucceeded)
		{
			PublishTelemetryToClient(ClientId);
		}
		break;
	case EVirtualDeviceMessageType::InjectFault:
		bSucceeded = Message.Payload.Num() == 1
			&& Message.Payload[0] <= static_cast<uint8>(EGimbalDeviceFault::SensorFailure);
		if (bSucceeded)
		{
			DeviceState->InjectFault(static_cast<EGimbalDeviceFault>(Message.Payload[0]));
		}
		break;
	case EVirtualDeviceMessageType::ConfigureResponse:
		{
			uint16 DelayMilliseconds = 0;
			const uint8 Mode = Message.Payload.IsEmpty() ? MAX_uint8 : Message.Payload[0];
			bSucceeded = Message.Payload.Num() == 3
				&& Mode <= static_cast<uint8>(EVirtualDeviceResponseMode::NoResponse)
				&& VirtualDeviceProtocol::TryReadUint16BigEndian(
					Message.Payload, 1, DelayMilliseconds)
				&& (Mode != static_cast<uint8>(EVirtualDeviceResponseMode::Delayed)
					|| DelayMilliseconds > 0);
			SendAcknowledgeImmediately(
				ClientId, Message.Sequence, Message.MessageType, bSucceeded);
			if (bSucceeded)
			{
				FClientResponseConfiguration& Configuration =
					ClientResponseConfigurations.FindOrAdd(ClientId);
				Configuration.Mode = static_cast<EVirtualDeviceResponseMode>(Mode);
				Configuration.DelayMilliseconds =
					Configuration.Mode == EVirtualDeviceResponseMode::Delayed
						? DelayMilliseconds : 0;
				UE_LOG(LogTemp, Display, TEXT("Response simulation mode=%u delay=%u ms."),
					Mode, Configuration.DelayMilliseconds);
			}
			return;
		}
	case EVirtualDeviceMessageType::SelectEquipmentMode:
		bSucceeded = Message.Payload.Num() == 1 && Message.Payload[0] <= 1;
		if (bSucceeded)
		{
			bDroneMode = Message.Payload[0] == 1;
			bDroneAirborne = false;
			DroneTargetLocation = FVector::ZeroVector;
			DroneBody->SetRelativeLocation(FVector::ZeroVector);
			GimbalMesh->SetRelativeLocation(FVector::ZeroVector);
			DroneBody->SetVisibility(bDroneMode);
		}
		break;
	case EVirtualDeviceMessageType::DroneTakeOff:
		bSucceeded = IsDroneMode() && !bDroneAirborne && DeviceState->SetPower(true);
		if (bSucceeded) { bDroneAirborne = true; DroneTargetLocation.Z = 1000.0F; }
		break;
	case EVirtualDeviceMessageType::DroneLand:
		bSucceeded = IsDroneMode() && bDroneAirborne;
		if (bSucceeded) { bDroneAirborne = false; DroneTargetLocation.Z = 0.0F; }
		break;
	case EVirtualDeviceMessageType::DroneMoveTo:
		{
			int16 X = 0; int16 Y = 0; int16 Altitude = 0;
			bSucceeded = IsDroneMode() && bDroneAirborne && Message.Payload.Num() == 6
				&& VirtualDeviceProtocol::TryReadInt16BigEndian(Message.Payload, 0, X)
				&& VirtualDeviceProtocol::TryReadInt16BigEndian(Message.Payload, 2, Y)
				&& VirtualDeviceProtocol::TryReadInt16BigEndian(Message.Payload, 4, Altitude);
			if (bSucceeded) { DroneTargetLocation = FVector(X * 10.0F, Y * 10.0F, Altitude * 10.0F); }
		}
		break;
	case EVirtualDeviceMessageType::DroneReturnHome:
		bSucceeded = IsDroneMode() && bDroneAirborne;
		if (bSucceeded) { DroneTargetLocation = FVector(0.0F, 0.0F, 1000.0F); }
		break;
	case EVirtualDeviceMessageType::SetCameraView:
		bSucceeded = Message.Payload.Num() == 1 && Message.Payload[0] <= 1;
		if (bSucceeded)
		{
			CameraViewRequestedDelegate.Broadcast(Message.Payload[0] != 0);
		}
		break;
	default:
		break;
	}
	SendAcknowledge(ClientId, Message.Sequence, Message.MessageType, bSucceeded);
}

void AVirtualGimbalDevice::HandleClientDisconnected(const uint64 ClientId)
{
	ClientResponseConfigurations.Remove(ClientId);
}

bool AVirtualGimbalDevice::IsDroneMode() const
{
	return bDroneMode;
}

void AVirtualGimbalDevice::SendAcknowledge(
	const uint64 ClientId,
	const uint32 Sequence,
	const uint16 CommandType,
	const bool bSucceeded)
{
	FVirtualDeviceMessage Response;
	Response.MessageType = static_cast<uint16>(EVirtualDeviceMessageType::Acknowledge);
	Response.Sequence = Sequence;
	VirtualDeviceProtocol::WriteUint16BigEndian(Response.Payload, CommandType);
	Response.Payload.Add(bSucceeded ? 0 : 1);
	SendResponse(ClientId, MoveTemp(Response));
}

void AVirtualGimbalDevice::SendAcknowledgeImmediately(
	const uint64 ClientId,
	const uint32 Sequence,
	const uint16 CommandType,
	const bool bSucceeded)
{
	FVirtualDeviceMessage Response;
	Response.MessageType = static_cast<uint16>(EVirtualDeviceMessageType::Acknowledge);
	Response.Sequence = Sequence;
	VirtualDeviceProtocol::WriteUint16BigEndian(Response.Payload, CommandType);
	Response.Payload.Add(bSucceeded ? 0 : 1);
	Network->SendMessageToClient(ClientId, Response);
}

void AVirtualGimbalDevice::SendResponse(
	const uint64 ClientId, FVirtualDeviceMessage Message)
{
	const FClientResponseConfiguration Configuration =
		ClientResponseConfigurations.FindRef(ClientId);
	if (Configuration.Mode == EVirtualDeviceResponseMode::NoResponse)
	{
		return;
	}
	if (Configuration.Mode == EVirtualDeviceResponseMode::Delayed)
	{
		FTimerHandle DelayedResponseTimer;
		FTimerDelegate Delegate = FTimerDelegate::CreateWeakLambda(
			this,
			[this, ClientId, Message = MoveTemp(Message)]() mutable
			{
				const FClientResponseConfiguration CurrentConfiguration =
					ClientResponseConfigurations.FindRef(ClientId);
				if (CurrentConfiguration.Mode != EVirtualDeviceResponseMode::NoResponse)
				{
					Network->SendMessageToClient(ClientId, Message);
				}
			});
		GetWorldTimerManager().SetTimer(
			DelayedResponseTimer,
			MoveTemp(Delegate),
			Configuration.DelayMilliseconds / 1000.0F,
			false);
		return;
	}
	Network->SendMessageToClient(ClientId, Message);
}

void AVirtualGimbalDevice::PublishTelemetry()
{
	for (const uint64 ClientId : Network->ConnectedClientIds())
	{
		PublishTelemetryToClient(ClientId);
	}
}

void AVirtualGimbalDevice::PublishTelemetryToClient(const uint64 ClientId)
{
	if (ClientId == 0)
	{
		return;
	}
	FVirtualDeviceMessage Telemetry;
	Telemetry.MessageType = static_cast<uint16>(EVirtualDeviceMessageType::Telemetry);
	Telemetry.Sequence = TelemetrySequence++;
	Telemetry.Payload = CreateTelemetryPayload();
	SendResponse(ClientId, MoveTemp(Telemetry));
}

TArray<uint8> AVirtualGimbalDevice::CreateTelemetryPayload() const
{
	const FGimbalDeviceSnapshot Snapshot = DeviceState->GetSnapshot();
	TArray<uint8> Payload;
	Payload.Reserve(16);
	Payload.Add(static_cast<uint8>(Snapshot.State));
	Payload.Add(static_cast<uint8>(Snapshot.Fault));
	VirtualDeviceProtocol::WriteInt16BigEndian(Payload, static_cast<int16>(FMath::RoundToInt(Snapshot.PanDegrees * 100.0F)));
	VirtualDeviceProtocol::WriteInt16BigEndian(Payload, static_cast<int16>(FMath::RoundToInt(Snapshot.TiltDegrees * 100.0F)));
	VirtualDeviceProtocol::WriteInt16BigEndian(Payload, static_cast<int16>(FMath::RoundToInt(Snapshot.TargetPanDegrees * 100.0F)));
	VirtualDeviceProtocol::WriteInt16BigEndian(Payload, static_cast<int16>(FMath::RoundToInt(Snapshot.TargetTiltDegrees * 100.0F)));
	VirtualDeviceProtocol::WriteInt16BigEndian(Payload, static_cast<int16>(FMath::RoundToInt(Snapshot.TemperatureCelsius * 100.0F)));
	VirtualDeviceProtocol::WriteUint16BigEndian(Payload, static_cast<uint16>(Snapshot.SupplyVoltage * 1000.0F));
	return Payload;
}

void AVirtualGimbalDevice::UpdateVisualState()
{
	const FGimbalDeviceSnapshot Snapshot = DeviceState->GetSnapshot();
	if (GimbalMesh->GetSkinnedAsset() != nullptr)
	{
		GimbalMesh->ResetBoneTransformByName(YawBoneName);
		GimbalMesh->ResetBoneTransformByName(RollBoneName);
		GimbalMesh->ResetBoneTransformByName(PitchBoneName);

		const FQuat YawDelta(FVector::ZAxisVector, FMath::DegreesToRadians(Snapshot.PanDegrees));
		ApplyComponentSpaceRotation(*GimbalMesh, YawBoneName, YawDelta);

		const float ClampedRoll = FMath::Clamp(VisualRollDegrees, -60.0F, 60.0F);
		const FVector RollAxis = YawDelta.RotateVector(FVector::XAxisVector);
		const FQuat RollDelta(RollAxis, FMath::DegreesToRadians(ClampedRoll));
		ApplyComponentSpaceRotation(*GimbalMesh, RollBoneName, RollDelta);

		const FVector PitchAxis = RollDelta.RotateVector(
			YawDelta.RotateVector(FVector::YAxisVector));
		const FQuat PitchDelta(PitchAxis, FMath::DegreesToRadians(Snapshot.TiltDegrees));
		ApplyComponentSpaceRotation(*GimbalMesh, PitchBoneName, PitchDelta);

		// Keep the camera proxy aligned with the animated skeletal sensor.
		PanPivot->SetRelativeRotation(FRotator(0.0F, Snapshot.PanDegrees, 0.0F));
		TiltPivot->SetRelativeRotation(FRotator(Snapshot.TiltDegrees, 0.0F, 0.0F));
	}
	else
	{
		// The imported articulated fixed-camera hinge runs along local X. Pan
		// remains around vertical Z; only this model's tilt axis changes.
		PanPivot->SetRelativeRotation(FRotator(0.0F, Snapshot.PanDegrees, 0.0F));
		TiltPivot->SetRelativeRotation(FRotator(0.0F, 0.0F, Snapshot.TiltDegrees));
	}

	FLinearColor StatusColor = FLinearColor::Red;
	if (Snapshot.State == EGimbalDeviceState::Ready)
	{
		StatusColor = FLinearColor::Green;
	}
	else if (Snapshot.State == EGimbalDeviceState::Initializing
		|| Snapshot.State == EGimbalDeviceState::Scanning)
	{
		StatusColor = FLinearColor::Yellow;
	}
	StatusLight->SetLightColor(StatusColor);
}
