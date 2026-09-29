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
		uint64 ClientId = 0;
		TArray<uint8> Bytes;
	};

	struct FOutgoingMessage final
	{
		uint64 ClientId = 0;
		TArray<uint8> Bytes;
	};

	FVirtualDeviceTcpServer(const uint16 InListenPort, const bool bInAllowLanConnections)
		: ListenPort(InListenPort)
		, bAllowLanConnections(bInAllowLanConnections)
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

		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		ListenSocket.Reset(SocketSubsystem->CreateSocket(NAME_Stream, TEXT("DeviceLinkListen"), false));
		if (ListenSocket.Get() == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("Could not create the virtual device listen socket."));
			return false;
		}

		TSharedRef<FInternetAddr> Address = SocketSubsystem->CreateInternetAddr();
		if (bAllowLanConnections)
		{
			Address->SetAnyAddress();
		}
		else
		{
			Address->SetLoopbackAddress();
		}
		Address->SetPort(ListenPort);
		const TCHAR* BindAddress = bAllowLanConnections ? TEXT("0.0.0.0") : TEXT("127.0.0.1");
		if (!ListenSocket.Get()->SetReuseAddr(true)
			|| !ListenSocket.Get()->SetNonBlocking(true)
			|| !ListenSocket.Get()->Bind(*Address)
			|| !ListenSocket.Get()->Listen(8))
		{
			UE_LOG(LogTemp, Error, TEXT("Could not listen on %s:%u."), BindAddress, ListenPort);
			ListenSocket.Reset();
			return false;
		}

		bRunning.Store(true);
		bListening.Store(true);
		Thread.Reset(FRunnableThread::Create(this, TEXT("DeviceLinkVirtualDeviceTcp")));
		if (!Thread.IsValid())
		{
			bRunning.Store(false);
			bListening.Store(false);
			ListenSocket.Reset();
			return false;
		}
		UE_LOG(LogTemp, Display, TEXT("Virtual device listening on %s:%u."), BindAddress, ListenPort);
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
		bListening.Store(false);
		ListenSocket.Reset();
	}

	virtual uint32 Run() override
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		TArray<uint8> ReceiveBuffer;
		ReceiveBuffer.SetNumUninitialized(64 * 1024);

		while (bRunning.Load())
		{
			bool bPendingConnection = false;
			while (ListenSocket.Get() != nullptr
				&& ListenSocket.Get()->HasPendingConnection(bPendingConnection)
				&& bPendingConnection)
			{
				FSocket* AcceptedSocket = ListenSocket.Get()->Accept(TEXT("DeviceLinkClient"));
				if (AcceptedSocket == nullptr)
				{
					break;
				}
				AcceptedSocket->SetNonBlocking(true);
				auto Client = MakeUnique<FClientState>();
				Client->Id = NextClientId++;
				Client->Socket.Reset(AcceptedSocket);
				FEvent Event;
				Event.Type = EEventType::Connected;
				Event.ClientId = Client->Id;
				IncomingEvents.Enqueue(MoveTemp(Event));
				UE_LOG(LogTemp, Display, TEXT("DeviceLink client %llu connected."), Client->Id);
				Clients.Add(MoveTemp(Client));
				ConnectedClientCount.Store(Clients.Num());
			}

			FOutgoingMessage Outgoing;
			while (OutgoingMessages.Dequeue(Outgoing))
			{
				for (const TUniquePtr<FClientState>& Client : Clients)
				{
					if (Outgoing.ClientId == 0 || Outgoing.ClientId == Client->Id)
					{
						Client->QueuedSends.Enqueue(Outgoing.Bytes);
					}
				}
			}

			for (int32 Index = Clients.Num() - 1; Index >= 0; --Index)
			{
				FClientState& Client = *Clients[Index];
				bool bConnectionClosed = false;
				if (Client.Socket.Get()->Wait(
					ESocketWaitConditions::WaitForRead,
					FTimespan::FromMilliseconds(0)))
				{
					int32 BytesRead = 0;
					const bool bReceiveSucceeded = Client.Socket.Get()->Recv(
						ReceiveBuffer.GetData(), ReceiveBuffer.Num(), BytesRead);
					if (bReceiveSucceeded && BytesRead > 0)
					{
						FEvent Event;
						Event.Type = EEventType::BytesReceived;
						Event.ClientId = Client.Id;
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

				const bool bSendSucceeded = FlushOutgoing(Client);
				if (bConnectionClosed || !bSendSucceeded
					|| Client.Socket.Get()->GetConnectionState() == SCS_ConnectionError)
				{
					const uint64 DisconnectedId = Client.Id;
					Clients.RemoveAtSwap(Index);
					ConnectedClientCount.Store(Clients.Num());
					FEvent Event;
					Event.Type = EEventType::Disconnected;
					Event.ClientId = DisconnectedId;
					IncomingEvents.Enqueue(MoveTemp(Event));
					UE_LOG(LogTemp, Display, TEXT("DeviceLink client %llu disconnected."), DisconnectedId);
				}
			}
			FPlatformProcess::SleepNoStats(0.002F);
		}

		for (const TUniquePtr<FClientState>& Client : Clients)
		{
			FEvent Event;
			Event.Type = EEventType::Disconnected;
			Event.ClientId = Client->Id;
			IncomingEvents.Enqueue(MoveTemp(Event));
		}
		Clients.Reset();
		ConnectedClientCount.Store(0);
		DiscardOutgoing();
		return 0;
	}

	virtual void Stop() override
	{
		bRunning.Store(false);
	}

	bool EnqueueOutgoing(const uint64 ClientId, TArray<uint8>&& Bytes)
	{
		if (ConnectedClientCount.Load() == 0)
		{
			return false;
		}
		OutgoingMessages.Enqueue(FOutgoingMessage{ClientId, MoveTemp(Bytes)});
		return true;
	}

	bool DequeueEvent(FEvent& OutEvent)
	{
		return IncomingEvents.Dequeue(OutEvent);
	}

	bool IsClientConnected() const
	{
		return ConnectedClientCount.Load() > 0;
	}

	bool IsListening() const
	{
		return bListening.Load();
	}

private:
	struct FClientState final
	{
		uint64 Id = 0;
		FScopedSocket Socket;
		TQueue<TArray<uint8>> QueuedSends;
		TArray<uint8> PendingSend;
		int32 PendingSendOffset = 0;
	};

	bool FlushOutgoing(FClientState& Client)
	{
		while (bRunning.Load())
		{
			if (Client.PendingSend.IsEmpty())
			{
				if (!Client.QueuedSends.Dequeue(Client.PendingSend))
				{
					return true;
				}
				Client.PendingSendOffset = 0;
			}

			int32 BytesSent = 0;
			if (Client.Socket.Get()->Send(
				Client.PendingSend.GetData() + Client.PendingSendOffset,
				Client.PendingSend.Num() - Client.PendingSendOffset,
				BytesSent)
				&& BytesSent > 0)
			{
				Client.PendingSendOffset += BytesSent;
				if (Client.PendingSendOffset == Client.PendingSend.Num())
				{
					Client.PendingSend.Reset();
					Client.PendingSendOffset = 0;
				}
				continue;
			}

			const ESocketErrors Error = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode();
			if (Error != SE_NO_ERROR && Error != SE_EWOULDBLOCK)
			{
				return false;
			}
			if (!Client.Socket.Get()->Wait(
				ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(20)))
			{
				return true;
			}
		}
		return true;
	}

	void DiscardOutgoing()
	{
		FOutgoingMessage Discarded;
		while (OutgoingMessages.Dequeue(Discarded))
		{
		}
	}

	uint16 ListenPort = 0;
	bool bAllowLanConnections = false;
	TAtomic<bool> bRunning{false};
	TAtomic<bool> bListening{false};
	TAtomic<int32> ConnectedClientCount{0};
	uint64 NextClientId = 1;
	FScopedSocket ListenSocket;
	TArray<TUniquePtr<FClientState>> Clients;
	TUniquePtr<FRunnableThread> Thread;
	TQueue<FEvent, EQueueMode::Mpsc> IncomingEvents;
	TQueue<FOutgoingMessage, EQueueMode::Mpsc> OutgoingMessages;
};

