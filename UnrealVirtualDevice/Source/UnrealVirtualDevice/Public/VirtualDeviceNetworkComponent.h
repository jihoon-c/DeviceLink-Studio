#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "VirtualDeviceProtocol.h"

#include "VirtualDeviceNetworkComponent.generated.h"

class FVirtualDeviceTcpServer;

DECLARE_MULTICAST_DELEGATE_TwoParams(
	FOnVirtualDeviceMessageReceived,
	uint64,
	const FVirtualDeviceMessage&);

DECLARE_MULTICAST_DELEGATE_OneParam(FOnVirtualDeviceClientDisconnected, uint64);

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
	bool SendMessageToClient(uint64 ClientId, const FVirtualDeviceMessage& Message);
	bool StartListening(int32 InListenPort, bool bInAllowLanConnections = false);
	bool IsClientConnected() const;
	TArray<uint64> ConnectedClientIds() const;
	FOnVirtualDeviceMessageReceived& OnMessageReceived();
	FOnVirtualDeviceClientDisconnected& OnClientDisconnected();

private:
	UPROPERTY(EditAnywhere, Category="DeviceLink|Network", meta=(ClampMin="1", ClampMax="65535"))
	int32 ListenPort = 5000;

	UPROPERTY(EditAnywhere, Category="DeviceLink|Network")
	bool bStartAutomatically = true;

	UPROPERTY(EditAnywhere, Category="DeviceLink|Network")
	bool bAllowLanConnections = false;

	TSharedPtr<FVirtualDeviceTcpServer> Server;
	TMap<uint64, TArray<uint8>> ReceiveFrameBuffers;
	TSet<uint64> ConnectedClients;
	FOnVirtualDeviceMessageReceived MessageReceivedDelegate;
	FOnVirtualDeviceClientDisconnected ClientDisconnectedDelegate;
};
