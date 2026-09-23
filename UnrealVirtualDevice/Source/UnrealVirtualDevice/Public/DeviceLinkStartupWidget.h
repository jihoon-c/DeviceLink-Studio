#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "DeviceLinkStartupWidget.generated.h"

class SEditableTextBox;
class SCheckBox;
class STextBlock;
class FReply;

DECLARE_DELEGATE_TwoParams(FOnDeviceLinkStartupConfirmed, int32, bool);

UCLASS()
class UNREALVIRTUALDEVICE_API UDeviceLinkStartupWidget final : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetStartupConfirmedHandler(FOnDeviceLinkStartupConfirmed InHandler);
	TSharedPtr<SWidget> GetInitialFocusWidget() const;
	static bool TryParseListenPort(const FString& Text, int32& OutListenPort);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;

private:
	FReply HandleStartClicked();

	TSharedPtr<SEditableTextBox> PortTextBox;
	TSharedPtr<SCheckBox> AllowLanCheckBox;
	TSharedPtr<STextBlock> ValidationText;
	FOnDeviceLinkStartupConfirmed StartupConfirmedHandler;
};
