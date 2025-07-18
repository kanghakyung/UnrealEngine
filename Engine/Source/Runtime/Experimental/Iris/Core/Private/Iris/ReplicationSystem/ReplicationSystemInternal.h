// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Iris/ReplicationState/ReplicationStateStorage.h"
#include "Iris/ReplicationSystem/DirtyNetObjectTracker.h"
#include "Iris/ReplicationSystem/NetRefHandleManager.h"
#include "Iris/ReplicationSystem/ChangeMaskCache.h"
#include "Iris/ReplicationSystem/Conditionals/ReplicationConditionals.h"
#include "Iris/ReplicationSystem/DeltaCompression/DeltaCompressionBaselineManager.h"
#include "Iris/ReplicationSystem/DeltaCompression/DeltaCompressionBaselineInvalidationTracker.h"
#include "Iris/ReplicationSystem/Filtering/ReplicationFiltering.h"
#include "Iris/ReplicationSystem/Filtering/NetObjectGroups.h"
#include "Iris/ReplicationSystem/ObjectReferenceCache.h"
#include "Iris/ReplicationSystem/ReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationConnections.h"
#include "Iris/ReplicationSystem/Prioritization/ReplicationPrioritization.h"
#include "Iris/ReplicationSystem/ReplicationProtocolManager.h"
#include "Iris/ReplicationSystem/NetBlob/NetBlobManager.h"
#include "Net/Core/NetToken/NetToken.h"
#include "Iris/ReplicationSystem/StringTokenStore.h"
#include "Iris/ReplicationSystem/NameTokenStore.h"
#include "Iris/ReplicationSystem/WorldLocations.h"
#include "Iris/ReplicationState/ReplicationStateDescriptorRegistry.h"
#include "Iris/Stats/NetStats.h"

class UIrisObjectReferencePackageMap;

namespace UE::Net::Private
{

struct FReplicationSystemInternalInitParams
{
	uint32 ReplicationSystemId;
	uint32 MaxReplicatedObjectCount;
	uint32 NetChunkedArrayCount;
	uint32 MaxReplicationWriterObjectCount;
};

class FReplicationSystemInternal
{
public:
	explicit FReplicationSystemInternal(const FReplicationSystemInternalInitParams& Params)
	: NetRefHandleManager(ReplicationProtocolManager)
	, InternalInitParams(Params)
	, DirtyNetObjectTracker()
	, ReplicationBridge(nullptr)
	, IrisObjectReferencePackageMap(nullptr)
	, Id(Params.ReplicationSystemId)
	{}

	FReplicationProtocolManager& GetReplicationProtocolManager() { return ReplicationProtocolManager; }

	FNetRefHandleManager& GetNetRefHandleManager() { return NetRefHandleManager; }
	const FNetRefHandleManager& GetNetRefHandleManager() const { return NetRefHandleManager; }

	void InitDirtyNetObjectTracker(const struct FDirtyNetObjectTrackerInitParams& Params) { DirtyNetObjectTracker.Init(Params); }
	bool IsDirtyNetObjectTrackerInitialized() const { return DirtyNetObjectTracker.IsInit(); }
	FDirtyNetObjectTracker& GetDirtyNetObjectTracker() { checkf(DirtyNetObjectTracker.IsInit(), TEXT("Not allowed to access the DirtyNetObjectTracker unless object replication is enabled.")); return DirtyNetObjectTracker; }

	FReplicationStateDescriptorRegistry& GetReplicationStateDescriptorRegistry() { return ReplicationStateDescriptorRegistry; }

	FReplicationStateStorage& GetReplicationStateStorage() { return ReplicationStateStorage; }

	FObjectReferenceCache& GetObjectReferenceCache() { return ObjectReferenceCache; }

	void SetReplicationBridge(UReplicationBridge* InReplicationBridge) { ReplicationBridge = InReplicationBridge; }
	UReplicationBridge* GetReplicationBridge() const { return ReplicationBridge; }
	UReplicationBridge* GetReplicationBridge(FNetRefHandle Handle) const { return ReplicationBridge; }

