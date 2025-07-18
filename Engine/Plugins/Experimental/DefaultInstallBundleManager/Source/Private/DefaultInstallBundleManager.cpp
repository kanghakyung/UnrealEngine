// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultInstallBundleManager.h"

#include "DefaultInstallBundleManagerPrivate.h"
#include "InstallBundleSourceInterface.h"
#include "InstallBundleManagerModule.h"

#include "HAL/IConsoleManager.h"
#include "Misc/Base64.h"
#include "Misc/ConfigContext.h"
#include "Misc/CommandLine.h"
#include "IO/IoStoreOnDemand.h"
#include "PatchCheck.h"
#include "Templates/Greater.h"
#include "ShaderPipelineCache.h"
#include "Algo/Find.h"
#include "Algo/AnyOf.h"

#include "IAnalyticsProviderET.h"

#if WITH_EDITOR
#include "Misc/KeyChainUtilities.h"
#endif // WITH_EDITOR

static FAutoConsoleCommand RequestBundleCommand(
	TEXT("RequestBundle"),
	TEXT("Request a bundle for download and installation"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
{
	TSharedPtr<IInstallBundleManager> InstallBundleManager = IInstallBundleManager::GetPlatformInstallBundleManager();
	if (InstallBundleManager == nullptr)
	{
		LOG_INSTALL_BUNDLE_MAN(Error, TEXT("RequestBundle: InstallBundleManager not found!"));
	}
	else if (InstallBundleManager->GetInitState() != EInstallBundleManagerInitState::Succeeded)
	{
		LOG_INSTALL_BUNDLE_MAN(Error, TEXT("RequestBundle: InstallBundleManager not initialized!"));
	}
	else if (Args.Num() == 0)
	{
		LOG_INSTALL_BUNDLE_MAN(Display, TEXT("RequestBundle: Usage:\n")
							   TEXT("RequestBundle <BundleName>"));
	}
	else
	{
		LOG_INSTALL_BUNDLE_MAN(Warning, TEXT("Requesting bundle %s"), *Args[0]);
		InstallBundleManager->RequestUpdateContent(FName(*Args[0]), EInstallBundleRequestFlags::Defaults);
	}
}),
ECVF_Cheat);

static FAutoConsoleCommand SetInstallBundleErrorSimulationCommands(
	TEXT("SetInstallBundleErrorSimulationCommands"),
	TEXT("Set flags for simulating install bundle errors"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
{
	TSharedPtr<IInstallBundleManager> InstallBundleManager = IInstallBundleManager::GetPlatformInstallBundleManager();
	if (InstallBundleManager == nullptr)
	{
		LOG_INSTALL_BUNDLE_MAN(Error, TEXT("SetInstallBundleErrorSimulationCommands: InstallBundleManager not found!"));
	}
	else if (InstallBundleManager->GetInitState() != EInstallBundleManagerInitState::Succeeded)
	{
		LOG_INSTALL_BUNDLE_MAN(Error, TEXT("SetInstallBundleErrorSimulationCommands: InstallBundleManager not initialized!"));
	}
	else if (Args.Num() == 0)
	{
		LOG_INSTALL_BUNDLE_MAN(Display, TEXT("SetInstallBundleErrorSimulationCommands: Usage:\n")
			TEXT("SetInstallBundleErrorSimulationCommands [-SimulateClientNotLatest, -SimulateContentNotLatest, -SimulateOnCellularNetwork, -SimulateLowDiskSpace_NoRoomForInstall, -SimulateLowDiskSpace_InstallFailure, -SimulateBuildMetaDataDownloadError, -SimulateRemoteBuildMetaDataNotFoundError, -SimulateBuildVerificationError]"));
	}
	else
	{
		InstallBundleManager->SetErrorSimulationCommands(FString::Join(Args, TEXT(", ")));
	}
}),
ECVF_Cheat);

static FAutoConsoleCommand FlushInstallBundleCacheCommand(
	TEXT("FlushInstallBundleCache"),
	TEXT("Flush all install bundle caches"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		TSharedPtr<IInstallBundleManager> InstallBundleManager = IInstallBundleManager::GetPlatformInstallBundleManager();
		if (InstallBundleManager == nullptr)
		{
			LOG_INSTALL_BUNDLE_MAN(Error, TEXT("FlushInstallBundleCache: InstallBundleManager not found!"));
		}
		else if (InstallBundleManager->GetInitState() != EInstallBundleManagerInitState::Succeeded)
		{
			LOG_INSTALL_BUNDLE_MAN(Error, TEXT("FlushInstallBundleCache: InstallBundleManager not initialized!"));
		}
		else
		{
			InstallBundleManager->FlushCache(FInstallBundleManagerFlushCacheCompleteDelegate::CreateLambda([]() {
				LOG_INSTALL_BUNDLE_MAN(Log, TEXT("FlushInstallBundleCache: Cache flush complete!"));
			}));
		}
	}),
	ECVF_Cheat);

static FAutoConsoleCommand InstallBundleCacheStatsCommand(
	TEXT("InstallBundleCacheStats"),
	TEXT("Dump install bundle cache stats"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		TSharedPtr<IInstallBundleManager> InstallBundleManager = IInstallBundleManager::GetPlatformInstallBundleManager();
		if (InstallBundleManager == nullptr)
		{
			LOG_INSTALL_BUNDLE_MAN(Error, TEXT("InstallBundleCacheStats: InstallBundleManager not found!"));
		}
		else if (InstallBundleManager->GetInitState() != EInstallBundleManagerInitState::Succeeded)
		{
			LOG_INSTALL_BUNDLE_MAN(Error, TEXT("InstallBundleCacheStats: InstallBundleManager not initialized!"));
		}
		else if(Args.Num() > 0 && Args[0] == TEXT("CSV"))
		{
			InstallBundleManager->GetCacheStats(
				EInstallBundleCacheStatsFlags::DumpToLog | EInstallBundleCacheStatsFlags::CSVFormat);
		}
		else
		{
			InstallBundleManager->GetCacheStats(EInstallBundleCacheStatsFlags::DumpToLog);
		}
	}),
	ECVF_Cheat);

static int32 MaxContentInstallTimePerTickMS = 0;
static FAutoConsoleVariableRef CVarMaxContentInstallTimePerTickMS(TEXT("InstallBundleManager.MaxContentInstallTimePerTickMS"),
	MaxContentInstallTimePerTickMS,
	TEXT("Maximum duration in milliseconds to allot for content install requests"));

FDefaultInstallBundleManager::EBundleState FDefaultInstallBundleManager::GetBundleStatus(const FBundleInfo& BundleInfo) const
{
	return BundleInfo.GetBundleStatus(*this);
}

void FDefaultInstallBundleManager::SetBundleStatus(FBundleInfo& BundleInfo, EBundleState InBundleState)
{
	BundleInfo.SetBundleStatus(*this, InBundleState);
}

bool FDefaultInstallBundleManager::GetMustWaitForPSOCache(const FBundleInfo& BundleInfo) const
{
	return BundleInfo.GetMustWaitForPSOCache(*this);
}

uint32 FDefaultInstallBundleManager::GetInitialShaderPrecompiles(const FBundleInfo& BundleInfo) const
{
	return BundleInfo.GetInitialShaderPrecompiles(*this);
}

void FDefaultInstallBundleManager::SetMustWaitForPSOCache(FBundleInfo& BundleInfo, uint32 InNumPSOPrecompilesRemaining)
{
	BundleInfo.SetMustWaitForPSOCache(*this, InNumPSOPrecompilesRemaining);
}

#if WITH_EDITOR
void LoadKeychainFromInI(FKeyChain& OutCryptoSettings)
{
	FConfigFile ConfigFile;
	FConfigContext::ReadIntoLocalFile(ConfigFile, TEXT("Windows")).Load(TEXT("Crypto"));
	if (ConfigFile.Num())
	{
		static const TCHAR* SectionName = TEXT("/Script/CryptoKeys.CryptoKeysSettings");
		FString EncryptionKeyString;
		ConfigFile.GetString(SectionName, TEXT("EncryptionKey"), EncryptionKeyString);

		if (EncryptionKeyString.Len() > 0)
		{
			TArray<uint8> Key;
			FBase64::Decode(EncryptionKeyString, Key);
			check(Key.Num() == sizeof(FAES::FAESKey::Key));
			FNamedAESKey NewKey;
			NewKey.Name = TEXT("Default");
			NewKey.Guid = FGuid();
			FMemory::Memcpy(NewKey.Key.Key, &Key[0], sizeof(FAES::FAESKey::Key));
			OutCryptoSettings.GetEncryptionKeys().Add(NewKey.Guid, NewKey);
		}
	}

	FGuid EncryptionKeyOverrideGuid;
	OutCryptoSettings.SetPrincipalEncryptionKey(OutCryptoSettings.GetEncryptionKeys().Find(EncryptionKeyOverrideGuid));
}
#endif // WITH_EDITOR

FDefaultInstallBundleManager::FContentRequest::~FContentRequest() = default;

FDefaultInstallBundleManager::FDefaultInstallBundleManager(FInstallBundleSourceFactoryFunction InInstallBundleSourceFactory)
	: InstallBundleSourceFactory(InInstallBundleSourceFactory ? MoveTemp(InInstallBundleSourceFactory) : InstallBundleManagerUtil::MakeBundleSource )
	, PersistentStats(MakeShared<InstallBundleManagerUtil::FPersistentStatContainer>())
	, AnalyticsProvider(nullptr)
	, StatsMap(MakeShared<InstallBundleUtil::FContentRequestStatsMap>())
{
#if WITH_EDITOR
	// -UsePaks needs to be specified on the command line for valid pak to be created.
	// To support mounting pak files in the editor binary add the encryption key.
	FKeyChain KeyChain;
	LoadKeychainFromInI(KeyChain);
	KeyChainUtilities::ApplyEncryptionKeys(KeyChain);
#endif // WITH_EDITOR

	SetErrorSimulationCommands(FCommandLine::Get());
	SetCommandLineOverrides(FCommandLine::Get());
}

FDefaultInstallBundleManager::~FDefaultInstallBundleManager()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	TickHandle.Reset();

	InstallBundleUtil::CleanupInstallBundleAsyncIOTasks(AsyncMountTasks);
}

void FDefaultInstallBundleManager::Initialize()
{
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FDefaultInstallBundleManager::Tick));

	InitErrorHandlerStack.Push(FInstallBundleManagerInitErrorHandler::CreateLambda(
		[](EInstallBundleManagerInitResult Error)
		{
			// Default Handler, Just keep retrying
			return EInstallBundleManagerInitErrorHandlerResult::Retry;
		}
	));

	if (InitResult == EInstallBundleManagerInitResult::OK)
		InitResult = Init_DefaultBundleSources();

	if (InitResult != EInstallBundleManagerInitResult::OK)
	{
		LOG_INSTALL_BUNDLE_MAN(Warning, TEXT("Unrecoverable Initialization Failure - %s"), LexToString(InitResult));
		bUnrecoverableInitError = true;
	}
}

bool FDefaultInstallBundleManager::Tick(float dt)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FDefaultInstallBundleManager_Tick);

	CSV_SCOPED_TIMING_STAT(InstallBundleManager, InstallBundleManager_Tick);

	TickInit();

	if (InitState == EInstallBundleManagerInitState::Succeeded && bDelayCheckingForContentPatch)
	{
		bDelayCheckingForContentPatch = false;
		StartContentPatchCheck();
	}

	TickGetContentState();

	TickGetInstallState();

	TickContentRequests();

	TickCacheFlush();

	TickReserveCache();

	TickWaitForShaderCache();

	TickPauseStatus(false);

	TickAsyncMountTasks();

	TickReleaseRequests();

	TickPruneBundleInfo();

	return true;
}

EInstallBundleManagerInitErrorHandlerResult FDefaultInstallBundleManager::HandleAsyncInitError(EInstallBundleManagerInitResult InitResultError)
{
	check(InitResultError != EInstallBundleManagerInitResult::OK);
	check(InitErrorHandlerStack.Num() > 0);

	EInstallBundleManagerInitErrorHandlerResult RetResult = EInstallBundleManagerInitErrorHandlerResult::Retry;
	for (int32 iHandler = 0; iHandler < InitErrorHandlerStack.Num(); ++iHandler)
	{
		FInstallBundleManagerInitErrorHandler& Handler = InitErrorHandlerStack.Last(iHandler);

		EInstallBundleManagerInitErrorHandlerResult HandlerResult = EInstallBundleManagerInitErrorHandlerResult::NotHandled;
		if (Handler.IsBound())
		{
			HandlerResult = Handler.Execute(InitResultError);
		}

		if (HandlerResult != EInstallBundleManagerInitErrorHandlerResult::NotHandled)
		{
			if (HandlerResult == EInstallBundleManagerInitErrorHandlerResult::StopInitialization)
			{
				RetResult = HandlerResult;
				break;
			}
		}
	}

	check(RetResult != EInstallBundleManagerInitErrorHandlerResult::NotHandled);

	return RetResult;
}

void FDefaultInstallBundleManager::TickInit()
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, InstallBundleManager_TickInit);

	auto HandleInitStopped = [this]()
	{
		LOG_INSTALL_BUNDLE_MAN(Error, TEXT("Initialization Failed - %s"), LexToString(InitResult));
		InitState = EInstallBundleManagerInitState::Failed;
		InitCompleteDelegate.Broadcast(InitResult);
	};

	auto GetConfigInitRetryTimeSecondsMin = []() -> double
	{
		double InitRetryTimeSecondsMin = 0.1;
		GConfig->GetDouble(TEXT("InstallBundleManager.MiscSettings"), TEXT("InitRetryTimeSecondsMin"), InitRetryTimeSecondsMin, GInstallBundleIni);
		if (InitRetryTimeSecondsMin <= 0)
		{
			ensureMsgf(false, TEXT("InitRetryTimeSecondsMin must be greater than 0!"));
			InitRetryTimeSecondsMin = 0.1;
		}
		return InitRetryTimeSecondsMin;
	};

	auto GetConfigInitRetryTimeSecondsMax = []() -> double
	{
		double InitRetryTimeSecondsMax = 5.0;
		GConfig->GetDouble(TEXT("InstallBundleManager.MiscSettings"), TEXT("InitRetryTimeSecondsMax"), InitRetryTimeSecondsMax, GInstallBundleIni);
		return InitRetryTimeSecondsMax;
	};

	while (InitState == EInstallBundleManagerInitState::NotInitialized && InitStepResult == EAsyncInitStepResult::Done)
	{
		if (bUnrecoverableInitError)
		{
			HandleInitStopped();
			break;
		}

		if (InitResult != EInstallBundleManagerInitResult::OK)
		{
			if (LastInitStep != InitStep)
			{
				LastInitStep = InitStep;

				// Reset retry timer for first retry
				InitRetryTimeDeltaSeconds = 0;
			}

			// Attempt to try the step again
			const double CurrentTime = FPlatformTime::Seconds();
			if (LastInitRetryTimeSeconds <= 0)
			{
				LastInitRetryTimeSeconds = CurrentTime;
			}

			if (CurrentTime < LastInitRetryTimeSeconds + InitRetryTimeDeltaSeconds)
			{
				break;
			}

			// call error handler before allowing the retry
			const EInstallBundleManagerInitErrorHandlerResult HandlerResult = HandleAsyncInitError(InitResult);

			if (InitRetryTimeDeltaSeconds <= 0)
			{
				// Only fire init analytic for failures the first time we retry
				AsyncInit_FireInitAnlaytic(HandlerResult != EInstallBundleManagerInitErrorHandlerResult::StopInitialization);
			}

			if (HandlerResult == EInstallBundleManagerInitErrorHandlerResult::StopInitialization)
			{
				HandleInitStopped();
				break;
			}

			if (InitStep != EAsyncInitStep::None) // Don't spam this for unrecoverable errors
			{
				LOG_INSTALL_BUNDLE_MAN(Warning, TEXT("Retrying initialization after %s - waited %f seconds"), LexToString(InitResult), InitRetryTimeDeltaSeconds);
			}

			const double ConfigInitRetryTimeSecondsMin = GetConfigInitRetryTimeSecondsMin();
			if (InitRetryTimeDeltaSeconds <= 0)
			{
				InitRetryTimeDeltaSeconds = ConfigInitRetryTimeSecondsMin;
			}
			else
			{
				double ConfigInitRetryTimeSecondsMax = GetConfigInitRetryTimeSecondsMax();
				if (ConfigInitRetryTimeSecondsMax < ConfigInitRetryTimeSecondsMin)
				{
					ensureMsgf(false, TEXT("InitRetryTimeSecondsMax must be greater than or equal to InitRetryTimeSecondsMin!"));
					ConfigInitRetryTimeSecondsMax = ConfigInitRetryTimeSecondsMin;
				}

				InitRetryTimeDeltaSeconds = FMath::Min(InitRetryTimeDeltaSeconds * 2.0, ConfigInitRetryTimeSecondsMax);
			}
		}

		if (InitResult == EInstallBundleManagerInitResult::OK)
		{
			LastInitRetryTimeSeconds = 0; //Reset after async op
			LastInitStep = InitStep;
			++InstallBundleUtil::CastAsUnderlying(InitStep);
		}

		bIsCurrentlyInAsyncInit = true;
		switch (InitStep)
		{
		case EAsyncInitStep::None:
			LOG_INSTALL_BUNDLE_MAN(Fatal, TEXT("Trying to use init state None"));
			break;
		case EAsyncInitStep::InitBundleSources:
			InitResult = EInstallBundleManagerInitResult::OK;
			AsyncInit_InitBundleSources();
			break;
		case EAsyncInitStep::InitBundleCaches:
			InitResult = EInstallBundleManagerInitResult::OK;
			AsyncInit_InitBundleCaches();
			break;
		case EAsyncInitStep::QueryBundleInfo:
			InitResult = EInstallBundleManagerInitResult::OK;
			AsyncInit_QueryBundleInfo();
			break;
		case EAsyncInitStep::SetUpdateBundleInfoCallback:
			InitResult = EInstallBundleManagerInitResult::OK;
			AsyncInit_SetUpdateBundleInfoCallback();
			break;
		case EAsyncInitStep::CreateAnalyticsSession:
			InitResult = EInstallBundleManagerInitResult::OK;
			AsyncInit_CreateAnalyticsSession();
			break;
		case EAsyncInitStep::Finishing:
			AsyncInit_FireInitAnlaytic(true);
			BundleSourcesToDelete.Empty();
			InitState = EInstallBundleManagerInitState::Succeeded;
			InitCompleteDelegate.Broadcast(InitResult);
			break;
		default:
			LOG_INSTALL_BUNDLE_MAN(Fatal, TEXT("Unknown Init Step %s"), LexToString(InitStep));
			break;
		}
		bIsCurrentlyInAsyncInit = false;

		if (InitResult == EInstallBundleManagerInitResult::OK)
		{
			LastInitRetryTimeSeconds = 0; //Reset after calling step function
		}
	}
}

void FDefaultInstallBundleManager::TickGetContentState()
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, InstallBundleManager_TickGetContentState);

	switch (InitState)
	{
	case EInstallBundleManagerInitState::NotInitialized:
		return;
	case EInstallBundleManagerInitState::Failed:
		for (FGetContentStateRequestRef& Request : GetContentStateRequests)
		{
			Request->ExecCallbackIfValid(FInstallBundleCombinedContentState());
		}
		GetContentStateRequests.Empty();
		return;
	}

	int32 iRequest = 0;
	while (iRequest < GetContentStateRequests.Num())
	{
		FGetContentStateRequestRef& Request = GetContentStateRequests[iRequest];

		//if we were canceled and not started, just remove right away without calling anything back
		if (Request->bCancelled && !Request->Started)
		{
			GetContentStateRequests.RemoveAt(iRequest);
			continue;
		}

		if (!Request->Started && Request->BundleNames.Num() == 0)
		{
			FInstallBundleCombinedContentState State;
			Request->ExecCallbackIfValid(MoveTemp(State));
			GetContentStateRequests.RemoveAt(iRequest);
			continue;
		}

		if (Request->Started)
		{
			// Check if done
			if (Request->BundleSourceContentStates.Num() != BundleSources.Num())
			{
				++iRequest;
				continue;
			}

			FInstallBundleCombinedContentState State;
			State.FreeSpace = 0;
			State.BackgroundDownloadContentSize = FInstallBundleContentSize{}; // it's optional for individual sources, but we always have a value
			
			for (const TPair<FInstallBundleSourceType, FInstallBundleCombinedContentState>& SourcePair : Request->BundleSourceContentStates)
			{
				const FInstallBundleSourceType SourceType = SourcePair.Key;
				const FInstallBundleCombinedContentState& SourceState = SourcePair.Value;

				// combine state enums and weights
				State.CurrentVersion.Add(SourceType, SourceState.CurrentVersion.FindChecked(SourceType));
				float SourceWeight = BundleSources.FindChecked(SourceType)->GetSourceWeight();
				for (const TPair<FName, FInstallBundleContentState>& StatePair : SourceState.IndividualBundleStates)
				{
					FInstallBundleContentState* BundleState = State.IndividualBundleStates.Find(StatePair.Key);
					if (!BundleState)
					{
						BundleState = &State.IndividualBundleStates.Add(StatePair.Key);
						BundleState->State = EInstallBundleInstallState::UpToDate;
					}
					if (BundleState->State == EInstallBundleInstallState::UpToDate)
					{
						BundleState->State = StatePair.Value.State;
					}
					else if (BundleState->State == EInstallBundleInstallState::NotInstalled && StatePair.Value.State != EInstallBundleInstallState::NotInstalled)
					{
						BundleState->State = EInstallBundleInstallState::NeedsUpdate;
					}

					if (StatePair.Value.State < BundleState->State)
					{
						BundleState->State = StatePair.Value.State;
					}
					// Combine weights
					BundleState->Weight += StatePair.Value.Weight * SourceWeight;
					BundleState->Version.Add(SourceType, StatePair.Value.Version.FindChecked(SourceType));
				}

				State.ContentSize += SourceState.ContentSize;
				State.BackgroundDownloadContentSize.GetValue() += SourceState.BackgroundDownloadContentSize.Get(SourceState.ContentSize);
				State.FreeSpace = State.FreeSpace ? FMath::Min(State.FreeSpace, SourceState.FreeSpace) : SourceState.FreeSpace;
			}

			for (const TPair<FName, FInstallBundleContentState>& Pair : State.IndividualBundleStates)
			{
				const FBundleInfo* BundleInfo = BundleInfoMap.Find(Pair.Key);
				if (ensure(BundleInfo) && BundleInfo->bContainsIoStoreOnDemandTocs)
				{
					State.BundlesWithIoStoreOnDemand.Add(Pair.Key);
				}
			}

			Request->ExecCallbackIfValid(MoveTemp(State));
			GetContentStateRequests.RemoveAt(iRequest);
			continue;
		}

		Request->Started = true;
		for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
		{
			Pair.Value->GetContentState(Request->BundleNames, Request->Flags, FInstallBundleGetContentStateDelegate::CreateLambda(
				[Request, SourceType = Pair.Key](FInstallBundleCombinedContentState SourceState)
			{
				Request->BundleSourceContentStates.Add(SourceType, MoveTemp(SourceState));
			}));
		}
	}
}

void FDefaultInstallBundleManager::TickGetInstallState()
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, InstallBundleManager_TickGetInstallState);

	switch (InitState)
	{
	case EInstallBundleManagerInitState::NotInitialized:
		return;
	case EInstallBundleManagerInitState::Failed:
		for (FGetInstallStateRequestRef& Request : GetInstallStateRequests)
		{
			Request->ExecCallbackIfValid(FInstallBundleCombinedInstallState());
		}
		GetInstallStateRequests.Empty();
		return;
	}

	while (GetInstallStateRequests.Num())
	{
		FGetInstallStateRequestRef& Request = GetInstallStateRequests[0];

		if (Request->bCancelled)
		{
			GetInstallStateRequests.RemoveAt(0);
			continue;
		}

		Request->ExecCallbackIfValid(GetInstallStateInternal(Request->BundleNames));
		GetInstallStateRequests.RemoveAt(0);
	}
}

FInstallBundleCombinedInstallState FDefaultInstallBundleManager::GetInstallStateInternal(TArrayView<const FName> BundleNames) const
{
	FInstallBundleCombinedInstallState RetVal;
	RetVal.IndividualBundleStates.Reserve(BundleNames.Num());
	for (const FName& BundleName : BundleNames)
	{
		EInstallBundleInstallState InstallState = EInstallBundleInstallState::NotInstalled;
		const FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(BundleName);
		switch (GetBundleStatus(BundleInfo))
		{
		case EBundleState::NotInstalled:
			InstallState = EInstallBundleInstallState::NotInstalled;
			break;
		case EBundleState::NeedsUpdate:
			InstallState = EInstallBundleInstallState::NeedsUpdate;
			break;
		case EBundleState::NeedsMount:
		case EBundleState::Mounted:
			InstallState = EInstallBundleInstallState::UpToDate;
			break;
		default:
			ensureMsgf(false, TEXT("Unknown EBundleState"));
			break;
		}

		RetVal.IndividualBundleStates.Add(BundleName, InstallState);

		if (BundleInfo.bContainsIoStoreOnDemandTocs)
		{
			RetVal.BundlesWithIoStoreOnDemand.Add(BundleName);
		}
	}

	static_assert(InstallBundleUtil::CastToUnderlying(EBundleState::Count) == 4, "");

	return RetVal;
}

void FDefaultInstallBundleManager::CacheHintRequested(FContentRequestRef Request, bool bRequested)
{
	ON_SCOPE_EXIT
	{
		Request->StepResult = EContentRequestStepResult::Done;
	};

	if (bRequested)
	{
		if (Request->bIsCanceled || Request->bDidCacheHintRequested)
		{
			return;
		}
	}
	else
	{
		if (!Request->bDidCacheHintRequested)
		{
			return;
		}
	}

	Request->bDidCacheHintRequested = bRequested;

	for (const TPair<FName, TSharedRef<FInstallBundleCache>>& Pair : BundleCaches)
	{
		Pair.Value->HintRequested(Request->BundleName, bRequested);
	}
}

