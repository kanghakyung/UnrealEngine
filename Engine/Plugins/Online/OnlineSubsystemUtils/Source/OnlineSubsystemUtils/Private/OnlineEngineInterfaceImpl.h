// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Net/OnlineEngineInterface.h"
#include "OnlineEngineInterfaceImpl.generated.h"

#define UE_API ONLINESUBSYSTEMUTILS_API

struct FJoinabilitySettings;
struct FUniqueNetIdWrapper;

class Error;
class FVoicePacket;
struct FWorldContext;

UCLASS(MinimalAPI, config = Engine)
class UOnlineEngineInterfaceImpl : public UOnlineEngineInterface
{
	GENERATED_UCLASS_BODY()

public:
	UE_API virtual void PostInitProperties() override;

	/**
	 * Subsystem
	 */
public:

	UE_API virtual bool IsLoaded(FName OnlineIdentifier) override;
	UE_API virtual FName GetOnlineIdentifier(FWorldContext& WorldContext) override;
	UE_API virtual bool DoesInstanceExist(FName OnlineIdentifier) override;

	// NOTE: In OSSv1 it only shuts down the default type of subsystem instance corresponding to the identifier
	UE_API virtual void ShutdownOnlineSubsystem(FName OnlineIdentifier) override;
	UE_API virtual void DestroyOnlineSubsystem(FName OnlineIdentifier) override;
	UE_API virtual FName GetDefaultOnlineSubsystemName() const override;
	UE_API virtual bool IsCompatibleUniqueNetId(const FUniqueNetIdWrapper& InUniqueNetId) const override;

	/**
	 * Utils
	 */
	UE_API virtual uint8 GetReplicationHashForSubsystem(FName InSubsystemName) const override;
	UE_API virtual FName GetSubsystemFromReplicationHash(uint8 InHash) const override;

private:
	/** Mapping of unique net ids that should not be treated as foreign ids to the local subsystem. */
	UPROPERTY(config)
	TMap<FName, FName> MappedUniqueNetIdTypes;

	/** Array of unique net ids that are deemed valid when tested against gameplay login checks. */
	UPROPERTY(config)
	TArray<FName> CompatibleUniqueNetIdTypes;
	
	/** Allow the subsystem used for voice functions to be overridden, in case it needs to be different than the default subsystem. May be useful on console platforms. */
	UPROPERTY(config)
	FName VoiceSubsystemNameOverride;

	/** @return the identifier/context handle associated with a UWorld */
	FName GetOnlineIdentifier(UWorld* World);

	/** Returns the name of a corresponding dedicated server subsystem for the given subsystem, or NAME_None if such a system doesn't exist. */
	FName GetDedicatedServerSubsystemNameForSubsystem(const FName Subsystem) const;

	/**
	 * Identity
	 */
public:

	UE_API virtual FUniqueNetIdWrapper CreateUniquePlayerIdWrapper(const FString& Str, FName Type) override;
	UE_API virtual FUniqueNetIdWrapper GetUniquePlayerIdWrapper(UWorld* World, int32 LocalUserNum, FName Type) override;

	UE_API virtual FString GetPlayerNickname(UWorld* World, const FUniqueNetIdWrapper& UniqueId) override;
	UE_API virtual bool GetPlayerPlatformNickname(UWorld* World, int32 LocalUserNum, FString& OutNickname) override;

	UE_API virtual bool AutoLogin(UWorld* World, int32 LocalUserNum, const FOnlineAutoLoginComplete& InCompletionDelegate) override;
	UE_API virtual bool IsLoggedIn(UWorld* World, int32 LocalUserNum) override;

private:

	FDelegateHandle OnLoginCompleteDelegateHandle;
	void OnAutoLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error, FName OnlineIdentifier, FOnlineAutoLoginComplete InCompletionDelegate);

	/**
	 * Session
	 */
public:

	UE_API virtual void StartSession(UWorld* World, FName SessionName, FOnlineSessionStartComplete& InCompletionDelegate) override;
	UE_API virtual void EndSession(UWorld* World, FName SessionName, FOnlineSessionEndComplete& InCompletionDelegate) override;
	UE_API virtual bool DoesSessionExist(UWorld* World, FName SessionName) override;

	UE_API virtual bool GetSessionJoinability(UWorld* World, FName SessionName, FJoinabilitySettings& OutSettings) override;
	UE_API virtual void UpdateSessionJoinability(UWorld* World, FName SessionName, bool bPublicSearchable, bool bAllowInvites, bool bJoinViaPresence, bool bJoinViaPresenceFriendsOnly) override;

	UE_API virtual void RegisterPlayer(UWorld* World, FName SessionName, const FUniqueNetIdWrapper& UniqueId, bool bWasInvited) override;
	UE_API virtual void UnregisterPlayer(UWorld* World, FName SessionName, const FUniqueNetIdWrapper& UniqueId) override;
	UE_API virtual void UnregisterPlayers(UWorld* World, FName SessionName, const TArray<FUniqueNetIdWrapper>& Players) override;

	UE_API virtual bool GetResolvedConnectString(UWorld* World, FName SessionName, FString& URL) override;

