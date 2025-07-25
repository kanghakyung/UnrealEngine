// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterGameEngine.h"

#include "Algo/Accumulate.h"
#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Cluster/Controller/IDisplayClusterClusterNodeController.h"
#include "Cluster/NetAPI/DisplayClusterNetApiFacade.h"
#include "Config/IPDisplayClusterConfigManager.h"
#include "DisplayClusterEnums.h"
#include "Engine/DynamicBlueprintBinding.h"

#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfigurationVersion.h"
#include "IDisplayClusterConfiguration.h"

#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DisplayClusterAppExit.h"
#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"
#include "Misc/Parse.h"

#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"

#include "EngineGlobals.h"
#include "Engine/NetDriver.h"
#include "Engine/GameInstance.h"

#include "UObject/Linker.h"
#include "GameMapsSettings.h"
#include "GameFramework/GameModeBase.h"

#include "Stats/Stats.h"

#include "GameDelegates.h"
#include "Kismet/GameplayStatics.h"


namespace DisplayClusterGameEngineUtils
{
	static const FString WaitForGameCategory = DisplayClusterResetSyncType;
	static const FString WaitForGameName     = TEXT("WaitForGameStart");
}

// Advanced cluster synchronization during LoadMap
static TAutoConsoleVariable<int32> CVarGameStartBarrierAvoidance(
	TEXT("nDisplay.game.GameStartBarrierAvoidance"),
	1,
	TEXT("Avoid entering GameStartBarrier on loading level\n")
	TEXT("0 : disabled\n")
	TEXT("1 : enabled\n")
);

void UDisplayClusterGameEngine::Init(class IEngineLoop* InEngineLoop)
{
	UE_LOG(LogDisplayClusterEngine, Log, TEXT("UDisplayClusterGameEngine::Init"));

	// Detect requested operation mode
	OperationMode = DetectOperationMode();

	// Initialize Display Cluster
	if (!GDisplayCluster->Init(OperationMode))
	{
		FDisplayClusterAppExit::ExitApplication(FString("Couldn't initialize DisplayCluster module"), FDisplayClusterAppExit::EExitType::KillImmediately);
	}

	// This is required to prevent GameThread dead-locking when Alt+F4 is used
	// to terminate p-node of a 2+ nodes cluster. This gets called before
	// virtual PreExit which allows to unlock GameThread in advance.
	FCoreDelegates::GetApplicationWillTerminateDelegate().AddLambda([this]()
	{
		PreExitImpl();
	});

	if (OperationMode == EDisplayClusterOperationMode::Cluster)
	{
		// Our parsing function for arguments like:
		// -ArgName1="ArgValue 1" -ArgName2=ArgValue2 ArgName3=ArgValue3
		//
		auto ParseCommandArg = [](const FString& CommandLine, const FString& ArgName, FString& OutArgVal)
		{
			const FString Tag = FString::Printf(TEXT("-%s="), *ArgName);
			const int32 TagPos = CommandLine.Find(Tag);

			if (TagPos == INDEX_NONE)
			{
				// Try old method, where the '-' prefix is missing and quoted values with spaces are not supported.
				return FParse::Value(*CommandLine, *ArgName, OutArgVal);
			}

			const TCHAR* TagValue = &CommandLine[TagPos + Tag.Len()];
			return FParse::Token(TagValue, OutArgVal, false);
		};

		// Extract config path from command line
		FString ConfigPath;
		if (!ParseCommandArg(FCommandLine::Get(), DisplayClusterStrings::args::Config, ConfigPath))
		{
			FDisplayClusterAppExit::ExitApplication(FString("No config file specified. Cluster operation mode requires config file."), FDisplayClusterAppExit::EExitType::KillImmediately);
		}

		// Clean the file path before using it
		DisplayClusterHelpers::str::TrimStringValue(ConfigPath);

		// Validate the config file first. Since 4.27, we don't allow the old formats to be used
		const EDisplayClusterConfigurationVersion ConfigVersion = IDisplayClusterConfiguration::Get().GetConfigVersion(ConfigPath);
		if (!ValidateConfigFile(ConfigPath))
		{
			FDisplayClusterAppExit::ExitApplication(FString("An invalid or outdated configuration file was specified. Please consider using nDisplay configurator to update the config files."), FDisplayClusterAppExit::EExitType::KillImmediately);
		}

		// Load config data
		UDisplayClusterConfigurationData* ConfigData = IDisplayClusterConfiguration::Get().LoadConfig(ConfigPath);
		if (!ConfigData)
		{
			FDisplayClusterAppExit::ExitApplication(FString("An error occurred during loading the configuration file"), FDisplayClusterAppExit::EExitType::KillImmediately);
		}

		// Extract node ID from command line
		FString NodeId;
		if (!ParseCommandArg(FCommandLine::Get(), DisplayClusterStrings::args::Node, NodeId))
		{
			UE_LOG(LogDisplayClusterEngine, Log, TEXT("Node ID is not specified. Trying to resolve from host address..."));

			// Find node ID based on the host address
			if (!GetResolvedNodeId(ConfigData, NodeId))
			{
				FDisplayClusterAppExit::ExitApplication(FString("Couldn't resolve node ID. Try to specify host addresses explicitly."), FDisplayClusterAppExit::EExitType::KillImmediately);
			}

			UE_LOG(LogDisplayClusterEngine, Log, TEXT("Node ID has been successfully resolved: %s"), *NodeId);
		}

		// Clean node ID string
		DisplayClusterHelpers::str::TrimStringValue(NodeId);

		// Start game session
		if (!GDisplayCluster->StartSession(ConfigData, NodeId))
		{
			FDisplayClusterAppExit::ExitApplication(FString("Couldn't start DisplayCluster session"), FDisplayClusterAppExit::EExitType::KillImmediately);
		}

		// Initialize internals
		InitializeInternals();
	}

	// Initialize base stuff.
	UGameEngine::Init(InEngineLoop);

	OnOverrideBrowseURL.BindUObject(this, &UDisplayClusterGameEngine::BrowseLoadMap);
	OnOverridePendingNetGameUpdate.BindUObject(this, &UDisplayClusterGameEngine::PendingLevelUpdate);
}

