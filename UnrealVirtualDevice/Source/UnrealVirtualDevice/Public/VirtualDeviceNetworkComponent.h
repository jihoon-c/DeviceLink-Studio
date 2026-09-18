#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "VirtualDeviceProtocol.h"

#include "VirtualDeviceNetworkComponent.generated.h"

class FVirtualDeviceTcpServer;

DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnVirtualDeviceMessageReceived,
	const FVirtualDeviceMessage&);

UCLASS(ClassGroup=(DeviceLink), meta=(BlueprintSpawnableComponent))
class UNREALVIRTUALDEVICE_API UVirtualDeviceNetworkComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	UVirtualDeviceNetworkComponent();
	virtual ~UVirtualDeviceNetworkComponent() override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	bool SendMessage(const FVirtualDeviceMessage& Message);
	bool StartListening(int32 InListenPort);
	bool IsClientConnected() const;
	FOnVirtualDeviceMessageReceived& OnMessageReceived();

private:
	UPROPERTY(EditAnywhere, Category="DeviceLink|Network", meta=(ClampMin="1", ClampMax="65535"))
	int32 ListenPort = 5000;

	UPROPERTY(EditAnywhere, Category="DeviceLink|Network")
	bool bStartAutomatically = true;

	TSharedPtr<FVirtualDeviceTcpServer> Server;
	TArray<uint8> ReceiveFrameBuffer;
	FOnVirtualDeviceMessageReceived MessageReceivedDelegate;
};
