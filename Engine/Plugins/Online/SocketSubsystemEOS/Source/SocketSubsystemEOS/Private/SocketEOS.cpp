// Copyright Epic Games, Inc. All Rights Reserved.

#include "SocketEOS.h"
#include "SocketSubsystemEOS.h"
#include "EOSShared.h"
#include "Misc/ConfigCacheIni.h"

#if WITH_EOS_SDK
	#include "eos_p2p.h"
#endif

FSocketEOS::FSocketEOS(FSocketSubsystemEOS& InSocketSubsystem, const FString& InSocketDescription)
	: FSocket(ESocketType::SOCKTYPE_Datagram, InSocketDescription, NAME_None)
	, SocketSubsystem(InSocketSubsystem)
	, bIsListening(false)
#if WITH_EOS_SDK
	, ConnectNotifyCallback(nullptr)
	, ConnectNotifyId(EOS_INVALID_NOTIFICATIONID)
	, ClosedNotifyCallback(nullptr)
	, ClosedNotifyId(EOS_INVALID_NOTIFICATIONID)
#endif
{
	CallbackAliveTracker = MakeShared<FCallbackBase>();

#if WITH_EOS_SDK
	FString PacketReliabilityTypeStr;
	if (GConfig->GetString(TEXT("SocketSubsystemEOS"), TEXT("PacketReliabilityType"), PacketReliabilityTypeStr, GEngineIni))
	{
		EOS_EPacketReliability PacketReliabilityType;
		if (LexFromString(PacketReliabilityType, *PacketReliabilityTypeStr))
		{
			PacketReliability = PacketReliabilityType;
		}
	}
	IEOSSDKManager::Get()->OnNetworkStatusChanged.AddRaw(this, &FSocketEOS::OnNetworkStatusChanged);
#endif
}

FSocketEOS::~FSocketEOS()
{
	Close();

	if (LocalAddress.IsValid())
	{
		SocketSubsystem.UnbindChannel(LocalAddress);
		LocalAddress = FInternetAddrEOS();
	}
	CallbackAliveTracker = nullptr;
}

bool FSocketEOS::Shutdown(ESocketShutdownMode Mode)
{
	SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EOPNOTSUPP);
	return false;
}

#if WITH_EOS_SDK
void FSocketEOS::OnNetworkStatusChanged(EOS_ENetworkStatus OldNetworkStatus, EOS_ENetworkStatus NewNetworkStatus)
{
	// Only needed if we are listening on a socket, otherwise nothing needs to be done
	if (bIsListening)
	{
		// Network status changes from an offline state to online - need to rebind P2P connection request notification 
		if (OldNetworkStatus != EOS_ENetworkStatus::EOS_NS_Online && NewNetworkStatus == EOS_ENetworkStatus::EOS_NS_Online)
		{
			Listen(0);
		}
		// Network status changes from online to another state - clean up what was set on Listen()
		else if (OldNetworkStatus == EOS_ENetworkStatus::EOS_NS_Online && NewNetworkStatus != EOS_ENetworkStatus::EOS_NS_Online)
		{
			// This is a special case where the P2P interface in the EOS SDK unbinds notifiers for us. We need to cleanup bound functions and notification Ids
			ConnectNotifyId = EOS_INVALID_NOTIFICATIONID;
			ClosedNotifyId = EOS_INVALID_NOTIFICATIONID;
			ConnectNotifyCallback = nullptr;
			ClosedNotifyCallback = nullptr;
		}
	}	
}
#endif

