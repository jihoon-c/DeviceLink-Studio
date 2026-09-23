#include "DeviceLinkStartupWidget.h"
#include "DeviceLinkPlayerController.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeviceLinkStartupPortValidationTest,
	"DeviceLink.VirtualDevice.Startup.PortValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeviceLinkStartupPortValidationTest::RunTest(const FString& Parameters)
{
	int32 ListenPort = 0;
	TestTrue(TEXT("Default port is valid"),
		UDeviceLinkStartupWidget::TryParseListenPort(TEXT("5000"), ListenPort));
	TestEqual(TEXT("Default port is parsed exactly"), ListenPort, 5000);

	TestFalse(TEXT("Locale grouping separators are rejected"),
		UDeviceLinkStartupWidget::TryParseListenPort(TEXT("5,000"), ListenPort));
	TestFalse(TEXT("Zero is rejected"),
		UDeviceLinkStartupWidget::TryParseListenPort(TEXT("0"), ListenPort));
	TestFalse(TEXT("Values above uint16 are rejected"),
		UDeviceLinkStartupWidget::TryParseListenPort(TEXT("65536"), ListenPort));
	TestFalse(TEXT("Non-numeric text is rejected"),
		UDeviceLinkStartupWidget::TryParseListenPort(TEXT("port"), ListenPort));

	TestTrue(TEXT("Automation mode uses its default port"),
		ADeviceLinkPlayerController::TryGetAutomationListenPort(
			TEXT("-unattended -DeviceLinkAutoStart"), ListenPort));
	TestEqual(TEXT("Automation default port is exact"), ListenPort, 5000);
	TestTrue(TEXT("Automation mode accepts an explicit port"),
		ADeviceLinkPlayerController::TryGetAutomationListenPort(
			TEXT("-DeviceLinkAutoStart -DeviceLinkPort=5510"), ListenPort));
	TestEqual(TEXT("Automation explicit port is exact"), ListenPort, 5510);
	TestFalse(TEXT("Automation mode rejects an invalid explicit port"),
		ADeviceLinkPlayerController::TryGetAutomationListenPort(
			TEXT("-DeviceLinkAutoStart -DeviceLinkPort=70000"), ListenPort));
	TestFalse(TEXT("Interactive mode is not treated as automation"),
		ADeviceLinkPlayerController::TryGetAutomationListenPort(TEXT("-game"), ListenPort));

	TestFalse(TEXT("LAN access is opt-in"),
		ADeviceLinkPlayerController::ShouldAllowLanConnections(
			TEXT("-DeviceLinkAutoStart -DeviceLinkPort=5000")));
	TestTrue(TEXT("LAN access flag is recognized"),
		ADeviceLinkPlayerController::ShouldAllowLanConnections(
			TEXT("-DeviceLinkAutoStart -DeviceLinkAllowLan -DeviceLinkPort=5000")));
	TestFalse(TEXT("Null command line keeps local-only mode"),
		ADeviceLinkPlayerController::ShouldAllowLanConnections(nullptr));
	return true;
}

#endif