void FDefaultInstallBundleManager::CheckPrereqHasNoPendingCancels(FContentRequestRef Request)
{
	if (Request->bIsCanceled)
	{
		Request->StepResult = EContentRequestStepResult::Done;
		return;
	}

	for (const EContentRequestBatch b : TEnumRange<EContentRequestBatch>())
	{
		const FContentRequestRef* CanceledRequest = ContentRequests[b].FindByPredicate(
			[BundleName = Request->BundleName](const FContentRequestRef& BatchedRequest)
		{
			return BatchedRequest->BundleName == BundleName && BatchedRequest->bIsCanceled;
		});
		if (CanceledRequest)
		{
			Request->StepResult = EContentRequestStepResult::Waiting;
			return;
		}
	}

	Request->StepResult = EContentRequestStepResult::Done;
}

void FDefaultInstallBundleManager::CheckPrereqHasNoPendingCancels(FContentReleaseRequestRef Request)
{
	if (Request->bIsCanceled)
	{
		Request->StepResult = EContentRequestStepResult::Done;
		return;
	}

	for (const EContentReleaseRequestBatch b : TEnumRange<EContentReleaseRequestBatch>())
	{
		const FContentReleaseRequestRef* CanceledRequest = ContentReleaseRequests[b].FindByPredicate(
			[BundleName = Request->BundleName](const FContentReleaseRequestRef& BatchedRequest)
		{
			return BatchedRequest->BundleName == BundleName && BatchedRequest->bIsCanceled;
		});
		if (CanceledRequest)
		{
			Request->StepResult = EContentRequestStepResult::Waiting;
			return;
		}
	}

	Request->StepResult = EContentRequestStepResult::Done;
}

void FDefaultInstallBundleManager::CheckPrereqHasNoPendingReleaseRequests(FContentRequestRef Request)
{
	if (Request->bIsCanceled)
	{
		Request->StepResult = EContentRequestStepResult::Done;
		return;
	}

	for (const EContentReleaseRequestBatch b : TEnumRange<EContentReleaseRequestBatch>())
	{
		if (b == EContentReleaseRequestBatch::Requested)
			continue;

		const FContentReleaseRequestRef* ReleaseRequest = ContentReleaseRequests[b].FindByPredicate(
			[BundleName = Request->BundleName](const FContentReleaseRequestRef& BatchedRequest)
		{
			return BatchedRequest->BundleName == BundleName;
		});
		if (ReleaseRequest)
		{
			Request->StepResult = EContentRequestStepResult::Waiting;
			return;
		}
	}

	Request->StepResult = EContentRequestStepResult::Done;
}

void FDefaultInstallBundleManager::CheckPrereqHasNoPendingUpdateRequests(FContentReleaseRequestRef Request)
{
	if (Request->bIsCanceled)
	{
		Request->StepResult = EContentRequestStepResult::Done;
		return;
	}

	for (const EContentRequestBatch b : TEnumRange<EContentRequestBatch>())
	{
		if (b == EContentRequestBatch::Requested)
			continue;

		const FContentRequestRef* UpdateRequest = ContentRequests[b].FindByPredicate(
			[BundleName = Request->BundleName](const FContentRequestRef& BatchedRequest)
		{
			return BatchedRequest->BundleName == BundleName;
		});
		if (UpdateRequest)
		{
			Request->StepResult = EContentRequestStepResult::Waiting;
			return;
		}
	}

	Request->StepResult = EContentRequestStepResult::Done;
}

void FDefaultInstallBundleManager::CheckPrereqLatestClient(FContentRequestRef Request)
{
	if (Request->bIsCanceled)
	{
		Request->StepResult = EContentRequestStepResult::Done;
		return;
	}

	const FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(Request->BundleName);
	EBundleState BundleState = GetBundleStatus(BundleInfo);

#if WITH_EDITOR
	constexpr bool bIsEditorBuild = true;
#else
	constexpr bool bIsEditorBuild = false;
#endif // WITH_EDITOR

	FString ReasonForPatchSkip;
	if (bIsEditorBuild)
	{
		ReasonForPatchSkip = TEXT("With Editor");
	}
	else if (!StateSignifiesNeedsInstall(BundleState))
	{
		ReasonForPatchSkip = TEXT("Content up to date");
	}
	else if (bOverrideCommand_SkipPatchCheck)
	{
		ReasonForPatchSkip = TEXT("bOverrideCommand_SkipPatchCheck set");
	}
	else
	{
		Request->StepResult = EContentRequestStepResult::Waiting;

		check(!Request->CheckLastestClientDelegateHandle.IsValid());
		Request->CheckLastestClientDelegateHandle = PatchCheckCompleteDelegate.AddRaw(
			this, &FDefaultInstallBundleManager::HandlePatchInformationReceived, Request);
		StartPatchCheck();
		return;
	}

	LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Skipped Checking Prereq latest client for Request %s, %s"), *BundleInfo.BundleNameString, *ReasonForPatchSkip);

	InstallBundleManagerAnalytics::FireEvent_BundleLatestClientCheckComplete(AnalyticsProvider.Get(),
		BundleInfo.BundleNameString, true, ReasonForPatchSkip, true, false);

	Request->StepResult = EContentRequestStepResult::Done;
}

void FDefaultInstallBundleManager::HandlePatchInformationReceived(EInstallBundleManagerPatchCheckResult Result, FContentRequestRef Request)
{
	PatchCheckCompleteDelegate.Remove(Request->CheckLastestClientDelegateHandle);
	Request->CheckLastestClientDelegateHandle.Reset();

#if INSTALL_BUNDLE_ALLOW_ERROR_SIMULATION
	if (bSimulateClientNotLatest)
	{
		Result = EInstallBundleManagerPatchCheckResult::ClientPatchRequired;
	}
#endif

	if (Result == EInstallBundleManagerPatchCheckResult::ClientPatchRequired)
	{
		LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Failed Prereq latest client for Request %s"), *Request->BundleName.ToString());
		Request->Result = EInstallBundleResult::FailedPrereqRequiresLatestClient;
	}
	else if (Result == EInstallBundleManagerPatchCheckResult::ContentPatchRequired)
	{
		LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Failed Prereq latest content for Request %s"), *Request->BundleName.ToString());
		Request->Result = EInstallBundleResult::FailedPrereqRequiresLatestContent;
	}

	bool bRequestFailed = (
		Result != EInstallBundleManagerPatchCheckResult::ClientPatchRequired &&
		Result != EInstallBundleManagerPatchCheckResult::ContentPatchRequired &&
		Result != EInstallBundleManagerPatchCheckResult::NoPatchRequired);
	InstallBundleManagerAnalytics::FireEvent_BundleLatestClientCheckComplete(AnalyticsProvider.Get(),
		Request->BundleName.ToString(), false, TEXT(""),
		Request->Result == EInstallBundleResult::OK, bRequestFailed);

	Request->StepResult = EContentRequestStepResult::Done;
}

void FDefaultInstallBundleManager::DetermineSteps(FContentRequestRef Request)
{
	check(Request->Steps.Num() == 0);
	check(Request->iStep == INDEX_NONE);

	const FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(Request->BundleName);
	switch (GetBundleStatus(BundleInfo))
	{
	case EBundleState::NotInstalled:
	case EBundleState::NeedsUpdate:
	case EBundleState::NeedsMount:
		Request->Steps.Add(EContentRequestState::ReservingCache);
		Request->iOnCanceledStep[EContentRequestBatch::Cache] = Request->Steps.Add(EContentRequestState::FinishingCache);
		Request->Steps.Add(EContentRequestState::UpdatingBundleSources);
		Request->Steps.Add(EContentRequestState::Mounting);
	case EBundleState::Mounted:
		Request->Steps.Add(EContentRequestState::WaitingForShaderCache);
		Request->iOnCanceledStep[EContentRequestBatch::Install] = Request->Steps.Add(EContentRequestState::Finishing);
		Request->Steps.Add(EContentRequestState::CleaningUp);
		break;
	default:
		LOG_INSTALL_BUNDLE_MAN(Fatal, TEXT("Unknown Bundle Status %s"), LexToString(GetBundleStatus(BundleInfo)));
		break;
	}

	check(Request->Steps.Num() > 0);
	check(Request->Steps.Last() == EContentRequestState::CleaningUp);
}

void FDefaultInstallBundleManager::AddRequestToInitialBatch(FContentRequestRef Request)
{
	FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(Request->BundleName);

	EContentRequestBatch InitialBatch = EContentRequestBatch::Cache;
	switch (GetBundleStatus(BundleInfo))
	{
	case EBundleState::NeedsMount:
		Request->bShouldSendAnalytics = false;
		break;

	case EBundleState::Mounted:
		InitialBatch = EContentRequestBatch::Install;
		BundleInfo.bReleaseRequired = true;
		Request->bShouldSendAnalytics = false;
		break;
	}

	LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Verbose, TEXT("Adding Request %s to batch %s"), *BundleInfo.BundleNameString, LexToString(EContentRequestBatch::Cache));
	ContentRequests[InitialBatch].Emplace(MoveTemp(Request));
}

void FDefaultInstallBundleManager::ReserveCache(FContentRequestRef Request)
{
	StatsBegin(Request->BundleName, EContentRequestState::ReservingCache);

	TryReserveCache(Request);
}

void FDefaultInstallBundleManager::TryReserveCache(FContentRequestRef Request)
{
	Request->BundlesToEvictFromSourcesMap.Empty();
	Request->LastCacheReserveResult = EInstallBundleCacheReserveResult::Success;

	bool bSuccess = true;
	bool bMustWaitForCacheEvict = false;
	TMap<FName, EInstallBundleCacheReserveResult> ReserveResults;
	ReserveResults.Reserve(BundleCaches.Num());

	TSet<FName> EnabledBundleCaches;
	{
		TArray<TSharedPtr<IInstallBundleSource>> EnabledBundleSources = GetEnabledBundleSourcesForRequest(Request);
		for (const TSharedPtr<IInstallBundleSource>& Source : EnabledBundleSources)
		{
			FName* BundleCacheName = BundleSourceCaches.Find(Source->GetSourceType());
			if (BundleCacheName)
			{
				EnabledBundleCaches.Add(*BundleCacheName);
			}
		}
	}
	// we will try reserve cache space for only enabled bundle sources.  Each bundle source knows how much cache it should require for the bundle. 
	for (const FName& BundleCacheName : EnabledBundleCaches)
	{
		TSharedRef<FInstallBundleCache> BundleCache = BundleCaches.FindRef(BundleCacheName);
		FInstallBundleCacheReserveResult Result = BundleCache->Reserve(Request->BundleName);
		ReserveResults.Add(BundleCacheName, Result.Result);
		switch (Result.Result)
		{
		case EInstallBundleCacheReserveResult::Fail_CacheFull:
			bSuccess = false;
			break;

		case EInstallBundleCacheReserveResult::Fail_NeedsEvict:
			for (TPair<FName, TArray<FInstallBundleSourceType>>& ResultPair : Result.BundlesToEvict)
			{
				// A source can only map to one cache, so we can just append them to the list for this bundle without checking if its already there.
				TArray<FInstallBundleSourceType>& EvictFromSources = Request->BundlesToEvictFromSourcesMap.FindOrAdd(ResultPair.Key);
				EvictFromSources.Append(MoveTemp(ResultPair.Value));
			}
			break;

		case EInstallBundleCacheReserveResult::Fail_PendingEvict:
			bMustWaitForCacheEvict = true;
			break;

		case EInstallBundleCacheReserveResult::Success:
			break;

		default:
			bSuccess = false;
			break;
		}

		if (!bSuccess)
			break;
	}

	if (!bSuccess)
	{
		LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Failed to reserve cache for Request %s"), *Request->BundleName.ToString());

		for (const TPair<FName, EInstallBundleCacheReserveResult>& Pair : ReserveResults)
		{
			if (Pair.Value == EInstallBundleCacheReserveResult::Success)
			{
				// Release from any caches that were reserved
				verify(BundleCaches.FindChecked(Pair.Key)->Release(Request->BundleName));
			}
			else if (Pair.Value == EInstallBundleCacheReserveResult::Fail_CacheFull)
			{
				// Dump useful info
				GetCacheStats(FInstallBundleSourceOrCache(Pair.Key), EInstallBundleCacheStatsFlags::DumpToLog, Request->GetLogVerbosityOverride());

				TSharedRef<FInstallBundleCache> Cache = BundleCaches.FindChecked(Pair.Key);
				if (TOptional<FInstallBundleCacheBundleInfo> CacheBundleInfo = Cache->GetBundleInfo(Request->BundleName))
				{
					LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("* Reserve attempt for request %s"), *Request->BundleName.ToString());
					LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("* \tfull size: %" UINT64_FMT), CacheBundleInfo->FullInstallSize)
					LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("* \tcurrent size: %" UINT64_FMT), CacheBundleInfo->CurrentInstallSize)
				}
			}
		}

		Request->LastCacheReserveResult = EInstallBundleCacheReserveResult::Fail_CacheFull;

		StatsEnd(Request->BundleName, EContentRequestState::ReservingCache);
		Request->Result = EInstallBundleResult::FailedCacheReserve;
		Request->StepResult = EContentRequestStepResult::Done;
		return;
	}

	if (bMustWaitForCacheEvict)
	{
		Request->LastCacheReserveResult = EInstallBundleCacheReserveResult::Fail_PendingEvict;
		Request->StepResult = EContentRequestStepResult::Waiting;
		return;
	}

	if (Request->BundlesToEvictFromSourcesMap.Num() == 0)
	{
		StatsEnd(Request->BundleName, EContentRequestState::ReservingCache);
		Request->StepResult = EContentRequestStepResult::Done;
		return;
	}

	Request->LastCacheReserveResult = EInstallBundleCacheReserveResult::Fail_NeedsEvict;

	Request->StepResult = EContentRequestStepResult::Waiting;
	RequestEviction(Request);
}

void FDefaultInstallBundleManager::RequestEviction(FCacheEvictionRequestorRef Requestor)
{
	LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Requestor->GetLogVerbosityOverride(), Display, TEXT("Attempting to evict %i bundles for %s"), Requestor->BundlesToEvictFromSourcesMap.Num(), *Requestor->GetEvictionRequestorName());

	for (const TPair<FName, TArray<FInstallBundleSourceType>>& Pair : Requestor->BundlesToEvictFromSourcesMap)
	{
		for (FInstallBundleSourceType SourceType : Pair.Value)
		{
			const TSharedPtr<IInstallBundleSource>& Source = BundleSources.FindChecked(SourceType);
			const TSharedRef<FInstallBundleCache>& BundleCache = BundleCaches.FindChecked(BundleSourceCaches.FindChecked(SourceType));

			// Multiple sources can map to the same cache, but it should be fine to call this more than once on the same cache with the same bundle
			verify(BundleCache->SetPendingEvict(Pair.Key));

			auto CacheEvictKey = MakeTuple(BundleCache->GetName(), Pair.Key);
			CachesPendingEvictToSources.FindOrAdd(CacheEvictKey).AddUnique(SourceType);

			auto SourceEvictionKey = MakeTuple(SourceType, Pair.Key);

			TArray<FCacheEvictionRequestorRef>* CacheEvictionRequestors = PendingCacheEvictions.Find(SourceEvictionKey);
			bool bAlreadyRequested = CacheEvictionRequestors != nullptr;
			if (CacheEvictionRequestors == nullptr)
			{
				CacheEvictionRequestors = &PendingCacheEvictions.Add(SourceEvictionKey);
			}

			CacheEvictionRequestors->Add(Requestor);

			if (bAlreadyRequested)
			{
				continue;
			}

			IInstallBundleSource::FRequestReleaseContentBundleContext RemoveContext;
			RemoveContext.BundleName = Pair.Key;
			RemoveContext.Flags |= EInstallBundleReleaseRequestFlags::RemoveFilesIfPossible;
			RemoveContext.LogVerbosityOverride = Requestor->GetLogVerbosityOverride();
			RemoveContext.CompleteCallback.BindRaw(this, &FDefaultInstallBundleManager::CacheEvictionComplete);

			Source->RequestReleaseContent(MoveTemp(RemoveContext));
		}
	}
}

void FDefaultInstallBundleManager::CacheEvictionComplete(TSharedRef<IInstallBundleSource> Source, FInstallBundleSourceReleaseContentResultInfo InResultInfo)
{
	const TSharedRef<FInstallBundleCache>& BundleCache = BundleCaches.FindChecked(BundleSourceCaches.FindChecked(Source->GetSourceType()));

	TOptional<FInstallBundleCacheBundleInfo> CacheInfo = BundleCache->GetBundleInfo(Source->GetSourceType(), InResultInfo.BundleName);
	check(CacheInfo);
	FDateTime TimeStampBeforeEviction = CacheInfo->TimeStamp;

	if (InResultInfo.Result == EInstallBundleReleaseResult::OK)
	{
		// Evicted bundles are no longer installed because at least one of their sources is completely uninstalled although
		// they may still have data in uncached sources
		FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(InResultInfo.BundleName);
		checkf(GetBundleStatus(BundleInfo) != EBundleState::Mounted, TEXT("Mounted Install Bundles should never be evicted!"));
		SetBundleStatus(BundleInfo, EBundleState::NotInstalled);

		// Update current size in cache size if eviction succeeded
		CacheInfo->CurrentInstallSize = 0;
		CacheInfo->InstallOverheadSize = 0; // Current contract is that overhead only exists for content that may be patched
		CacheInfo->TimeStamp = FDateTime::MinValue();
		BundleCache->AddOrUpdateBundle(Source->GetSourceType(), *CacheInfo);
	}

	// Check to clear PendingEvict status
	auto CacheEvictKey = MakeTuple(BundleCache->GetName(), InResultInfo.BundleName);
	TArray<FInstallBundleSourceType> SourcesForCache = CachesPendingEvictToSources.FindChecked(CacheEvictKey);
	SourcesForCache.RemoveSwap(Source->GetSourceType(), EAllowShrinking::No);
	if (SourcesForCache.Num() == 0)
	{
		CachesPendingEvictToSources.Remove(CacheEvictKey);

		// Clear PendingEvict and set back to released state
		verify(BundleCache->ClearPendingEvict(InResultInfo.BundleName));
	}

	// Update Pending Evictions
	auto SourceEvictionKey = MakeTuple(Source->GetSourceType(), InResultInfo.BundleName);
	TArray<FCacheEvictionRequestorRef> CacheEvictionRequestors = PendingCacheEvictions.FindAndRemoveChecked(SourceEvictionKey);

	// Logs / Analytics
	bool bHasContentRequest = Algo::AnyOf(CacheEvictionRequestors, [](const FCacheEvictionRequestorRef& Requestor) -> bool
		{
			return Requestor->GetEvictionRequestorType() == ECacheEvictionRequestorType::ContentRequest;
		});

	if (bHasContentRequest)
	{
		LOG_INSTALL_BUNDLE_MAN(Verbose, TEXT("Evicted Bundle %s with result %s from %s. TimeStamp: %s"),
			*InResultInfo.BundleName.ToString(), LexToString(InResultInfo.Result), LexToString(Source->GetSourceType()), *TimeStampBeforeEviction.ToString());

		InstallBundleManagerAnalytics::FireEvent_BundleEvictedFromCache(AnalyticsProvider.Get(),
			InResultInfo.BundleName.ToString(),
			LexToString(Source->GetSourceType()),
			TimeStampBeforeEviction,
			LexToString(InResultInfo.Result));
	}
	else
	{
		// Assume cache flush, don't send analytics
		LOG_INSTALL_BUNDLE_MAN(Verbose, TEXT("Flushed Bundle %s with result %s from %s."),
			*InResultInfo.BundleName.ToString(), LexToString(InResultInfo.Result), LexToString(Source->GetSourceType()));
	}

	// Notify Requestors last since calling TryReserveCache will modify PendingCacheEvictions and CachesPendingEvictToSources
	for (FCacheEvictionRequestorRef& Requestor : CacheEvictionRequestors)
	{
		CacheEvictionComplete(Source, InResultInfo, MoveTemp(Requestor));
	}
}

void FDefaultInstallBundleManager::CacheEvictionComplete(TSharedRef<IInstallBundleSource> Source, const FInstallBundleSourceReleaseContentResultInfo& InResultInfo, FCacheEvictionRequestorRef Requestor)
{
	TArray<FInstallBundleSourceType>& EvictFromSources = Requestor->BundlesToEvictFromSourcesMap.FindChecked(InResultInfo.BundleName);
	EvictFromSources.RemoveSwap(Source->GetSourceType(), EAllowShrinking::No);
	if (EvictFromSources.Num() == 0)
	{
		Requestor->BundlesToEvictFromSourcesMap.Remove(InResultInfo.BundleName);
	}

	if (Requestor->BundlesToEvictFromSourcesMap.Num() > 0)
	{
		return;
	}

	LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Requestor->GetLogVerbosityOverride(), Display, TEXT("%s done waiting for all cache evictions!"), *Requestor->GetEvictionRequestorName());

	if (Requestor->GetEvictionRequestorType() == ECacheEvictionRequestorType::ContentRequest)
	{
		FContentRequestRef ContentRequest = StaticCastSharedRef<FContentRequest>(Requestor);
		if (ContentRequest->bIsCanceled) // If canceled don't keep trying to reserve, just finish so we can release any reserves that succeeded ASAP
		{
			StatsEnd(ContentRequest->BundleName, EContentRequestState::ReservingCache);
			ContentRequest->StepResult = EContentRequestStepResult::Done;
		}
		else
		{
			// Retry reserve - We can't just assume we have enough space now because
			// we don't what happened with other bundles and caches while we were waiting on eviction.
			TryReserveCache(ContentRequest);
		}
	}
	else if (Requestor->GetEvictionRequestorType() == ECacheEvictionRequestorType::CacheFlush)
	{
		FCacheFlushRequestRef FlushRequest = StaticCastSharedRef<FCacheFlushRequest>(Requestor);
		FlushRequest->Callback.ExecuteIfBound();
	}
}

TArray<TSharedPtr<IInstallBundleSource>> FDefaultInstallBundleManager::GetEnabledBundleSourcesForRequest(FContentRequestRef Request) const
{
	const FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(Request->BundleName);
	return GetEnabledBundleSourcesForRequest(BundleInfo);
}

TArray<TSharedPtr<IInstallBundleSource>> FDefaultInstallBundleManager::GetEnabledBundleSourcesForRequest(const FBundleInfo& BundleInfo) const
{
	TArray<TSharedPtr<IInstallBundleSource>> EnabledSources;
	EnabledSources.Reserve(BundleInfo.ContributingSources.Num());
	for (const FBundleSourceRelevance& SourceRel : BundleInfo.ContributingSources)
	{
		EnabledSources.Add(BundleSources.FindChecked(SourceRel.SourceType));
	}
	return EnabledSources;
}

void FDefaultInstallBundleManager::UpdateBundleSources(FContentRequestRef Request)
{
	StatsBegin(Request->BundleName, EContentRequestState::UpdatingBundleSources);

	Request->StepResult = EContentRequestStepResult::Waiting;
	TArray<TSharedPtr<IInstallBundleSource>> EnabledBundleSources = GetEnabledBundleSourcesForRequest(Request);
	Request->RequiredSourceRequestResultsCount = EnabledBundleSources.Num();
	for (const TSharedPtr<IInstallBundleSource>& Source : EnabledBundleSources)
	{
		Request->SourcePauseFlags.Emplace(Source->GetSourceType(), EInstallBundlePauseFlags::None);

		IInstallBundleSource::FRequestUpdateContentBundleContext Context;
		Context.BundleName = Request->BundleName;
		Context.Flags = Request->Flags;
		Context.LogVerbosityOverride = Request->LogVerbosityOverride;
		Context.PausedCallback.BindRaw(this, &FDefaultInstallBundleManager::UpdateBundleSourcePause, Request);
		Context.CompleteCallback.BindRaw(this, &FDefaultInstallBundleManager::UpdateBundleSourceComplete, Request);
		Context.RequestSharedContext = Request->RequestSharedContext;

		Source->RequestUpdateContent(MoveTemp(Context));
	}

	// Release shared context here.  We want to free it ASAP so we aren't pinning items added by
	// bundle sources.  Some bundle sources depended on cleanup happening at specific times.
	Request->RequestSharedContext = nullptr;
}

