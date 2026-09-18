#include "VirtualGimbalDevice.h"

#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
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
	Network->OnMessageReceived().AddUObject(this, &AVirtualGimbalDevice::HandleMessage);
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

bool AVirtualGimbalDevice::ApplyEndpointConfiguration(const int32 ListenPort)
{
	return Network->StartListening(ListenPort);
}

void AVirtualGimbalDevice::HandleMessage(const FVirtualDeviceMessage& Message)
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
			PublishTelemetry();
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
			SendAcknowledgeImmediately(Message.Sequence, Message.MessageType, bSucceeded);
			if (bSucceeded)
			{
				ResponseMode = static_cast<EVirtualDeviceResponseMode>(Mode);
				ResponseDelayMilliseconds = ResponseMode == EVirtualDeviceResponseMode::Delayed
					? DelayMilliseconds : 0;
				UE_LOG(LogTemp, Display, TEXT("Response simulation mode=%u delay=%u ms."),
					Mode, ResponseDelayMilliseconds);
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
	default:
		break;
	}
	SendAcknowledge(Message.Sequence, Message.MessageType, bSucceeded);
}

bool AVirtualGimbalDevice::IsDroneMode() const
{
	return bDroneMode;
}

void AVirtualGimbalDevice::SendAcknowledge(
	const uint32 Sequence,
	const uint16 CommandType,
	const bool bSucceeded)
{
	FVirtualDeviceMessage Response;
	Response.MessageType = static_cast<uint16>(EVirtualDeviceMessageType::Acknowledge);
	Response.Sequence = Sequence;
	VirtualDeviceProtocol::WriteUint16BigEndian(Response.Payload, CommandType);
	Response.Payload.Add(bSucceeded ? 0 : 1);
	SendResponse(MoveTemp(Response));
}

void AVirtualGimbalDevice::SendAcknowledgeImmediately(
	const uint32 Sequence,
	const uint16 CommandType,
	const bool bSucceeded)
{
	FVirtualDeviceMessage Response;
	Response.MessageType = static_cast<uint16>(EVirtualDeviceMessageType::Acknowledge);
	Response.Sequence = Sequence;
	VirtualDeviceProtocol::WriteUint16BigEndian(Response.Payload, CommandType);
	Response.Payload.Add(bSucceeded ? 0 : 1);
	Network->SendMessage(Response);
}

void AVirtualGimbalDevice::SendResponse(FVirtualDeviceMessage Message)
{
	if (ResponseMode == EVirtualDeviceResponseMode::NoResponse)
	{
		return;
	}
	if (ResponseMode == EVirtualDeviceResponseMode::Delayed)
	{
		FTimerHandle DelayedResponseTimer;
		FTimerDelegate Delegate = FTimerDelegate::CreateWeakLambda(
			this,
			[this, Message = MoveTemp(Message)]() mutable
			{
				if (ResponseMode != EVirtualDeviceResponseMode::NoResponse)
				{
					Network->SendMessage(Message);
				}
			});
		GetWorldTimerManager().SetTimer(
			DelayedResponseTimer,
			MoveTemp(Delegate),
			ResponseDelayMilliseconds / 1000.0F,
			false);
		return;
	}
	Network->SendMessage(Message);
}

void AVirtualGimbalDevice::PublishTelemetry()
{
	if (!Network->IsClientConnected())
	{
		return;
	}
	FVirtualDeviceMessage Telemetry;
	Telemetry.MessageType = static_cast<uint16>(EVirtualDeviceMessageType::Telemetry);
	Telemetry.Sequence = TelemetrySequence++;
	Telemetry.Payload = CreateTelemetryPayload();
	SendResponse(MoveTemp(Telemetry));
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
	}
	else
	{
		PanPivot->SetRelativeRotation(FRotator(0.0F, Snapshot.PanDegrees, 0.0F));
		TiltPivot->SetRelativeRotation(FRotator(Snapshot.TiltDegrees, 0.0F, 0.0F));
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