EDisplayClusterOperationMode UDisplayClusterGameEngine::DetectOperationMode() const
{
	EDisplayClusterOperationMode OpMode = EDisplayClusterOperationMode::Disabled;
	if (FParse::Param(FCommandLine::Get(), DisplayClusterStrings::args::Cluster))
	{
		OpMode = EDisplayClusterOperationMode::Cluster;
	}

	UE_LOG(LogDisplayClusterEngine, Log, TEXT("Detected operation mode: %s"), *DisplayClusterTypesConverter::template ToString(OpMode));

	return OpMode;
}

bool UDisplayClusterGameEngine::InitializeInternals()
{
	// This function is called after a session had been started so it's safe to get config data from the config manager
	const UDisplayClusterConfigurationData* Config = GDisplayCluster->GetPrivateConfigMgr()->GetConfig();
	check(Config);

	// Store diagnostics settings locally
	Diagnostics = Config->Diagnostics;

	FOnClusterEventJsonListener GameSyncTransition = FOnClusterEventJsonListener::CreateUObject(this, &UDisplayClusterGameEngine::GameSyncChange);
	GDisplayCluster->GetPrivateClusterMgr()->AddClusterEventJsonListener(GameSyncTransition);

	const UDisplayClusterConfigurationClusterNode* CfgLocalNode = GDisplayCluster->GetPrivateConfigMgr()->GetLocalNode();
	const bool bSoundEnabled = (CfgLocalNode ? CfgLocalNode->bIsSoundEnabled : false);
	UE_LOG(LogDisplayClusterEngine, Log, TEXT("Configuring sound enabled: %s"), *DisplayClusterTypesConverter::template ToString(bSoundEnabled));
	if (!bSoundEnabled)
	{
		AudioDeviceManager = nullptr;
	}

	return true;
}

// This function works if you have 1 cluster node per PC. In case of multiple nodes, all of them will have the same node ID.
bool UDisplayClusterGameEngine::GetResolvedNodeId(const UDisplayClusterConfigurationData* ConfigData, FString& NodeId) const
{
	TArray<TSharedPtr<FInternetAddr>> LocalAddresses;
	if (!ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLocalAdapterAddresses(LocalAddresses))
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("Couldn't get local addresses list. Cannot find node ID by its address."));
		return false;
	}

	if (LocalAddresses.Num() < 1)
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("No local addresses found"));
		return false;
	}

	for (const auto& it : ConfigData->Cluster->Nodes)
	{
		for (const auto& LocalAddress : LocalAddresses)
		{
			const FIPv4Endpoint ep(LocalAddress);
			const FString epaddr = ep.Address.ToString();

			UE_LOG(LogDisplayClusterCluster, Log, TEXT("Comparing addresses: %s - %s"), *epaddr, *it.Value->Host);

			//@note: don't add "127.0.0.1" or "localhost" here. There will be a bug. It has been proved already.
			if (epaddr.Equals(it.Value->Host, ESearchCase::IgnoreCase))
			{
				// Found!
				NodeId = it.Key;
				return true;
			}
		}
	}

	// We haven't found anything
	return false;
}