UVirtualDeviceNetworkComponent::UVirtualDeviceNetworkComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

UVirtualDeviceNetworkComponent::~UVirtualDeviceNetworkComponent() = default;

void UVirtualDeviceNetworkComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bStartAutomatically && !StartListening(ListenPort, bAllowLanConnections))
	{
		UE_LOG(LogTemp, Error, TEXT("Could not start the default virtual device endpoint."));
	}
}

void UVirtualDeviceNetworkComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Server.Reset();
	ReceiveFrameBuffers.Reset();
	ConnectedClients.Reset();
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
		if (Event.Type == FVirtualDeviceTcpServer::EEventType::Connected)
		{
			ConnectedClients.Add(Event.ClientId);
			ReceiveFrameBuffers.FindOrAdd(Event.ClientId);
			continue;
		}
		if (Event.Type == FVirtualDeviceTcpServer::EEventType::Disconnected)
		{
			ConnectedClients.Remove(Event.ClientId);
			ReceiveFrameBuffers.Remove(Event.ClientId);
			ClientDisconnectedDelegate.Broadcast(Event.ClientId);
			continue;
		}

		TArray<FVirtualDeviceMessage> Messages;
		TArray<uint8>& ReceiveFrameBuffer = ReceiveFrameBuffers.FindOrAdd(Event.ClientId);
		VirtualDeviceProtocol::Consume(ReceiveFrameBuffer, Event.Bytes, Messages);
		for (const FVirtualDeviceMessage& Message : Messages)
		{
			MessageReceivedDelegate.Broadcast(Event.ClientId, Message);
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
		&& Server->EnqueueOutgoing(0, MoveTemp(Bytes));

}