bool FSocketEOS::Close()
{
	check(IsInGameThread() && "p2p does not support multithreading");

#if WITH_EOS_SDK
	if (ConnectNotifyId != EOS_INVALID_NOTIFICATIONID)
	{
		EOS_P2P_RemoveNotifyPeerConnectionRequest(SocketSubsystem.GetP2PHandle(), ConnectNotifyId);
	}
	delete ConnectNotifyCallback;
	ConnectNotifyCallback = nullptr;
	if (ClosedNotifyId != EOS_INVALID_NOTIFICATIONID)
	{
		EOS_P2P_RemoveNotifyPeerConnectionClosed(SocketSubsystem.GetP2PHandle(), ClosedNotifyId);
	}
	delete ClosedNotifyCallback;
	ClosedNotifyCallback = nullptr;

	if (LocalAddress.IsValid())
	{
		EOS_P2P_SocketId SocketId = { };
		SocketId.ApiVersion = 1;
		UE_EOS_CHECK_API_MISMATCH(EOS_P2P_SOCKETID_API_LATEST, 1);
		FCStringAnsi::Strcpy(SocketId.SocketName, LocalAddress.GetSocketName());

		EOS_P2P_CloseConnectionsOptions Options = { };
		Options.ApiVersion = 1;
		UE_EOS_CHECK_API_MISMATCH(EOS_P2P_CLOSECONNECTIONS_API_LATEST, 1);
		Options.LocalUserId = SocketSubsystem.GetLocalUserId();
		Options.SocketId = &SocketId;

		UE_LOG(LogSocketSubsystemEOS, VeryVerbose, TEXT("Calling EOS_P2P_CloseConnections SocketId=[%hs]"), SocketId.SocketName);
		EOS_EResult CloseResult = EOS_P2P_CloseConnections(SocketSubsystem.GetP2PHandle(), &Options);
		UE_CLOG(CloseResult == EOS_EResult::EOS_Success, LogSocketSubsystemEOS, Log, TEXT("EOS_P2P_CloseConnections SocketId=[%hs] Result=[%s]"), SocketId.SocketName, *LexToString(CloseResult));
		UE_CLOG(CloseResult != EOS_EResult::EOS_Success, LogSocketSubsystemEOS, Error, TEXT("EOS_P2P_CloseConnections SocketId=[%hs] Result=[%s]"), SocketId.SocketName, *LexToString(CloseResult));

		ClosedRemotes.Empty();
	}
#endif
	return true;
}

bool FSocketEOS::Bind(const FInternetAddr& Addr)
{
	check(IsInGameThread() && "p2p does not support multithreading");

	if (!Addr.IsValid())
	{
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Attempted to bind to invalid Address=[%s]"), *Addr.ToString(true));
		SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EADDRNOTAVAIL);
		return false;
	}

	// If we have a remote user id, we're already bound
	if (LocalAddress.GetRemoteUserId() != nullptr)
	{
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Attempted to bind a socket that was already bound ExistingAddress=[%s] NewAddress=[%s]"), *LocalAddress.ToString(true), *Addr.ToString(true));
		SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EADDRINUSE);
		return false;
	}

	const FInternetAddrEOS& EOSAddr = static_cast<const FInternetAddrEOS&>(Addr);
	if (!SocketSubsystem.BindChannel(EOSAddr))
	{
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Attempted to bind a socket to a port in use NewAddress=[%s]"), *Addr.ToString(true));
		// BindPort sets our LastSocketError
		return false;
	}

#if WITH_EOS_SDK
	EOS_ProductUserId LocalUserId = LocalAddress.GetLocalUserId();
#else
	void* LocalUserId = LocalAddress.GetLocalUserId();
#endif
	LocalAddress = EOSAddr;
	LocalAddress.SetLocalUserId(LocalUserId);

	UE_LOG(LogSocketSubsystemEOS, Verbose, TEXT("Successfully bound socket to Address=[%s]"), *LocalAddress.ToString(true));
	return true;
}

bool FSocketEOS::Connect(const FInternetAddr& Addr)
{
	/** Not supported - connectionless (UDP) only */
	SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EOPNOTSUPP);
	return false;
}

bool FSocketEOS::Listen(int32)
{
	check(IsInGameThread() && "p2p does not support multithreading");

	if (!LocalAddress.IsValid())
	{
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Attempted to listen without a bound Address=[%s]"), *LocalAddress.ToString(true));
		SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EADDRINUSE);
		return false;
	}