private:

	/** Mapping of delegate handles for each online StartSession() call while in flight */
	TMap<FName, FDelegateHandle> OnStartSessionCompleteDelegateHandles;
	void OnStartSessionComplete(FName SessionName, bool bWasSuccessful, FName OnlineIdentifier, FOnlineSessionStartComplete CompletionDelegate);

	/** Mapping of delegate handles for each online EndSession() call while in flight */
	TMap<FName, FDelegateHandle> OnEndSessionCompleteDelegateHandles;
	void OnEndSessionComplete(FName SessionName, bool bWasSuccessful, FName OnlineIdentifier, FOnlineSessionEndComplete CompletionDelegate);

	/**
	 * Voice
	 */
public:

	UE_API virtual TSharedPtr<FVoicePacket> GetLocalPacket(UWorld* World, uint8 LocalUserNum) override;
	UE_API virtual TSharedPtr<FVoicePacket> SerializeRemotePacket(UWorld* World, const UNetConnection* const RemoteConnection, FArchive& Ar) override;

	UE_API virtual void StartNetworkedVoice(UWorld* World, uint8 LocalUserNum) override;
	UE_API virtual void StopNetworkedVoice(UWorld* World, uint8 LocalUserNum) override;
	UE_API virtual void ClearVoicePackets(UWorld* World) override;

	UE_API virtual bool MuteRemoteTalker(UWorld* World, uint8 LocalUserNum, const FUniqueNetIdWrapper& PlayerId, bool bIsSystemWide) override;
	UE_API virtual bool UnmuteRemoteTalker(UWorld* World, uint8 LocalUserNum, const FUniqueNetIdWrapper& PlayerId, bool bIsSystemWide) override;

	UE_API virtual int32 GetNumLocalTalkers(UWorld* World) override;

	/**
	 * External UI
	 */
public:

	UE_API virtual void ShowLeaderboardUI(UWorld* World, const FString& CategoryName) override;
	UE_API virtual void ShowAchievementsUI(UWorld* World, int32 LocalUserNum) override;
	UE_API virtual void BindToExternalUIOpening(const FOnlineExternalUIChanged& Delegate) override;
	UE_API virtual void ShowWebURL(const FString& CurrentURL, const UOnlineEngineInterface::FShowWebUrlParams& ShowParams, const FOnlineShowWebUrlClosed& CompletionDelegate) override;
	UE_API virtual bool CloseWebURL() override;

private:

	void OnExternalUIChange(bool bInIsOpening, FOnlineExternalUIChanged Delegate);

	/**
	 * Debug
	 */
public:

	UE_API virtual void DumpSessionState(UWorld* World) override;
	UE_API virtual void DumpPartyState(UWorld* World) override;
	UE_API virtual void DumpVoiceState(UWorld* World) override;
	UE_API virtual void DumpChatState(UWorld* World) override;

#if WITH_EDITOR
	/**
	 * PIE Utilities
	 */
public:

	UE_API virtual bool SupportsOnlinePIE() override;
	UE_API virtual void SetShouldTryOnlinePIE(bool bShouldTry) override;
	UE_API virtual int32 GetNumPIELogins() override;
	UE_API virtual FString GetPIELoginCommandLineArgs(int32 Index) override;
	UE_API virtual void SetForceDedicated(FName OnlineIdentifier, bool bForce) override;
	UE_API virtual void LoginPIEInstance(FName OnlineIdentifier, int32 LocalUserNum, int32 PIELoginNum, FOnPIELoginComplete& CompletionDelegate) override;
#endif

private:

	/** Mapping of delegate handles for each online Login() call while in flight */
	TMap<FName, FDelegateHandle> OnLoginPIECompleteDelegateHandlesForPIEInstances;
	void OnPIELoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error, FName OnlineIdentifier, FOnlineAutoLoginComplete InCompletionDelegate);

private:
	void InitCompatibilityInterface();

	/** Whether to enable a compatibility interface for transitioning from OSSv1 to OSSv2. */
	UPROPERTY(config)
	bool bOnlineServicesCompatibilityEnabled = false;

	UPROPERTY()
	TObjectPtr<UOnlineEngineInterface> OnlineServicesCompatibilityInterface;
};

#undef UE_API
