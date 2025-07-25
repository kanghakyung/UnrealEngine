// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "CollectionManagerTypes.h"
#include "Styling/SlateTypes.h"

class ICollectionContainer;

/** Handles the collection management for the given assets */
class ASSETTOOLS_API FCollectionAssetManagement
{
public:
	/** Constructor */
	UE_DEPRECATED(5.6, "Use the ICollectionContainer constructor instead.")
	FCollectionAssetManagement();
	explicit FCollectionAssetManagement(const TSharedRef<ICollectionContainer>& InCollectionContainer);
	FCollectionAssetManagement(const FCollectionAssetManagement&) = delete;

	/** Destructor */
	~FCollectionAssetManagement();

	FCollectionAssetManagement& operator=(const FCollectionAssetManagement&) = delete;

	/** Set the assets that we are currently observing and managing the collection state of */
	void SetCurrentAssets(const TArray<FAssetData>& CurrentAssets);

	/** Set the asset paths that we are currently observing and managing the collection state of */
	void SetCurrentAssetPaths(const TArray<FSoftObjectPath>& CurrentAssets);

	/** Get the number of assets in the current set */
	int32 GetCurrentAssetCount() const;

	/** Add the current assets to the given collection */
	void AddCurrentAssetsToCollection(FCollectionNameType InCollectionKey);

	/** Remove the current assets from the given collection */
	void RemoveCurrentAssetsFromCollection(FCollectionNameType InCollectionKey);

	/** Return whether or not the given collection should be enabled in any management UIs */
	bool IsCollectionEnabled(FCollectionNameType InCollectionKey) const;

	/** Get the check box state the given collection should use in any management UIs */
	ECheckBoxState GetCollectionCheckState(FCollectionNameType InCollectionKey) const;

private:
	/** Update the internal state used to track the check box status for each collection */
	void UpdateAssetManagementState();

	/** Handles an on collection renamed event */
	void HandleCollectionRenamed(ICollectionContainer&, const FCollectionNameType& OriginalCollection, const FCollectionNameType& NewCollection);

	/** Handles an on collection updated event */
	void HandleCollectionUpdated(ICollectionContainer&, const FCollectionNameType& Collection);

	/** Handles an on collection destroyed event */
	void HandleCollectionDestroyed(ICollectionContainer&, const FCollectionNameType& Collection);

	/** Handles assets being added to a collection */
	void HandleAssetsAddedToCollection(ICollectionContainer&, const FCollectionNameType& Collection, TConstArrayView<FSoftObjectPath> AssetsAdded);

	/** Handles assets being removed from a collection */
	void HandleAssetsRemovedFromCollection(ICollectionContainer&, const FCollectionNameType& Collection, TConstArrayView<FSoftObjectPath> AssetsRemoved);

	TSharedRef<ICollectionContainer> CollectionContainer;

	/** Set of asset paths that we are currently observing and managing the collection state of */
	TSet<FSoftObjectPath> CurrentAssetPaths;

	/** Mapping between a collection and its asset management state (based on the current assets). A missing item is assumed to be unchecked */
	TMap<FCollectionNameType, ECheckBoxState> AssetManagementState;

	/** Delegate handles */
	FDelegateHandle OnCollectionRenamedHandle;
	FDelegateHandle OnCollectionDestroyedHandle;
	FDelegateHandle OnCollectionUpdatedHandle;
	FDelegateHandle OnAssetsAddedHandle;
	FDelegateHandle OnAssetsRemovedHandle;
};