void FDefaultInstallBundleManager::UpdateBundleSourceComplete(TSharedRef<IInstallBundleSource> Source, FInstallBundleSourceUpdateContentResultInfo InResultInfo, FContentRequestRef Request)
{
	FInstallBundleSourceType SourceType = Source->GetSourceType();

	Request->SourcePauseFlags.Remove(SourceType);

	Request->SourceRequestResults.Emplace(SourceType, MoveTemp(InResultInfo));

	// Update cached source progress.  We will need it to combine with progress from any bundle sources that are not yet finished.
	TOptional<FInstallBundleSourceProgress> Progress = BundleSources.FindChecked(SourceType)->GetBundleProgress(Request->BundleName);
	if (Progress)
	{
		Request->CachedSourceProgress.Emplace(SourceType, MoveTemp(*Progress));
	}

	if (Request->SourceRequestResults.Num() != Request->RequiredSourceRequestResultsCount)
		return;

	LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Bundle %s done waiting for all bundle sources!"), *Request->BundleName.ToString());

	FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(Request->BundleName);
	BundleInfo.ContentPaths = {};

	for (TPair<FInstallBundleSourceType, FInstallBundleSourceUpdateContentResultInfo>& Pair : Request->SourceRequestResults)
	{
		FInstallBundleSourceUpdateContentResultInfo& ResultInfo = Pair.Value;

		//Update cache sizes
		if (FName* CacheName = BundleSourceCaches.Find(Pair.Key))
		{
			const TSharedRef<FInstallBundleCache>& BundleCache = BundleCaches.FindChecked(*CacheName);

			if (TOptional<FInstallBundleCacheBundleInfo> CacheBundleInfo = BundleCache->GetBundleInfo(Pair.Key, Request->BundleName))
			{
				if (ResultInfo.Result == EInstallBundleResult::OK)
				{
					// Cache Analytics
					// Only, send on success - if the request were canceled or something, there is no way to tell if the source got far enough
					// to tell us if we had a cache hit.  We could just go by sizes in the cache, but that will
					// be wrong in the case that we need to patch but the size stayed the same.
					const bool bCacheHit = !ResultInfo.bContentWasInstalled;
					if (bCacheHit)
					{
						InstallBundleManagerAnalytics::FireEvent_BundleCacheHit(AnalyticsProvider.Get(),
							BundleInfo.BundleNameString, Pair.Key.GetNameStr());
					}
					else
					{
						// Any Data -> Any Data	- Patch
						// Any Data -> 0		- Patch
						// 0 -> 0				- Patch
						// 0 -> Any Data		- No Patch
						const bool bWasPatchRequired = CacheBundleInfo->CurrentInstallSize > 0 || ResultInfo.CurrentInstallSize == 0;
						InstallBundleManagerAnalytics::FireEvent_BundleCacheMiss(AnalyticsProvider.Get(),
							BundleInfo.BundleNameString, Pair.Key.GetNameStr(), bWasPatchRequired);
					}

					// Since the update succeeded, we know that there is no longer any install overhead
					CacheBundleInfo->InstallOverheadSize = 0;
				}

				CacheBundleInfo->CurrentInstallSize = ResultInfo.CurrentInstallSize;
				CacheBundleInfo->TimeStamp = ResultInfo.LastAccessTime;

				// If the request doesn't finish successfully, its possible that CurrentInstallSize could be > FullInstallSize
				// because data from a previous install could be larger than the current patch.
				check(ResultInfo.Result != EInstallBundleResult::OK || CacheBundleInfo->CurrentInstallSize == CacheBundleInfo->FullInstallSize); //Sanity
				BundleCache->AddOrUpdateBundle(Pair.Key, *CacheBundleInfo);
			}
		}

		if (Request->Result == EInstallBundleResult::OK && ResultInfo.Result != EInstallBundleResult::OK)
		{
			LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Bundle %s finished bundle sources with error %s"), *Request->BundleName.ToString(), LexToString(ResultInfo.Result));

			Request->Result = ResultInfo.Result;
			Request->OptionalErrorText = MoveTemp(ResultInfo.OptionalErrorText);
			Request->OptionalErrorCode = MoveTemp(ResultInfo.OptionalErrorCode);
		}
		else if (Request->Result == EInstallBundleResult::OK)
		{
			for (FString& ContentPath : ResultInfo.ContentPaths)
			{
				// We only need to keep track of the pak paths for unmounting so 
				// lets save a little memory
				if (ContentPath.EndsWith(TEXTVIEW(".pak")))
				{
					BundleInfo.ContentPaths.ContentPaths.Emplace(MoveTemp(ContentPath), ResultInfo.MountOptions);
				}
			}

			Request->OnDemandMountArgs.Append(MoveTemp(ResultInfo.OnDemandMountArgs));

			BundleInfo.ContentPaths.AdditionalRootDirs.Append(MoveTemp(ResultInfo.AdditionalRootDirs));
			BundleInfo.ContentPaths.ProjectName = ResultInfo.ProjectName;
		}

		if (ResultInfo.bContentWasInstalled)
		{
			Request->bContentWasInstalled = true;
		}
	}
	Request->SourceRequestResults.Empty();

	// Sort in descending order for determinism
	BundleInfo.ContentPaths.ContentPaths.Sort([](const TPair<FString, FPakMountOptions>& A, const TPair<FString, FPakMountOptions>& B)
	{
		return A.Key > B.Key;
	});

	ensureMsgf(!Request->OnDemandMountArgs.IsEmpty() == BundleInfo.bContainsIoStoreOnDemandTocs,
		TEXT("OnDemandMountArgs should only be provided by bundle sources that set bContainsIoStoreOnDemandTocs"));

	// If there are no content paths, its likely this is a chunk that doesn't exist on the current platfrom, so set bContainsChunks true.
	// This is a corner case but as far as I know there is no other situation that would allow for an empty bundle that was not chunked.
	// If such a case were to arise, then bContainsChunks would need to be determined by each bundle source individually.
	BundleInfo.ContentPaths.bContainsChunks = BundleInfo.ContentPaths.ContentPaths.IsEmpty();
	for (const TPair<FString, FPakMountOptions>& Pair : BundleInfo.ContentPaths.ContentPaths)
	{
		const FString& ContentPath = Pair.Key;
		BundleInfo.ContentPaths.bContainsChunks = 
			ContentPath.EndsWith(TEXTVIEW(".pak")) &&
			FPlatformMisc::GetPakchunkIndexFromPakFile(ContentPath) != INDEX_NONE;
		if (BundleInfo.ContentPaths.bContainsChunks)
		{
			break;
		}
	}

	if (StateSignifiesNeedsInstall(GetBundleStatus(BundleInfo)))
	{
		if (Request->Result == EInstallBundleResult::OK)
		{
			SetBundleStatus(BundleInfo, EBundleState::NeedsMount);
		}
	}

	Request->StepResult = EContentRequestStepResult::Done;

	StatsEnd(Request->BundleName, EContentRequestState::UpdatingBundleSources);

	PersistentStats->UpdateForBundleSource(InResultInfo, SourceType, Request->BundleName.ToString());
}

void FDefaultInstallBundleManager::UpdateBundleSourcePause(TSharedRef<IInstallBundleSource> Source, FInstallBundleSourcePauseInfo InPauseInfo, FContentRequestRef Request)
{
	FInstallBundleSourceType SourceType = Source->GetSourceType();

	Request->SourcePauseFlags.FindChecked(SourceType) = InPauseInfo.PauseFlags;
	if (InPauseInfo.bDidPauseChange)
	{
		Request->bForcePauseCallback = true;
	}
}

void FDefaultInstallBundleManager::UpdateBundleSources(FContentReleaseRequestRef Request)
{
	if (Request->bIsCanceled)
	{
		Request->StepResult = EContentRequestStepResult::Done;
		return;
	}

	if (EnumHasAnyFlags(Request->Flags, EInstallBundleReleaseRequestFlags::SkipReleaseUnmountOnly))
	{
		LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Skipping Updated Sources for Release Request %s"), *Request->BundleName.ToString());
		Request->StepResult = EContentRequestStepResult::Done;
		return;
	}

	FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(Request->BundleName);

	// Only unmount on demand tocs if this is a full release
	if (BundleInfo.bMountedOnDemandTocs)
	{
		check(OnDemandIoStore);
		const FIoStatus UnmountStatus = OnDemandIoStore->Unmount(Request->BundleName.ToString());
		if (!UnmountStatus.IsOk())
		{
			LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Error, TEXT("Error unmounting ondemand toc for bundle '%s' : %s"), *BundleInfo.BundleNameString, *UnmountStatus.ToString());

			// Use as a catch-all for something going wrong with IOStore, might want to add another code for this
			Request->Result = EInstallBundleReleaseResult::ManifestArchiveError;
			Request->StepResult = EContentRequestStepResult::Done;
			return;
		}

		BundleInfo.bMountedOnDemandTocs = false;
	}

	// Release from any caches that were reserved
	for (const TPair<FName, TSharedRef<FInstallBundleCache>>& Pair : BundleCaches)
	{
		Pair.Value->Release(Request->BundleName);
	}

	for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
	{
		if (const FName* CacheName = BundleSourceCaches.Find(Pair.Key))
		{
			if (BundleCaches.FindChecked(*CacheName)->GetBundleInfo(Pair.Key, Request->BundleName))
			{
				Request->SourceReleaseRequestResults.Emplace(Pair.Key);
				continue; // Don't try to remove if the bundle is in this source's cache
			}
		}

		if (EnumHasAnyFlags(Request->Flags, EInstallBundleReleaseRequestFlags::RemoveFilesIfPossible))
		{
			Request->SourceRemoveRequestResults.Emplace(Pair.Key);
		}
		else
		{
			Request->SourceReleaseRequestResults.Emplace(Pair.Key);
		}
	}

	check(Request->SourceReleaseRequestResults.Num() > 0 || Request->SourceRemoveRequestResults.Num() > 0);

	Request->StepResult = EContentRequestStepResult::Waiting;

	for (const TPair<FInstallBundleSourceType, TOptional<FInstallBundleSourceReleaseContentResultInfo>>& Pair : Request->SourceRemoveRequestResults)
	{
		const TSharedPtr<IInstallBundleSource>& BundleSource = BundleSources.FindChecked(Pair.Key);

		IInstallBundleSource::FRequestReleaseContentBundleContext Context;
		Context.BundleName = Request->BundleName;
		Context.Flags = Request->Flags;
		Context.LogVerbosityOverride = Request->LogVerbosityOverride;
		Context.CompleteCallback.BindRaw(this, &FDefaultInstallBundleManager::UpdateBundleSourceReleaseComplete, Request);

		BundleSource->RequestReleaseContent(MoveTemp(Context));
	}

	for (const TPair<FInstallBundleSourceType, TOptional<FInstallBundleSourceReleaseContentResultInfo>>& Pair : Request->SourceReleaseRequestResults)
	{
		const TSharedPtr<IInstallBundleSource>& BundleSource = BundleSources.FindChecked(Pair.Key);

		IInstallBundleSource::FRequestReleaseContentBundleContext Context;
		Context.BundleName = Request->BundleName;
		Context.Flags = (Request->Flags & ~EInstallBundleReleaseRequestFlags::RemoveFilesIfPossible);
		Context.LogVerbosityOverride = Request->LogVerbosityOverride;
		Context.CompleteCallback.BindRaw(this, &FDefaultInstallBundleManager::UpdateBundleSourceReleaseComplete, Request);

		BundleSource->RequestReleaseContent(MoveTemp(Context));
	}
}

void FDefaultInstallBundleManager::UpdateBundleSourceReleaseComplete(TSharedRef<IInstallBundleSource> Source, FInstallBundleSourceReleaseContentResultInfo InResultInfo, FContentReleaseRequestRef Request)
{
	{
		TOptional<FInstallBundleSourceReleaseContentResultInfo>* Result = Request->SourceRemoveRequestResults.Find(Source->GetSourceType());
		if (Result == nullptr)
		{
			Result = Request->SourceReleaseRequestResults.Find(Source->GetSourceType());
		}

		check(Result);
		Result->Emplace(InResultInfo);
	}

	for (const TPair<FInstallBundleSourceType, TOptional<FInstallBundleSourceReleaseContentResultInfo>>& Pair : Request->SourceRemoveRequestResults)
	{
		if (!Pair.Value)
		{
			return;
		}
	}

	for (const TPair<FInstallBundleSourceType, TOptional<FInstallBundleSourceReleaseContentResultInfo>>& Pair : Request->SourceReleaseRequestResults)
	{
		if (!Pair.Value)
		{
			return;
		}
	}

	bool bContentWasRemoved = false;
	for (const TPair<FInstallBundleSourceType, TOptional<FInstallBundleSourceReleaseContentResultInfo>>& Pair : Request->SourceRemoveRequestResults)
	{
		const FInstallBundleSourceReleaseContentResultInfo& ResultInfo = *Pair.Value;
		if (Request->Result == EInstallBundleReleaseResult::OK && ResultInfo.Result != EInstallBundleReleaseResult::OK)
		{
			Request->Result = ResultInfo.Result;
		}

		if (ResultInfo.bContentWasRemoved)
		{
			bContentWasRemoved = true;
		}
	}

	for (const TPair<FInstallBundleSourceType, TOptional<FInstallBundleSourceReleaseContentResultInfo>>& Pair : Request->SourceReleaseRequestResults)
	{
		const FInstallBundleSourceReleaseContentResultInfo& ResultInfo = *Pair.Value;
		if (Request->Result == EInstallBundleReleaseResult::OK && ResultInfo.Result != EInstallBundleReleaseResult::OK)
		{
			Request->Result = ResultInfo.Result;
		}

		if (ResultInfo.bContentWasRemoved)
		{
			// Removing content wasn't requested, but the source did it anyway so deal with it
			bContentWasRemoved = true;
		}

		// Update last access times in any caches this bundle participates in
		if (const FName* CacheName = BundleSourceCaches.Find(Pair.Key))
		{
			const TSharedRef<FInstallBundleCache>& BundleCache = BundleCaches.FindChecked(*CacheName);
			if (TOptional<FInstallBundleCacheBundleInfo> CacheInfo = BundleCache->GetBundleInfo(Pair.Key, Request->BundleName))
			{
				CacheInfo->TimeStamp = ResultInfo.LastAccessTime;
				BundleCache->AddOrUpdateBundle(Pair.Key, *CacheInfo);
			}
		}
	}

	FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(Request->BundleName);

	LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Release of Bundle %s done waiting for all bundle sources!"), *BundleInfo.BundleNameString);

	if (bContentWasRemoved)
	{
		// The Bundle is no longer installed because at least one of its sources is not completely installed although
		// it may still have data in cached sources
		SetBundleStatus(BundleInfo, EBundleState::NotInstalled);
	}

	Request->StepResult = EContentRequestStepResult::Done;
}

void FDefaultInstallBundleManager::TickAsyncMountTasks()
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, InstallBundleManager_TickAsyncMountTasks);

	InstallBundleUtil::FinishInstallBundleAsyncIOTasks(AsyncMountTasks);
}

void FDefaultInstallBundleManager::MountPaks(FContentRequestRef Request)
{
	check(!Request->bIsCanceled && Request->Result == EInstallBundleResult::OK);

	if (EnumHasAnyFlags(Request->Flags, EInstallBundleRequestFlags::SkipMount) && Request->OnDemandMountArgs.IsEmpty())
	{
		LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Skipping Mounting Paks for Request %s"), *Request->BundleName.ToString());

		Request->StepResult = EContentRequestStepResult::Done;
		return;
	}

	SCOPED_BOOT_TIMING("FDefaultInstallBundleManager::MountPaks");
	TRACE_BOOKMARK(TEXT("Start Mount Bundle %s"), *Request->BundleName.ToString());

	FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(Request->BundleName);

	// Set waiting flag in case aysnc work happens
	Request->StepResult = EContentRequestStepResult::Waiting;

	auto MountPaksWorkFunc =
		[ContentPaths = GetPakMountList(Request, BundleInfo)
		, LogVerbosityOverride = Request->LogVerbosityOverride](TValueOrError<void, UE::UnifiedError::FError>& OutResult) mutable -> bool
	{
		return MountPaksInList(ContentPaths, OutResult, LogVerbosityOverride);
	};

	auto OnPakMountCompleteFunc = [this, WeakThis = AsWeak(), Request](bool bMountedPaks, const TValueOrError<void, UE::UnifiedError::FError>& MountResult) mutable
	{
		TSharedPtr<IInstallBundleManager> PinnedThis = WeakThis.Pin();
		if (!PinnedThis)
		{
			return;
		}

		FBundleInfo& BundleInfoLambda = BundleInfoMap.FindChecked(Request->BundleName);

		if (bMountedPaks)
		{
			FCoreDelegates::OnOptimizeMemoryUsageForMountedPaks.Execute();
		}

		if (!MountResult.HasValue())
		{
			// TODO: Improve this with FError - currently the error code is lost
			Request->Result = EInstallBundleResult::InstallError;
			Request->OptionalErrorText = MountResult.GetError().GetErrorMessage(); 
			Request->StepResult = EContentRequestStepResult::Done;

			StatsEnd(Request->BundleName, EContentRequestState::Mounting);
			TRACE_BOOKMARK(TEXT("Finished Mount Bundle %s"), *Request->BundleName.ToString());
			return;
		}

		for (const FString& RootDir : BundleInfoLambda.ContentPaths.AdditionalRootDirs)
		{
			FPlatformMisc::AddAdditionalRootDirectory(RootDir);
		}

		OnPaksMountedInternal(Request, BundleInfoLambda);

		// Update bundle status
		SetBundleStatus(BundleInfoLambda, EBundleState::Mounted);

		StatsEnd(Request->BundleName, EContentRequestState::Mounting);
		TRACE_BOOKMARK(TEXT("Finished Mount Bundle %s"), *Request->BundleName.ToString());
		Request->StepResult = EContentRequestStepResult::Done;
	};

	const bool bIsAsyncMount = EnumHasAnyFlags(Request->Flags, EInstallBundleRequestFlags::AsyncMount);
	TUniqueFunction<void()> StartMountPaksFunc = 
		[MountPaksWorkFunc = MoveTemp(MountPaksWorkFunc)
		, OnPakMountCompleteFunc = MoveTemp(OnPakMountCompleteFunc)
		, bIsAsyncMount]() mutable
	{
		if (!bIsAsyncMount)
		{
			TValueOrError<void, UE::UnifiedError::FError> MountResult = MakeValue();
			const bool bMountedPaks = MountPaksWorkFunc(MountResult);
			OnPakMountCompleteFunc(bMountedPaks, MountResult);
		}
		else
		{
			InstallBundleUtil::StartInstallBundleAsyncIOTask(
				[MountPaksWorkFunc = MoveTemp(MountPaksWorkFunc)
				, OnPakMountCompleteFunc = MoveTemp(OnPakMountCompleteFunc)]() mutable
			{
				TValueOrError<void, UE::UnifiedError::FError> MountResult = MakeValue();
				const bool bMountedPaks = MountPaksWorkFunc(MountResult);
				ExecuteOnGameThread(UE_SOURCE_LOCATION, 
					[OnPakMountCompleteFunc = MoveTemp(OnPakMountCompleteFunc)
					, MountResult = MoveTemp(MountResult)
					, bMountedPaks]() mutable
				{
					OnPakMountCompleteFunc(bMountedPaks, MountResult);
				});
			});
		}
	};

	/*
	* 1 - Optionally, mount On Demand IOStore Tocs, this is always async
	* 2 - Mount paks, respect EInstallBundleRequestFlags::AsyncMount
	*/

	if (Request->OnDemandMountArgs.IsEmpty() || BundleInfo.bMountedOnDemandTocs || !AllowIoStoreOnDemandMount(Request, BundleInfo))
	{
		LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Mounting Paks for Request %s"), *BundleInfo.BundleNameString);
		StatsBegin(Request->BundleName, EContentRequestState::Mounting);
		StartMountPaksFunc();
		return;
	}

	if (!OnDemandIoStore)
	{
		OnDemandIoStore = UE::IoStore::TryGetOnDemandIoStore();
		if (!OnDemandIoStore)
		{
			Request->Result = EInstallBundleResult::InitializationError;
			Request->OptionalErrorCode = TEXT("IoStoreOnDemand_Not_Found");
			Request->StepResult = EContentRequestStepResult::Done;
			return;
		}
	}

	LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Mounting OnDemand Tocs for Request %s"), *BundleInfo.BundleNameString);
	StatsBegin(Request->BundleName, EContentRequestState::Mounting);

	struct FOnDemandMountContext
	{
		FContentRequestPtr Request;
		TArray<UE::IoStore::FOnDemandMountResult> Results;
		TUniqueFunction<void()> StartMountPaksFunc;
		TWeakPtr<FDefaultInstallBundleManager> WeakThis;

		~FOnDemandMountContext()
		{
			ExecuteOnGameThread(UE_SOURCE_LOCATION, 
			[Request = Request
			, StartMountPaksFunc = MoveTemp(StartMountPaksFunc)
			, Results = MoveTemp(Results)
			, WeakThis = MoveTemp(WeakThis)]
			{
				TSharedPtr<FDefaultInstallBundleManager> PinnedThis = WeakThis.Pin();
				if (!PinnedThis)
				{
					return;
				}

				for (const UE::IoStore::FOnDemandMountResult& MountResult : Results)
				{
					if (!MountResult.Status.IsOk() && MountResult.Status.GetErrorCode() != EIoErrorCode::PendingEncryptionKey)
					{
						FString StatusString = MountResult.Status.ToString();

						LOG_INSTALL_BUNDLE_MAN(Error, TEXT("Error mounting on demand toc for '%s' : %s"), *Request->BundleName.ToString(), *StatusString);

						FString ErrorCode = GetIoErrorText(MountResult.Status.GetErrorCode());
						ErrorCode.ReplaceCharInline(TEXT(' '), TEXT('_'), ESearchCase::CaseSensitive);

						PinnedThis->StatsEnd(Request->BundleName, EContentRequestState::Mounting);
						TRACE_BOOKMARK(TEXT("Finished Mount Bundle %s"), *Request->BundleName.ToString());

						Request->Result = EInstallBundleResult::InstallError;
						Request->OptionalErrorCode = MoveTemp(ErrorCode);
						Request->OptionalErrorText = FText::AsCultureInvariant(MountResult.Status.ToString()); // IOStore errors aren't localized 
						Request->StepResult = EContentRequestStepResult::Done;
						return;
					}
				}

				FBundleInfo& BundleInfo = PinnedThis->BundleInfoMap.FindChecked(Request->BundleName);
				BundleInfo.bMountedOnDemandTocs = true;

				if (EnumHasAnyFlags(Request->Flags, EInstallBundleRequestFlags::SkipMount))
				{
					LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Skipping Mounting Paks for Request %s"), *Request->BundleName.ToString());

					PinnedThis->StatsEnd(Request->BundleName, EContentRequestState::Mounting);
					TRACE_BOOKMARK(TEXT("Finished Mount Bundle %s"), *Request->BundleName.ToString());
					Request->StepResult = EContentRequestStepResult::Done;
					return;
				}

				LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Mounting Paks for Request %s"), *BundleInfo.BundleNameString);
				StartMountPaksFunc();
			});
		}
	};
	TSharedRef<FOnDemandMountContext> OnDemandMountContext = MakeShared<FOnDemandMountContext>();
	OnDemandMountContext->Request = Request;
	OnDemandMountContext->StartMountPaksFunc = MoveTemp(StartMountPaksFunc);
	OnDemandMountContext->WeakThis = StaticCastWeakPtr<FDefaultInstallBundleManager>(AsWeak());

	for (TUniquePtr<UE::IoStore::FOnDemandMountArgs>& BundleMountArgs : Request->OnDemandMountArgs)
	{
		if (BundleMountArgs->MountId.IsEmpty())
		{
			BundleMountArgs->MountId = BundleInfo.BundleNameString;
		}
		else if (!ensureMsgf(BundleMountArgs->MountId == BundleInfo.BundleNameString, TEXT("Expected MountId to match bundle name!")))
		{
			BundleMountArgs->MountId = BundleInfo.BundleNameString;
		}

		BundleMountArgs->Options &= ~UE::IoStore::EOnDemandMountOptions::CallbackOnGameThread;

		UE::IoStore::GetOnDemandIoStore().Mount(MoveTemp(*BundleMountArgs), [OnDemandMountContext](UE::IoStore::FOnDemandMountResult OnDemandMountResult)
		{
			OnDemandMountContext->Results.Add(MoveTemp(OnDemandMountResult));
		});
	}
}

bool FDefaultInstallBundleManager::MountPaksInList(TArrayView<TPair<FString, FPakMountOptions>> Paths, TValueOrError<void, UE::UnifiedError::FError>& OutResult, ELogVerbosity::Type LogVerbosityOverride)
{
	OutResult = MakeValue();

	if (Paths.IsEmpty())
	{
		return false;
	}

	if (!FCoreDelegates::MountPaksEx.IsBound())
	{
		ensureMsgf(false, TEXT("Pak files have not been correctly initalized. Use -UsePaks on the cmdline if you are using the UnrealEditor.exe"));
		return false;
	}

	TArray<UE::FMountPaksExArgs> MountArgs;
	MountArgs.Reserve(Paths.Num());

	for (const TPair<FString, FPakMountOptions>& Pair : Paths)
	{
		MountArgs.Emplace(UE::FMountPaksExArgs
		{
			.PakFilePath = *Pair.Key,
			.MountOptions = Pair.Value
		});
	}

	//May not mount encrypted Paks
	const bool bMountedPaks = FCoreDelegates::MountPaksEx.Execute(MountArgs);

	for (UE::FMountPaksExArgs& Args : MountArgs)
	{
		if (Args.Result.HasError())
		{
			LOG_INSTALL_BUNDLE_MAN_OVERRIDE(LogVerbosityOverride, Error, TEXT("Failed to mount pak %s, error: %s"), Args.PakFilePath, *Args.Result.GetError().GetErrorMessage().ToString());

			if (!OutResult.HasError())
			{
				OutResult = MakeError(Args.Result.StealError());
			}
		}
	}

	return bMountedPaks;
}

void FDefaultInstallBundleManager::UnmountPaks(FContentReleaseRequestRef Request)
{
	if (Request->bIsCanceled)
	{
		Request->StepResult = EContentRequestStepResult::Done;
		return;
	}

	FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(Request->BundleName);

	LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Unmounting Paks for Release Request %s"), *BundleInfo.BundleNameString);

	if (FCoreDelegates::OnUnmountPak.IsBound())
	{
		for (const TPair<FString, FPakMountOptions>& Pair : BundleInfo.ContentPaths.ContentPaths)
		{
			LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Unmounting pak file: %s \n"), *Pair.Key);
			FCoreDelegates::OnUnmountPak.Execute(Pair.Key);
		}
	}

	if (BundleInfo.bMountedOnDemandTocs)
	{
		BundleInfo.bMountedOnDemandTocs = false;
		if (UE::IoStore::IOnDemandIoStore* IoStore = UE::IoStore::TryGetOnDemandIoStore())
		{
			if (FIoStatus Status = IoStore->Unmount(Request->BundleName.ToString()); !Status.IsOk())
			{
				LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Error,
					TEXT("Error unmounting ondemand toc for bundle '%s' : %s"),
					*BundleInfo.BundleNameString, *Status.ToString());
			}
		}
	}

	OnPaksUnmountedInternal(Request, BundleInfo);

	SetBundleStatus(BundleInfo, EBundleState::NeedsMount);

	Request->StepResult = EContentRequestStepResult::Done;
}

bool FDefaultInstallBundleManager::AllowIoStoreOnDemandMount(FContentRequestRef Request, const FBundleInfo& BundleInfo)
{
	return true;
}

TArray<TPair<FString, FPakMountOptions>> FDefaultInstallBundleManager::GetPakMountList(FContentRequestRef Request, const FBundleInfo& BundleInfo)
{
	return BundleInfo.ContentPaths.ContentPaths;
}

void FDefaultInstallBundleManager::WaitForShaderCache(FContentRequestRef Request)
{
	if (EnumHasAnyFlags(Request->Flags, EInstallBundleRequestFlags::SkipMount))
	{
		LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Skipping Waiting for PSO cache for Request %s"), *Request->BundleName.ToString());
		Request->StepResult = EContentRequestStepResult::Done;
		return;
	}

	StatsBegin(Request->BundleName, EContentRequestState::WaitingForShaderCache);
	PersistentTimingStatsBegin(Request, InstallBundleUtil::PersistentStats::ETimingStatNames::PSOTime_Real);

	int32 NumPrecompilesRemaining = FShaderPipelineCache::NumPrecompilesRemaining();

	FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(Request->BundleName);
	if (GetMustWaitForPSOCache(BundleInfo) && NumPrecompilesRemaining > 0)
	{
		LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Waiting for PSO cache for %s"), *BundleInfo.BundleNameString);

		Request->StepResult = EContentRequestStepResult::Waiting;

		// Have to keep everything awake until shader caching finishes
		Request->KeepAwake.Emplace();
		Request->ScreenSaveControl.Emplace();
	}
	else
	{
		LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("No PSO cache to wait on for %s"), *BundleInfo.BundleNameString);

		SetMustWaitForPSOCache(BundleInfo, 0); // Make sure we pass 0 to flag that there are no more shaders to wait on

		StatsEnd(Request->BundleName, EContentRequestState::WaitingForShaderCache);
		PersistentTimingStatsEnd(Request, InstallBundleUtil::PersistentStats::ETimingStatNames::PSOTime_Real);
		Request->StepResult = EContentRequestStepResult::Done;
	}
}