#if WITH_EOS_SDK
	// Add listener for inbound connections
	EOS_P2P_SocketId SocketId = { };
	SocketId.ApiVersion = 1;
	UE_EOS_CHECK_API_MISMATCH(EOS_P2P_SOCKETID_API_LATEST, 1);
	FCStringAnsi::Strcpy(SocketId.SocketName, LocalAddress.GetSocketName());

	EOS_P2P_AddNotifyPeerConnectionRequestOptions Options = { };
	Options.ApiVersion = 1;
	UE_EOS_CHECK_API_MISMATCH(EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST, 1);
	Options.LocalUserId = LocalAddress.GetLocalUserId();
	Options.SocketId = &SocketId;

	ConnectNotifyCallback = new FConnectNotifyCallback(CallbackAliveTracker);
	ConnectNotifyCallback->CallbackLambda = [this](const EOS_P2P_OnIncomingConnectionRequestInfo* Info)
	{
		char PuidBuffer[64];
		int32 BufferLen = 64;
		if (EOS_ProductUserId_ToString(Info->RemoteUserId, PuidBuffer, &BufferLen) != EOS_EResult::EOS_Success)
		{
			PuidBuffer[0] = '\0';
		}
		FString RemoteUser(PuidBuffer);

		if (Info->LocalUserId == LocalAddress.GetLocalUserId() && FCStringAnsi::Stricmp(Info->SocketId->SocketName, LocalAddress.GetSocketName()) == 0)
		{
			// In case they disconnected and then reconnected, remove them from our closed list
			FInternetAddrEOS RemoteAddress(Info->RemoteUserId, Info->SocketId->SocketName, LocalAddress.GetChannel());
			RemoteAddress.SetLocalUserId(LocalAddress.GetLocalUserId());
			ClosedRemotes.Remove(RemoteAddress);

			EOS_P2P_SocketId SocketId = { };
			SocketId.ApiVersion = 1;
			UE_EOS_CHECK_API_MISMATCH(EOS_P2P_SOCKETID_API_LATEST, 1);
			FCStringAnsi::Strcpy(SocketId.SocketName, Info->SocketId->SocketName);

			EOS_P2P_AcceptConnectionOptions Options = { };
			Options.ApiVersion = 1;
			UE_EOS_CHECK_API_MISMATCH(EOS_P2P_ACCEPTCONNECTION_API_LATEST, 1);
			Options.LocalUserId = LocalAddress.GetLocalUserId();
			Options.RemoteUserId = Info->RemoteUserId;
			Options.SocketId = &SocketId;
			UE_LOG(LogSocketSubsystemEOS, VeryVerbose, TEXT("Calling EOS_P2P_AcceptConnection SocketId=[%hs]"), SocketId.SocketName);
			EOS_EResult AcceptResult = EOS_P2P_AcceptConnection(SocketSubsystem.GetP2PHandle(), &Options);
			UE_CLOG(AcceptResult == EOS_EResult::EOS_Success, LogSocketSubsystemEOS, Log, TEXT("EOS_P2P_AcceptConnection RemoteUser=[%s] SocketId=[%hs] Result=[%s]"), *RemoteUser, SocketId.SocketName, *LexToString(AcceptResult));
			UE_CLOG(AcceptResult != EOS_EResult::EOS_Success, LogSocketSubsystemEOS, Error, TEXT("EOS_P2P_AcceptConnection RemoteUser=[%s] SocketId=[%hs] Result=[%s]"), *RemoteUser, SocketId.SocketName, *LexToString(AcceptResult));
		}
		else
		{
			UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Ignoring connection request from RemoteUser=[%s] SocketId=[%hs]"), *RemoteUser, Info->SocketId->SocketName);
		}
	};
	ConnectNotifyId = EOS_P2P_AddNotifyPeerConnectionRequest(SocketSubsystem.GetP2PHandle(), &Options, ConnectNotifyCallback, ConnectNotifyCallback->GetCallbackPtr());

	// Need to handle closures too
	RegisterClosedNotification();
#endif

	bIsListening = true;

	return true;
}

bool FSocketEOS::WaitForPendingConnection(bool& bHasPendingConnection, const FTimespan& WaitTime)
{
	/** Not supported - connectionless (UDP) only */
	SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EOPNOTSUPP);
	return false;
}

