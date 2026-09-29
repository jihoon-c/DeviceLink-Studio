#include "CameraViewStatusComponent.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

UCameraViewStatusComponent::UCameraViewStatusComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    PrimaryComponentTick.TickInterval = 0.1F;
}

void UCameraViewStatusComponent::BeginPlay()
{
    Super::BeginPlay();
    RefreshVisibility();
}

void UCameraViewStatusComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    HideStatusBanner();
    StatusBanner.Reset();
    Super::EndPlay(EndPlayReason);
}

void UCameraViewStatusComponent::TickComponent(
    const float DeltaTime,
    const ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    RefreshVisibility();
}

void UCameraViewStatusComponent::RefreshVisibility()
{
    const UWorld* World = GetWorld();
    const APlayerController* PlayerController = World != nullptr
        ? World->GetFirstPlayerController()
        : nullptr;
    const bool bShouldBeVisible = PlayerController != nullptr
        && PlayerController->GetViewTarget() == GetOwner();

    if (bShouldBeVisible)
    {
        ShowStatusBanner();
    }
    else
    {
        HideStatusBanner();
    }
}

void UCameraViewStatusComponent::ShowStatusBanner()
{
    if (bIsStatusBannerVisible || GEngine == nullptr || GEngine->GameViewport == nullptr)
    {
        return;
    }

    if (!StatusBanner.IsValid())
    {
        StatusBanner =
            SNew(SBox)
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Top)
            .Padding(FMargin(0.0F, 32.0F, 0.0F, 0.0F))
            .Visibility(EVisibility::HitTestInvisible)
            [
                SNew(SBorder)
                .Padding(FMargin(16.0F, 8.0F))
                .BorderBackgroundColor(FLinearColor(0.01F, 0.06F, 0.10F, 0.88F))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("CAMERA VIEW  |  VirtualEO SENSOR")))
                    .ColorAndOpacity(FLinearColor(0.10F, 0.90F, 1.00F, 1.00F))
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 22))
                ]
            ];
    }

    GEngine->GameViewport->AddViewportWidgetContent(StatusBanner.ToSharedRef(), 1000);
    bIsStatusBannerVisible = true;
}

void UCameraViewStatusComponent::HideStatusBanner()
{
    if (!bIsStatusBannerVisible)
    {
        return;
    }

    if (GEngine != nullptr && GEngine->GameViewport != nullptr && StatusBanner.IsValid())
    {
        GEngine->GameViewport->RemoveViewportWidgetContent(StatusBanner.ToSharedRef());
    }

    bIsStatusBannerVisible = false;
}
