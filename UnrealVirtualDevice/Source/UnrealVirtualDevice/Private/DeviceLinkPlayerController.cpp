#include "DeviceLinkPlayerController.h"

#include "DeviceCameraViewWidget.h"
#include "DeviceLinkStartupWidget.h"
#include "EngineUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/PlatformMisc.h"
#include "TimerManager.h"
#include "Widgets/Layout/Anchors.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "VirtualGimbalDevice.h"

bool ADeviceLinkPlayerController::TryGetAutomationListenPort(
	const TCHAR* CommandLine,
	int32& OutListenPort)
{
	OutListenPort = 5000;
	if (CommandLine == nullptr || !FParse::Param(CommandLine, TEXT("DeviceLinkAutoStart")))
	{
		return false;
	}

	FString PortText;
	return !FParse::Value(CommandLine, TEXT("DeviceLinkPort="), PortText)
		|| UDeviceLinkStartupWidget::TryParseListenPort(PortText, OutListenPort);
}

bool ADeviceLinkPlayerController::ShouldAllowLanConnections(const TCHAR* CommandLine)
{
	return CommandLine != nullptr
		&& FParse::Param(CommandLine, TEXT("DeviceLinkAllowLan"));
}

void ADeviceLinkPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	// The device and player controller do not have a guaranteed BeginPlay order.
	// Retry briefly so the overlay is also available when the render target is
	// initialized after this controller.
	GetWorldTimerManager().SetTimer(
		CameraViewRetryTimer,
		this,
		&ADeviceLinkPlayerController::TryShowCameraView,
		0.25F,
		true,
		0.1F);
	TryShowCameraView();

	if (FParse::Param(FCommandLine::Get(), TEXT("DeviceLinkAutoStart")))
	{
		int32 AutomationPort = 0;
		if (!TryGetAutomationListenPort(FCommandLine::Get(), AutomationPort))
		{
			UE_LOG(LogTemp, Error, TEXT("DeviceLinkPort must be in the range 1-65535."));
			FPlatformMisc::RequestExit(false);
			return;
		}
		const bool bAllowLanConnections = ShouldAllowLanConnections(FCommandLine::Get());
		HandleStartupConfirmed(AutomationPort, bAllowLanConnections);
		return;
	}

	StartupWidget = CreateWidget<UDeviceLinkStartupWidget>(
		this,
		UDeviceLinkStartupWidget::StaticClass());
	if (!IsValid(StartupWidget))
	{
		UE_LOG(LogTemp, Error, TEXT("Could not create the DeviceLink startup widget."));
		return;
	}

	StartupWidget->SetStartupConfirmedHandler(
		FOnDeviceLinkStartupConfirmed::CreateUObject(
			this,
			&ADeviceLinkPlayerController::HandleStartupConfirmed));
	StartupWidget->AddToViewport(100);

	bShowMouseCursor = true;
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(StartupWidget->GetInitialFocusWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	UE_LOG(LogTemp, Display, TEXT("DeviceLink startup widget displayed."));
}

void ADeviceLinkPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(CameraViewRetryTimer);
	if (BoundDevice.IsValid())
	{
		BoundDevice->OnCameraViewRequested().RemoveAll(this);
	}
	if (IsValid(CameraViewWidget))
	{
		CameraViewWidget->RemoveFromParent();
		CameraViewWidget = nullptr;
	}
	if (IsValid(StartupWidget))
	{
		StartupWidget->RemoveFromParent();
		StartupWidget = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void ADeviceLinkPlayerController::HandleStartupConfirmed(
	const int32 ListenPort, const bool bAllowLanConnections)
{
	AVirtualGimbalDevice* Device = nullptr;
	for (TActorIterator<AVirtualGimbalDevice> Iterator(GetWorld()); Iterator; ++Iterator)
	{
		Device = *Iterator;
		break;
	}

	if (!IsValid(Device)
		|| !Device->ApplyEndpointConfiguration(ListenPort, bAllowLanConnections))
	{
		UE_LOG(LogTemp, Error, TEXT("Could not apply the DeviceLink endpoint configuration."));
		return;
	}
	BindToDevice(*Device);

	if (IsValid(StartupWidget))
	{
		StartupWidget->RemoveFromParent();
		StartupWidget = nullptr;
	}
	TryShowCameraView();
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
	UE_LOG(LogTemp, Display, TEXT("DeviceLink simulation started on %s:%d."),
		bAllowLanConnections ? TEXT("0.0.0.0") : TEXT("127.0.0.1"),
		ListenPort);
}

void ADeviceLinkPlayerController::TryShowCameraView()
{
	if (IsValid(CameraViewWidget))
	{
		GetWorldTimerManager().ClearTimer(CameraViewRetryTimer);
		return;
	}
	for (TActorIterator<AVirtualGimbalDevice> Iterator(GetWorld()); Iterator; ++Iterator)
	{
		BindToDevice(**Iterator);
		if (ShowCameraView(**Iterator))
		{
			GetWorldTimerManager().ClearTimer(CameraViewRetryTimer);
		}
		return;
	}
}

void ADeviceLinkPlayerController::BindToDevice(AVirtualGimbalDevice& Device)
{
	if (BoundDevice.Get() == &Device)
	{
		return;
	}
	if (BoundDevice.IsValid())
	{
		BoundDevice->OnCameraViewRequested().RemoveAll(this);
	}
	BoundDevice = &Device;
	Device.OnCameraViewRequested().AddUObject(
		this, &ADeviceLinkPlayerController::HandleCameraViewRequested);
}

void ADeviceLinkPlayerController::HandleCameraViewRequested(const bool bEnabled)
{
	AVirtualGimbalDevice* Device = BoundDevice.Get();
	if (!IsValid(Device))
	{
		return;
	}

	FViewTargetTransitionParams Transition;
	Transition.BlendTime = 0.25F;
	if (bEnabled)
	{
		if (!bSensorViewActive)
		{
			PreviousViewTarget = GetViewTarget();
		}
		SetViewTarget(Device, Transition);
		if (IsValid(CameraViewWidget))
		{
			CameraViewWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
		bSensorViewActive = true;
		UE_LOG(LogTemp, Display, TEXT("Switched to the virtual device sensor camera."));
		return;
	}

	AActor* RestoreTarget = PreviousViewTarget.Get();
	if (!IsValid(RestoreTarget) || RestoreTarget == Device)
	{
		RestoreTarget = GetPawn();
	}
	if (IsValid(RestoreTarget))
	{
		SetViewTarget(RestoreTarget, Transition);
	}
	if (IsValid(CameraViewWidget))
	{
		CameraViewWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	PreviousViewTarget.Reset();
	bSensorViewActive = false;
	UE_LOG(LogTemp, Display, TEXT("Restored the previous field camera view."));
}

bool ADeviceLinkPlayerController::ShowCameraView(AVirtualGimbalDevice& Device)
{
	if (!IsValid(Device.GetSensorRenderTarget()))
	{
		return false;
	}

	CameraViewWidget = CreateWidget<UDeviceCameraViewWidget>(
		this,
		UDeviceCameraViewWidget::StaticClass());
	if (!IsValid(CameraViewWidget))
	{
		UE_LOG(LogTemp, Error, TEXT("Could not create the sensor camera view widget."));
		return false;
	}

	CameraViewWidget->SetCameraTexture(Device.GetSensorRenderTarget());
	if (!CameraViewWidget->AddToPlayerScreen(20))
	{
		UE_LOG(LogTemp, Error, TEXT("Could not add the sensor camera view to the player screen."));
		CameraViewWidget = nullptr;
		return false;
	}
	CameraViewWidget->SetVisibility(bSensorViewActive
		? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	CameraViewWidget->SetDesiredSizeInViewport(FVector2D(492.0F, 306.0F));
	CameraViewWidget->SetAnchorsInViewport(FAnchors(1.0F, 0.0F));
	CameraViewWidget->SetAlignmentInViewport(FVector2D(1.0F, 0.0F));
	CameraViewWidget->SetPositionInViewport(FVector2D(-24.0F, 24.0F), false);
	UE_LOG(LogTemp, Display, TEXT("Sensor camera view displayed in the top-right viewport overlay."));
	return true;
}