bool FSocketEOS::HasPendingData(uint32& PendingDataSize)
{
	check(IsInGameThread() && "p2p does not support multithreading");

	PendingDataSize = 0;

#if WITH_EOS_SDK
	EOS_P2P_GetNextReceivedPacketSizeOptions Options = { };
	Options.ApiVersion = 2;
	UE_EOS_CHECK_API_MISMATCH(EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST, 2);
	Options.LocalUserId = LocalAddress.GetLocalUserId();
	uint8 Channel = LocalAddress.GetChannel();
	Options.RequestedChannel = &Channel;

	EOS_EResult Result = EOS_P2P_GetNextReceivedPacketSize(SocketSubsystem.GetP2PHandle(), &Options, &PendingDataSize);
	if (Result == EOS_EResult::EOS_NotFound)
	{
		return false;
	}
	if (Result != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Unable to check for data Address=[%s] Result=[%s]"), *LocalAddress.ToString(true), *LexToString(Result));

		// @todo joeg - map EOS codes to UE4's
		SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EINVAL);
		return false;
	}

	return true;
#else
	return false;
#endif
}

FSocket* FSocketEOS::Accept(const FString& InSocketDescription)
{
	/** Not supported - connectionless (UDP) only */
	SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EOPNOTSUPP);
	return nullptr;
}

FSocket* FSocketEOS::Accept(FInternetAddr& OutAddr, const FString& InSocketDescription)
{
	/** Not supported - connectionless (UDP) only */
	SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EOPNOTSUPP);
	return nullptr;
}

bool FSocketEOS::SendTo(const uint8* Data, int32 Count, int32& OutBytesSent, const FInternetAddr& Destination)
{
	check(IsInGameThread() && "p2p does not support multithreading");

	OutBytesSent = 0;

	if (!Destination.IsValid())
	{
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Unable to send data, invalid DestinationAddress=[%s]"), *Destination.ToString(true));

		SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EADDRNOTAVAIL);
		return false;
	}

#if WITH_EOS_SDK
	if (Count > EOS_P2P_MAX_PACKET_SIZE)
	{
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Unable to send data, data over maximum size. Amount=[%d/%d] DestinationAddress=[%s]"), Count, EOS_P2P_MAX_PACKET_SIZE, *Destination.ToString(true));

		SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EMSGSIZE);
		return false;
	}

	if (Count < 0)
	{
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Unable to send data, data invalid. Amount=[%d/%d] DestinationAddress=[%s]"), Count, EOS_P2P_MAX_PACKET_SIZE, *Destination.ToString(true));

		SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EINVAL);
		return false;
	}
#endif 

	if (Data == nullptr && Count != 0)
	{
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Unable to send data, data invalid. DestinationAddress=[%s]"), *Destination.ToString(true));

		SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EINVAL);
		return false;
	}

	if (!LocalAddress.IsValid())
	{
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Unable to send data, socket was not initialized. DestinationAddress=[%s]"), *Destination.ToString(true));

		SocketSubsystem.SetLastSocketError(ESocketErrors::SE_NOTINITIALISED);
		return false;
	}

	const FInternetAddrEOS& DestinationAddress = static_cast<const FInternetAddrEOS&>(Destination);
	if (LocalAddress == DestinationAddress)
	{
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Unable to send data, unable to send data to ourselves. DestinationAddress=[%s]"), *Destination.ToString(true));

		SocketSubsystem.SetLastSocketError(ESocketErrors::SE_ECONNREFUSED);
		return false;
	}

	// Check for sending to an address we explicitly closed
	if (WasClosed(DestinationAddress))
	{
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Unable to send data to closed connection. DestinationAddress=[%s]"), *Destination.ToString(true));

		SocketSubsystem.SetLastSocketError(ESocketErrors::SE_ECONNREFUSED);
		return false;
	}

