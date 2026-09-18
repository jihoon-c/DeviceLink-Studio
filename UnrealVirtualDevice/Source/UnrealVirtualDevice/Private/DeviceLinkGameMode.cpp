#include "DeviceLinkGameMode.h"

#include "DeviceLinkPlayerController.h"

ADeviceLinkGameMode::ADeviceLinkGameMode()
{
	PlayerControllerClass = ADeviceLinkPlayerController::StaticClass();
}
