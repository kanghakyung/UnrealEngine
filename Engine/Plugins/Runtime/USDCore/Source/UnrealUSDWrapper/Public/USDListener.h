// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ContainersFwd.h"
#include "Containers/Map.h"
#include "Delegates/Delegate.h"

#include "UsdWrappers/ForwardDeclarations.h"
#include "UsdWrappers/VtValue.h"

class FUsdListenerImpl;

namespace UE
{
	class FSdfPath;
}

namespace UsdUtils
{
	/** Analogous to pxr::SdfChangeList::Entry::_Flags */
	struct FPrimChangeFlags
	{
		bool bDidChangeIdentifier : 1;
		bool bDidChangeResolvedPath : 1;
		bool bDidReplaceContent : 1;
		bool bDidReloadContent : 1;
		bool bDidReorderChildren : 1;
		bool bDidReorderProperties : 1;
		bool bDidRename : 1;
		bool bDidChangePrimVariantSets : 1;
		bool bDidChangePrimInheritPaths : 1;
		bool bDidChangePrimSpecializes : 1;
		bool bDidChangePrimReferences : 1;
		bool bDidChangeAttributeTimeSamples : 1;
		bool bDidChangeAttributeConnection : 1;
		bool bDidChangeRelationshipTargets : 1;
		bool bDidAddTarget : 1;
		bool bDidRemoveTarget : 1;
		bool bDidAddInertPrim : 1;
		bool bDidAddNonInertPrim : 1;
		bool bDidRemoveInertPrim : 1;
		bool bDidRemoveNonInertPrim : 1;
		bool bDidAddPropertyWithOnlyRequiredFields : 1;
		bool bDidAddProperty : 1;
		bool bDidRemovePropertyWithOnlyRequiredFields : 1;
		bool bDidRemoveProperty : 1;

		FPrimChangeFlags()
		{
			FMemory::Memset(this, 0, sizeof(*this));
		}
	};

	/**
	 * Analogous to pxr::SdfChangeList::Entry::InfoChange, describes a change to an attribute.
	 * Here we break off PropertyName and Field for simplicity
	 */
	struct FFieldChange
	{
		FString Field;						  // default, variability, timeSamples, etc.
		UE::FVtValue OldValue;				  // Can be empty when we create a new attribute opinion
		UE::FVtValue NewValue;				  // Can be empty when we clear an existing attribute opinion
	};
	using FAttributeChange = FFieldChange;	  // Renamed in 5.6 as this can refer to a relationship, or even prim metadata

	/** Analogous to pxr::SdfChangeList::SubLayerChangeType, describes a change to a sublayer */
	enum ESubLayerChangeType
	{
		SubLayerAdded,
		SubLayerRemoved,
		SubLayerOffset
	};

	/** Analogous to pxr::SdfChangeList::Entry, describes a generic change to an object */
	struct FSdfChangeListEntry
	{
		TArray<FFieldChange> FieldChanges;
		FPrimChangeFlags Flags;
		FString OldPath;		  // Empty if Flags.bDidRename is not set
		FString OldIdentifier;	  // Empty if Flags.bDIdChangeIdentifier is not set
		TArray<TPair<FString, ESubLayerChangeType>> SubLayerChanges;
	};

	using FObjectChangeNotice = FSdfChangeListEntry;	// Renamed in 5.3 as it is used for layer changes now too

	using FSdfChangeList = TArray<TPair<UE::FSdfPath, FSdfChangeListEntry>>;
	using FLayerToSdfChangeList = TArray<TPair<UE::FSdfLayerWeak, FSdfChangeList>>;

	/**
	 * Describes USD object changes by object path (an object can be a prim, a property, etc.)
	 */
	using FObjectChangesByPath = TMap<FString, TArray<FSdfChangeListEntry>>;
}	 // namespace UsdUtils

/**
 * Registers to Usd Notices and emits events when the Usd Stage has changed
 */
class UNREALUSDWRAPPER_API FUsdListener
{
public:
	FUsdListener();
	FUsdListener(const UE::FUsdStage& Stage);

	FUsdListener(const FUsdListener& Other) = delete;
	FUsdListener(FUsdListener&& Other) = delete;

	virtual ~FUsdListener();

	FUsdListener& operator=(const FUsdListener& Other) = delete;
	FUsdListener& operator=(FUsdListener&& Other) = delete;

	void Register(const UE::FUsdStage& Stage);

	// Increment/decrement the block counter
	void Block();
	void Unblock();
	bool IsBlocked() const;

	// Stage-specific events
	DECLARE_TS_MULTICAST_DELEGATE(FOnStageEditTargetChanged);
	FOnStageEditTargetChanged& GetOnStageEditTargetChanged();

	DECLARE_TS_MULTICAST_DELEGATE_OneParam(FOnLayersChanged, const TArray<FString>&);
	UE_DEPRECATED(5.3, "Use GetOnSdfLayersChanged")
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FOnLayersChanged& GetOnLayersChanged();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	DECLARE_TS_MULTICAST_DELEGATE_OneParam(FOnSdfLayersChanged, const UsdUtils::FLayerToSdfChangeList&);
	FOnSdfLayersChanged& GetOnSdfLayersChanged();

	DECLARE_TS_MULTICAST_DELEGATE(FOnSdfLayerDirtinessChanged);
	FOnSdfLayerDirtinessChanged& GetOnSdfLayerDirtinessChanged();

	DECLARE_TS_MULTICAST_DELEGATE_TwoParams(
		FOnObjectsChanged,
		const UsdUtils::FObjectChangesByPath& /* InfoChanges */,
		const UsdUtils::FObjectChangesByPath& /* ResyncChanges */
	);
	FOnObjectsChanged& GetOnObjectsChanged();

private:
	TUniquePtr<FUsdListenerImpl> Impl;
};

class UNREALUSDWRAPPER_API FScopedBlockNotices final
{
public:
	explicit FScopedBlockNotices(FUsdListener& InListener);
	~FScopedBlockNotices();

	FScopedBlockNotices() = delete;
	FScopedBlockNotices(const FScopedBlockNotices&) = delete;
	FScopedBlockNotices(FScopedBlockNotices&&) = delete;
	FScopedBlockNotices& operator=(const FScopedBlockNotices&) = delete;
	FScopedBlockNotices& operator=(FScopedBlockNotices&&) = delete;

private:
	FUsdListener& Listener;
};
