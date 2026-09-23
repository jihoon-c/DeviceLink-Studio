#include "DeviceLinkPlayerController.h"

#include "DeviceLinkStartupWidget.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
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

	if (IsValid(StartupWidget))
	{
		StartupWidget->RemoveFromParent();
		StartupWidget = nullptr;
	}
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
	UE_LOG(LogTemp, Display, TEXT("DeviceLink simulation started on %s:%d."),
		bAllowLanConnections ? TEXT("0.0.0.0") : TEXT("127.0.0.1"),
		ListenPort);
}