bool UDisplayClusterGameEngine::ValidateConfigFile(const FString& FilePath)
{
	const EDisplayClusterConfigurationVersion ConfigVersion = IDisplayClusterConfiguration::Get().GetConfigVersion(FilePath);
	switch (ConfigVersion)
	{
		case EDisplayClusterConfigurationVersion::Version_426:
			// Old 4.26 and 4.27p1 formats are not allowed as well
			UE_LOG(LogDisplayClusterEngine, Error, TEXT("Detected old (.ndisplay 4.26 or .ndisplay 4.27p1) config format. Please upgrade to the actual version."));
			return true;

		case EDisplayClusterConfigurationVersion::Version_427:
			// Ok, it's one of the actual config formats
			UE_LOG(LogDisplayClusterEngine, Log, TEXT("Detected (.ndisplay 4.27) config format"));
			return true;

		case EDisplayClusterConfigurationVersion::Version_500:
			// Ok, it's one of the actual config formats
			UE_LOG(LogDisplayClusterEngine, Log, TEXT("Detected (.ndisplay 5.00) config format"));
			return true;

		case EDisplayClusterConfigurationVersion::Unknown:
		default:
			// Something unexpected came here
			UE_LOG(LogDisplayClusterEngine, Error, TEXT("Unknown or unsupported config format"));
			break;
	}

	return false;
}

void UDisplayClusterGameEngine::PreExit()
{
	PreExitImpl();

	// Release the engine
	UGameEngine::PreExit();
}

void UDisplayClusterGameEngine::PreExitImpl()
{
	UE_LOG(LogDisplayClusterEngine, Log, TEXT("UDisplayClusterGameEngine::PreExitImpl"));

	if (OperationMode == EDisplayClusterOperationMode::Cluster)
	{
		// Finalize current world
		GDisplayCluster->EndScene();
		// Close current DisplayCluster session
		GDisplayCluster->EndSession();
	}
}

bool UDisplayClusterGameEngine::LoadMap(FWorldContext& WorldContext, FURL URL, class UPendingNetGame* Pending, FString& Error)
{
	UE_LOG(LogDisplayClusterEngine, Log, TEXT("UDisplayClusterGameEngine::LoadMap, URL=%s"), *URL.ToString());

	if (OperationMode == EDisplayClusterOperationMode::Cluster)
	{
		// Finish previous scene
		GDisplayCluster->EndScene();

		// Perform map loading
		if (!Super::LoadMap(WorldContext, URL, Pending, Error))
		{
			return false;
		}

		// Start new scene
		GDisplayCluster->StartScene(WorldContext.World());
		WorldContextObject = WorldContext.World();

		UGameplayStatics::SetGamePaused(WorldContextObject, BarrierAvoidanceOn());

		if(BarrierAvoidanceOn() && RunningMode != EDisplayClusterRunningMode::Startup)
		{
			IPDisplayClusterClusterManager& ClusterMgr = *GDisplayCluster->GetPrivateClusterMgr();

			FDisplayClusterClusterEventJson WaitForGameEvent;
			WaitForGameEvent.Category = DisplayClusterGameEngineUtils::WaitForGameCategory;
			WaitForGameEvent.Type = URL.ToString();
			WaitForGameEvent.Name = ClusterMgr.GetNodeId();
			WaitForGameEvent.bIsSystemEvent = true;
			WaitForGameEvent.bShouldDiscardOnRepeat = false;

			ClusterMgr.EmitClusterEventJson(WaitForGameEvent, false);

			RunningMode = EDisplayClusterRunningMode::WaitingForSync;
			// Assume that all nodes are now out of sync.
			UE_LOG(LogDisplayClusterEngine, Display, TEXT("LoadMap occurred after startup for Level %s"), *WaitForGameEvent.Type);
		}
		else
		{
			CheckGameStartBarrier();
		}
	}
	else
	{
		return Super::LoadMap(WorldContext, URL, Pending, Error);
	}

	return true;
}