	void SetIrisObjectReferencePackageMap(UIrisObjectReferencePackageMap* InIrisObjectReferencePackageMap) { IrisObjectReferencePackageMap = InIrisObjectReferencePackageMap; }
	UIrisObjectReferencePackageMap* GetIrisObjectReferencePackageMap() { return IrisObjectReferencePackageMap; }

	FChangeMaskCache& GetChangeMaskCache() { return ChangeMaskCache; }

	FReplicationConnections& GetConnections() { return Connections; }

	FReplicationFiltering& GetFiltering() { return Filtering; }
	const FReplicationFiltering& GetFiltering() const { return Filtering; }

	FNetObjectGroups& GetGroups() { return Groups; }

	FReplicationConditionals& GetConditionals() { return Conditionals; }

	FReplicationPrioritization& GetPrioritization() { return Prioritization; }

	FNetBlobManager& GetNetBlobManager() { return NetBlobManager; }
	FNetBlobHandlerManager& GetNetBlobHandlerManager() { return NetBlobManager.GetNetBlobHandlerManager(); }
	const FNetBlobHandlerManager& GetNetBlobHandlerManager() const { return NetBlobManager.GetNetBlobHandlerManager(); }

	FWorldLocations& GetWorldLocations() { return WorldLocations; }

	FDeltaCompressionBaselineManager& GetDeltaCompressionBaselineManager() { return DeltaCompressionBaselineManager; }
	FDeltaCompressionBaselineInvalidationTracker& GetDeltaCompressionBaselineInvalidationTracker() { return DeltaCompressionBaselineInvalidationTracker; }

	FNetTypeStats& GetNetTypeStats() { return TypeStats; }

	FReplicationSystemInternalInitParams& GetInitParams() { return InternalInitParams; }

	FNetSendStats& GetSendStats()
	{ 
		return SendStats;
	}

	FReplicationStats& GetTickReplicationStats()
	{
		return TickReplicationStats;
	}

	const FReplicationStats& GetTickReplicationStats() const
	{
		return TickReplicationStats;
	}

	FReplicationStats& GetAccumulatedReplicationStats()
	{
		return AccumulatedReplicationStats;
	}

	const FReplicationStats& GetAccumulatedReplicationStats() const
	{
		return AccumulatedReplicationStats;
	}

	FForwardNetRPCCallMulticastDelegate& GetForwardNetRPCCallMulticastDelegate()
	{
		return ForwardNetRPCCallMulticastDelegate;
	}

	void SetBlockFilterChanges(bool bBlock) { bBlockFilterChanges = bBlock; }

	bool AreFilterChangesBlocked() const { return bBlockFilterChanges; }

private:
	FReplicationProtocolManager ReplicationProtocolManager;
	FNetRefHandleManager NetRefHandleManager;
	FReplicationSystemInternalInitParams InternalInitParams;
	FDirtyNetObjectTracker DirtyNetObjectTracker;
	FReplicationStateStorage ReplicationStateStorage;
	FReplicationStateDescriptorRegistry ReplicationStateDescriptorRegistry;
	UReplicationBridge* ReplicationBridge;
	UIrisObjectReferencePackageMap* IrisObjectReferencePackageMap;
	FChangeMaskCache ChangeMaskCache;
	FReplicationConnections Connections;
	FReplicationFiltering Filtering;
	FNetObjectGroups Groups;
	FReplicationConditionals Conditionals;
	FReplicationPrioritization Prioritization;
	FObjectReferenceCache ObjectReferenceCache;
	FNetBlobManager NetBlobManager;
	FWorldLocations WorldLocations;
	FDeltaCompressionBaselineManager DeltaCompressionBaselineManager;
	FDeltaCompressionBaselineInvalidationTracker DeltaCompressionBaselineInvalidationTracker;
	FNetSendStats SendStats;
	FNetTypeStats TypeStats;
	FReplicationStats TickReplicationStats = {};
	FReplicationStats AccumulatedReplicationStats = {};
	FForwardNetRPCCallMulticastDelegate ForwardNetRPCCallMulticastDelegate;
	uint32 Id;

	/** When true this prevents any changes to the filter system. Enabled during times where adding filter options is unsupported. */
	bool bBlockFilterChanges = false;
};

}
