#include "VirtualDeviceNetworkComponent.h"

#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace
{
class FScopedSocket final
{
public:
	explicit FScopedSocket(FSocket* InSocket = nullptr)
		: Socket(InSocket)
	{
	}

	~FScopedSocket()
	{
		Reset();
	}

	FScopedSocket(const FScopedSocket&) = delete;
	FScopedSocket& operator=(const FScopedSocket&) = delete;

	FSocket* Get() const
	{
		return Socket;
	}

	void Reset(FSocket* InSocket = nullptr)
	{
		if (Socket != nullptr)
		{
			Socket->Close();
			ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		}
		Socket = InSocket;
	}

private:
	FSocket* Socket = nullptr;
};
}

class FVirtualDeviceTcpServer final : public FRunnable
{
public:
	enum class EEventType : uint8
	{
		Connected,
		BytesReceived,
		Disconnected,
	};

	struct FEvent final
	{
		EEventType Type = EEventType::Disconnected;
		TArray<uint8> Bytes;
	};

	explicit FVirtualDeviceTcpServer(const uint16 InListenPort)
		: ListenPort(InListenPort)
	{
	}

	~FVirtualDeviceTcpServer() override
	{
		StopServer();
	}

	bool StartServer()
	{
		if (Thread.IsValid())
		{
			return true;
		}
		bRunning.Store(true);
		Thread.Reset(FRunnableThread::Create(this, TEXT("DeviceLinkVirtualDeviceTcp")));
		if (!Thread.IsValid())
		{
			bRunning.Store(false);
			return false;
		}
		return true;
	}

	void StopServer()
	{
		bRunning.Store(false);
		if (Thread.IsValid())
		{
			Thread->WaitForCompletion();
			Thread.Reset();
		}
	}

	virtual uint32 Run() override
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		FScopedSocket ListenSocket(SocketSubsystem->CreateSocket(NAME_Stream, TEXT("DeviceLinkListen"), false));
		if (ListenSocket.Get() == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("Could not create the virtual device listen socket."));
			return 1;
		}

		bool bAddressValid = false;
		TSharedRef<FInternetAddr> Address = SocketSubsystem->CreateInternetAddr();
		Address->SetIp(TEXT("127.0.0.1"), bAddressValid);
		Address->SetPort(ListenPort);
		if (!bAddressValid
			|| !ListenSocket.Get()->SetReuseAddr(true)
			|| !ListenSocket.Get()->SetNonBlocking(true)
			|| !ListenSocket.Get()->Bind(*Address)
			|| !ListenSocket.Get()->Listen(1))
		{
			UE_LOG(LogTemp, Error, TEXT("Could not listen on 127.0.0.1:%u."), ListenPort);
			return 1;
		}

		UE_LOG(LogTemp, Display, TEXT("Virtual device listening on 127.0.0.1:%u."), ListenPort);
		FScopedSocket ClientSocket;
		TArray<uint8> ReceiveBuffer;
		ReceiveBuffer.SetNumUninitialized(64 * 1024);
		TArray<uint8> PendingSend;
		int32 PendingSendOffset = 0;

		while (bRunning.Load())
		{
			if (ClientSocket.Get() == nullptr)
			{
				ClientSocket.Reset(ListenSocket.Get()->Accept(TEXT("DeviceLinkClient")));
				if (ClientSocket.Get() != nullptr)
				{
					ClientSocket.Get()->SetNonBlocking(true);
					bClientConnected.Store(true);
					FEvent Event;
					Event.Type = EEventType::Connected;
					IncomingEvents.Enqueue(MoveTemp(Event));
					UE_LOG(LogTemp, Display, TEXT("DeviceLink client connected."));
				}
				else
				{
					FPlatformProcess::SleepNoStats(0.01F);
				}
				continue;
			}

			bool bConnectionClosed = false;
			if (ClientSocket.Get()->Wait(
				ESocketWaitConditions::WaitForRead,
				FTimespan::FromMilliseconds(0)))
			{
				int32 BytesRead = 0;
				const bool bReceiveSucceeded = ClientSocket.Get()->Recv(
					ReceiveBuffer.GetData(),
					ReceiveBuffer.Num(),
					BytesRead);
				if (bReceiveSucceeded && BytesRead > 0)
				{
					FEvent Event;
					Event.Type = EEventType::BytesReceived;
					Event.Bytes.Append(ReceiveBuffer.GetData(), BytesRead);
					IncomingEvents.Enqueue(MoveTemp(Event));
				}
				else if (bReceiveSucceeded)
				{
					bConnectionClosed = true;
				}
				else
				{
					const ESocketErrors Error = SocketSubsystem->GetLastErrorCode();
					bConnectionClosed = Error != SE_NO_ERROR && Error != SE_EWOULDBLOCK;
				}
			}

			const bool bSendSucceeded = FlushOutgoing(
				*ClientSocket.Get(),
				PendingSend,
				PendingSendOffset);
			if (bConnectionClosed
				|| !bSendSucceeded
				|| ClientSocket.Get()->GetConnectionState() == SCS_ConnectionError)
			{
				bClientConnected.Store(false);
				ClientSocket.Reset();
				PendingSend.Reset();
				PendingSendOffset = 0;
				FEvent Event;
				Event.Type = EEventType::Disconnected;
				IncomingEvents.Enqueue(MoveTemp(Event));
				DiscardOutgoing();
				UE_LOG(LogTemp, Display, TEXT("DeviceLink client disconnected."));
			}
			FPlatformProcess::SleepNoStats(0.002F);
		}

		bClientConnected.Store(false);
		ClientSocket.Reset();
		DiscardOutgoing();
		return 0;
	}

	virtual void Stop() override
	{
		bRunning.Store(false);
	}

	bool EnqueueOutgoing(TArray<uint8>&& Bytes)
	{
		if (!bClientConnected.Load())
		{
			return false;
		}
		OutgoingMessages.Enqueue(MoveTemp(Bytes));
		return true;
	}

	bool DequeueEvent(FEvent& OutEvent)
	{
		return IncomingEvents.Dequeue(OutEvent);
	}

	bool IsClientConnected() const
	{
		return bClientConnected.Load();
	}

