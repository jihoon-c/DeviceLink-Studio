#include "DeviceLinkStartupWidget.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

void UDeviceLinkStartupWidget::SetStartupConfirmedHandler(
	FOnDeviceLinkStartupConfirmed InHandler)
{
	StartupConfirmedHandler = MoveTemp(InHandler);
}

TSharedPtr<SWidget> UDeviceLinkStartupWidget::GetInitialFocusWidget() const
{
	return PortTextBox;
}

bool UDeviceLinkStartupWidget::TryParseListenPort(
	const FString& Text,
	int32& OutListenPort)
{
	OutListenPort = 0;
	return Text.IsNumeric()
		&& LexTryParseString(OutListenPort, *Text)
		&& OutListenPort >= 1
		&& OutListenPort <= MAX_uint16;
}

TSharedRef<SWidget> UDeviceLinkStartupWidget::RebuildWidget()
{
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 30);
	const FSlateFontInfo HeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 17);
	const FSlateFontInfo BodyFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14);

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderBackgroundColor(FLinearColor(0.008F, 0.015F, 0.025F, 0.92F))
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(24.0F)
		[
			SNew(SBox)
			.WidthOverride(680.0F)
			[
				SNew(SBorder)
				.Padding(36.0F)
				.BorderBackgroundColor(FLinearColor(0.025F, 0.055F, 0.085F, 0.98F))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("DeviceLink Virtual Equipment Lab")))
						.Font(TitleFont)
						.ColorAndOpacity(FLinearColor(0.22F, 0.78F, 1.0F))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0F, 8.0F, 0.0F, 22.0F)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT(
							"DeviceLink Studio와 TCP로 연결되는 EO/IR 짐벌 가상장비입니다.\n"
							"전원, 초기화, Pan/Tilt, 스캔, 고장 주입 및 텔레메트리를 시험할 수 있습니다.")))
						.Font(BodyFont)
						.AutoWrapText(true)
						.LineHeightPercentage(1.25F)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0F, 0.0F, 0.0F, 10.0F)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("통신 초기 설정")))
						.Font(HeadingFont)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0F, 4.0F)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("기본 바인딩   127.0.0.1  (로컬 전용)")))
						.Font(BodyFont)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0F, 4.0F, 0.0F, 8.0F)
					[
						SAssignNew(AllowLanCheckBox, SCheckBox)
						.IsChecked(ECheckBoxState::Unchecked)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT(
								"같은 네트워크의 다른 PC 접속 허용 (0.0.0.0)")))
							.Font(BodyFont)
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0F, 4.0F)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0F, 0.0F, 18.0F, 0.0F)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("수신 포트")))
							.Font(BodyFont)
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0F)
						[
							SAssignNew(PortTextBox, SEditableTextBox)
							.Text(FText::FromString(TEXT("5000")))
							.HintText(FText::FromString(TEXT("1 - 65535")))
							.Font(BodyFont)
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0F, 8.0F, 0.0F, 14.0F)
					[
						SAssignNew(ValidationText, STextBlock)
						.Text(FText::GetEmpty())
						.Font(BodyFont)
						.ColorAndOpacity(FLinearColor(1.0F, 0.3F, 0.25F))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0F, 8.0F)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.ContentPadding(FMargin(24.0F, 12.0F))
						.OnClicked(BIND_UOBJECT_DELEGATE(FOnClicked, HandleStartClicked))
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("시뮬레이션 시작")))
							.Font(HeadingFont)
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0F, 14.0F, 0.0F, 0.0F)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT(
							"로컬 테스트는 127.0.0.1, 노트북 테스트는 데스크톱 IPv4로 연결하세요.\n"
							"LAN 허용은 신뢰할 수 있는 개인 네트워크에서만 사용하세요.\n"
							"상태등: 빨강=전원 꺼짐/고장, 초록=준비, 노랑=초기화/스캔")))
						.Font(BodyFont)
						.ColorAndOpacity(FLinearColor(0.65F, 0.72F, 0.78F))
						.AutoWrapText(true)
					]
				]
			]
		];
}

void UDeviceLinkStartupWidget::NativeDestruct()
{
	StartupConfirmedHandler.Unbind();
	AllowLanCheckBox.Reset();
	PortTextBox.Reset();
	ValidationText.Reset();
	Super::NativeDestruct();
}

FReply UDeviceLinkStartupWidget::HandleStartClicked()
{
	const FString PortText = PortTextBox.IsValid()
		? PortTextBox->GetText().ToString()
		: FString();
	const bool bAllowLanConnections = AllowLanCheckBox.IsValid()
		&& AllowLanCheckBox->GetCheckedState() == ECheckBoxState::Checked;
	int32 ListenPort = 0;
	if (!PortTextBox.IsValid()
		|| !TryParseListenPort(PortText, ListenPort))
	{
		if (ValidationText.IsValid())
		{
			ValidationText->SetText(FText::FromString(TEXT("포트는 1~65535 범위의 숫자여야 합니다.")));
		}
		return FReply::Handled();
	}

	if (ValidationText.IsValid())
	{
		ValidationText->SetText(FText::GetEmpty());
	}
	StartupConfirmedHandler.ExecuteIfBound(ListenPort, bAllowLanConnections);
	return FReply::Handled();
}
