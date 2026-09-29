#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "DeviceLinkPlayerController.generated.h"

class AVirtualGimbalDevice;
class UDeviceCameraViewWidget;
class UDeviceLinkStartupWidget;

UCLASS()
class UNREALVIRTUALDEVICE_API ADeviceLinkPlayerController final : public APlayerController
{
	GENERATED_BODY()

public:
	static bool TryGetAutomationListenPort(const TCHAR* CommandLine, int32& OutListenPort);
	static bool ShouldAllowLanConnections(const TCHAR* CommandLine);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleStartupConfirmed(int32 ListenPort, bool bAllowLanConnections);
	void TryShowCameraView();
	bool ShowCameraView(AVirtualGimbalDevice& Device);
	void BindToDevice(AVirtualGimbalDevice& Device);
	void HandleCameraViewRequested(bool bEnabled);

	UPROPERTY(Transient)
	TObjectPtr<UDeviceLinkStartupWidget> StartupWidget;

	UPROPERTY(Transient)
	TObjectPtr<UDeviceCameraViewWidget> CameraViewWidget;

	TWeakObjectPtr<AVirtualGimbalDevice> BoundDevice;
	TWeakObjectPtr<AActor> PreviousViewTarget;
	bool bSensorViewActive = false;

	FTimerHandle CameraViewRetryTimer;
};
