#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "DeviceLinkPlayerController.generated.h"

class UDeviceLinkStartupWidget;

UCLASS()
class UNREALVIRTUALDEVICE_API ADeviceLinkPlayerController final : public APlayerController
{
	GENERATED_BODY()

public:
	static bool TryGetAutomationListenPort(const TCHAR* CommandLine, int32& OutListenPort);

protected:
	virtual void BeginPlay() override;

private:
	void HandleStartupConfirmed(int32 ListenPort);

	UPROPERTY(Transient)
	TObjectPtr<UDeviceLinkStartupWidget> StartupWidget;
};
