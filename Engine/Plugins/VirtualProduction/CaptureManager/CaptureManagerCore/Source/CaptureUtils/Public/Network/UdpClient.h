// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Network/Error.h"

#include "Runtime/Sockets/Public/IPAddress.h"

#include "Common/UdpSocketReceiver.h"
#include "Common/UdpSocketSender.h"
#include "Common/UdpSocketBuilder.h"

#include "EngineLogs.h"
#include "Engine/EngineTypes.h"
#include "Engine/EngineBaseTypes.h"

#include "Containers/UnrealString.h"

#include "Templates/UniquePtr.h"

namespace UE::CaptureManager
{

struct FUdpClientConfigure
{
	uint16 ListenPort = 0;
	FString MulticastIpAddress = "";
};

#define CHECK_BOOL(Function) if (bool Result = Function; !Result) { return FCaptureProtocolError("Failed to execute function"); }

class CAPTUREUTILS_API FUdpClient final
{
public:

	FUdpClient();
	~FUdpClient();

	TProtocolResult<void> Init(FUdpClientConfigure InConfig, FOnSocketDataReceived InReceiveHandler);
	TProtocolResult<void> Start();
	TProtocolResult<void> Stop();

	TProtocolResult<int32> SendMessage(const TArray<uint8>& InPayload, const FString& InEndpoint);

private:

	static constexpr uint32 ThreadWaitTime = 500; // Milliseconds
	static constexpr uint32 BufferSize = 2 * 1024 * 1024;

	TUniquePtr<FSocket> UdpSocket;

	TUniquePtr<FUdpSocketReceiver> UdpReceiver;

	bool bRunning;
};

}