void FDefaultInstallBundleManager::FinishRequest(FContentRequestRef Request)
{
	StatsBegin(Request->BundleName, EContentRequestState::Finishing);

	if (Request->Result == EInstallBundleResult::OK || Request->Result == EInstallBundleResult::UserCancelledError)
	{
		LOG_INSTALL_BUNDLE_MAN(Display, TEXT("Finishing Request %s with Result %s"), *Request->BundleName.ToString(), LexToString(Request->Result));
	}
	else
	{
		LOG_INSTALL_BUNDLE_MAN(Warning, TEXT("Finishing Request %s with Result %s"), *Request->BundleName.ToString(), LexToString(Request->Result));
	}

	if (Request->Result != EInstallBundleResult::OK)
	{
		FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(Request->BundleName);

		if (GetBundleStatus(BundleInfo) != EBundleState::Mounted) // If we cancel late enough then we could be mounted when we get here
		{
			// Release from any caches that were reserved
			for (const TPair<FName, TSharedRef<FInstallBundleCache>>& Pair : BundleCaches)
			{
				Pair.Value->Release(Request->BundleName);
			}
		}
	}

	const bool bCallback = !Request->bIsCanceled || Request->bFinishWhenCanceled;
	if (bCallback)
	{
		const FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(Request->BundleName);

		FInstallBundleRequestResultInfo ResultInfo;
		ResultInfo.BundleName = Request->BundleName;
		ResultInfo.Result = Request->Result;
		ResultInfo.bIsStartup = BundleInfo.bIsStartup;
		ResultInfo.bContentWasInstalled = Request->bContentWasInstalled;
		ResultInfo.bContainsChunks = BundleInfo.ContentPaths.bContainsChunks;
		ResultInfo.bContainsIoStoreOnDemand = BundleInfo.bMountedOnDemandTocs;
		ResultInfo.OptionalErrorText = Request->OptionalErrorText;
		ResultInfo.OptionalErrorCode = Request->OptionalErrorCode;

		InstallBundleCompleteDelegate.Broadcast(MoveTemp(ResultInfo));
	}

	StatsEnd(Request->BundleName, EContentRequestState::Finishing);
	PersistentTimingStatsEnd(Request, InstallBundleUtil::PersistentStats::ETimingStatNames::TotalTime_Real);
	StopBundlePersistentStatTracking(Request);

	Request->StepResult = EContentRequestStepResult::Done;
}

void FDefaultInstallBundleManager::FinishRequest(FContentReleaseRequestRef Request)
{
	const FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(Request->BundleName);

	if (Request->Result == EInstallBundleReleaseResult::OK || Request->Result == EInstallBundleReleaseResult::UserCancelledError)
	{
		LOG_INSTALL_BUNDLE_MAN(Display, TEXT("Finishing Release Request %s with Result %s"), *Request->BundleName.ToString(), LexToString(Request->Result));
	}
	else
	{
		LOG_INSTALL_BUNDLE_MAN(Warning, TEXT("Finishing Release Request %s with Result %s"), *Request->BundleName.ToString(), LexToString(Request->Result));
	}

	const bool bCallback = !Request->bIsCanceled || Request->bFinishWhenCanceled;
	if (bCallback)
	{
		FInstallBundleReleaseRequestResultInfo ResultInfo;
		ResultInfo.BundleName = Request->BundleName;
		ResultInfo.Result = Request->Result;

		ReleasedDelegate.Broadcast(ResultInfo);
	}

	Request->StepResult = EContentRequestStepResult::Done;
}

void FDefaultInstallBundleManager::TickUpdatePrereqs()
{
	for (FContentRequestRef& Request : ContentRequests[EContentRequestBatch::Requested])
	{
		if (!Request->Prereqs.IsValidIndex(Request->iPrereq))
			continue;

		EBundlePrereqs Prereq = Request->Prereqs[Request->iPrereq];
		if (Request->StepResult != EContentRequestStepResult::Waiting)
			continue;

		// These Prereqs need to be unconditionally ticked to see when they are done
		if (Prereq == EBundlePrereqs::HasNoPendingCancels)
		{
			CheckPrereqHasNoPendingCancels(Request);
		}
		else if (Prereq == EBundlePrereqs::HasNoPendingReleaseRequests)
		{
			CheckPrereqHasNoPendingReleaseRequests(Request);
		}
	}
}

void FDefaultInstallBundleManager::TickReleasePrereqs()
{
	for (FContentReleaseRequestRef& Request : ContentReleaseRequests[EContentReleaseRequestBatch::Requested])
	{
		if (!Request->Prereqs.IsValidIndex(Request->iPrereq))
			continue;

		EBundlePrereqs Prereq = Request->Prereqs[Request->iPrereq];
		if (Request->StepResult != EContentRequestStepResult::Waiting)
			continue;

		// These Prereqs need to be unconditionally ticked to see when they are done
		if (Prereq == EBundlePrereqs::HasNoPendingCancels)
		{
			CheckPrereqHasNoPendingCancels(Request);
		}
		else if (Prereq == EBundlePrereqs::HasNoPendingUpdateRequests)
		{
			CheckPrereqHasNoPendingUpdateRequests(Request);
		}
	}
}

void FDefaultInstallBundleManager::TickContentRequests()
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, InstallBundleManager_TickContentRequests);

	// We won't enqueue any requests unless init succeeds
	if (InitState != EInstallBundleManagerInitState::Succeeded)
	{
		for (EContentRequestBatch iBatch : TEnumRange<EContentRequestBatch>())
		{
			check(ContentRequests[iBatch].Num() == 0);
		}

		return;
	}

	// Sorts highest by priority, then most complete
	// This is "reverse" order putting the most urgent and complete bundles at the end of the list
	auto ContentRequestBatchSortPredicate = [this](const FContentRequestRef& A, const FContentRequestRef& B)
	{
		const FBundleInfo& BundleInfoA = BundleInfoMap.FindChecked(A->BundleName);
		const FBundleInfo& BundleInfoB = BundleInfoMap.FindChecked(B->BundleName);

		if (BundleInfoA.Priority == BundleInfoB.Priority)
		{
			bool bAValid = A->Steps.IsValidIndex(A->iStep);
			bool bBValid = B->Steps.IsValidIndex(B->iStep);

			if (bAValid && bBValid)
			{
				EContentRequestState StateA = A->Steps[A->iStep];
				EContentRequestState StateB = B->Steps[B->iStep];
				return StateA < StateB;
			}
			else
			{
				return !bAValid && bBValid;
			}
		}

		return BundleInfoA.Priority > BundleInfoB.Priority;
	};

	TickUpdatePrereqs();

	for (FContentRequestRef& Request : ContentRequests[EContentRequestBatch::Requested])
	{
		while (Request->StepResult == EContentRequestStepResult::Done)
		{
			if (Request->iPrereq == INDEX_NONE)
			{
				StatsBegin(Request->BundleName);
				LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Starting Request %s"), *Request->BundleName.ToString());
			}

			// Must satisfy all Prereqs before beginning update
			check(Request->iPrereq < Request->Prereqs.Num() - 1);
			++Request->iPrereq;
			EBundlePrereqs Prereq = Request->Prereqs[Request->iPrereq];
			switch (Prereq)
			{
			case EBundlePrereqs::CacheHintRequested:
				CacheHintRequested(Request, true);
				break;
			case EBundlePrereqs::RequiresLatestClient:
				CheckPrereqLatestClient(Request);
				break;
			case EBundlePrereqs::HasNoPendingCancels:
				CheckPrereqHasNoPendingCancels(Request);
				break;
			case EBundlePrereqs::HasNoPendingReleaseRequests:
				CheckPrereqHasNoPendingReleaseRequests(Request);
				break;
			case EBundlePrereqs::DetermineSteps:
				DetermineSteps(Request);

				Request->StepResult = EContentRequestStepResult::Waiting;
				break;
			default:
				LOG_INSTALL_BUNDLE_MAN(Fatal, TEXT("Unknown or Invalid Bundle Prereq %s"), LexToString(Prereq));
				break;
			}
		}
	}

	// Prereqs should be completed and bundles dispatched to cache/install in priority order
	ContentRequests[EContentRequestBatch::Requested].Sort(ContentRequestBatchSortPredicate);
	while (ContentRequests[EContentRequestBatch::Requested].Num() > 0)
	{
		FContentRequestRef& Request = ContentRequests[EContentRequestBatch::Requested].Last();
		if (!Request->Prereqs.IsValidIndex(Request->iPrereq))
			break;

		EBundlePrereqs Prereq = Request->Prereqs[Request->iPrereq];
		if (Prereq != EBundlePrereqs::DetermineSteps)
			break;

		AddRequestToInitialBatch(Request);

		if (Request->bShouldSendAnalytics)
		{
			InstallBundleManagerAnalytics::FireEvent_BundleRequestStarted(AnalyticsProvider.Get(), Request->BundleName.ToString());
		}

		check(Request->StepResult == EContentRequestStepResult::Waiting);
		Request->StepResult = EContentRequestStepResult::Done;
		ContentRequests[EContentRequestBatch::Requested].RemoveAt(ContentRequests[EContentRequestBatch::Requested].Num() - 1); //Invalidates Request
	}

	for (FContentRequestRef& Request : ContentRequests[EContentRequestBatch::Cache])
	{
		while (Request->StepResult == EContentRequestStepResult::Done)
		{
			check(Request->iStep < Request->Steps.Num() - 1);
			++Request->iStep;
			if (Request->bIsCanceled || Request->Result != EInstallBundleResult::OK)
			{
				if (Request->iStep < Request->iOnCanceledStep[EContentRequestBatch::Cache])
				{
					// Got Canceled Or Errored out, just go to Finish
					Request->iStep = Request->iOnCanceledStep[EContentRequestBatch::Cache];
				}
			}

			EContentRequestState State = Request->Steps[Request->iStep];
			switch (State)
			{
			case EContentRequestState::ReservingCache:
				ReserveCache(Request);
				break;
			case EContentRequestState::FinishingCache:
				Request->StepResult = EContentRequestStepResult::Waiting;
				break;
			default:
				LOG_INSTALL_BUNDLE_MAN(Fatal, TEXT("Unknown Request State for cache %s"), LexToString(State));
				break;
			}
		}
	}

	// Make sure we dispatch to install in priority order
	ContentRequests[EContentRequestBatch::Cache].Sort(ContentRequestBatchSortPredicate);
	while (ContentRequests[EContentRequestBatch::Cache].Num() > 0)
	{
		FContentRequestRef& Request = ContentRequests[EContentRequestBatch::Cache].Last();

		if (!Request->Steps.IsValidIndex(Request->iStep))
			break;

		EContentRequestState State = Request->Steps[Request->iStep];
		if (State != EContentRequestState::FinishingCache)
			break;

		Request->StepResult = EContentRequestStepResult::Done;

		FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(Request->BundleName);
		BundleInfo.bReleaseRequired = true;

		LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Verbose, TEXT("Moving Request %s from batch %s to batch %s"), *Request->BundleName.ToString(), LexToString(EContentRequestBatch::Cache), LexToString(EContentRequestBatch::Install));
		ContentRequests[EContentRequestBatch::Install].Add(MoveTemp(Request));
		ContentRequests[EContentRequestBatch::Cache].RemoveAt(ContentRequests[EContentRequestBatch::Cache].Num() - 1); //Invalidates Request
	}

	// Set an optional maximum end time for processing install requests to ensure we don't hang
	const double ContentInstallEndTime = MaxContentInstallTimePerTickMS <= 0 ? DBL_MAX : FPlatformTime::Seconds() + MaxContentInstallTimePerTickMS / 1000.0;

	for (int i = 0; i < ContentRequests[EContentRequestBatch::Install].Num() && FPlatformTime::Seconds() < ContentInstallEndTime;)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_TickContentRequests_Install);

		bool bRequestComplete = false;
		FContentRequestRef& Request = ContentRequests[EContentRequestBatch::Install][i];
		while (!bRequestComplete && Request->StepResult == EContentRequestStepResult::Done)
		{
			check(Request->iStep < Request->Steps.Num() - 1);
			++Request->iStep;
			if (Request->bIsCanceled || Request->Result != EInstallBundleResult::OK)
			{
				if (Request->iStep < Request->iOnCanceledStep[EContentRequestBatch::Install])
				{
					// Got Canceled Or Errored out, just go to Finish
					Request->iStep = Request->iOnCanceledStep[EContentRequestBatch::Install];
				}
			}

			EContentRequestState State = Request->Steps[Request->iStep];
			switch (State)
			{
			case EContentRequestState::UpdatingBundleSources:
				UpdateBundleSources(Request);
				break;
			case EContentRequestState::Mounting:
				MountPaks(Request);
				break;
			case EContentRequestState::WaitingForShaderCache:
				WaitForShaderCache(Request);
				break;
			case EContentRequestState::Finishing:
				FinishRequest(Request);
				break;
			case EContentRequestState::CleaningUp:
				StatsEnd(Request->BundleName);
				LogStats(Request->BundleName, Request->LogVerbosityOverride);

				CacheHintRequested(Request, false);

				if (Request->bShouldSendAnalytics || Request->Result != EInstallBundleResult::OK)
				{
					InstallBundleManagerAnalytics::FireEvent_BundleRequestComplete(AnalyticsProvider.Get(),
						Request->BundleName.ToString(),
						Request->bContentWasInstalled,
						LexToString(Request->Result),
						StatsMap->GetMap().FindChecked(Request->BundleName));
				}

				LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Removing Request %s"), *Request->BundleName.ToString());
				StatsMap->StatsReset(Request->BundleName);
				ContentRequests[EContentRequestBatch::Install].RemoveAtSwap(i); //Invalidates Request
				bRequestComplete = true;
				break;
			default:
				LOG_INSTALL_BUNDLE_MAN(Fatal, TEXT("Unknown Request State for install %s"), LexToString(State));
				break;
			}
		}

		if (!bRequestComplete)
		{
			++i;
		}
	}
}

void FDefaultInstallBundleManager::DetermineSteps(FContentReleaseRequestRef Request)
{
	check(Request->Steps.Num() == 0);
	check(Request->iStep == INDEX_NONE);

	const FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(Request->BundleName);
	switch (GetBundleStatus(BundleInfo))
	{
	case EBundleState::Mounted:
		Request->Steps.Add(EContentReleaseRequestState::Unmounting);
	case EBundleState::NeedsMount:
	case EBundleState::NeedsUpdate:
		Request->Steps.Add(EContentReleaseRequestState::UpdatingBundleSources);
	case EBundleState::NotInstalled:
		Request->iOnCanceledStep[EContentReleaseRequestBatch::Release] = Request->Steps.Add(EContentReleaseRequestState::Finishing);
		Request->Steps.Add(EContentReleaseRequestState::CleaningUp);
		break;

	default:
		LOG_INSTALL_BUNDLE_MAN(Fatal, TEXT("Unknown Bundle Status %s"), LexToString(GetBundleStatus(BundleInfo)));
		break;
	}

	check(Request->Steps.Num() > 0);
	check(Request->Steps.Last() == EContentReleaseRequestState::CleaningUp);
}

void FDefaultInstallBundleManager::AddRequestToInitialBatch(FContentReleaseRequestRef Request)
{
	const FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(Request->BundleName);

	// This is the only batch to add it to right now
	EContentReleaseRequestBatch InitialBatch = EContentReleaseRequestBatch::Release;

	LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Adding Release Request %s to batch %s"), *BundleInfo.BundleNameString, LexToString(EContentReleaseRequestBatch::Release));
	ContentReleaseRequests[InitialBatch].Emplace(MoveTemp(Request));
}

void FDefaultInstallBundleManager::TickReleaseRequests()
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, InstallBundleManager_TickReleaseRequests);

	// We won't enqueue any requests unless init succeeds
	if (InitState != EInstallBundleManagerInitState::Succeeded)
	{
		for (EContentReleaseRequestBatch iBatch : TEnumRange<EContentReleaseRequestBatch>())
		{
			check(ContentReleaseRequests[iBatch].Num() == 0);
		}

		return;
	}

	TickReleasePrereqs();

	for (int i = 0; i < ContentReleaseRequests[EContentReleaseRequestBatch::Requested].Num();)
	{
		bool bRequestComplete = false;
		const FContentReleaseRequestRef& Request = ContentReleaseRequests[EContentReleaseRequestBatch::Requested][i];
		while (!bRequestComplete && Request->StepResult == EContentRequestStepResult::Done)
		{
			if (Request->iPrereq == INDEX_NONE)
			{
				LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Starting Release Request %s"), *Request->BundleName.ToString());
			}

			// Must satisfy all Prereqs before beginning update
			check(Request->iPrereq < Request->Prereqs.Num() - 1);
			++Request->iPrereq;
			EBundlePrereqs Prereq = Request->Prereqs[Request->iPrereq];
			switch (Prereq)
			{
			case EBundlePrereqs::HasNoPendingCancels:
				CheckPrereqHasNoPendingCancels(Request);
				break;
			case EBundlePrereqs::HasNoPendingUpdateRequests:
				CheckPrereqHasNoPendingUpdateRequests(Request);
				break;
			case EBundlePrereqs::DetermineSteps:
				DetermineSteps(Request);
				AddRequestToInitialBatch(Request);

				InstallBundleManagerAnalytics::FireEvent_BundleReleaseRequestStarted(AnalyticsProvider.Get(),
					Request->BundleName.ToString(),
					EnumHasAnyFlags(Request->Flags, EInstallBundleReleaseRequestFlags::RemoveFilesIfPossible),
					EnumHasAnyFlags(Request->Flags, EInstallBundleReleaseRequestFlags::SkipReleaseUnmountOnly));

				ContentReleaseRequests[EContentReleaseRequestBatch::Requested].RemoveAtSwap(i);
				bRequestComplete = true;
				break;
			default:
				LOG_INSTALL_BUNDLE_MAN(Fatal, TEXT("Unknown or Invalid Bundle Release Prereq %s"), LexToString(Prereq));
				break;
			}
		}

		if (!bRequestComplete)
		{
			++i;
		}
	}

	for (int i = 0; i < ContentReleaseRequests[EContentReleaseRequestBatch::Release].Num();)
	{
		FContentReleaseRequestRef& Request = ContentReleaseRequests[EContentReleaseRequestBatch::Release][i];
		bool bRequestComplete = false;

		while (!bRequestComplete && Request->StepResult == EContentRequestStepResult::Done)
		{
			check(Request->iStep < Request->Steps.Num() - 1);
			++Request->iStep;
			if (Request->bIsCanceled || Request->Result != EInstallBundleReleaseResult::OK)
			{
				if (Request->iStep < Request->iOnCanceledStep[EContentReleaseRequestBatch::Release])
				{
					// Errored out, just go to Finish
					Request->iStep = Request->iOnCanceledStep[EContentReleaseRequestBatch::Release];
				}
			}

			EContentReleaseRequestState State = Request->Steps[Request->iStep];
			switch (State)
			{
			case EContentReleaseRequestState::Unmounting:
				UnmountPaks(Request);
				break;
			case EContentReleaseRequestState::UpdatingBundleSources:
				UpdateBundleSources(Request);
				break;
			case EContentReleaseRequestState::Finishing:
				FinishRequest(Request);
				break;
			case EContentReleaseRequestState::CleaningUp:
				InstallBundleManagerAnalytics::FireEvent_BundleReleaseRequestComplete(AnalyticsProvider.Get(),
					Request->BundleName.ToString(),
					EnumHasAnyFlags(Request->Flags, EInstallBundleReleaseRequestFlags::RemoveFilesIfPossible),
					EnumHasAnyFlags(Request->Flags, EInstallBundleReleaseRequestFlags::SkipReleaseUnmountOnly),
					LexToString(Request->Result));

				LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Removing Release Request %s"), *Request->BundleName.ToString());
				ContentReleaseRequests[EContentReleaseRequestBatch::Release].RemoveAtSwap(i); //Invalidates Request
				bRequestComplete = true;
				break;
			default:
				LOG_INSTALL_BUNDLE_MAN(Fatal, TEXT("Unknown Release Request State for Release %s"), LexToString(State));
				break;
			}
		}

		if (!bRequestComplete)
		{
			++i;
		}
	}
}

void FDefaultInstallBundleManager::TickPruneBundleInfo()
{
	// Unfortunately, there isn't any good way to do this other than on tick because
	// we need to have no requests for a bundles in flight when we prune it.

	for (auto It = BundlesInfosToPrune.CreateIterator(); It; ++It)
	{
		const FName BundleName = *It;
		const FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(BundleName);

		const bool bIsRelevant = Algo::AnyOf(BundleInfo.ContributingSources, &FBundleSourceRelevance::bIsRelevant);
		if (bIsRelevant)
		{
			ensureAlwaysMsgf(false, TEXT("TickPruneBundleInfo - Found relevant bundle in prune list %s"), *BundleInfo.BundleNameString);
			It.RemoveCurrent();
			continue;
		}

		bool bIsRequested = false;
		for (EContentRequestBatch b : TEnumRange<EContentRequestBatch>())
		{
			for (const FContentRequestRef& QueuedRequest : ContentRequests[b])
			{
				if (QueuedRequest->BundleName == BundleName)
				{
					bIsRequested = true;
					break;
				}
			}

			if (bIsRequested)
			{	break; }
		}

		if (bIsRequested)
		{	continue; }

		for (EContentReleaseRequestBatch b : TEnumRange<EContentReleaseRequestBatch>())
		{
			for (const FContentReleaseRequestRef& QueuedRequest : ContentReleaseRequests[b])
			{
				if (QueuedRequest->BundleName == BundleName)
				{
					bIsRequested = true;
					break;
				}
			}

			if (bIsRequested)
			{	break; }
		}

		if (bIsRequested)
		{	continue; }

		for (const FGetContentStateRequestRef& Request : GetContentStateRequests)
		{
			if (Request->BundleNames.Contains(BundleName))
			{
				bIsRequested = true;
				break;
			}
		}

		if (bIsRequested)
		{	continue; }

		for (const FGetInstallStateRequestRef& Request : GetInstallStateRequests)
		{
			if (Request->BundleNames.Contains(BundleName))
			{
				bIsRequested = true;
				break;
			}
		}

		if (bIsRequested)
		{	continue; }

		for (const FBundleSourceRelevance& SourceRelevance : BundleInfo.ContributingSources)
		{
			BundleSources.FindChecked(SourceRelevance.SourceType)->OnBundleInfoPruned(BundleName);
			if (FName* CacheName = BundleSourceCaches.Find(SourceRelevance.SourceType))
			{
				const TSharedRef<FInstallBundleCache>& BundleCache = BundleCaches.FindChecked(*CacheName);
				if (TOptional<FInstallBundleCacheBundleInfo> CacheInfo = BundleCache->GetBundleInfo(SourceRelevance.SourceType, BundleName))
				{
					// Sanity check
					check(CacheInfo->CurrentInstallSize == 0);
				}

				BundleCache->RemoveBundle(SourceRelevance.SourceType, BundleName);
			}
		}

		BundleInfoMap.Remove(BundleName);
		It.RemoveCurrent();
	}
}

void FDefaultInstallBundleManager::IterateContentRequests(TFunctionRef<bool(const FContentRequestRef& QueuedRequest)> OnFound)
{
	bool bContinue = true;
	for (EContentRequestBatch iBatch : TEnumRange<EContentRequestBatch>())
	{
		for (FContentRequestRef& QueuedRequest : ContentRequests[iBatch])
		{
			// Consider the case that we may re-enqueue the same request from its own callback.
			// In that case, don't consider it active.
			int32 iOnCanceledStep = QueuedRequest->iOnCanceledStep[EContentRequestBatch::Install];
			if (iOnCanceledStep != INDEX_NONE && QueuedRequest->iStep >= iOnCanceledStep)
				continue;

			bContinue = OnFound(QueuedRequest);
			if (!bContinue)
				break;
		}

		if (!bContinue)
			break;
	}
}

void FDefaultInstallBundleManager::IterateReleaseRequests(TFunctionRef<bool(const FContentReleaseRequestRef& QueuedRequest)> OnFound)
{
	bool bContinue = true;
	for (EContentReleaseRequestBatch b : TEnumRange<EContentReleaseRequestBatch>())
	{
		for (FContentReleaseRequestRef& QueuedRequest : ContentReleaseRequests[b])
		{
			// Consider the case that we may re-enqueue the same request from its own callback.
			// In that case, don't consider it active.
			int32 iOnCanceledStep = QueuedRequest->iOnCanceledStep[EContentReleaseRequestBatch::Release];
			if (iOnCanceledStep != INDEX_NONE && QueuedRequest->iStep >= iOnCanceledStep)
				continue;

			bContinue = OnFound(QueuedRequest);
			if (!bContinue)
				break;
		}

		if (!bContinue)
			break;
	}
}

void FDefaultInstallBundleManager::IterateContentRequestsForBundle(FName BundleName, TFunctionRef<bool(const FContentRequestRef& QueuedRequest)> OnFound)
{
	bool bContinue = true;
	for (EContentRequestBatch iBatch : TEnumRange<EContentRequestBatch>())
	{
		for (FContentRequestRef& QueuedRequest : ContentRequests[iBatch])
		{
			if (QueuedRequest->BundleName != BundleName)
				continue;

			// Consider the case that we may re-enqueue the same request from its own callback.
			// In that case, don't consider it active.
			int32 iOnCanceledStep = QueuedRequest->iOnCanceledStep[EContentRequestBatch::Install];
			if (iOnCanceledStep != INDEX_NONE && QueuedRequest->iStep >= iOnCanceledStep)
				continue;

			bContinue = OnFound(QueuedRequest);
			if (!bContinue)
				break;
		}

		if (!bContinue)
			break;
	}
}

