#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CameraViewStatusComponent.generated.h"

class SWidget;

/**
 * Displays a viewport status banner while the owning actor is the local
 * player's active camera view target.
 */
UCLASS(ClassGroup = (DeviceLink), meta = (BlueprintSpawnableComponent))
class UNREALVIRTUALDEVICE_API UCameraViewStatusComponent final : public UActorComponent
{
    GENERATED_BODY()

public:
    UCameraViewStatusComponent();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    void RefreshVisibility();
    void ShowStatusBanner();
    void HideStatusBanner();

    TSharedPtr<SWidget> StatusBanner;
    bool bIsStatusBannerVisible = false;
};