#if WITH_EOS_SDK
	// Need to handle closures if we are a client and the server closes down on us
	RegisterClosedNotification();

	EOS_P2P_SocketId SocketId = { };
	SocketId.ApiVersion = 1;
	UE_EOS_CHECK_API_MISMATCH(EOS_P2P_SOCKETID_API_LATEST, 1);
	FCStringAnsi::Strcpy(SocketId.SocketName, DestinationAddress.GetSocketName());

	EOS_P2P_SendPacketOptions Options = { };
	Options.ApiVersion = 3;
	UE_EOS_CHECK_API_MISMATCH(EOS_P2P_SENDPACKET_API_LATEST, 3);
	Options.LocalUserId = LocalAddress.GetLocalUserId();
	Options.RemoteUserId = DestinationAddress.GetRemoteUserId();
	Options.SocketId = &SocketId;
	Options.bAllowDelayedDelivery = EOS_TRUE;
	Options.Reliability = PacketReliability;
	Options.Channel = DestinationAddress.GetChannel();
	Options.DataLengthBytes = Count;
	Options.Data = Data;

	UE_LOG(LogSocketSubsystemEOS, VeryVerbose, TEXT("Calling EOS_P2P_SendPacket DestinationAddress=[%s] SocketId=[%hs]"), *DestinationAddress.ToString(true), SocketId.SocketName);
	EOS_EResult SendResult = EOS_P2P_SendPacket(SocketSubsystem.GetP2PHandle(), &Options);
	if (SendResult != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogSocketSubsystemEOS, Error, TEXT("EOS_P2P_SendPacket DestinationAddress=[%s] SocketId=[%hs] Result=[%s]"), *DestinationAddress.ToString(true), SocketId.SocketName, *LexToString(SendResult));

		// @todo joeg - map EOS codes to UE4's
		SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EINVAL);
		return false;
	}
	UE_LOG(LogSocketSubsystemEOS, VeryVerbose, TEXT("EOS_P2P_SendPacket DestinationAddress=[%s] SocketId=[%hs] Result=[%s]"), *DestinationAddress.ToString(true), SocketId.SocketName,*LexToString(SendResult));
	OutBytesSent = Count;
	return true;
#else
	return false;
#endif
}

bool FSocketEOS::Send(const uint8* Data, int32 Count, int32& BytesSent)
{
	/** Not supported - connectionless (UDP) only */
	SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EOPNOTSUPP);
	BytesSent = 0;
	return false;
}

bool FSocketEOS::RecvFrom(uint8* Data, int32 BufferSize, int32& BytesRead, FInternetAddr& Source, ESocketReceiveFlags::Type Flags)
{
	check(IsInGameThread() && "p2p does not support multithreading");
	BytesRead = 0;

	if (BufferSize < 0)
	{
		UE_LOG(LogSocketSubsystemEOS, Error, TEXT("Unable to receive data, receiving buffer was invalid. BufferSize=[%d]"), BufferSize);

		SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EINVAL);
		return false;
	}

	if (Flags != ESocketReceiveFlags::None)
	{
		// We do not support peaking / blocking until a packet comes
		UE_LOG(LogSocketSubsystemEOS, Error, TEXT("Socket receive Flags=[%d] are not supported"), int32(Flags));

		SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EOPNOTSUPP);
		return false;
	}

#if WITH_EOS_SDK
	EOS_P2P_ReceivePacketOptions Options = { };
	Options.ApiVersion = 2;
	UE_EOS_CHECK_API_MISMATCH(EOS_P2P_RECEIVEPACKET_API_LATEST, 2);
	Options.LocalUserId = LocalAddress.GetLocalUserId();
	Options.MaxDataSizeBytes = BufferSize;
	uint8 Channel = LocalAddress.GetChannel();
	Options.RequestedChannel = &Channel;

	EOS_ProductUserId RemoteUserId = nullptr;
	EOS_P2P_SocketId SocketId;
	
	UE_LOG(LogSocketSubsystemEOS, VeryVerbose, TEXT("Calling EOS_P2P_ReceivePacket RequestedChannel=[%d]"), Options.RequestedChannel);
	EOS_EResult ReceiveResult = EOS_P2P_ReceivePacket(SocketSubsystem.GetP2PHandle(), &Options, &RemoteUserId, &SocketId, &Channel, Data, (uint32*)&BytesRead);
	if (ReceiveResult == EOS_EResult::EOS_NotFound)
	{
		// No data to read
		SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EWOULDBLOCK);
		return false;
	}
	else if (ReceiveResult != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogSocketSubsystemEOS, Error, TEXT("EOS_P2P_ReceivePacket RequestedChannel=[%d] Result=[%s]"), Options.RequestedChannel, *LexToString(ReceiveResult));

		// @todo joeg - map EOS codes to UE4's
		SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EINVAL);
		return false;
	}

	FInternetAddrEOS& SourceAddress = static_cast<FInternetAddrEOS&>(Source);
	SourceAddress.SetLocalUserId(LocalAddress.GetLocalUserId());
	SourceAddress.SetRemoteUserId(RemoteUserId);
	SourceAddress.SetSocketName(SocketId.SocketName);
	SourceAddress.SetChannel(Channel);

	UE_LOG(LogSocketSubsystemEOS, VeryVerbose, TEXT("EOS_P2P_ReceivePacket RemoteAddress=[%s] SocketId=[%hs] Result=[%s]"), *SourceAddress.ToString(true), SocketId.SocketName, *LexToString(ReceiveResult));

	return true;