void FDefaultInstallBundleManager::IterateReleaseRequestsForBundle(FName BundleName, TFunctionRef<bool(const FContentReleaseRequestRef& QueuedRequest)> OnFound)
{
	bool bContinue = true;
	for (EContentReleaseRequestBatch b : TEnumRange<EContentReleaseRequestBatch>())
	{
		for (FContentReleaseRequestRef& QueuedRequest : ContentReleaseRequests[b])
		{
			if (QueuedRequest->BundleName != BundleName)
				continue;

			// Consider the case that we may re-enqueue the same request from its own callback.
			// In that case, don't consider it active.
			int32 iOnCanceledStep = QueuedRequest->iOnCanceledStep[EContentReleaseRequestBatch::Release];
			if (iOnCanceledStep != INDEX_NONE && QueuedRequest->iStep >= iOnCanceledStep)
				continue;

			bContinue = OnFound(QueuedRequest);
			if (!bContinue)
				break;
		}

		if (!bContinue)
			break;
	}
}

void FDefaultInstallBundleManager::TickReserveCache()
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, InstallBundleManager_TickReserveCache);

	for (const FContentRequestRef& Request : ContentRequests[EContentRequestBatch::Cache])
	{
		if (Request->Steps.IsValidIndex(Request->iStep) &&
			Request->Steps[Request->iStep] == EContentRequestState::ReservingCache)
		{
			if (Request->LastCacheReserveResult == EInstallBundleCacheReserveResult::Fail_PendingEvict)
			{
				// Check to see if pending evict completed
				TryReserveCache(Request);
			}
		}
	}
}

void FDefaultInstallBundleManager::TickCacheFlush()
{
	for (auto It = CacheFlushRequests.CreateIterator(); It; ++It)
	{
		FCacheFlushRequestRef Request = *It;

		// Determine which cache(s) to evict from
		const TSharedRef<FInstallBundleCache>* pBundleCache = nullptr;
		if (Request->SourceOrCache.HasSubtype<FName>())
		{
			pBundleCache = BundleCaches.Find(Request->SourceOrCache.GetSubtype<FName>());
			if (pBundleCache == nullptr)
			{
				// For now, don't make this an error.  It's possible the cache exists on some other build configuration
				Request->Callback.ExecuteIfBound();
				It.RemoveCurrent();
				continue;
			}
		}
		else if (Request->SourceOrCache.HasSubtype<FInstallBundleSourceType>())
		{
			if (FName* CacheName = BundleSourceCaches.Find(Request->SourceOrCache.GetSubtype<FInstallBundleSourceType>()))
			{
				pBundleCache = BundleCaches.Find(*CacheName);
			}

			if (pBundleCache == nullptr)
			{
				// For now, don't make this an error.  It's possible the cache exists on some other build configuration
				Request->Callback.ExecuteIfBound();
				It.RemoveCurrent();
				continue;
			}
		}
		else
		{
			// No cache or source specified, evict all the things!
			check(pBundleCache == nullptr);
		}

		// Determine which sources hold the bundles we need to evict
		bool bIsAnyPotentialEvictionPendingRelease = false;
		TMap<FName, TArray<FInstallBundleSourceType>> BundlesToEvictFromSourcesMap;
		if (pBundleCache)
		{
			const TSharedRef<FInstallBundleCache>& BundleCache = *pBundleCache;

			// Check for pending release requests and wait on them if needed
			// This isn't a hard requirement, but satisfies the user expectation that releasing a bundle just before a flush will
			// actually remove it.
			IterateReleaseRequests([this, &BundleCache, &bIsAnyPotentialEvictionPendingRelease](const FContentReleaseRequestRef& QueuedRequest)
				{
					if (BundleCache->Contains(QueuedRequest->BundleName))
					{
						bIsAnyPotentialEvictionPendingRelease = true;
						return false;
					}
					return true;
				});

			if (!bIsAnyPotentialEvictionPendingRelease)
			{
				FInstallBundleCacheFlushResult FlushResult;
				if (Request->SourceOrCache.HasSubtype<FInstallBundleSourceType>())
				{
					FlushResult = BundleCache->Flush(&Request->SourceOrCache.GetSubtype<FInstallBundleSourceType>());
				}
				else
				{
					FlushResult = BundleCache->Flush();
				}

				BundlesToEvictFromSourcesMap = MoveTemp(FlushResult.BundlesToEvict);
			}
		}
		else
		{
			for (const TPair<FName, TSharedRef<FInstallBundleCache>>& Pair : BundleCaches)
			{
				const TSharedRef<FInstallBundleCache>& BundleCache = Pair.Value;

				// Check for pending release requests and wait on them if needed
				// This isn't a hard requirement, but satisfies the user expectation that releasing a bundle just before a flush will
				// actually remove it.
				IterateReleaseRequests([this, &BundleCache, &bIsAnyPotentialEvictionPendingRelease](const FContentReleaseRequestRef& QueuedRequest)
					{
						if (BundleCache->Contains(QueuedRequest->BundleName))
						{
							bIsAnyPotentialEvictionPendingRelease = true;
							return false;
						}
						return true;
					});

				if (bIsAnyPotentialEvictionPendingRelease)
				{
					break;
				}

				FInstallBundleCacheFlushResult FlushResult = BundleCache->Flush();
				for (TPair<FName, TArray<FInstallBundleSourceType>>& ResultPair : FlushResult.BundlesToEvict)
				{
					// A source can only map to one cache, so we can just append them to the list for this bundle without checking if its already there.
					TArray<FInstallBundleSourceType>& EvictFromSources = BundlesToEvictFromSourcesMap.FindOrAdd(ResultPair.Key);
					EvictFromSources.Append(MoveTemp(ResultPair.Value));
				}
			}
		}

		if (bIsAnyPotentialEvictionPendingRelease)
		{
			continue;
		}

		// Nothing to evict
		if (BundlesToEvictFromSourcesMap.Num() == 0)
		{
			Request->Callback.ExecuteIfBound();
			It.RemoveCurrent();
			continue;
		}

		// Start the evictions
		Request->BundlesToEvictFromSourcesMap = MoveTemp(BundlesToEvictFromSourcesMap);
		RequestEviction(MoveTemp(Request));
		It.RemoveCurrent();
	}
}

void FDefaultInstallBundleManager::TickWaitForShaderCache()
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, InstallBundleManager_TickWaitForShaderCache);

	for (const FContentRequestRef& Request : ContentRequests[EContentRequestBatch::Install])
	{
		if (Request->Steps.IsValidIndex(Request->iStep) &&
			Request->Steps[Request->iStep] == EContentRequestState::WaitingForShaderCache &&
			Request->StepResult == EContentRequestStepResult::Waiting)
		{
			FBundleInfo& BundleInfo = BundleInfoMap.FindChecked(Request->BundleName);

			uint32 NumPrecompilesRemaining = FShaderPipelineCache::NumPrecompilesRemaining();

			if (Request->bIsCanceled)
			{
				LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Canceled Waiting for PSO cache for %s"), *BundleInfo.BundleNameString);
				StatsEnd(Request->BundleName, EContentRequestState::WaitingForShaderCache);
				PersistentTimingStatsEnd(Request, InstallBundleUtil::PersistentStats::ETimingStatNames::PSOTime_Real);
				Request->StepResult = EContentRequestStepResult::Done;
			}
			else if (GetMustWaitForPSOCache(BundleInfo) && NumPrecompilesRemaining > 0)
			{
				if (GetInitialShaderPrecompiles(BundleInfo) < NumPrecompilesRemaining)
				{
					SetMustWaitForPSOCache(BundleInfo, NumPrecompilesRemaining); // Update initial precompiles
				}
			}
			else
			{
				LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Done Waiting for PSO cache for %s"), *BundleInfo.BundleNameString);

				SetMustWaitForPSOCache(BundleInfo, 0); // Make sure we pass 0 to flag that there are no more shaders to wait on

				StatsEnd(Request->BundleName, EContentRequestState::WaitingForShaderCache);
				PersistentTimingStatsEnd(Request, InstallBundleUtil::PersistentStats::ETimingStatNames::PSOTime_Real);
				Request->StepResult = EContentRequestStepResult::Done;
			}
		}
	}
}

void FDefaultInstallBundleManager::TickPauseStatus(bool bForceCallback)
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, InstallBundleManager_TickPauseStatus);

	for (const FContentRequestRef& Request : ContentRequests[EContentRequestBatch::Install])
	{
		EInstallBundlePauseFlags PauseFlags = EInstallBundlePauseFlags::None;
		for (const TPair<FInstallBundleSourceType, EInstallBundlePauseFlags>& Pair : Request->SourcePauseFlags)
		{
			PauseFlags |= Pair.Value;
		}

		if (bForceCallback || Request->bForcePauseCallback || PauseFlags != Request->LastSentPauseFlags)
		{
			FInstallBundlePauseInfo PauseInfo;
			PauseInfo.BundleName = Request->BundleName;
			PauseInfo.PauseFlags = PauseFlags;
			PausedBundleDelegate.Broadcast(MoveTemp(PauseInfo));
		}

		Request->LastSentPauseFlags = PauseFlags;
		Request->bForcePauseCallback = false;
	}
}

TSet<FName> FDefaultInstallBundleManager::GetBundleDependencies(FName InBundleName, bool* bSkippedUnknownBundles /*= nullptr*/) const
{
	TSet<FName> BundlesToLoad;

	if (bSkippedUnknownBundles)
	{
		*bSkippedUnknownBundles = false;

		TSet<FName> SkippedUnknownBundles;
		for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
		{
			TSet<FName> SkippedUnknownBundlesForSource;
			BundlesToLoad.Append(Pair.Value->GetBundleDependencies(InBundleName, &SkippedUnknownBundlesForSource));
			SkippedUnknownBundles.Append(SkippedUnknownBundlesForSource);
		}

		// Only consider a bundle "skipped" if all sources don't recognize it
		// Its OK for an individual source not to care about a bundle
		for (const FName& SkippedBundle : SkippedUnknownBundles)
		{
			if (!BundlesToLoad.Contains(SkippedBundle))
			{
				LOG_INSTALL_BUNDLE_MAN(Verbose, TEXT("Unknown Bundle dependency %s, skipping"), *SkippedBundle.ToString());
				*bSkippedUnknownBundles = true;
			}
		}
	}
	else
	{
		for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
		{
			BundlesToLoad.Append(Pair.Value->GetBundleDependencies(InBundleName));
		}
	}

	return BundlesToLoad;
}

TSet<FName> FDefaultInstallBundleManager::GetBundleDependencies(TArrayView<const FName> InBundleNames, bool* bSkippedUnknownBundles /*= nullptr*/) const
{
	if (bSkippedUnknownBundles)
	{
		*bSkippedUnknownBundles = false;
	}

	TSet<FName> BundlesToLoad;
	for (const FName& InBundleName : InBundleNames)
	{
		bool bSkippedUnknownBundlesForBundle = false;
		BundlesToLoad.Append(GetBundleDependencies(InBundleName, &bSkippedUnknownBundlesForBundle));
		if (bSkippedUnknownBundles && bSkippedUnknownBundlesForBundle)
		{
			*bSkippedUnknownBundles = true;
		}
	}

	return BundlesToLoad;
}

TSet<FName> FDefaultInstallBundleManager::GatherBundlesForRequest(TArrayView<const FName> InBundleNames, EInstallBundleRequestInfoFlags& OutFlags)
{
	bool bSkippedUnknownBundles = false;
	TSet<FName> BundlesToLoad = GetBundleDependencies(InBundleNames, &bSkippedUnknownBundles);
	if (bSkippedUnknownBundles)
	{
		OutFlags |= EInstallBundleRequestInfoFlags::SkippedUnknownBundles;
	}
	return BundlesToLoad;
}

TSet<FName> FDefaultInstallBundleManager::GatherBundlesForRequest(TArrayView<const FName> InBundleNames)
{
	return GetBundleDependencies(InBundleNames);
}

FInstallBundleSourceType FDefaultInstallBundleManager::GetBundleSourceFallback(FInstallBundleSourceType Type) const
{
	const FInstallBundleSourceType* Fallback = BundleSourceFallbacks.Find(Type);
	if (Fallback)
	{
		return *Fallback;
	}

	return Type;
}

EInstallBundleSourceUpdateBundleInfoResult FDefaultInstallBundleManager::OnUpdateBundleInfoFromSource(TSharedRef<IInstallBundleSource> Source, FInstallBundleSourceUpdateBundleInfoResult UpdateInfo)
{
	if (InitState != EInstallBundleManagerInitState::Succeeded)
	{
		return EInstallBundleSourceUpdateBundleInfoResult::NotInitailized;
	}

	FInstallBundleSourceType SourceType = Source->GetSourceType();

	TSet<FName> ExistingBundles;
	ExistingBundles.Reserve(UpdateInfo.SourceBundleInfoMap.Num());
	for (const TPair<FName, FInstallBundleSourceUpdateBundleInfo>& UpdateInfoPair : UpdateInfo.SourceBundleInfoMap)
	{
		const FInstallBundleSourceUpdateBundleInfo& SourceBundleInfo = UpdateInfoPair.Value;

		if (FBundleInfo* BundleInfo = BundleInfoMap.Find(UpdateInfoPair.Key))
		{
			if (GetBundleStatus(*BundleInfo) == EBundleState::Mounted)
			{
				return EInstallBundleSourceUpdateBundleInfoResult::AlreadyMounted;
			}
			else
			{
				ExistingBundles.Add(UpdateInfoPair.Key);

				// Don't allow changing whether the bundle is cached after the fact since this
				// could mess up in flight cache operations.
				TOptional<FInstallBundleCacheBundleInfo> CacheInfo;
				if (FName* CacheName = BundleSourceCaches.Find(SourceType))
				{
					const TSharedRef<FInstallBundleCache>& BundleCache = BundleCaches.FindChecked(*CacheName);
					CacheInfo = BundleCache->GetBundleInfo(SourceType, UpdateInfoPair.Key);
				}

				if (CacheInfo.IsSet() != SourceBundleInfo.bIsCached)
				{
					return EInstallBundleSourceUpdateBundleInfoResult::IllegalCacheStatus;
				}
			}
		}
	}

	// Make sure it hasn't been asked for already
	if (ExistingBundles.Num() > 0)
	{
		bool bAlreadyRequested = false;

		IterateContentRequests([&ExistingBundles, &bAlreadyRequested](const FContentRequestRef& QueuedRequest)
			{
				if (ExistingBundles.Contains(QueuedRequest->BundleName))
				{
					bAlreadyRequested = true;
					return false;
				}
				return true;
			});
		if (bAlreadyRequested)
		{
			return EInstallBundleSourceUpdateBundleInfoResult::AlreadyRequested;
		}

		IterateReleaseRequests([&ExistingBundles, &bAlreadyRequested](const FContentReleaseRequestRef& QueuedRequest)
			{
				if (ExistingBundles.Contains(QueuedRequest->BundleName))
				{
					bAlreadyRequested = true;
					return false;
				}
				return true;
			});
		if (bAlreadyRequested)
		{
			return EInstallBundleSourceUpdateBundleInfoResult::AlreadyRequested;
		}
	}

	for (TPair<FName, FInstallBundleSourceUpdateBundleInfo>& UpdateInfoPair : UpdateInfo.SourceBundleInfoMap)
	{
		FInstallBundleSourceUpdateBundleInfo& SourceBundleInfo = UpdateInfoPair.Value;

		FBundleInfo& BundleInfo = BundleInfoMap.FindOrAdd(UpdateInfoPair.Key);

		bool bIsNewBundle = false;
		if (BundleInfo.BundleNameString.IsEmpty())
		{
			BundleInfo.BundleNameString = MoveTemp(SourceBundleInfo.BundleNameString);
			bIsNewBundle = true;
		}

		if (FName* CacheName = BundleSourceCaches.Find(SourceType))
		{
			const TSharedRef<FInstallBundleCache>& BundleCache = BundleCaches.FindChecked(*CacheName);

			if (SourceBundleInfo.bIsCached)
			{
				// Make sure its in the cache and sizes are correct
				TOptional<FInstallBundleCacheBundleInfo> CacheBundleInfo = BundleCache->GetBundleInfo(SourceType, UpdateInfoPair.Key);
				if (!CacheBundleInfo)
				{
					CacheBundleInfo.Emplace();
				}

				CacheBundleInfo->BundleName = UpdateInfoPair.Key;
				CacheBundleInfo->FullInstallSize = SourceBundleInfo.FullInstallSize;
				CacheBundleInfo->InstallOverheadSize = SourceBundleInfo.InstallOverheadSize;
				CacheBundleInfo->TimeStamp = SourceBundleInfo.LastAccessTime;
				CacheBundleInfo->AgeScalar = Source->GetSourceCacheAgeScalar();
				BundleCache->AddOrUpdateBundle(SourceType, *CacheBundleInfo);
			}
			else
			{
				// Make sure its not in the cache
				BundleCache->RemoveBundle(SourceType, UpdateInfoPair.Key);
			}
		}

		// This wouldn't ever be expected to change for a particular source,
		// but I don't have a good way of detected that.
		if (SourceBundleInfo.bContainsIoStoreOnDemandToc)
		{
			BundleInfo.bContainsIoStoreOnDemandTocs = true;
		}

		if (SourceBundleInfo.Priority < BundleInfo.Priority)
		{
			BundleInfo.Priority = SourceBundleInfo.Priority;
		}

		if (SourceBundleInfo.BundleContentState != EInstallBundleInstallState::UpToDate)
		{
			BundleInfo.Prereqs.AddUnique(EBundlePrereqs::RequiresLatestClient);
		}

		if (bIsNewBundle)
		{
			if (SourceBundleInfo.BundleContentState == EInstallBundleInstallState::UpToDate)
			{
				SetBundleStatus(BundleInfo, EBundleState::NeedsMount);
			}
			else if (SourceBundleInfo.BundleContentState == EInstallBundleInstallState::NeedsUpdate)
			{
				SetBundleStatus(BundleInfo, EBundleState::NeedsUpdate);
			}
		}
		else if (SourceBundleInfo.BundleContentState != EInstallBundleInstallState::UpToDate)
		{
			EBundleState BundleStatus = GetBundleStatus(BundleInfo);

			if (SourceBundleInfo.BundleContentState == EInstallBundleInstallState::NotInstalled)
			{
				if (BundleStatus != EBundleState::NotInstalled)
				{
					SetBundleStatus(BundleInfo, EBundleState::NotInstalled);
				}
			}
			else if (SourceBundleInfo.BundleContentState == EInstallBundleInstallState::NeedsUpdate)
			{
				if (BundleStatus == EBundleState::NeedsMount)
				{
					SetBundleStatus(BundleInfo, EBundleState::NeedsUpdate);
				}
			}
		}

		if (FBundleSourceRelevance* SourceRelevance = Algo::FindBy(BundleInfo.ContributingSources, SourceType, &FBundleSourceRelevance::SourceType))
		{
			SourceRelevance->bIsRelevant = true;
		}
		else
		{
			BundleInfo.ContributingSources.Add(FBundleSourceRelevance{ SourceType, true });
		}

		// Since the bundle is now relevant make sure its not in the prune list
		BundlesInfosToPrune.Remove(UpdateInfoPair.Key);
	}

	return EInstallBundleSourceUpdateBundleInfoResult::OK;
}

void FDefaultInstallBundleManager::OnBundleLostRelevanceForSource(TSharedRef<IInstallBundleSource> Source, TSet<FName> BundleNames)
{
	FInstallBundleSourceType SourceType = Source->GetSourceType();

	for (FName BundleName : BundleNames)
	{
		FBundleInfo* BundleInfo = BundleInfoMap.Find(BundleName);
		if (BundleInfo == nullptr)
		{
			ensureAlwaysMsgf(false, TEXT("OnBundleLostRelevanceForSource - Could not find bundle for %s"), *BundleName.ToString());
			continue;
		}

		FBundleSourceRelevance* SourceRelevance = Algo::FindBy(BundleInfo->ContributingSources, SourceType, &FBundleSourceRelevance::SourceType);
		if (SourceRelevance == nullptr)
		{
			ensureAlwaysMsgf(false, TEXT("OnBundleLostRelevanceForSource - %s Is not a valid bundle source for bundle %s"), LexToString(SourceType), *BundleInfo->BundleNameString);
			continue;
		}

		SourceRelevance->bIsRelevant = false;

		// See if all relevance has been lost. If so, add to the set of bundle infos to be pruned
		const bool bIsRelevant = Algo::AnyOf(BundleInfo->ContributingSources, &FBundleSourceRelevance::bIsRelevant);
		if (!bIsRelevant)
		{
			BundlesInfosToPrune.Add(BundleName);
		}
	}
}

void FDefaultInstallBundleManager::StartClientPatchCheck()
{
	PatchCheckHandle = FPatchCheck::Get().GetOnComplete().AddRaw(this, &FDefaultInstallBundleManager::HandleClientPatchCheck);
	FPatchCheck::Get().StartPatchCheck();
}

void FDefaultInstallBundleManager::StartContentPatchCheck()
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, InstallBundleManager_StartContentPatchCheck);

	// Must be fully initialized to do a content patch check
	switch (InitState)
	{
	case EInstallBundleManagerInitState::NotInitialized:
		bDelayCheckingForContentPatch = true;
		return;
	case EInstallBundleManagerInitState::Failed:
		PatchCheckCompleteDelegate.Broadcast(EInstallBundleManagerPatchCheckResult::PatchCheckFailure);
		bIsCheckingForPatch = false;
		return;
	}

	FContentPatchCheckSharedContextRef Context = MakeShared<FContentPatchCheckSharedContext>();

	for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
	{
		const TSharedPtr<IInstallBundleSource>& Source = Pair.Value;
		Source->CheckForContentPatch(FInstallBundleSourceContentPatchResultDelegate::CreateRaw(this, &FDefaultInstallBundleManager::HandleBundleSourceContentPatchCheck, Context));
	}
}

void FDefaultInstallBundleManager::HandleClientPatchCheck(EPatchCheckResult Result)
{
	FPatchCheck::Get().GetOnComplete().Remove(PatchCheckHandle);
	PatchCheckHandle.Reset();

	EInstallBundleManagerPatchCheckResult MyResult = EInstallBundleManagerPatchCheckResult::NoPatchRequired;
	switch (Result)
	{
	case EPatchCheckResult::NoPatchRequired:
		MyResult = EInstallBundleManagerPatchCheckResult::NoPatchRequired;
		break;
	case EPatchCheckResult::PatchRequired:
		MyResult = EInstallBundleManagerPatchCheckResult::ClientPatchRequired;
		break;
	case EPatchCheckResult::NoLoggedInUser:
		MyResult = EInstallBundleManagerPatchCheckResult::NoLoggedInUser;
		break;
	case EPatchCheckResult::PatchCheckFailure:
		MyResult = EInstallBundleManagerPatchCheckResult::PatchCheckFailure;
		break;
	default:
		ensureAlwaysMsgf(false, TEXT("Unknown EPatchCheckResult"));
		MyResult = EInstallBundleManagerPatchCheckResult::PatchCheckFailure;
		break;
	}

	// Make sure we don't miss a case
	static_assert(InstallBundleUtil::CastToUnderlying(EPatchCheckResult::Count) == 4, "");

#if INSTALL_BUNDLE_ALLOW_ERROR_SIMULATION
	if (bSimulateClientNotLatest)
	{
		MyResult = EInstallBundleManagerPatchCheckResult::ClientPatchRequired;
	}
#endif

	if (MyResult == EInstallBundleManagerPatchCheckResult::NoPatchRequired)
	{
		StartContentPatchCheck();
	}
	else
	{
		PatchCheckCompleteDelegate.Broadcast(MyResult);
		bIsCheckingForPatch = false;
	}
}

void FDefaultInstallBundleManager::HandleBundleSourceContentPatchCheck(TSharedRef<IInstallBundleSource> Source, bool bContentPatchRequired, FContentPatchCheckSharedContextRef Context)
{
	Context->Results.Add(Source->GetSourceType(), bContentPatchRequired);

	if (Context->Results.Num() == BundleSources.Num())
	{
		HandleContentPatchCheck(Context);
	}
}

void FDefaultInstallBundleManager::HandleContentPatchCheck(FContentPatchCheckSharedContextRef Context)
{
	EInstallBundleManagerPatchCheckResult MyResult = EInstallBundleManagerPatchCheckResult::NoPatchRequired;
	for (const TPair<FInstallBundleSourceType, bool>& Pair : Context->Results)
	{
		if (Pair.Value)
		{
			MyResult = EInstallBundleManagerPatchCheckResult::ContentPatchRequired;
			break;
		}
	}

#if INSTALL_BUNDLE_ALLOW_ERROR_SIMULATION
	if (bSimulateContentNotLatest)
	{
		MyResult = EInstallBundleManagerPatchCheckResult::ContentPatchRequired;
	}
#endif

	PatchCheckCompleteDelegate.Broadcast(MyResult);
	bIsCheckingForPatch = false;
}

bool FDefaultInstallBundleManager::HasBundleSource(FInstallBundleSourceType SourceType) const
{
	if (InitState != EInstallBundleManagerInitState::Succeeded)
		return false;

	return BundleSources.Contains(SourceType);
}

const TSharedPtr<IInstallBundleSource> FDefaultInstallBundleManager::GetBundleSource(FInstallBundleSourceType SourceType) const
{
	if (InitState != EInstallBundleManagerInitState::Succeeded)
		return nullptr;

	const TSharedPtr<IInstallBundleSource>* Found = BundleSources.Find(SourceType);
	return Found ? *Found : nullptr;
}

FDelegateHandle FDefaultInstallBundleManager::PushInitErrorCallback(FInstallBundleManagerInitErrorHandler Callback)
{
	InitErrorHandlerStack.Push(MoveTemp(Callback));
	return InitErrorHandlerStack.Last().GetHandle();
}

void FDefaultInstallBundleManager::PopInitErrorCallback(FDelegateUserObjectConst InUserObject)
{
	if (InitErrorHandlerStack.Num() > 1)  // Don't remove default handler
	{
		for (TArray<FInstallBundleManagerInitErrorHandler>::TIterator It = InitErrorHandlerStack.CreateIterator(); It;)
		{
			if (It->IsBoundToObject(InUserObject))
			{
				It.RemoveCurrent();
			}
			else
			{
				++It;
			}
		}
	}
}

void FDefaultInstallBundleManager::PopInitErrorCallback(FDelegateHandle Handle)
{
	if (InitErrorHandlerStack.Num() > 1)  // Don't remove default handler
	{
		for (TArray<FInstallBundleManagerInitErrorHandler>::TIterator It = InitErrorHandlerStack.CreateIterator(); It;)
		{
			if (It->GetHandle() == Handle)
			{
				It.RemoveCurrent();
			}
			else
			{
				++It;
			}
		}
	}
}