void UDisplayClusterGameEngine::Tick(float DeltaSeconds, bool bIdleMode)
{
	UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("UDisplayClusterGameEngine::Tick, Delta=%f, Idle=%d"), DeltaSeconds, bIdleMode ? 1 : 0);

	if (CanTick())
	{
		IPDisplayClusterClusterManager& ClusterMgr = *GDisplayCluster->GetPrivateClusterMgr();

		//////////////////////////////////////////////////////////////////////////////////////////////
		// Frame start barrier
		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("Sync frame start"));
		ClusterMgr.GetNetApi().GetClusterSyncAPI()->WaitForFrameStart();

		// Perform StartFrame notification
		GDisplayCluster->StartFrame(GFrameCounter);

		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("DisplayCluster delta seconds: %f"), DeltaSeconds);

		// Perform PreTick for DisplayCluster module
		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("Perform PreTick()"));
		GDisplayCluster->PreTick(DeltaSeconds);

		// Perform UGameEngine::Tick() calls for scene actors
		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("Perform UGameEngine::Tick()"));
		Super::Tick(DeltaSeconds, bIdleMode || bForcedTickIdleMode);

		// Perform PostTick for DisplayCluster module
		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("Perform PostTick()"));
		GDisplayCluster->PostTick(DeltaSeconds);

		if (Diagnostics.bSimulateLag)
		{
			const float LagTime = FMath::RandRange(Diagnostics.MinLagTime, Diagnostics.MaxLagTime);
			UE_LOG(LogDisplayClusterEngine, Log, TEXT("Simulating lag: %f seconds"), LagTime);
			FPlatformProcess::Sleep(LagTime);
		}

		//////////////////////////////////////////////////////////////////////////////////////////////
		// Frame end barrier
		ClusterMgr.GetNetApi().GetClusterSyncAPI()->WaitForFrameEnd();

		// Perform EndFrame notification
		GDisplayCluster->EndFrame(GFrameCounter);

		if (bIsRenderingSuspended)
		{
			bIsRenderingSuspended = false;
		}

		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("Sync frame end"));
	}
	else
	{
		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("UDisplayClusterGameEngine::Tick, Tick() is not allowed"));
		Super::Tick(DeltaSeconds, bIdleMode);
	}
}


void UDisplayClusterGameEngine::UpdateTimeAndHandleMaxTickRate()
{
	UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("UDisplayClusterGameEngine::UpdateTimeAndHandleMaxTickRate"));

	UEngine::UpdateTimeAndHandleMaxTickRate();

	if (CanTick())
	{
		// Synchronize time data
		GDisplayCluster->GetPrivateClusterMgr()->SyncTimeData();
	}
}

bool UDisplayClusterGameEngine::CanTick() const
{
	return (RunningMode == EDisplayClusterRunningMode::Synced
			|| RunningMode == EDisplayClusterRunningMode::WaitingForSync)
		&& OperationMode == EDisplayClusterOperationMode::Cluster;
}

bool UDisplayClusterGameEngine::BarrierAvoidanceOn() const
{
	return CVarGameStartBarrierAvoidance.GetValueOnGameThread() != 0;
}

bool UDisplayClusterGameEngine::OutOfSync() const
{
	return SyncMap.Num() != 0;
}

void UDisplayClusterGameEngine::ReceivedSync(const FString &Level, const FString &NodeId)
{
	UE_LOG(LogDisplayClusterEngine,Display, TEXT("GameSyncChange event received."));
	TSet<FString> &SyncItem = SyncMap.FindOrAdd(Level);
	SyncItem.Add(NodeId);
	if (SyncItem.Num() == GDisplayCluster->GetPrivateClusterMgr()->GetNodesAmount())
	{
		SyncMap.Remove(Level);
	}
	for (const TTuple<FString, TSet<FString>>& SyncObj : SyncMap)
	{
		FString Join = Algo::Accumulate(SyncObj.Value, FString(), [](FString Result, const FString& Value)
		{
			Result = Result + ", " + Value;
			return MoveTemp(Result);
		});
		UE_LOG(LogDisplayClusterEngine,Display, TEXT("    %s -> %s"), *SyncObj.Key, *Join);
	}
}