#else
	return false;
#endif
}

bool FSocketEOS::Recv(uint8* Data, int32 BufferSize, int32& BytesRead, ESocketReceiveFlags::Type Flags)
{
	BytesRead = 0;
	SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EOPNOTSUPP);
	return false;
}

bool FSocketEOS::Wait(ESocketWaitConditions::Type Condition, FTimespan WaitTime)
{
	SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EOPNOTSUPP);
	return false;
}

ESocketConnectionState FSocketEOS::GetConnectionState()
{
	return ESocketConnectionState::SCS_NotConnected;
}

void FSocketEOS::GetAddress(FInternetAddr& OutAddr)
{
	OutAddr = LocalAddress;
}

bool FSocketEOS::GetPeerAddress(FInternetAddr& OutAddr)
{
	SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EOPNOTSUPP);
	return false;
}

bool FSocketEOS::SetNonBlocking(bool bIsNonBlocking)
{
	return true;
}

bool FSocketEOS::SetBroadcast(bool bAllowBroadcast)
{
	return true;
}

bool FSocketEOS::SetNoDelay(bool bIsNoDelay)
{
	return true;
}

bool FSocketEOS::JoinMulticastGroup(const FInternetAddr& GroupAddress)
{
	SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EOPNOTSUPP);
	return false;
}

bool FSocketEOS::LeaveMulticastGroup(const FInternetAddr& GroupAddress)
{
	SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EOPNOTSUPP);
	return false;
}

bool FSocketEOS::SetMulticastLoopback(bool bLoopback)
{
	SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EOPNOTSUPP);
	return false;
}

bool FSocketEOS::SetMulticastTtl(uint8 TimeToLive)
{
	SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EOPNOTSUPP);
	return false;
}

bool FSocketEOS::JoinMulticastGroup(const FInternetAddr& GroupAddress, const FInternetAddr& InterfaceAddress)
{
	SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EOPNOTSUPP);
	return false;
}

bool FSocketEOS::LeaveMulticastGroup(const FInternetAddr& GroupAddress, const FInternetAddr& InterfaceAddress)
{
	SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EOPNOTSUPP);
	return false;
}

bool FSocketEOS::SetMulticastInterface(const FInternetAddr& InterfaceAddress)
{
	SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EOPNOTSUPP);
	return false;
}

bool FSocketEOS::SetReuseAddr(bool bAllowReuse)
{
	return true;
}

bool FSocketEOS::SetLinger(bool bShouldLinger, int32 Timeout)
{
	return true;
}

bool FSocketEOS::SetRecvErr(bool bUseErrorQueue)
{
	return true;
}

bool FSocketEOS::SetSendBufferSize(int32 Size, int32& NewSize)
{
	return true;
}

bool FSocketEOS::SetReceiveBufferSize(int32 Size, int32& NewSize)
{
	return true;
}

int32 FSocketEOS::GetPortNo()
{
	return LocalAddress.GetChannel();
}

void FSocketEOS::SetLocalAddress(const FInternetAddrEOS& InLocalAddress)
{
	LocalAddress = InLocalAddress;
}