bool UVirtualDeviceNetworkComponent::SendMessageToClient(
	const uint64 ClientId, const FVirtualDeviceMessage& Message)
{
	if (!Server.IsValid() || !ConnectedClients.Contains(ClientId))
	{
		return false;
	}
	TArray<uint8> Bytes;
	return VirtualDeviceProtocol::Serialize(Message, Bytes)
		&& Server->EnqueueOutgoing(ClientId, MoveTemp(Bytes));
}

bool UVirtualDeviceNetworkComponent::StartListening(
	const int32 InListenPort, const bool bInAllowLanConnections)
{
	if (InListenPort < 1 || InListenPort > MAX_uint16)
	{
		return false;
	}

	if (Server.IsValid() && Server->IsListening()
		&& ListenPort == InListenPort
		&& bAllowLanConnections == bInAllowLanConnections)
	{
		return true;
	}

	Server.Reset();
	ReceiveFrameBuffers.Reset();
	ConnectedClients.Reset();
	ListenPort = InListenPort;
	bAllowLanConnections = bInAllowLanConnections;
	Server = MakeShared<FVirtualDeviceTcpServer>(
		static_cast<uint16>(ListenPort), bAllowLanConnections);
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

TArray<uint64> UVirtualDeviceNetworkComponent::ConnectedClientIds() const
{
	return ConnectedClients.Array();
}

FOnVirtualDeviceMessageReceived& UVirtualDeviceNetworkComponent::OnMessageReceived()
{
	return MessageReceivedDelegate;
}

FOnVirtualDeviceClientDisconnected& UVirtualDeviceNetworkComponent::OnClientDisconnected()
{
	return ClientDisconnectedDelegate;
}