void UDisplayClusterGameEngine::CheckGameStartBarrier()
{
	if (!BarrierAvoidanceOn())
	{
		GDisplayCluster->GetPrivateClusterMgr()->GetNetApi().GetClusterSyncAPI()->WaitForGameStart();
	}
	else
	{
		if (!OutOfSync())
		{
			UE_LOG(LogDisplayClusterEngine, Display, TEXT("CheckGameStartBarrier - we are no longer out of sync. Restoring Play."));
			if (RunningMode == EDisplayClusterRunningMode::Startup)
			{
				GDisplayCluster->GetPrivateClusterMgr()->GetNetApi().GetClusterSyncAPI()->WaitForGameStart();
			}
			UGameplayStatics::SetGamePaused(WorldContextObject,false);
			RunningMode = EDisplayClusterRunningMode::Synced;
		}
		else if (!UGameplayStatics::IsGamePaused(WorldContextObject))
		{
			UE_LOG(LogDisplayClusterEngine, Display, TEXT("CheckGameStartBarrier - we are out of sync. Pausing Play."));
			// A 1 or more nodes is out of sync. Do not advance game until everyone is back in sync.
			//
			UGameplayStatics::SetGamePaused(WorldContextObject,true);
		}
	}
}

void UDisplayClusterGameEngine::GameSyncChange(const FDisplayClusterClusterEventJson& InEvent)
{
	if (BarrierAvoidanceOn())
	{
		if(InEvent.Category == DisplayClusterGameEngineUtils::WaitForGameCategory)
		{
			ReceivedSync(InEvent.Type,InEvent.Name);
			CheckGameStartBarrier();
		}
	}
}

EBrowseReturnVal::Type UDisplayClusterGameEngine::BrowseLoadMap(FWorldContext& WorldContext, FURL URL, FString& Error)
{
	FString DisplayClusterServerType;
	FParse::Value(FCommandLine::Get(), TEXT("dc_replicationserver_type"), DisplayClusterServerType);

	const bool IsDisplayCluster = (IDisplayCluster::Get().GetOperationMode() == EDisplayClusterOperationMode::Cluster);
	const bool IsDisplayClusterListenServer = IsDisplayCluster && DisplayClusterServerType.Equals(TEXT("listen"));

	if (URL.IsLocalInternal() && !IsDisplayClusterListenServer)
	{
		// Local map file.
		return LoadMap(WorldContext, URL, NULL, Error) ? EBrowseReturnVal::Success : EBrowseReturnVal::Failure;
	}
	else if ((URL.IsInternal() && GIsClient) || (URL.IsLocalInternal() && IsDisplayClusterListenServer))
	{
		EBrowseReturnVal::Type BrowseResult = EBrowseReturnVal::Failure;

		if (IsDisplayClusterListenServer)
		{
			BrowseResult = LoadMap(WorldContext, URL, NULL, Error) ? EBrowseReturnVal::Success : EBrowseReturnVal::Failure;
		}

		if ((URL.IsInternal() && GIsClient) || (BrowseResult == EBrowseReturnVal::Success))
		{
			// Network URL.
			if (WorldContext.PendingNetGame)
			{
				CancelPending(WorldContext);
			}

			// Clean up the netdriver/socket so that the pending level succeeds
			if (WorldContext.World() && ShouldShutdownWorldNetDriver())
			{
				ShutdownWorldNetDriver(WorldContext.World());
			}

			WorldContext.PendingNetGame = NewObject<UPendingNetGame>();
			WorldContext.PendingNetGame->Initialize(URL); //-V595
			WorldContext.PendingNetGame->InitNetDriver(); //-V595

			UNetDriver* PendingNetDriver = WorldContext.PendingNetGame->GetNetDriver();

			if (IsDisplayCluster && PendingNetDriver)
			{
				const bool bIsDisplayClusterNetDriver = PendingNetDriver->GetClass()->GetName().Equals(TEXT("DisplayClusterNetDriver"));
				
				if (bIsDisplayClusterNetDriver)
				{
					// multiplayer packes, including session handshake are processed on ticks thus we need to enforce engine to tick but preven from rendering until cluster is ready
					// Force tick idle mode for multiplayer connections
					bForcedTickIdleMode = true;

					// Suspend rendering until the cluster is ready
					// By default, engine allowed to render while session being established
					// to prevent rendering from being invoked we overrided function IsRenderingSuspended() and only enabling this flag here in BrowseLoadMap which is called on in multiplayer
					// once loading is finished and game tick called in the flag will be reset to false
					bIsRenderingSuspended = true;
				}
			}

			if (!WorldContext.PendingNetGame)
			{
				// If the inital packet sent in InitNetDriver results in a socket error, HandleDisconnect() and CancelPending() may be called, which will null the PendingNetGame.
				Error = NSLOCTEXT("Engine", "PendingNetGameInitFailure", "Error initializing the network driver.").ToString();
				BroadcastTravelFailure(WorldContext.World(), ETravelFailure::PendingNetGameCreateFailure, Error);
				return EBrowseReturnVal::Failure;
			}

			if (!WorldContext.PendingNetGame->NetDriver)
			{
				// UPendingNetGame will set the appropriate error code and connection lost type, so
				// we just have to propagate that message to the game.
				BroadcastTravelFailure(WorldContext.World(), ETravelFailure::PendingNetGameCreateFailure, WorldContext.PendingNetGame->ConnectionError);
				WorldContext.PendingNetGame = NULL;
				return EBrowseReturnVal::Failure;
			}
			return EBrowseReturnVal::Pending;
		}
	}
	else if (URL.IsInternal())
	{
		// Invalid.
		Error = NSLOCTEXT("Engine", "ServerOpen", "Servers can't open network URLs").ToString();
		return EBrowseReturnVal::Failure;
	}

	return EBrowseReturnVal::Failure;
}