bool FSocketEOS::Close(const FInternetAddrEOS& RemoteAddress)
{
	check(IsInGameThread() && "p2p does not support multithreading");

	if (!RemoteAddress.IsValid())
	{
		UE_LOG(LogSocketSubsystemEOS, Error, TEXT("Unable to close socket with RemoteAddress=[%s] as it is invalid"), *RemoteAddress.ToString(true));
		SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EADDRNOTAVAIL);
		return false;
	}

#if WITH_EOS_SDK
	// So we don't reopen a connection by sending to it
	ClosedRemotes.Add(RemoteAddress);

	EOS_P2P_SocketId SocketId = { };
	SocketId.ApiVersion = 1;
	UE_EOS_CHECK_API_MISMATCH(EOS_P2P_SOCKETID_API_LATEST, 1);
	FCStringAnsi::Strcpy(SocketId.SocketName, RemoteAddress.GetSocketName());

	EOS_P2P_CloseConnectionOptions Options = { };
	Options.ApiVersion = 1;
	UE_EOS_CHECK_API_MISMATCH(EOS_P2P_CLOSECONNECTION_API_LATEST, 1);
	Options.LocalUserId = LocalAddress.GetLocalUserId();
	Options.RemoteUserId = RemoteAddress.GetRemoteUserId();
	Options.SocketId = &SocketId;

	UE_LOG(LogSocketSubsystemEOS, VeryVerbose, TEXT("Calling EOS_P2P_CloseConnection RemoteAddress=[%s] SocketId=[%hs])"), *RemoteAddress.ToString(true), SocketId.SocketName);
	EOS_EResult CloseResult = EOS_P2P_CloseConnection(SocketSubsystem.GetP2PHandle(), &Options);
	if (CloseResult != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogSocketSubsystemEOS, Error, TEXT("EOS_P2P_CloseConnection RemoteAddress=[%s] SocketId=[%hs] Result=[%s]"), *RemoteAddress.ToString(true), SocketId.SocketName, *LexToString(CloseResult));

		// @todo joeg - map EOS codes to UE4's
		SocketSubsystem.SetLastSocketError(ESocketErrors::SE_EINVAL);
		return false;
	}

	UE_LOG(LogSocketSubsystemEOS, Log, TEXT("EOS_P2P_CloseConnection RemoteAddress=[%s] SocketId=[%hs] Result=[%s]"), *RemoteAddress.ToString(true), SocketId.SocketName, *LexToString(CloseResult));
	return true;
#else
	return false;
#endif
}

void FSocketEOS::RegisterClosedNotification()
{
#if WITH_EOS_SDK
	if (ClosedNotifyId != EOS_INVALID_NOTIFICATIONID)
	{
		// Already listening for these events so ignore
		return;
	}
	
	EOS_P2P_SocketId SocketId = { };
	SocketId.ApiVersion = 1;
	UE_EOS_CHECK_API_MISMATCH(EOS_P2P_SOCKETID_API_LATEST, 1);
	FCStringAnsi::Strcpy(SocketId.SocketName, LocalAddress.GetSocketName());

	EOS_P2P_AddNotifyPeerConnectionClosedOptions Options = { };
	Options.ApiVersion = 1;
	UE_EOS_CHECK_API_MISMATCH(EOS_P2P_ADDNOTIFYPEERCONNECTIONCLOSED_API_LATEST, 1);
	Options.LocalUserId = LocalAddress.GetLocalUserId();
	Options.SocketId = &SocketId;

	ClosedNotifyCallback = new FClosedNotifyCallback(CallbackAliveTracker);
	ClosedNotifyCallback->CallbackLambda = [this](const EOS_P2P_OnRemoteConnectionClosedInfo* Info)
	{
		// Add this connection to the list of closed ones
		FInternetAddrEOS RemoteAddress(Info->RemoteUserId, Info->SocketId->SocketName, LocalAddress.GetChannel());
		RemoteAddress.SetLocalUserId(LocalAddress.GetLocalUserId());
		ClosedRemotes.Add(RemoteAddress);
	};
	ClosedNotifyId = EOS_P2P_AddNotifyPeerConnectionClosed(SocketSubsystem.GetP2PHandle(), &Options, ClosedNotifyCallback, ClosedNotifyCallback->GetCallbackPtr());
#endif
}