void FDefaultInstallBundleManager::PopInitErrorCallback()
{
	if (InitErrorHandlerStack.Num() > 1)  // Don't remove default handler
	{
		InitErrorHandlerStack.Pop();
	}
}

EInstallBundleManagerInitState FDefaultInstallBundleManager::GetInitState() const
{
	return InitState;
}

TValueOrError<FInstallBundleRequestInfo, EInstallBundleResult> FDefaultInstallBundleManager::RequestUpdateContent(
	TArrayView<const FName> InBundleNames, EInstallBundleRequestFlags Flags,
	ELogVerbosity::Type LogVerbosityOverride /*= ELogVerbosity::NoLogging*/,
	InstallBundleUtil::FContentRequestSharedContextPtr RequestSharedContext /*= nullptr*/)
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, InstallBundleManager_RequestUpdateContent);

	FInstallBundleRequestInfo RetInfo;

	// Check for failing init, this is not recoverable and we can't safely enqueue requests.
	// bUnrecoverableInitError usually means something is wrong with the build.
	if (bUnrecoverableInitError || InitState == EInstallBundleManagerInitState::Failed)
	{
		return MakeError(EInstallBundleResult::InitializationError);
	}

	if (InitState == EInstallBundleManagerInitState::NotInitialized)
	{
		return MakeError(EInstallBundleResult::InitializationPending);
	}

	TSet<FName> BundlesToLoad = GatherBundlesForRequest(InBundleNames, RetInfo.InfoFlags);
	for (const FName& BundleName : BundlesToLoad)
	{
		const FBundleInfo* BundleInfo = BundleInfoMap.Find(BundleName);
		if (BundleInfo == nullptr)
		{
			RetInfo.InfoFlags |= EInstallBundleRequestInfoFlags::SkippedUnknownBundles;
			continue;
		}

		FContentRequestPtr ActiveQueuedRequest;
		IterateContentRequestsForBundle(BundleName, [&ActiveQueuedRequest](const FContentRequestRef& QueuedRequest)
			{
				if (QueuedRequest->bIsCanceled)
				{
					// Don't finish the canceled version, we are going to re-enqueue
					QueuedRequest->bFinishWhenCanceled = false;
				}
				else
				{
					ActiveQueuedRequest = QueuedRequest;
				}

				return true;
			});

		// Cancel any release requests that might exist for this for this bundle.
		// We don't want to be racing them.
		bool bCanceledRelease = CancelReleaseContentInternal(MakeArrayView(&BundleName, 1));

		// Don't request finished bundles
		if (ActiveQueuedRequest == nullptr)
		{
			bool bIsFinished = false;

			// If we canceled a release during an async op, that op could change bundle status when it completes, so enqueue the request to run after the 
			// canceled release has finished.
			if (!bCanceledRelease && EnumHasAnyFlags(Flags, EInstallBundleRequestFlags::SkipMount) && GetBundleStatus(*BundleInfo) == EBundleState::NeedsMount)
			{
				bool bNeedsCacheReserve = false;
				for (const FBundleSourceRelevance& SourceRelevance : BundleInfo->ContributingSources)
				{
					if (FName* CacheName = BundleSourceCaches.Find(SourceRelevance.SourceType))
					{
						const TSharedRef<FInstallBundleCache>& BundleCache = BundleCaches.FindChecked(*CacheName);
						if (BundleCache->Contains(BundleName) && !BundleCache->IsReserved(BundleName))
						{
							bNeedsCacheReserve = true;
							break;
						}
					}
				}

				// If this bundle is not reserved in a cache but uses a cache, an install request cannot be skipped
				// If this bundle uses IoStoreOnDemandTocs, an install request cannot be skipped
				if (!bNeedsCacheReserve && !BundleInfo->bContainsIoStoreOnDemandTocs)
				{
					RetInfo.InfoFlags |= EInstallBundleRequestInfoFlags::SkippedAlreadyUpdatedBundles;
					LOG_INSTALL_BUNDLE_MAN_OVERRIDE(LogVerbosityOverride, Verbose, TEXT("RequestUpdateContent Bundle %s  - Already Updated"), *BundleInfo->BundleNameString);
					bIsFinished = true;
				}
			}
			// No need to check bCanceledRelease here.  Unmounting is not Async so if we canceled it early enough we will remain mounted
			else if (!GetMustWaitForPSOCache(*BundleInfo) && GetBundleStatus(*BundleInfo) == EBundleState::Mounted)
			{
				RetInfo.InfoFlags |= EInstallBundleRequestInfoFlags::SkippedAlreadyMountedBundles;
				LOG_INSTALL_BUNDLE_MAN_OVERRIDE(LogVerbosityOverride, Verbose, TEXT("RequestUpdateContent Bundle %s  - Already Mounted"), *BundleInfo->BundleNameString);
				bIsFinished = true;
			}

			if (bIsFinished)
			{
				FInstallBundleRequestResultInfo& ResultInfo = RetInfo.BundleResults.Emplace_GetRef();
				ResultInfo.BundleName = BundleName;
				ResultInfo.Result = EInstallBundleResult::OK;
				ResultInfo.bIsStartup = BundleInfo->bIsStartup;
				ResultInfo.bContainsChunks = BundleInfo->ContentPaths.bContainsChunks;
				continue;
			}
		}

		// Allow bundle sources to reject certain bundles
		EInstallBundleSourceBundleSkipReason BundleSourceSkipReason = EInstallBundleSourceBundleSkipReason::None;
		for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
		{
			BundleSourceSkipReason |= Pair.Value->GetBundleSkipReason(BundleName);
		}
		if (BundleSourceSkipReason != EInstallBundleSourceBundleSkipReason::None)
		{
			RetInfo.InfoFlags |= EInstallBundleRequestInfoFlags::SkippedBundlesDueToBundleSource;
			if (EnumHasAnyFlags(BundleSourceSkipReason, EInstallBundleSourceBundleSkipReason::LanguageNotCurrent))
			{
				RetInfo.InfoFlags |= EInstallBundleRequestInfoFlags::SkippedUnusableLanguageBundles;
			}
			if (EnumHasAnyFlags(BundleSourceSkipReason, EInstallBundleSourceBundleSkipReason::NotValid))
			{
				RetInfo.InfoFlags |= EInstallBundleRequestInfoFlags::SkippedInvalidBundles;
			}
			continue;
		}

		LOG_INSTALL_BUNDLE_MAN_OVERRIDE(LogVerbosityOverride, Display, TEXT("Requesting Bundle %s"), *BundleInfo->BundleNameString);

		// If there is an active Request with SkipMount and This request doesn't have skip mount
		if (ActiveQueuedRequest &&
			EnumHasAnyFlags(ActiveQueuedRequest->Flags, EInstallBundleRequestFlags::SkipMount) &&
			!EnumHasAnyFlags(Flags, EInstallBundleRequestFlags::SkipMount))
		{
			// Update flags on the active request to mount
			ActiveQueuedRequest->Flags &= ~EInstallBundleRequestFlags::SkipMount;

#if DO_CHECK
			// Since a request with SkipMount does no async work after updating, it shouldn't be possible to 
			// call RequestUpdateContent and find an ActiveQueuedRequest that is past the Updating step.
			if (ActiveQueuedRequest->Steps.IsValidIndex(ActiveQueuedRequest->iStep))
			{
				EContentRequestState State = ActiveQueuedRequest->Steps[ActiveQueuedRequest->iStep];
				check(State < EContentRequestState::Mounting);
			}
#endif // DO_CHECK
		}

		if (ActiveQueuedRequest == nullptr)
		{
			FContentRequestRef& NewRequest = ContentRequests[EContentRequestBatch::Requested].Emplace_GetRef(MakeShared<FContentRequest>());
			NewRequest->Flags = Flags;
			NewRequest->LogVerbosityOverride = LogVerbosityOverride;
			NewRequest->BundleName = BundleName;

			NewRequest->Prereqs.Reserve(BundleInfo->Prereqs.Num() + 4);
			NewRequest->Prereqs.Add(EBundlePrereqs::CacheHintRequested);
			NewRequest->Prereqs.Append(BundleInfo->Prereqs);
			NewRequest->Prereqs.Add(EBundlePrereqs::HasNoPendingCancels);
			NewRequest->Prereqs.Add(EBundlePrereqs::HasNoPendingReleaseRequests);
			NewRequest->Prereqs.Add(EBundlePrereqs::DetermineSteps);

			if (RequestSharedContext == nullptr)
			{
				RequestSharedContext = MakeShared<InstallBundleUtil::FContentRequestSharedContext>();
			}
			NewRequest->RequestSharedContext = RequestSharedContext;

			const EBundleState BundleState = GetBundleStatus(*BundleInfo);
			if (BundleState == EBundleState::NeedsUpdate || BundleState == EBundleState::NotInstalled)
			{
				bHasEverUpdatedContent = true;
			}
		}

		RetInfo.InfoFlags |= EInstallBundleRequestInfoFlags::EnqueuedBundles;

		RetInfo.BundlesEnqueued.Add(BundleName);
	}

	return MakeValue(RetInfo);
}

FDelegateHandle FDefaultInstallBundleManager::GetContentState(TArrayView<const FName> InBundleNames, EInstallBundleGetContentStateFlags Flags, bool bAddDependencies, FInstallBundleGetContentStateDelegate Callback, FName RequestTag /*= TEXT("None")*/)
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, InstallBundleManager_GetContentState);

	TSet<FName> AllBundles;
	if (bAddDependencies)
	{
		AllBundles = GetBundleDependencies(InBundleNames);
	}
	else
	{
		for (const FName& InBundleName : InBundleNames)
		{
			if (BundleInfoMap.Contains(InBundleName))
			{
				AllBundles.Add(InBundleName);
			}
		}
	}

	FGetContentStateRequestRef& Request = GetContentStateRequests.Emplace_GetRef(MakeShared<FGetContentStateRequest>());

	Request->BundleNames.Reserve(AllBundles.Num());
	for (const FName& BundleName : AllBundles)
	{
		EInstallBundleSourceBundleSkipReason BundleSourceSkipReason = EInstallBundleSourceBundleSkipReason::None;
		for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
		{
			BundleSourceSkipReason |= Pair.Value->GetBundleSkipReason(BundleName);
		}

		if (BundleSourceSkipReason == EInstallBundleSourceBundleSkipReason::None)
		{
			Request->BundleNames.Add(BundleName);
		}
	}

	Request->SetCallback(MoveTemp(Callback));
	Request->RequestTag = RequestTag;
	Request->Flags = Flags;

	return Request->GetCallbackDelegateHandle();
}

void FDefaultInstallBundleManager::CancelAllGetContentStateRequestsForTag(FName RequestTag)
{
	for (FGetContentStateRequestRef& Request : GetContentStateRequests)
	{
		if (Request->RequestTag == RequestTag)
		{
			//Flag request as cancelled. Will be removed when appropriate by TickGetContentState()
			Request->bCancelled = true;
		}
	}
}

void FDefaultInstallBundleManager::CancelAllGetContentStateRequests(FDelegateHandle Handle)
{
	for (FGetContentStateRequestRef& Request : GetContentStateRequests)
	{
		if (Request->GetCallbackDelegateHandle() == Handle)
		{
			//Flag request as cancelled. Will be removed when appropriate by TickGetContentState()
			Request->bCancelled = true;
		}
	}
}

FDelegateHandle FDefaultInstallBundleManager::GetInstallState(TArrayView<const FName> InBundleNames, bool bAddDependencies, FInstallBundleGetInstallStateDelegate Callback, FName RequestTag /*= NAME_None*/)
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, InstallBundleManager_GetInstallState);

	TSet<FName> AllBundles;
	if (bAddDependencies)
	{
		AllBundles = GetBundleDependencies(InBundleNames);
	}
	else
	{
		for (const FName& InBundleName : InBundleNames)
		{
			if (BundleInfoMap.Contains(InBundleName))
			{
				AllBundles.Add(InBundleName);
			}
		}
	}

	FGetInstallStateRequestRef& Request = GetInstallStateRequests.Emplace_GetRef(MakeShared<FGetInstallStateRequest>());

	Request->BundleNames.Reserve(AllBundles.Num());
	for (const FName& BundleName : AllBundles)
	{
		EInstallBundleSourceBundleSkipReason BundleSourceSkipReason = EInstallBundleSourceBundleSkipReason::None;
		for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
		{
			BundleSourceSkipReason |= Pair.Value->GetBundleSkipReason(BundleName);
		}

		if (BundleSourceSkipReason == EInstallBundleSourceBundleSkipReason::None)
		{
			Request->BundleNames.Add(BundleName);
		}
	}

	Request->SetCallback(MoveTemp(Callback));
	Request->RequestTag = RequestTag;

	return Request->GetCallbackDelegateHandle();
}

TValueOrError<FInstallBundleCombinedInstallState, EInstallBundleResult> FDefaultInstallBundleManager::GetInstallStateSynchronous(TArrayView<const FName> InBundleNames, bool bAddDependencies) const
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, InstallBundleManager_GetInstallStateSynchronous);

	if (bUnrecoverableInitError || InitState == EInstallBundleManagerInitState::Failed)
	{
		return MakeError(EInstallBundleResult::InitializationError);
	}

	if (InitState == EInstallBundleManagerInitState::NotInitialized)
	{
		return MakeError(EInstallBundleResult::InitializationPending);
	}

	TSet<FName> AllBundles;
	if (bAddDependencies)
	{
		AllBundles = GetBundleDependencies(InBundleNames);
	}
	else
	{
		for (const FName& InBundleName : InBundleNames)
		{
			if (BundleInfoMap.Contains(InBundleName))
			{
				AllBundles.Add(InBundleName);
			}
		}
	}

	TArray<FName> RetBundleNames;
	RetBundleNames.Reserve(AllBundles.Num());
	for (const FName& BundleName : AllBundles)
	{
		EInstallBundleSourceBundleSkipReason BundleSourceSkipReason = EInstallBundleSourceBundleSkipReason::None;
		for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
		{
			BundleSourceSkipReason |= Pair.Value->GetBundleSkipReason(BundleName);
		}

		if (BundleSourceSkipReason == EInstallBundleSourceBundleSkipReason::None)
		{
			RetBundleNames.Add(BundleName);
		}
	}

	return MakeValue(GetInstallStateInternal(RetBundleNames));
}

void FDefaultInstallBundleManager::CancelAllGetInstallStateRequestsForTag(FName RequestTag)
{
	for (FGetInstallStateRequestRef& Request : GetInstallStateRequests)
	{
		if (Request->RequestTag == RequestTag)
		{
			//Flag request as cancelled. Will be removed when appropriate by TickGetContentState()
			Request->bCancelled = true;
		}
	}
}

void FDefaultInstallBundleManager::CancelAllGetInstallStateRequests(FDelegateHandle Handle)
{
	for (FGetInstallStateRequestRef& Request : GetInstallStateRequests)
	{
		if (Request->GetCallbackDelegateHandle() == Handle)
		{
			//Flag request as cancelled. Will be removed when appropriate by TickGetContentState()
			Request->bCancelled = true;
		}
	}
}

TValueOrError<FInstallBundleReleaseRequestInfo, EInstallBundleResult> FDefaultInstallBundleManager::RequestReleaseContent(
	TArrayView<const FName> ReleaseNames, EInstallBundleReleaseRequestFlags Flags, TArrayView<const FName> KeepNames /*= TArrayView<const FName>()*/, ELogVerbosity::Type LogVerbosityOverride /*= ELogVerbosity::NoLogging*/)
{
	CSV_SCOPED_TIMING_STAT(InstallBundleManager, InstallBundleManager_RequestReleaseContent);

	FInstallBundleReleaseRequestInfo RetInfo;

	// Check for failing init, this is not recoverable and we can't safely enqueue requests.
	// bUnrecoverableInitError usually means something is wrong with the build.
	if (bUnrecoverableInitError || InitState == EInstallBundleManagerInitState::Failed)
	{
		return MakeError(EInstallBundleResult::InitializationError);
	}

	if (InitState == EInstallBundleManagerInitState::NotInitialized)
	{
		return MakeError(EInstallBundleResult::InitializationPending);
	}

	// RemoveFilesIfPossible and SkipReleaseUnmountOnly are incompatible
	if (EnumHasAllFlags(Flags, EInstallBundleReleaseRequestFlags::RemoveFilesIfPossible | EInstallBundleReleaseRequestFlags::SkipReleaseUnmountOnly))
	{
		return MakeError(EInstallBundleResult::InstallError);
	}

	LOG_INSTALL_BUNDLE_MAN_OVERRIDE(LogVerbosityOverride, Display, TEXT("RequestReleaseContent"));

	TSet<FName> BundlesToKeep = GatherBundlesForRequest(KeepNames, RetInfo.InfoFlags);
	
	TSet<FName> BundlesToRelease;
	if (EnumHasAnyFlags(Flags, EInstallBundleReleaseRequestFlags::ExplicitRemoveList))
	{
		BundlesToRelease.Append(ReleaseNames);
	}
	else
	{
		BundlesToRelease = GatherBundlesForRequest(ReleaseNames, RetInfo.InfoFlags);
	}

	// Don't release shared dependencies
	for (const FName& BundleName : BundlesToKeep)
	{
		BundlesToRelease.Remove(BundleName);
	}

	/*for (const FName& Name : BundlesToRelease)
	{
		LOG_INSTALL_BUNDLE_MAN_OVERRIDE(LogVerbosityOverride, Display, TEXT("BundlesToRelease Bundle %s"), *Name.ToString());
	}*/

	for (const FName& BundleName : BundlesToRelease)
	{
		const FBundleInfo* BundleInfo = BundleInfoMap.Find(BundleName);
		if (BundleInfo == nullptr)
		{
			RetInfo.InfoFlags |= EInstallBundleRequestInfoFlags::SkippedUnknownBundles;
			continue;
		}

		FContentReleaseRequestPtr ActiveQueuedRequest;
		IterateReleaseRequestsForBundle(BundleName, [&ActiveQueuedRequest](const FContentReleaseRequestRef& QueuedRequest)
			{
				if (QueuedRequest->bIsCanceled)
				{
					// Don't finish the canceled version, we are going to re-enqueue
					QueuedRequest->bFinishWhenCanceled = false;
				}
				else
				{
					ActiveQueuedRequest = QueuedRequest;
				}

				return true;
			});

		// Cancel any update requests that might exist for this for this bundle.
		// We don't want to be racing them.
		bool bCanceledUpdate = CancelUpdateContentInternal(MakeArrayView(&BundleName, 1));

		// Don't request already released bundles
		// If we canceled an update during an async op, that op could change bundle status when it completes, so enqueue the request to run after the 
		// canceled update has finished.
		if (ActiveQueuedRequest == nullptr && !bCanceledUpdate)
		{
			if (EnumHasAnyFlags(Flags, EInstallBundleReleaseRequestFlags::SkipReleaseUnmountOnly) && GetBundleStatus(*BundleInfo) != EBundleState::Mounted)
			{
				RetInfo.InfoFlags |= EInstallBundleRequestInfoFlags::SkippedAlreadyReleasedBundles;
				LOG_INSTALL_BUNDLE_MAN_OVERRIDE(LogVerbosityOverride, Verbose, TEXT("BundlesToRelease Bundle %s  - Already Unmounted"), *BundleInfo->BundleNameString);
				continue;
			}

			bool bCanSkipRelease = !BundleInfo->bReleaseRequired;
			if (bCanSkipRelease)
			{
				// If this bundle is reserved in a cache, a release request cannot be skipped
				for (const FBundleSourceRelevance& SourceRelevance : BundleInfo->ContributingSources)
				{
					if (FName* CacheName = BundleSourceCaches.Find(SourceRelevance.SourceType))
					{
						const TSharedRef<FInstallBundleCache>& BundleCache = BundleCaches.FindChecked(*CacheName);
						if (BundleCache->IsReserved(BundleName))
						{
							bCanSkipRelease = false;
							break;
						}
					}
				}
			}

			if (bCanSkipRelease && !EnumHasAnyFlags(Flags, EInstallBundleReleaseRequestFlags::RemoveFilesIfPossible) && GetBundleStatus(*BundleInfo) != EBundleState::Mounted)
			{
				RetInfo.InfoFlags |= EInstallBundleRequestInfoFlags::SkippedAlreadyReleasedBundles;
				LOG_INSTALL_BUNDLE_MAN_OVERRIDE(LogVerbosityOverride, Verbose, TEXT("BundlesToRelease Bundle %s  - Already Released"), *BundleInfo->BundleNameString);
				continue;
			}

			if (bCanSkipRelease && GetBundleStatus(*BundleInfo) == EBundleState::NotInstalled)
			{
				RetInfo.InfoFlags |= EInstallBundleRequestInfoFlags::SkippedAlreadyRemovedBundles;
				LOG_INSTALL_BUNDLE_MAN_OVERRIDE(LogVerbosityOverride, Verbose, TEXT("BundlesToRelease Bundle %s  - Already Removed"), *BundleInfo->BundleNameString);
				continue;
			}
		}

		// NOTE: FDefaultInstallBundleManager::RequestUpdateContent Allows bundle sources to skip enqueuing bundles
		// here that are invalid or don't match the current locale.  While that make sense for install, it doesn't 
		// for uninstall as we still want to clean up any stale data that may be lying around in these cases.

		LOG_INSTALL_BUNDLE_MAN_OVERRIDE(LogVerbosityOverride, Display, TEXT("Requesting Release of Bundle %s"), *BundleInfo->BundleNameString);

		if (ActiveQueuedRequest)
		{
			if (!EnumHasAnyFlags(ActiveQueuedRequest->Flags, EInstallBundleReleaseRequestFlags::RemoveFilesIfPossible) &&
				EnumHasAnyFlags(Flags, EInstallBundleReleaseRequestFlags::RemoveFilesIfPossible))
			{
				ActiveQueuedRequest->Flags |= EInstallBundleReleaseRequestFlags::RemoveFilesIfPossible;

				// TODO: This assumes that a bundle source that doesn't remove files will always immediatly callback on release.
				// That is probably a bad assumption, we can't control how the bundle source may be written.
				// It would be safer to instead in enqueue this with a prereq that there is no pending release
#if DO_CHECK
				// Since a request without RemoveFilesIfPossible does no async work after unmounting, it shouldn't be possible to 
				// call RequestReleaseContent and find an ActiveQueuedRequest that is past the unmounting step.
				if (ActiveQueuedRequest->Steps.IsValidIndex(ActiveQueuedRequest->iStep))
				{
					EContentReleaseRequestState State = ActiveQueuedRequest->Steps[ActiveQueuedRequest->iStep];
					check(State < EContentReleaseRequestState::UpdatingBundleSources);
				}
#endif // DO_CHECK
			}

			if (EnumHasAnyFlags(ActiveQueuedRequest->Flags, EInstallBundleReleaseRequestFlags::SkipReleaseUnmountOnly) &&
				!EnumHasAnyFlags(Flags, EInstallBundleReleaseRequestFlags::SkipReleaseUnmountOnly))
			{
				ActiveQueuedRequest->Flags &= ~EInstallBundleReleaseRequestFlags::SkipReleaseUnmountOnly;

#if DO_CHECK
				// Since a request with SkipReleaseUnmountOnly does no async work after unmounting, it shouldn't be possible to 
				// call RequestReleaseContent and find an ActiveQueuedRequest that is past the unmounting step.
				if (ActiveQueuedRequest->Steps.IsValidIndex(ActiveQueuedRequest->iStep))
				{
					EContentReleaseRequestState State = ActiveQueuedRequest->Steps[ActiveQueuedRequest->iStep];
					check(State < EContentReleaseRequestState::UpdatingBundleSources);
				}
#endif // DO_CHECK
			}
		}

		if (ActiveQueuedRequest == nullptr)
		{
			FContentReleaseRequestRef& NewRequest = ContentReleaseRequests[EContentReleaseRequestBatch::Requested].Emplace_GetRef(MakeShared<FContentReleaseRequest>());
			NewRequest->Flags = Flags;
			NewRequest->LogVerbosityOverride = LogVerbosityOverride;
			NewRequest->BundleName = BundleName;

			NewRequest->Prereqs.Reserve(3);
			NewRequest->Prereqs.Add(EBundlePrereqs::HasNoPendingCancels);
			NewRequest->Prereqs.Add(EBundlePrereqs::HasNoPendingUpdateRequests);
			NewRequest->Prereqs.Add(EBundlePrereqs::DetermineSteps);
		}

		RetInfo.InfoFlags |= EInstallBundleRequestInfoFlags::EnqueuedBundles;

		RetInfo.BundlesEnqueued.Add(BundleName);
	}

	return MakeValue(RetInfo);
}

EInstallBundleResult FDefaultInstallBundleManager::FlushCache(FInstallBundleSourceOrCache SourceOrCache, FInstallBundleManagerFlushCacheCompleteDelegate Callback, ELogVerbosity::Type LogVerbosityOverride /*= ELogVerbosity::NoLogging*/)
{
	// Check for failing init, this is not recoverable and we can't safely enqueue requests.
	// bUnrecoverableInitError usually means something is wrong with the build.
	if (bUnrecoverableInitError || InitState == EInstallBundleManagerInitState::Failed)
	{
		return EInstallBundleResult::InitializationError;
	}

	if (InitState == EInstallBundleManagerInitState::NotInitialized)
	{
		return EInstallBundleResult::InitializationPending;
	}

	FCacheFlushRequestRef Request = MakeShared<FCacheFlushRequest>();
	Request->SourceOrCache = SourceOrCache;
	Request->LogVerbosityOverride = LogVerbosityOverride;
	Request->Callback = MoveTemp(Callback);

	CacheFlushRequests.Add(MoveTemp(Request));

	return EInstallBundleResult::OK;
}

TArray<FInstallBundleCacheStats> FDefaultInstallBundleManager::GetCacheStats(EInstallBundleCacheStatsFlags Flags /*= EInstallBundleCacheStatsFlags::None*/, ELogVerbosity::Type LogVerbosityOverride /*= ELogVerbosity::NoLogging*/)
{
	TArray<FInstallBundleCacheStats> Stats;

	if (InitState == EInstallBundleManagerInitState::Succeeded)
	{
		for (const TPair<FName, TSharedRef<FInstallBundleCache>>& Pair : BundleCaches)
		{
			Stats.Add(Pair.Value->GetStats(Flags, LogVerbosityOverride < ELogVerbosity::Verbose));
		}
	}

	return Stats;
}

