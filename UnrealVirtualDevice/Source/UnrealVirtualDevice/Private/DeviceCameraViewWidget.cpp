#include "DeviceCameraViewWidget.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void UDeviceCameraViewWidget::SetCameraTexture(UTexture* InTexture)
{
	CameraBrush.SetResourceObject(InTexture);
	CameraBrush.DrawAs = ESlateBrushDrawType::Image;
	CameraBrush.ImageSize = FVector2D(480.0F, 270.0F);
	CameraBrush.TintColor = FSlateColor(FLinearColor::White);

	if (CameraImage.IsValid())
	{
		CameraImage->SetImage(&CameraBrush);
	}
}

TSharedRef<SWidget> UDeviceCameraViewWidget::RebuildWidget()
{
	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13);

	return SNew(SBox)
		.WidthOverride(492.0F)
		.HeightOverride(306.0F)
		[
			SNew(SBorder)
			.Padding(6.0F)
			.BorderBackgroundColor(FLinearColor(0.01F, 0.02F, 0.03F, 0.94F))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4.0F, 1.0F, 4.0F, 6.0F)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("EO/IR SENSOR VIEW  •  LIVE")))
					.Font(LabelFont)
					.ColorAndOpacity(FLinearColor(0.2F, 0.85F, 1.0F))
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0F)
				[
					SNew(SBorder)
					.Padding(1.0F)
					.BorderBackgroundColor(FLinearColor(0.1F, 0.3F, 0.38F, 1.0F))
					[
						SAssignNew(CameraImage, SImage)
						.Image(&CameraBrush)
					]
				]
			]
		];
}

void UDeviceCameraViewWidget::NativeDestruct()
{
	CameraImage.Reset();
	CameraBrush.SetResourceObject(nullptr);
	Super::NativeDestruct();
}
