#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"

#include "DeviceCameraViewWidget.generated.h"

class SImage;
class UTexture;

UCLASS()
class UNREALVIRTUALDEVICE_API UDeviceCameraViewWidget final : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetCameraTexture(UTexture* InTexture);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;

private:
	FSlateBrush CameraBrush;
	TSharedPtr<SImage> CameraImage;
};