TOptional<FInstallBundleCacheStats> FDefaultInstallBundleManager::GetCacheStats(FInstallBundleSourceOrCache SourceOrCache, EInstallBundleCacheStatsFlags Flags /*= EInstallBundleCacheStatsFlags::None*/, ELogVerbosity::Type LogVerbosityOverride /*= ELogVerbosity::NoLogging*/)
{
	TOptional<FInstallBundleCacheStats> Stats;

	if (InitState == EInstallBundleManagerInitState::Succeeded)
	{
		const TSharedRef<FInstallBundleCache>* pBundleCache = nullptr;
		if (SourceOrCache.HasSubtype<FName>())
		{
			pBundleCache = BundleCaches.Find(SourceOrCache.GetSubtype<FName>());
		}
		else if (SourceOrCache.HasSubtype<FInstallBundleSourceType>())
		{
			if (FName* CacheName = BundleSourceCaches.Find(SourceOrCache.GetSubtype<FInstallBundleSourceType>()))
			{
				pBundleCache = BundleCaches.Find(*CacheName);
			}
		}

		if (pBundleCache)
		{
			const TSharedRef<FInstallBundleCache>& BundleCache = *pBundleCache;
			Stats.Emplace(BundleCache->GetStats(Flags, LogVerbosityOverride < ELogVerbosity::Verbose));
		}
	}

	return Stats;
}

void FDefaultInstallBundleManager::RequestRemoveContentOnNextInit(TArrayView<const FName> RemoveNames, TArrayView<const FName> KeepNames /*= TArrayView<const FName>()*/)
{
	TSet<FName> BundlesToRemove = GatherBundlesForRequest(RemoveNames);
	TSet<FName> BundlesToKeep = GatherBundlesForRequest(KeepNames);

	// Don't uninstall shared dependencies
	for (const FName& BundleName : BundlesToKeep)
	{
		BundlesToRemove.Remove(BundleName);
	}

	TArray<FString> ConfigBundlesToRemove;
	GConfig->GetArray(TEXT("InstallBundleManager.UserSettings"), TEXT("RemoveBundleOnInit"), ConfigBundlesToRemove, GGameUserSettingsIni);

	for (const FName& BundleName : BundlesToRemove)
	{
		ConfigBundlesToRemove.AddUnique(BundleName.ToString());
	}

	GConfig->SetArray(TEXT("InstallBundleManager.UserSettings"), TEXT("RemoveBundleOnInit"), ConfigBundlesToRemove, GGameUserSettingsIni);

	GConfig->Flush(false);
}

void FDefaultInstallBundleManager::CancelRequestRemoveContentOnNextInit(TArrayView<const FName> BundleNames)
{
	TSet<FName> BundlesToRemove = GatherBundlesForRequest(BundleNames);

	TArray<FString> ConfigBundlesToRemove;
	GConfig->GetArray(TEXT("InstallBundleManager.UserSettings"), TEXT("RemoveBundleOnInit"), ConfigBundlesToRemove, GGameUserSettingsIni);

	for (const FName& BundleName : BundlesToRemove)
	{
		ConfigBundlesToRemove.Remove(BundleName.ToString());
	}

	GConfig->SetArray(TEXT("InstallBundleManager.UserSettings"), TEXT("RemoveBundleOnInit"), ConfigBundlesToRemove, GGameUserSettingsIni);

	GConfig->Flush(false);
}

TArray<FName> FDefaultInstallBundleManager::GetRequestedRemoveContentOnNextInit() const
{
	TArray<FString> ConfigBundlesToRemove;
	GConfig->GetArray(TEXT("InstallBundleManager.UserSettings"), TEXT("RemoveBundleOnInit"), ConfigBundlesToRemove, GGameUserSettingsIni);

	TArray<FName> ConfigBundleNamesToRemove;
	Algo::Transform(ConfigBundlesToRemove, ConfigBundleNamesToRemove, [](const FString& Item) {
		return FName(Item);
	});

	return ConfigBundleNamesToRemove;
}

void FDefaultInstallBundleManager::CancelUpdateContent(TArrayView<const FName> BundleNames)
{
	CancelUpdateContentInternal(BundleNames);
}

bool FDefaultInstallBundleManager::CancelUpdateContentInternal(TArrayView<const FName> InBundleNames)
{
	bool bCancledRequest = false;
	TConstArrayView<FName> BundleNames = InBundleNames;
	TArray<FName> CurrBundleNames;

	while (BundleNames.Num() > 0)
	{
		TSet<FName> BundleNamesNext;
		for (EContentRequestBatch b : TEnumRange<EContentRequestBatch>())
		{
			for (FContentRequestRef& Request : ContentRequests[b])
			{
				if (!BundleNames.Contains(Request->BundleName))
					continue;

				bCancledRequest = true;

				if (Request->bIsCanceled)
				{
					// Already canceled
					if (Request->Result != EInstallBundleResult::UserCancelledError) // User cancel always has priority
					{
						Request->Result = EInstallBundleResult::UserCancelledError;
					}
					continue;
				}

				if (Request->Steps.IsValidIndex(Request->iStep))
				{
					EContentRequestState State = Request->Steps[Request->iStep];
					if (State == EContentRequestState::UpdatingBundleSources)
					{
						for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
						{
							TArray<FName> AdditionalBundles;
							Pair.Value->CancelBundles(BundleNames, AdditionalBundles);
							BundleNamesNext.Append(AdditionalBundles);
						}
					}
				}

				LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Canceling Install Request %s Result: %s"),
					*Request->BundleName.ToString(), LexToString(EInstallBundleResult::UserCancelledError));

				Request->bIsCanceled = true;
				Request->Result = EInstallBundleResult::UserCancelledError;
			}
		}

		CurrBundleNames = BundleNamesNext.Array();
		BundleNames = CurrBundleNames;
	}

	return bCancledRequest;
}

bool FDefaultInstallBundleManager::CancelReleaseContentInternal(TArrayView<const FName> BundleNames)
{
	bool bCancledRequest = false;

	for (EContentReleaseRequestBatch iBatch : TEnumRange<EContentReleaseRequestBatch>())
	{
		for (FContentReleaseRequestRef& Request : ContentReleaseRequests[iBatch])
		{
			if (!BundleNames.Contains(Request->BundleName))
				continue;

			bCancledRequest = true;

			if (Request->bIsCanceled)
			{
				// Already canceled
				if (Request->Result != EInstallBundleReleaseResult::UserCancelledError) // User cancel always has priority
				{
					Request->Result = EInstallBundleReleaseResult::UserCancelledError;
				}
				continue;
			}

			LOG_INSTALL_BUNDLE_MAN_OVERRIDE(Request->LogVerbosityOverride, Display, TEXT("Canceling Release Request %s Result: %s"),
				*Request->BundleName.ToString(), LexToString(EInstallBundleResult::UserCancelledError));

			Request->bIsCanceled = true;
			Request->Result = EInstallBundleReleaseResult::UserCancelledError;
		}
	}

	return bCancledRequest;
}

void FDefaultInstallBundleManager::PauseUpdateContent(TArrayView<const FName> BundleNames)
{
	for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
	{
		Pair.Value->UserPauseBundles(BundleNames);
	}
}

void FDefaultInstallBundleManager::ResumeUpdateContent(TArrayView<const FName> BundleNames)
{
	for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
	{
		Pair.Value->UserResumeBundles(BundleNames);
	}
}

void FDefaultInstallBundleManager::RequestPausedBundleCallback()
{
	TickPauseStatus(true);
}

TOptional<FInstallBundleProgress> FDefaultInstallBundleManager::GetBundleProgress(FName BundleName) const
{
	TOptional<FInstallBundleProgress> CombinedStatus;

	const FBundleInfo* BundleInfo = BundleInfoMap.Find(BundleName);
	if (BundleInfo == nullptr)
	{
		return CombinedStatus;
	}

	// Find the request
	FContentRequestPtr Request;
	EContentRequestBatch Batch = EContentRequestBatch::Count;
	for (EContentRequestBatch iBatch : TEnumRange<EContentRequestBatch>())
	{
		for (const FContentRequestRef& QueuedRequest : ContentRequests[iBatch])
		{
			if (QueuedRequest->BundleName == BundleName && !QueuedRequest->bIsCanceled)
			{
				Request = QueuedRequest;
				Batch = iBatch;
				break;
			}
		}
	}

	if (Request == nullptr)
	{
		return CombinedStatus;
	}

	CombinedStatus.Emplace();
	CombinedStatus->BundleName = BundleName;
	for (const TPair<FInstallBundleSourceType, EInstallBundlePauseFlags>& Pair : Request->SourcePauseFlags)
	{
		CombinedStatus->PauseFlags |= Pair.Value;
	}

	if (Batch < EContentRequestBatch::Install)
	{
		CombinedStatus->Status = EInstallBundleStatus::Requested;
		return CombinedStatus;
	}

	check(Batch < EContentRequestBatch::Count);

	CombinedStatus->Status = EInstallBundleStatus::Updating;

	if (!Request->Steps.IsValidIndex(Request->iStep))
	{
		return CombinedStatus;
	}

	EContentRequestState State = Request->Steps[Request->iStep];

	// Combine bundle installer stats
	for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
	{
		CombinedStatus->Stats.Append(Pair.Value->GetBundleUpdateStats(BundleName));
	}

	if (State == EContentRequestState::UpdatingBundleSources && Request->Result == EInstallBundleResult::OK)
	{
		// Update cached status
		for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
		{
			TOptional<FInstallBundleSourceProgress> Progress = Pair.Value->GetBundleProgress(BundleName);
			if (Progress)
			{
				Request->CachedSourceProgress.Emplace(Pair.Key, MoveTemp(*Progress));
			}
		}

		// Combine cached status
		float TotalWeight = 0.0f;
		for (const TPair<FInstallBundleSourceType, FInstallBundleSourceProgress>& Pair : Request->CachedSourceProgress)
		{
			const float SourceWeight = BundleSources.FindChecked(Pair.Key)->GetSourceWeight();
			check(SourceWeight > 0.0f);
			TotalWeight += SourceWeight;
			CombinedStatus->BackgroundDownload_Percent += SourceWeight * Pair.Value.BackgroundDownload_Percent;
			CombinedStatus->InstallOnly_Percent += SourceWeight * (Pair.Value.InstallOnly_Percent >= 0 ? Pair.Value.InstallOnly_Percent : Pair.Value.Install_Percent);
			CombinedStatus->Install_Percent += SourceWeight * Pair.Value.Install_Percent;
		}

		if (TotalWeight > 0.0f)
		{
			CombinedStatus->BackgroundDownload_Percent /= TotalWeight;
			CombinedStatus->InstallOnly_Percent /= TotalWeight;
			CombinedStatus->Install_Percent /= TotalWeight;
		}
		else
		{
			// No progress returned from any source, assume everything is done
			CombinedStatus->BackgroundDownload_Percent = 1.0f;
			CombinedStatus->InstallOnly_Percent = 1.0f;
			CombinedStatus->Install_Percent = 1.0f;
		}
	}
	else if (State > EContentRequestState::UpdatingBundleSources && Request->Result == EInstallBundleResult::OK)
	{
		CombinedStatus->BackgroundDownload_Percent = 1.0f;
		CombinedStatus->InstallOnly_Percent = 1.0f;
		CombinedStatus->Install_Percent = 1.0f;
	}

	// Currently we don't track progress for mounting, but if we do we should add it here.
	// Async Mounting should let us do that althogh we can only measure how many paks are mounted and not progress for individual paks.
	// Would we make mount progress part of Install_Percent or report it separately?

	if (State >= EContentRequestState::WaitingForShaderCache)
	{
		CombinedStatus->Status = EInstallBundleStatus::Finishing;

		const bool MustWaitForPSOCache = GetMustWaitForPSOCache(*BundleInfo);
		const uint32 InitialShaderPrecompiles = GetInitialShaderPrecompiles(*BundleInfo);
		const uint32 NumPrecompilesRemaining = FShaderPipelineCache::NumPrecompilesRemaining();
		if (MustWaitForPSOCache && InitialShaderPrecompiles > 0)
		{
			CombinedStatus->Finishing_Percent = 1.0f - static_cast<float>(NumPrecompilesRemaining) / InitialShaderPrecompiles;
			CombinedStatus->Finishing_Percent = FMath::Clamp(CombinedStatus->Finishing_Percent, 0.0f, 1.0f);
		}
		else
		{
			CombinedStatus->Finishing_Percent = 1.0f;
		}
	}

	if (State >= EContentRequestState::Finishing)
	{
		if (!Request->bIsCanceled && Request->Result == EInstallBundleResult::OK && GetBundleStatus(*BundleInfo) == EBundleState::Mounted)
		{
			CombinedStatus->Status = EInstallBundleStatus::Ready;
		}
	}

	return CombinedStatus;
}

void FDefaultInstallBundleManager::StartSessionPersistentStatTracking(const FString& SessionName, const TArray<FName>& RequiredBundles /* = TArray<FName>() */, const FString& ExpectedAnalyticsID /* = FString() */, bool bForceResetStatData /*= false */, const FInstallBundleCombinedContentState* State /*= nullptr*/)
{
	LOG_INSTALL_BUNDLE_MAN(Display, TEXT("Attempting to Start Persistent Stat Tracking -- Session:%s"), *SessionName);

	if (ensureAlwaysMsgf((nullptr != State), TEXT("Default Install Bundle Manager expects to ALWAYS pass a FInstallBundleCombinedContentState into StartSessionPersistentStatTracking! Passed in a nullptr for session %s"), *SessionName))
	{
		PersistentStats->UpdateForContentState(*State, SessionName);
	}

	FInstallBundleSourceType BPSSourceType(TEXT("BuildPatchServices"));
	check(BPSSourceType.IsValid());

	for (EContentRequestBatch iBatch : TEnumRange<EContentRequestBatch>())
	{
		for (const FContentRequestRef& QueuedRequest : ContentRequests[iBatch])
		{
			const FName CurrentBundleName = QueuedRequest->BundleName;
			if (RequiredBundles.Contains(CurrentBundleName))
			{
				const FInstallBundleContentState* ContentState = (nullptr != State) ? State->IndividualBundleStates.Find(CurrentBundleName) : nullptr;
				if (nullptr != ContentState)
				{
					FString BundleContentVersion = ContentState->Version.FindRef(BPSSourceType);

					FString PreviousVersionCLString;
					if (!BundleContentVersion.Split(TEXT("CL-"), nullptr, &PreviousVersionCLString, ESearchCase::CaseSensitive))
					{
						PreviousVersionCLString = TEXT("Unknown");
					}

					FString CurrentVersion = (nullptr != State) ? State->CurrentVersion.FindRef(BPSSourceType) : FString();
					if (CurrentVersion.IsEmpty() && AnalyticsProvider)
					{
						//if we don't have a BPS CurrentVersion, fail back on whatever we sent to setup our Analytics Provider as that is pretty robust
						CurrentVersion = AnalyticsProvider->GetSessionID();
					}

					const FString AnalyticsID = PreviousVersionCLString + TEXT("_to_") + CurrentVersion;
					StartBundlePersistentStatTracking(QueuedRequest, AnalyticsID);
				}
				else
				{
					LOG_INSTALL_BUNDLE_MAN(Warning, TEXT("Could not start bundle persistent stat tracking with an accurate AnalyticsID! We could not find an entry for it in the IndividualBundleStates even though it was a required bundle to track! Starting with generic Analytics ID to hope for the best!"));
					StartBundlePersistentStatTracking(QueuedRequest);
				}
			}
		}
	}

	const InstallBundleUtil::PersistentStats::FSessionPersistentStats* FoundSessionStat = PersistentStats->GetSessionStat(SessionName);

	//We can end up calling this Start twice with the way FN handles data, so only actually run it if we haven't already added this session to the stat list or if we have previously stopped the session
	if ((nullptr == FoundSessionStat) || !(FoundSessionStat->IsActive()))
	{
		LOG_INSTALL_BUNDLE_MAN(Display, TEXT("Starting PersistentStat Tracking -- Session:%s"), *SessionName);
		PersistentStats->StartSessionPersistentStatTracking(SessionName, RequiredBundles, ExpectedAnalyticsID, bForceResetStatData);

		//Start our expected active timers
		PersistentStats->StartSessionPersistentStatTimer(SessionName, InstallBundleUtil::PersistentStats::ETimingStatNames::TotalTime_Real);

		//Increment our NumResumedFromLaunch as this was the first time we loaded this data this launch
		PersistentStats->IncrementSessionPersistentCounter(SessionName, InstallBundleUtil::PersistentStats::ECountStatNames::NumResumedFromLaunch);
	}
	else
	{
		LOG_INSTALL_BUNDLE_MAN(Display, TEXT("NOT Starting PersistentStat Tracking -- Session:%s as it was already active!"), *SessionName);
		//We already have a session with this name, but make sure we have all of these required bundles added
		PersistentStats->AddRequiredBundlesForSession(SessionName, RequiredBundles);
	}
}

void FDefaultInstallBundleManager::StopSessionPersistentStatTracking(const FString& SessionName)
{
	LOG_INSTALL_BUNDLE_MAN(Display, TEXT("Stopping PersistentStat Tracking -- Session:%s"), *SessionName);
	PersistentStats->StopSessionPersistentStatTracking(SessionName);
}

void FDefaultInstallBundleManager::StartBundlePersistentStatTracking(TSharedRef<FContentRequest> ContentRequest, const FString& ExpectedAnalyticsID /* = FString() */, bool bForceResetStatData /* = false */)
{
	//Begin PersistentStat tracking for this new bundle if needed
	if (EnumHasAnyFlags(ContentRequest->Flags, EInstallBundleRequestFlags::TrackPersistentBundleStats))
	{
		//Check if this bundle is already being tracked. We don't want to start twice, as bundle sources may have started tracking with a better analytics ID.
		//This is really just a catch-all for times where bundle sources don't start a bundle tracking.
		const InstallBundleUtil::PersistentStats::FBundlePersistentStats* FoundBundleStat = PersistentStats->GetBundleStat(ContentRequest->BundleName);
		if ((nullptr == FoundBundleStat) || !FoundBundleStat->IsActive())
		{
			LOG_INSTALL_BUNDLE_MAN(Display, TEXT("Starting PersistentStat Tracking -- BUNDLE:%s"), *ContentRequest->BundleName.ToString());
			PersistentStats->StartBundlePersistentStatTracking(ContentRequest->BundleName, ExpectedAnalyticsID, bForceResetStatData);
		}
		else
		{
			LOG_INSTALL_BUNDLE_MAN(Display, TEXT("NOT Starting PersistentStat Tracking -- BUNDLE:%s as it was already started"), *ContentRequest->BundleName.ToString());
		}
	}
}

#if !UE_BUILD_SHIPPING
void FDefaultInstallBundleManager::GetDebugText(TArray<FString>& Output)
{
	for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
	{
		Pair.Value->GetDebugText(Output);
	}
}
#endif

void FDefaultInstallBundleManager::StopBundlePersistentStatTracking(TSharedRef<FContentRequest> ContentRequest)
{
	//Only bother calling Stop if this bundle supported tracking persistent bundle stats
	if (EnumHasAnyFlags(ContentRequest->Flags, EInstallBundleRequestFlags::TrackPersistentBundleStats))
	{
		LOG_INSTALL_BUNDLE_MAN(Display, TEXT("Stopping PersistentStat Tracking -- BUNDLE:%s"), *ContentRequest->BundleName.ToString());
		PersistentStats->StopBundlePersistentStatTracking(ContentRequest->BundleName);
	}
}

void FDefaultInstallBundleManager::PersistentTimingStatsBegin(TSharedRef<FContentRequest> ContentRequest, InstallBundleUtil::PersistentStats::ETimingStatNames TimerStatName)
{
	if (EnumHasAnyFlags(ContentRequest->Flags, EInstallBundleRequestFlags::TrackPersistentBundleStats))
	{
		LOG_INSTALL_BUNDLE_MAN(Display, TEXT("Starting Persistent Timer -- BUNDLE:%s , Timer:%s"), *ContentRequest->BundleName.ToString(), *LexToString(TimerStatName));
		PersistentStats->StartBundlePersistentStatTimer(ContentRequest->BundleName, TimerStatName);
	}
}

void FDefaultInstallBundleManager::PersistentTimingStatsEnd(TSharedRef<FContentRequest> ContentRequest, InstallBundleUtil::PersistentStats::ETimingStatNames TimerStatName)
{
	if (EnumHasAnyFlags(ContentRequest->Flags, EInstallBundleRequestFlags::TrackPersistentBundleStats))
	{
		LOG_INSTALL_BUNDLE_MAN(Display, TEXT("Stopping Persistent Timer -- BUNDLE:%s , Timer:%s"), *ContentRequest->BundleName.ToString(), *LexToString(TimerStatName));
		PersistentStats->StopBundlePersistentStatTimer(ContentRequest->BundleName, TimerStatName);
	}
}

EInstallBundleRequestFlags FDefaultInstallBundleManager::GetModifyableContentRequestFlags() const
{
	EInstallBundleRequestFlags Result = EInstallBundleRequestFlags::None;
	for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
	{
		Result |= Pair.Value->GetModifyableContentRequestFlags();
	}

	return Result;
}

void FDefaultInstallBundleManager::UpdateContentRequestFlags(TArrayView<const FName> BundleNames, EInstallBundleRequestFlags AddFlags, EInstallBundleRequestFlags RemoveFlags)
{
	for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
	{
		EInstallBundleRequestFlags AllowedFlags = Pair.Value->GetModifyableContentRequestFlags();
		Pair.Value->UpdateContentRequestFlags(BundleNames, AddFlags & AllowedFlags, RemoveFlags & AllowedFlags);
	}
}

void FDefaultInstallBundleManager::SetCellularPreference(int32 Value)
{
	for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
	{
		Pair.Value->SetCellularPreference(Value);
	}
}

void FDefaultInstallBundleManager::SetCacheSize(FName CacheName, uint64 CacheSize)
{
	if (ensureMsgf(!BundleCaches.Contains(CacheName), TEXT("FDefaultInstallBundleManager::SetCacheSize is only supported prior to initialization, for now")))
	{
		BundleCacheSizeOverrides.Add(CacheName, CacheSize);
	}
}

void FDefaultInstallBundleManager::StartPatchCheck()
{
	if (bIsCheckingForPatch)
		return;

	bIsCheckingForPatch = true;

	StartClientPatchCheck();
}

void FDefaultInstallBundleManager::AddEnvironmentWantsPatchCheckBackCompatDelegate(FName Tag, FInstallBundleManagerEnvironmentWantsPatchCheck Delegate)
{
	FPatchCheck::Get().AddEnvironmentWantsPatchCheckBackCompatDelegate(Tag, Delegate);
}

void FDefaultInstallBundleManager::RemoveEnvironmentWantsPatchCheckBackCompatDelegate(FName Tag)
{
	FPatchCheck::Get().RemoveEnvironmentWantsPatchCheckBackCompatDelegate(Tag);
}

bool FDefaultInstallBundleManager::SupportsEarlyStartupPatching() const
{
	return false;
}

bool FDefaultInstallBundleManager::IsNullInterface() const
{
	return false;
}

void FDefaultInstallBundleManager::SetErrorSimulationCommands(const FString& CommandLine)
{
#if INSTALL_BUNDLE_ALLOW_ERROR_SIMULATION
	if (FParse::Param(*CommandLine, TEXT("SimulateClientNotLatest")))
		bSimulateClientNotLatest = true;
	if (FParse::Param(*CommandLine, TEXT("SimulateContentNotLatest")))
		bSimulateContentNotLatest = true;
#endif // INSTALL_BUNDLE_ALLOW_ERROR_SIMULATION

	for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
	{
		Pair.Value->SetErrorSimulationCommands(CommandLine);
	}
}

void FDefaultInstallBundleManager::SetCommandLineOverrides(const FString& CommandLine)
{
	if (FParse::Param(*CommandLine, TEXT("bDebugCommand_SkipPatchCheck")) || FParse::Param(*CommandLine, TEXT("SkipPatchCheck"))) // SkipPatchCheck is a legacy command we are supporting here
		bOverrideCommand_SkipPatchCheck = true;
}

TSharedPtr<IAnalyticsProviderET> FDefaultInstallBundleManager::GetAnalyticsProvider() const
{
	return AnalyticsProvider;
}

EInstallBundleManagerInitResult FDefaultInstallBundleManager::Init_DefaultBundleSources()
{
	TArray<FString> ConfgSources;
	TMap<FString, FString> ConfigFallbackSources;
	if (!InstallBundleUtil::GetConfiguredBundleSources(ConfgSources, ConfigFallbackSources))
	{
		return EInstallBundleManagerInitResult::ConfigurationError;
	}

	BundleSourceFallbacks.Empty(ConfigFallbackSources.Num());
	for (const TPair<FString, FString>& ConfigFallback : ConfigFallbackSources)
	{
		FInstallBundleSourceType Key(ConfigFallback.Key);
		FInstallBundleSourceType Value(ConfigFallback.Value);
		if (!Key.IsValid() || !Value.IsValid())
		{
			ensureAlwaysMsgf(false, TEXT("Malformed entry in InstallBundleManager.FallbackBundleSources"));
			return EInstallBundleManagerInitResult::ConfigurationError;
		}

		BundleSourceFallbacks.Add(Key, Value);
	}

	TArray<FInstallBundleSourceType> SourcesToCreate;
	SourcesToCreate.Reserve(ConfgSources.Num());
	for (const FString& ConfigSource : ConfgSources)
	{
		FInstallBundleSourceType SourceType(ConfigSource);
		if (SourceType.IsValid())
		{
			SourcesToCreate.Add(SourceType);
		}
		else
		{
			ensureAlwaysMsgf(false, TEXT("Malformed entry in InstallBundleManager.BundleSources"));
			return EInstallBundleManagerInitResult::ConfigurationError;
		}
	}

	BundleSources.Empty(SourcesToCreate.Num());
	EInstallBundleManagerInitResult Result = Init_TryCreateBundleSources(SourcesToCreate);
	if (Result != EInstallBundleManagerInitResult::OK)
	{
		return Result;
	}

	if (BundleSources.Num() == 0)
	{
		ensureAlwaysMsgf(false, TEXT("No Bundle Sources specified"));
		return EInstallBundleManagerInitResult::ConfigurationError;
	}

	return EInstallBundleManagerInitResult::OK;
}