private:
	bool FlushOutgoing(FSocket& Socket, TArray<uint8>& PendingBytes, int32& PendingOffset)
	{
		while (bRunning.Load())
		{
			if (PendingBytes.IsEmpty())
			{
				if (!OutgoingMessages.Dequeue(PendingBytes))
				{
					return true;
				}
				PendingOffset = 0;
			}

			int32 BytesSent = 0;
			if (Socket.Send(
				PendingBytes.GetData() + PendingOffset,
				PendingBytes.Num() - PendingOffset,
				BytesSent)
				&& BytesSent > 0)
			{
				PendingOffset += BytesSent;
				if (PendingOffset == PendingBytes.Num())
				{
					PendingBytes.Reset();
					PendingOffset = 0;
				}
				continue;
			}

			const ESocketErrors Error = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode();
			if (Error != SE_NO_ERROR && Error != SE_EWOULDBLOCK)
			{
				return false;
			}
			if (!Socket.Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(20)))
			{
				return true;
			}
		}
		return true;
	}

	void DiscardOutgoing()
	{
		TArray<uint8> Discarded;
		while (OutgoingMessages.Dequeue(Discarded))
		{
		}
	}

	uint16 ListenPort = 0;
	TAtomic<bool> bRunning{false};
	TAtomic<bool> bClientConnected{false};
	TUniquePtr<FRunnableThread> Thread;
	TQueue<FEvent, EQueueMode::Mpsc> IncomingEvents;
	TQueue<TArray<uint8>, EQueueMode::Mpsc> OutgoingMessages;
};

UVirtualDeviceNetworkComponent::UVirtualDeviceNetworkComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

UVirtualDeviceNetworkComponent::~UVirtualDeviceNetworkComponent() = default;

void UVirtualDeviceNetworkComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bStartAutomatically && !StartListening(ListenPort))
	{
		UE_LOG(LogTemp, Error, TEXT("Could not start the default virtual device endpoint."));
	}
}

void UVirtualDeviceNetworkComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Server.Reset();
	ReceiveFrameBuffer.Reset();
	Super::EndPlay(EndPlayReason);
}

void UVirtualDeviceNetworkComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Server.IsValid())
	{
		return;
	}

	FVirtualDeviceTcpServer::FEvent Event;
	while (Server->DequeueEvent(Event))
	{
		if (Event.Type != FVirtualDeviceTcpServer::EEventType::BytesReceived)
		{
			ReceiveFrameBuffer.Reset();
			continue;
		}

		TArray<FVirtualDeviceMessage> Messages;
		VirtualDeviceProtocol::Consume(ReceiveFrameBuffer, Event.Bytes, Messages);
		for (const FVirtualDeviceMessage& Message : Messages)
		{
			MessageReceivedDelegate.Broadcast(Message);
		}
	}
}

bool UVirtualDeviceNetworkComponent::SendMessage(const FVirtualDeviceMessage& Message)
{
	if (!Server.IsValid())
	{
		return false;
	}
	TArray<uint8> Bytes;
	return VirtualDeviceProtocol::Serialize(Message, Bytes)
		&& Server->EnqueueOutgoing(MoveTemp(Bytes));
}

bool UVirtualDeviceNetworkComponent::StartListening(const int32 InListenPort)
{
	if (InListenPort < 1 || InListenPort > MAX_uint16)
	{
		return false;
	}

	Server.Reset();
	ReceiveFrameBuffer.Reset();
	ListenPort = InListenPort;
	Server = MakeShared<FVirtualDeviceTcpServer>(static_cast<uint16>(ListenPort));
	if (!Server->StartServer())
	{
		UE_LOG(LogTemp, Error, TEXT("Could not start the virtual device TCP worker."));
		Server.Reset();
		return false;
	}
	return true;
}

bool UVirtualDeviceNetworkComponent::IsClientConnected() const
{
	return Server.IsValid() && Server->IsClientConnected();
}

FOnVirtualDeviceMessageReceived& UVirtualDeviceNetworkComponent::OnMessageReceived()
{
	return MessageReceivedDelegate;
}