void UDisplayClusterGameEngine::PendingLevelUpdate(FWorldContext& Context, float DeltaSeconds)
{
	// Update the pending level.
	if (Context.PendingNetGame)
	{
		Context.PendingNetGame->Tick(DeltaSeconds);
		if (Context.PendingNetGame && Context.PendingNetGame->ConnectionError.Len() > 0)
		{
			BroadcastNetworkFailure(NULL, Context.PendingNetGame->NetDriver, ENetworkFailure::PendingConnectionFailure, Context.PendingNetGame->ConnectionError);
			CancelPending(Context);
		}
		else if (Context.PendingNetGame && Context.PendingNetGame->bSuccessfullyConnected && !Context.PendingNetGame->bSentJoinRequest && !Context.PendingNetGame->bLoadedMapSuccessfully && (Context.OwningGameInstance == NULL || !Context.OwningGameInstance->DelayPendingNetGameTravel()))
		{
			if (Context.PendingNetGame->HasFailedTravel())
			{
				BrowseToDefaultMap(Context);
				BroadcastTravelFailure(Context.World(), ETravelFailure::TravelFailure, TEXT("Travel failed for unknown reason"));
			}
			else if (!MakeSureMapNameIsValid(Context.PendingNetGame->URL.Map))
			{
				BrowseToDefaultMap(Context);
				BroadcastTravelFailure(Context.World(), ETravelFailure::PackageMissing, Context.PendingNetGame->URL.Map);
			}
			else if (!Context.PendingNetGame->bLoadedMapSuccessfully)
			{
				// Attempt to load the map.
				FString Error;
				FString DisplayClusterServerType;
				FParse::Value(FCommandLine::Get(), TEXT("dc_replicationserver_type"), DisplayClusterServerType);

				const bool IsDisplayCluster = FParse::Param(FCommandLine::Get(), TEXT("dc_cluster"));
				const bool IsDisplayClusterListenServer = IsDisplayCluster && DisplayClusterServerType.Equals(TEXT("listen"));

				bool bLoadedMapSuccessfully = true;

				if (IsDisplayClusterListenServer)
				{
					MovePendingLevel(Context);
				}
				else
				{
					bLoadedMapSuccessfully = LoadMap(Context, Context.PendingNetGame->URL, Context.PendingNetGame, Error);
				}

				if (Context.PendingNetGame != nullptr)
				{
					if (!Context.PendingNetGame->LoadMapCompleted(this, Context, bLoadedMapSuccessfully, Error))
					{
						BrowseToDefaultMap(Context);
						BroadcastTravelFailure(Context.World(), ETravelFailure::LoadMapFailure, Error);
					}
				}
				else
				{
					BrowseToDefaultMap(Context);
					BroadcastTravelFailure(Context.World(), ETravelFailure::TravelFailure, Error);
				}
			}
		}

		if (Context.PendingNetGame && Context.PendingNetGame->bLoadedMapSuccessfully && (Context.OwningGameInstance == NULL || !Context.OwningGameInstance->DelayCompletionOfPendingNetGameTravel()))
		{
			if (!Context.PendingNetGame->HasFailedTravel())
			{
				Context.PendingNetGame->TravelCompleted(this, Context);
				Context.PendingNetGame = nullptr;
			}
			else
			{
				CancelPending(Context);
				BrowseToDefaultMap(Context);
				BroadcastTravelFailure(Context.World(), ETravelFailure::LoadMapFailure, TEXT("Travel failed for unknown reason"));
			}
		}
	}
	else if (TransitionType == ETransitionType::WaitingToConnect)
	{
		TransitionType = ETransitionType::None;
	}
}