EInstallBundleManagerInitResult FDefaultInstallBundleManager::Init_TryCreateBundleSources(
	TArray<FInstallBundleSourceType> SourcesToCreate, TArray<TSharedPtr<IInstallBundleSource>>* OutNewSources /*= nullptr*/)
{
	for (int i = 0; i < SourcesToCreate.Num(); ++i)
	{
		FInstallBundleSourceType SourceType = SourcesToCreate[i];
		check(SourceType.IsValid());

		TSharedPtr<IInstallBundleSource> Source = BundleSources.FindRef(SourceType);
		if (!Source)
		{
			Source = InstallBundleSourceFactory(SourceType);
			if (Source == nullptr)
			{
				ensureAlwaysMsgf(false, TEXT("Failed to create bundle source %s"), LexToString(SourceType));
				return EInstallBundleManagerInitResult::ConfigurationError;
			}

			FInstallBundleSourceInitInfo InitInfo = Source->Init(StatsMap, AnalyticsProvider, PersistentStats);
			if (InitInfo.Result == EInstallBundleManagerInitResult::OK)
			{
				check(SourceType == Source->GetSourceType());
				Source->SetErrorSimulationCommands(FCommandLine::Get());
				BundleSources.Add(SourceType, Source);
				if (OutNewSources)
				{
					OutNewSources->Add(Source);
				}
			}
			else if (!InitInfo.bShouldUseFallbackSource)
			{
				ensureAlwaysMsgf(false, TEXT("Failed to init bundle source %s"), LexToString(SourceType));
				return InitInfo.Result;
			}
			else
			{
				FInstallBundleSourceType FallbackSourceType = FindFallbackSource(SourceType);
				if (FallbackSourceType == Source->GetSourceType())
				{
					ensureAlwaysMsgf(false, TEXT("Failed to init bundle source %s"), LexToString(SourceType));
					return InitInfo.Result;
				}

				LOG_INSTALL_BUNDLE_MAN(Display, TEXT("Failed to init bundle source %s, falling back to %s"), LexToString(Source->GetSourceType()), LexToString(FallbackSourceType));

				if (!SourcesToCreate.Contains(FallbackSourceType))
				{
					SourcesToCreate.Add(FallbackSourceType);
				}

				BundleSourcesToDelete.Add(MoveTemp(Source));
			}
		}
	}

	return EInstallBundleManagerInitResult::OK;
}

FInstallBundleSourceType FDefaultInstallBundleManager::FindFallbackSource(FInstallBundleSourceType SourceType)
{
	// See if we can find a fallback
	FInstallBundleSourceType FallbackSourceType = SourceType;
	for (;;)
	{
		FInstallBundleSourceType FallbackSourceTypeNext = GetBundleSourceFallback(FallbackSourceType);

		// Check if we have reached the end of the fallback chain
		if (FallbackSourceTypeNext == FallbackSourceType)
			break;

		FallbackSourceType = FallbackSourceTypeNext;

		// Check if this fallback has already been tried
		bool bFallbackSourceWasReplaced = BundleSourcesToDelete.ContainsByPredicate([FallbackSourceType](const TSharedPtr<IInstallBundleSource>& ReplacedSource)
			{
				return ReplacedSource->GetSourceType() == FallbackSourceType;
			});

		if (!bFallbackSourceWasReplaced)
			break;
	}

	return FallbackSourceType;
}

void FDefaultInstallBundleManager::AsyncInit_InitBundleSources()
{
	LOG_INSTALL_BUNDLE_MAN(Display, TEXT("Initializing all bundle sources..."));

	check(BundleSources.Num());

	// Copy to temp array in case we modify BundleSources on callback
	TArray<TSharedPtr<IInstallBundleSource>> BundleSourcesToInit;
	BundleSources.GenerateValueArray(BundleSourcesToInit);

	InitStepResult = EAsyncInitStepResult::Waiting;

	BundleSourceInitResults.Empty(BundleSourcesToInit.Num());
	for (TSharedPtr<IInstallBundleSource>& Source : BundleSourcesToInit)
	{
		if (Source->GetInitState() == EInstallBundleManagerInitState::Succeeded)
			continue;

		BundleSourceInitResults.Add(Source->GetSourceType());
	}

	for (TSharedPtr<IInstallBundleSource>& Source : BundleSourcesToInit)
	{
		if (Source->GetInitState() == EInstallBundleManagerInitState::Succeeded)
			continue;

		Source->AsyncInit(FInstallBundleSourceInitDelegate::CreateRaw(this, &FDefaultInstallBundleManager::AsyncInit_OnBundleSourceInitComplete));
	}
}

void FDefaultInstallBundleManager::AsyncInit_OnBundleSourceInitComplete(TSharedRef<IInstallBundleSource> Source, FInstallBundleSourceAsyncInitInfo InInitInfo)
{
	const FInstallBundleSourceAsyncInitInfo& InitInfo = BundleSourceInitResults.FindChecked(Source->GetSourceType()).Emplace(MoveTemp(InInitInfo));

	if (InitInfo.Result != EInstallBundleManagerInitResult::OK && InitInfo.bShouldUseFallbackSource)
	{
		// See if we can find a fallback
		FInstallBundleSourceType FallbackSourceType = FindFallbackSource(Source->GetSourceType());
		if (FallbackSourceType != Source->GetSourceType())
		{
			LOG_INSTALL_BUNDLE_MAN(Display, TEXT("Failed to init bundle source %s, falling back to %s"), LexToString(Source->GetSourceType()), LexToString(FallbackSourceType));

			// Defer deletion of the replaced source until it is safe to do so
			BundleSourcesToDelete.Emplace(MoveTemp(BundleSources.FindChecked(Source->GetSourceType())));
			BundleSources.Remove(Source->GetSourceType());
			BundleSourceInitResults.Remove(Source->GetSourceType());

			TArray<TSharedPtr<IInstallBundleSource>> NewFallbackSources;
			EInstallBundleManagerInitResult CreateResult = Init_TryCreateBundleSources({ FallbackSourceType }, &NewFallbackSources);
			if (CreateResult == EInstallBundleManagerInitResult::OK)
			{
				for (const TSharedPtr<IInstallBundleSource>& NewFallbackSource : NewFallbackSources)
				{
					BundleSourceInitResults.Add(NewFallbackSource->GetSourceType());
					NewFallbackSource->AsyncInit(FInstallBundleSourceInitDelegate::CreateRaw(this, &FDefaultInstallBundleManager::AsyncInit_OnBundleSourceInitComplete));
				}
			}
			else
			{
				bUnrecoverableInitError = true;
				InitResult = CreateResult;
			}
		}
	}

	bool bHasInitializedAllSources = true;
	for (const TPair<FInstallBundleSourceType, TOptional<FInstallBundleSourceAsyncInitInfo>>& Pair : BundleSourceInitResults)
	{
		if (!Pair.Value)
		{
			bHasInitializedAllSources = false;
			break;
		}
	}

	if (bHasInitializedAllSources)
	{
#if DO_CHECK
		for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
		{
			check(Pair.Value->GetInitState() != EInstallBundleManagerInitState::NotInitialized);
		}
#endif // DO_CHECK

		LOG_INSTALL_BUNDLE_MAN(Display, TEXT("All bundle sources done initializing"));

		if (bUnrecoverableInitError)
		{
			if (InitResult == EInstallBundleManagerInitResult::OK)
			{
				// Make sure an error result has been set
				InitResult = EInstallBundleManagerInitResult::ConfigurationError;
			}
		}
		else
		{
			for (const TPair<FInstallBundleSourceType, TOptional<FInstallBundleSourceAsyncInitInfo>>& Pair : BundleSourceInitResults)
			{
				if (Pair.Value->Result != EInstallBundleManagerInitResult::OK)
				{
					InitResult = Pair.Value->Result;
					break;
				}
			}
		}

		// Don't need these anymore
		BundleSourceInitResults.Empty();

		if (bUnrecoverableInitError ||
			(InitResult != EInstallBundleManagerInitResult::BuildMetaDataDownloadError &&
			InitResult != EInstallBundleManagerInitResult::RemoteBuildMetaDataNotFound))
		{
			InitStepResult = EAsyncInitStepResult::Done;
		}
		else
		{
			// If we have BuildMetaDataDownloadError or RemoteBuildMetaDataNotFound, this client could simply be too old to function.
			// Check if there is a new client available
			AsyncInit_PatchCheckHandle = FPatchCheck::Get().GetOnComplete().AddRaw(
				this, &FDefaultInstallBundleManager::AsyncInit_OnQueryBundleInfoComplete_HandleClientPatchCheck);
			FPatchCheck::Get().StartPatchCheck();
		}
	}
}

void FDefaultInstallBundleManager::AsyncInit_OnQueryBundleInfoComplete_HandleClientPatchCheck(EPatchCheckResult Result)
{
	FPatchCheck::Get().GetOnComplete().Remove(AsyncInit_PatchCheckHandle);
	AsyncInit_PatchCheckHandle.Reset();

#if INSTALL_BUNDLE_ALLOW_ERROR_SIMULATION
	if (bSimulateClientNotLatest)
	{
		Result = EPatchCheckResult::PatchRequired;
	}
#endif

	if (Result == EPatchCheckResult::PatchRequired)
	{
		InitResult = EInstallBundleManagerInitResult::ClientPatchRequiredError;
	}

	InitStepResult = EAsyncInitStepResult::Done;
}

void FDefaultInstallBundleManager::AsyncInit_InitBundleCaches()
{
	TArray<FString> ConfigBundleCaches;
	if (0 == GConfig->GetArray(TEXT("InstallBundleManager.BundleCaches"), TEXT("BundleCaches"), ConfigBundleCaches, GInstallBundleIni))
	{
		InitStepResult = EAsyncInitStepResult::Done;
		return;
	}

	TArray<FString> ConfigBundleSourceCaches;
	if (0 == GConfig->GetArray(TEXT("InstallBundleManager.BundleCaches"), TEXT("BundleSourceCaches"), ConfigBundleSourceCaches, GInstallBundleIni))
	{
		InitStepResult = EAsyncInitStepResult::Done;
		return;
	}

	const TCHAR* PropertyName = TEXT("Name=");
	const TCHAR* PropertySize = TEXT("Size=");

	TMap<FName, FInstallBundleCacheInitInfo> BundleCacheInitInfo;
	for (FString& Mapping : ConfigBundleCaches)
	{
		// Remove parentheses
		Mapping.TrimStartAndEndInline();
		Mapping.ReplaceInline(TEXT("("), TEXT(""));
		Mapping.ReplaceInline(TEXT(")"), TEXT(""));

		FName CacheName;
		uint64 CacheSize = 0;
		if (!FParse::Value(*Mapping, PropertyName, CacheName) ||
			!FParse::Value(*Mapping, PropertySize, CacheSize))
		{
			ensureAlwaysMsgf(false, TEXT("Malformed entry in InstallBundleManager.BundleCaches"));
			InitResult = EInstallBundleManagerInitResult::ConfigurationError;
			return;
		}

		if (BundleCacheInitInfo.Contains(CacheName))
		{
			ensureAlwaysMsgf(false, TEXT("Malformed entry in InstallBundleManager.BundleCaches"));
			InitResult = EInstallBundleManagerInitResult::ConfigurationError;
			return;
		}

		FInstallBundleCacheInitInfo& InitInfo = BundleCacheInitInfo.Emplace(CacheName);
		InitInfo.CacheName = CacheName;
		InitInfo.Size = CacheSize;
	}

	// Apply cache size runtime overrides
	for (const TPair<FName, uint64>& BundleCacheSizeOverride : BundleCacheSizeOverrides)
	{
		FInstallBundleCacheInitInfo* InitInfo = BundleCacheInitInfo.Find(BundleCacheSizeOverride.Key);
		if (ensureMsgf(InitInfo, TEXT("Size override cannot be applied on cache '%s' because it doesn't exist"), *BundleCacheSizeOverride.Key.ToString()))
		{
			InitInfo->Size = BundleCacheSizeOverride.Value;
		}
	}

	// Check to override cache size from command line
	{
		FString Mapping;
		if (FParse::Value(FCommandLine::Get(), TEXT("InstallBundleCacheSize"), Mapping, false))
		{
			Mapping.ReplaceInline(TEXT("("), TEXT(""));
			Mapping.ReplaceInline(TEXT(")"), TEXT(""));

			FName CacheName;
			uint64 CacheSize = 0;
			if (!FParse::Value(*Mapping, PropertyName, CacheName) ||
				!FParse::Value(*Mapping, PropertySize, CacheSize))
			{
				ensureAlwaysMsgf(false, TEXT("Malformed entry from command line %s"), *Mapping);
				InitResult = EInstallBundleManagerInitResult::ConfigurationError;
				return;
			}

			FInstallBundleCacheInitInfo* InitInfo = BundleCacheInitInfo.Find(CacheName);
			if (InitInfo == nullptr)
			{
				ensureAlwaysMsgf(false, TEXT("No cache entry found for command line %s"), *Mapping);
				InitResult = EInstallBundleManagerInitResult::ConfigurationError;
				return;
			}

			InitInfo->Size = CacheSize;
		}
	}

	TSet<FInstallBundleSourceType> UniqueSources;
	for (FString& Mapping : ConfigBundleSourceCaches)
	{
		// Remove parentheses
		Mapping.TrimStartAndEndInline();
		Mapping.ReplaceInline(TEXT("("), TEXT(""));
		Mapping.ReplaceInline(TEXT(")"), TEXT(""));

		TArray<FString> Tokens;
		if (2 != Mapping.ParseIntoArrayWS(Tokens, TEXT(",")))
		{
			ensureAlwaysMsgf(false, TEXT("Malformed entry in InstallBundleManager.BundleCaches"));
			InitResult = EInstallBundleManagerInitResult::ConfigurationError;
		}

		FInstallBundleSourceType Source(Tokens[0]);
		FName CacheName = *Tokens[1];

		FInstallBundleCacheInitInfo* InitInfo = BundleCacheInitInfo.Find(CacheName);
		if (!Source.IsValid() || InitInfo == nullptr)
		{
			ensureAlwaysMsgf(false, TEXT("Malformed entry in InstallBundleManager.BundleCaches"));
			InitResult = EInstallBundleManagerInitResult::ConfigurationError;
			return;
		}

		if (UniqueSources.Contains(Source))
		{
			ensureAlwaysMsgf(false, TEXT("An install bundle source may only map to one bundle cache!"));
			InitResult = EInstallBundleManagerInitResult::ConfigurationError;
			return;
		}

		UniqueSources.Add(Source);

		// Create the cache lazily so we don't create caches that are not referenced
		if (!BundleCaches.Contains(CacheName))
		{
			BundleCaches.Emplace(CacheName, MakeShared<FInstallBundleCache>())->Init(MoveTemp(*InitInfo));
		}

		BundleSourceCaches.Add(Source, CacheName);

		LOG_INSTALL_BUNDLE_MAN(Display, TEXT("Using Bundle Cache %s for Bundle Source %s"), *CacheName.ToString(), LexToString(Source));
	}

	InitStepResult = EAsyncInitStepResult::Done;
}

void FDefaultInstallBundleManager::AsyncInit_QueryBundleInfo()
{
	LOG_INSTALL_BUNDLE_MAN(Display, TEXT("Querying bundle sources for bundle info..."));

	check(BundleSources.Num());

	InitStepResult = EAsyncInitStepResult::Waiting;

	BundleSourceBundleInfoQueryResults.Empty(BundleSources.Num());
	for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
	{
		Pair.Value->AsyncInit_QueryBundleInfo(FInstallBundleSourceQueryBundleInfoDelegate::CreateRaw(this, &FDefaultInstallBundleManager::AsyncInit_OnQueryBundleInfoComplete));
	}
}

void FDefaultInstallBundleManager::AsyncInit_OnQueryBundleInfoComplete(TSharedRef<IInstallBundleSource> InSource, FInstallBundleSourceBundleInfoQueryResult InResult)
{
	BundleSourceBundleInfoQueryResults.Add(InSource->GetSourceType(), MoveTemp(InResult));

	if (BundleSourceBundleInfoQueryResults.Num() != BundleSources.Num())
		return;

	LOG_INSTALL_BUNDLE_MAN(Display, TEXT("All bundle source bundle info querys complete"));

	// Manually set bIsCurrentlyInAsyncInit here because we are an arbitrary callback and there is no
	// task or anything to do it for us
	bIsCurrentlyInAsyncInit = true;
	ON_SCOPE_EXIT
	{
		bIsCurrentlyInAsyncInit = false;
	};

	// Pass one, add all the bundles
	for (const TPair<FInstallBundleSourceType, FInstallBundleSourceBundleInfoQueryResult>& SourcePair : BundleSourceBundleInfoQueryResults)
	{
		for (const TPair<FName, FInstallBundleSourcePersistentBundleInfo>& BundleInfoPair : SourcePair.Value.SourceBundleInfoMap)
		{
			const FName& BundleName = BundleInfoPair.Key;
			const FInstallBundleSourcePersistentBundleInfo& SourceBundleInfo = BundleInfoPair.Value;

			FBundleInfo& BundleInfo = BundleInfoMap.FindOrAdd(BundleName);
			if (BundleInfo.BundleNameString.IsEmpty())
			{
				BundleInfo.BundleNameString = SourceBundleInfo.BundleNameString;
			}

			if (!SourceBundleInfo.bIsCached)
				continue;

			// Add to bundle cache if needed
			if (FName* CacheName = BundleSourceCaches.Find(SourcePair.Key))
			{
				const TSharedRef<FInstallBundleCache>& BundleCache = BundleCaches.FindChecked(*CacheName);
				const TSharedPtr<IInstallBundleSource>& BundleSource = BundleSources.FindChecked(SourcePair.Key);

				FInstallBundleCacheBundleInfo CacheBundleInfo;
				CacheBundleInfo.BundleName = BundleName;
				CacheBundleInfo.FullInstallSize = SourceBundleInfo.FullInstallSize;
				CacheBundleInfo.InstallOverheadSize = SourceBundleInfo.InstallOverheadSize;
				CacheBundleInfo.CurrentInstallSize = SourceBundleInfo.CurrentInstallSize;
				CacheBundleInfo.TimeStamp = SourceBundleInfo.LastAccessTime;
				CacheBundleInfo.AgeScalar = BundleSource->GetSourceCacheAgeScalar();

				BundleCache->AddOrUpdateBundle(SourcePair.Key, CacheBundleInfo);
			}
		}
	}

	bool bFoundInstallBundleDefinitions = false;
	bool bFoundStartup = false;

	// Pass two, update status
	for (TPair<FName, FBundleInfo>& BundleInfoPair : BundleInfoMap)
	{
		bFoundInstallBundleDefinitions = true;

		const FName& BundleName = BundleInfoPair.Key;
		FBundleInfo& BundleInfo = BundleInfoPair.Value;

		EInstallBundlePriority Priority = EInstallBundlePriority::Low;
		bool bIsStartup = false;
		bool bDoPatchCheck = false;
		EBundleState BundleStatus = EBundleState::NeedsMount;

		TArray<FBundleSourceRelevance> ContributingBundleSources;

		for (const TPair<FInstallBundleSourceType, FInstallBundleSourceBundleInfoQueryResult>& SourcePair : BundleSourceBundleInfoQueryResults)
		{
			const FInstallBundleSourcePersistentBundleInfo* SourceBundleInfo = SourcePair.Value.SourceBundleInfoMap.Find(BundleName);
			if (SourceBundleInfo == nullptr)
				continue;

			if (SourceBundleInfo->Priority < Priority)
			{
				Priority = SourceBundleInfo->Priority;
			}

			if (SourceBundleInfo->bContainsIoStoreOnDemandToc)
			{
				BundleInfo.bContainsIoStoreOnDemandTocs = true;
			}

			if (SourceBundleInfo->bIsStartup)
			{
				bIsStartup = true;
			}

			if (SourceBundleInfo->bDoPatchCheck)
			{
				bDoPatchCheck = true;
			}

			ContributingBundleSources.Add(FBundleSourceRelevance{ SourcePair.Key, true });

			if (SourceBundleInfo->BundleContentState == EInstallBundleInstallState::NotInstalled)
			{
				if (BundleStatus != EBundleState::NotInstalled)
				{
					BundleStatus = EBundleState::NotInstalled;
				}
			}
			else if (SourceBundleInfo->BundleContentState == EInstallBundleInstallState::NeedsUpdate)
			{
				if (BundleStatus == EBundleState::NeedsMount)
				{
					BundleStatus = EBundleState::NeedsUpdate;
				}
			}
		}

		BundleInfo.Priority = Priority;

		if (bIsStartup)
		{
			ensureMsgf(bFoundStartup == false, TEXT("There can only be one startup bundle!"));
			bFoundStartup = true;
			BundleInfo.bIsStartup = bIsStartup;
		}

		if (bDoPatchCheck)
		{
			BundleInfo.Prereqs.AddUnique(EBundlePrereqs::RequiresLatestClient);
		}

		BundleInfo.ContributingSources = MoveTemp(ContributingBundleSources);

		SetBundleStatus(BundleInfo, BundleStatus);
	}

	if (!bFoundInstallBundleDefinitions)
	{
		InitResult = EInstallBundleManagerInitResult::BuildMetaDataParsingError;
	}

	ensureMsgf(bFoundStartup, TEXT("Failed to find a bundle with startup content."));

	// Don't need these anymore
	BundleSourceBundleInfoQueryResults.Empty();

	InitStepResult = EAsyncInitStepResult::Done;
}

void FDefaultInstallBundleManager::AsyncInit_SetUpdateBundleInfoCallback()
{
	LOG_INSTALL_BUNDLE_MAN(Display, TEXT("Setting bundle source callback to update bundle info..."));

	for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
	{
		Pair.Value->AsyncInit_SetUpdateBundleInfoCallback(
			FInstallBundleSourceUpdateBundleInfoDelegate::CreateRaw(this, &FDefaultInstallBundleManager::OnUpdateBundleInfoFromSource),
			FInstallBundleLostRelevanceForSourceDelegate::CreateRaw(this, &FDefaultInstallBundleManager::OnBundleLostRelevanceForSource)
		);
	}

	InitStepResult = EAsyncInitStepResult::Done;
}

void FDefaultInstallBundleManager::AsyncInit_CreateAnalyticsSession()
{
	LOG_INSTALL_BUNDLE_MAN(Display, TEXT("Setting up analytics"));

	if (AnalyticsProvider)
	{
		// Pick the latest content version from all sources
		FString ContentVersion;
		int64 MaxVersionCL = -1;
		for (const TPair<FInstallBundleSourceType, TSharedPtr<IInstallBundleSource>>& Pair : BundleSources)
		{
			const TSharedPtr<IInstallBundleSource>& Source = Pair.Value;
			FString SourceContentVersion = Source->GetContentVersion();
			int64 SourceContentVersionCL = -1;

			FString CLString;
			if (SourceContentVersion.Split(TEXT("CL-"), nullptr, &CLString, ESearchCase::CaseSensitive))
			{
				CLString.Split(TEXT("-"), &CLString, nullptr, ESearchCase::CaseSensitive);
				LexTryParseString(SourceContentVersionCL, *CLString);
			}

			if (MaxVersionCL == -1 || MaxVersionCL < SourceContentVersionCL)
			{
				MaxVersionCL = SourceContentVersionCL;
				ContentVersion = SourceContentVersion;
			}
		}
		ensureAlways(MaxVersionCL != -1);

		AnalyticsProvider->SetSessionID(FString(TEXT("IBMInstallSession-")) + FGuid::NewGuid().ToString() + TEXT("-") + ContentVersion);
	}
	InitStepResult = EAsyncInitStepResult::Done;
}

void FDefaultInstallBundleManager::AsyncInit_FireInitAnlaytic(bool bCanRetry)
{
	LOG_INSTALL_BUNDLE_MAN(Display, TEXT("Fire Init Analytic: %s"), LexToString(InitResult));

	if (InitResult == EInstallBundleManagerInitResult::OK)
	{
		for (const TPair<FName, TSharedRef<FInstallBundleCache>>& Pair : BundleCaches)
		{
			InstallBundleManagerAnalytics::FireEvent_BundleManagerCacheStats(
				AnalyticsProvider.Get(),
				Pair.Value->GetStats(EInstallBundleCacheStatsFlags::DumpToLog));
		}
	}

	InstallBundleManagerAnalytics::FireEvent_InitBundleManagerComplete(AnalyticsProvider.Get(),
		bCanRetry, LexToString(InitResult));

	InitStepResult = EAsyncInitStepResult::Done;
}

void FDefaultInstallBundleManager::StatsBegin(FName BundleName)
{
	StatsMap->StatsBegin(BundleName);
}

void FDefaultInstallBundleManager::StatsEnd(FName BundleName)
{
	StatsMap->StatsEnd(BundleName);
}

void FDefaultInstallBundleManager::StatsBegin(FName BundleName, EContentRequestState State)
{
	StatsMap->StatsBegin(BundleName, LexToString(State));
}

void FDefaultInstallBundleManager::StatsEnd(FName BundleName, EContentRequestState State, uint64 DataSize /*= 0*/)
{
	StatsMap->StatsEnd(BundleName, LexToString(State), DataSize);
}

void FDefaultInstallBundleManager::LogStats(FName BundleName, ELogVerbosity::Type LogVerbosityOverride)
{
	const InstallBundleUtil::FContentRequestStats& RequestStats = StatsMap->GetMap().FindChecked(BundleName);
	check(!RequestStats.bOpen);

	InstallBundleManagerUtil::LogBundleRequestStats(*BundleName.ToString(), RequestStats, LogVerbosityOverride);
}



const TCHAR* LexToString(FDefaultInstallBundleManager::EContentRequestBatch Val)
{
	static const TCHAR* Strings[] =
	{
		TEXT("Requested"),
		TEXT("Cache"),
		TEXT("Install"),
	};

	return InstallBundleUtil::TLexToString(Val, Strings);
}

const TCHAR* LexToString(FDefaultInstallBundleManager::EContentReleaseRequestBatch Val)
{
	static const TCHAR* Strings[] =
	{
		TEXT("Requested"),
		TEXT("Release"),
	};

	return InstallBundleUtil::TLexToString(Val, Strings);
}




class FDefaultInstallBundleManagerModule : public TInstallBundleManagerModule<FDefaultInstallBundleManager>
{
public:
	virtual bool IsGameModule() const override
	{
		return true;
	}
};

IMPLEMENT_GAME_MODULE(FDefaultInstallBundleManagerModule, DefaultInstallBundleManager)

