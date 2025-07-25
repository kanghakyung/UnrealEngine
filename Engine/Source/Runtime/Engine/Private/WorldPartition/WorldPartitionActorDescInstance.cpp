// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionActorDescInstance.h"

#if WITH_EDITOR

#include "WorldPartition/ActorDescContainerInstance.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "UObject/MetaData.h"

#define LOCTEXT_NAMESPACE "FWorldPartitionActorDescInstance"

FWorldPartitionActorDescInstance::FWorldPartitionActorDescInstance()
	: ContainerInstance(nullptr)
	, SoftRefCount(0)
	, HardRefCount(0)
	, bIsForcedNonSpatiallyLoaded(false)
	, bIsRegisteringOrUnregistering(false)
	, UnloadedReason(nullptr)
	, AsyncLoadID(INDEX_NONE)
	, ActorDesc(nullptr)
	, ChildContainerInstance(nullptr)
{}

FWorldPartitionActorDescInstance::FWorldPartitionActorDescInstance(UActorDescContainerInstance* InContainerInstance, FWorldPartitionActorDesc* InActorDesc)
	: FWorldPartitionActorDescInstance()
{
	check(InContainerInstance);
	ContainerInstance = InContainerInstance;

	check(InActorDesc);
	ActorDesc = InActorDesc;
}

void FWorldPartitionActorDescInstance::UpdateActorDesc(FWorldPartitionActorDesc* InActorDesc)
{
	ActorDesc = InActorDesc;
}

bool FWorldPartitionActorDescInstance::IsLoaded(bool bEvenIfPendingKill) const
{
	if (AsyncLoadID != INDEX_NONE)
	{
		return false;
	}

	if (ActorPtr.IsExplicitlyNull() || ActorPtr.IsStale())
	{
		ActorPtr = FindObject<AActor>(nullptr, *GetActorSoftPath().ToString());
	}

	return ActorPtr.IsValid(bEvenIfPendingKill);
}

AActor* FWorldPartitionActorDescInstance::GetActor(bool bEvenIfPendingKill, bool bEvenIfUnreachable) const
{
	FlushAsyncLoad();

	if (ActorPtr.IsExplicitlyNull() || ActorPtr.IsStale())
	{
		ActorPtr = FindObject<AActor>(nullptr, *GetActorSoftPath().ToString());
	}

	return bEvenIfUnreachable ? ActorPtr.GetEvenIfUnreachable() : ActorPtr.Get(bEvenIfPendingKill);
}

TWeakObjectPtr<AActor>* FWorldPartitionActorDescInstance::GetActorPtr(bool bEvenIfPendingKill, bool bEvenIfUnreachable) const
{
	return GetActor(bEvenIfPendingKill, bEvenIfUnreachable) ? &ActorPtr : nullptr;
}

FSoftObjectPath FWorldPartitionActorDescInstance::GetActorSoftPath() const
{
	return ActorPath.IsSet() ? ActorPath.GetValue() : ActorDesc->GetActorSoftPath();
}

FName FWorldPartitionActorDescInstance::GetActorName() const
{
	return ActorDesc->GetActorName();
}

bool FWorldPartitionActorDescInstance::IsValid() const
{
	return !!ActorDesc;
}

bool FWorldPartitionActorDescInstance::IsEditorRelevant() const
{
	return ActorDesc->IsEditorRelevant(this);
}

bool FWorldPartitionActorDescInstance::IsRuntimeRelevant() const
{
	return ActorDesc->IsRuntimeRelevant(this);
}

FBox FWorldPartitionActorDescInstance::GetEditorBounds() const
{
	return ActorDesc->GetEditorBounds().TransformBy(GetContainerInstance()->GetTransform());
}

FBox FWorldPartitionActorDescInstance::GetRuntimeBounds() const
{
	return ActorDesc->GetRuntimeBounds().TransformBy(GetContainerInstance()->GetTransform());
}

bool FWorldPartitionActorDescInstance::StartAsyncLoad()
{
	UnloadedReason = nullptr;

	if (ActorPtr.IsExplicitlyNull() || ActorPtr.IsStale())
	{
		// First, try to find the existing actor which could have been loaded by another actor (through standard serialization)
		ActorPtr = FindObject<AActor>(nullptr, * GetActorSoftPath().ToString());
	}

	// Then, if the actor isn't loaded, load it
	if (ActorPtr.IsExplicitlyNull())
	{
		const FName ActorPackage = GetActorPackage();
		const FLinkerInstancingContext* InstancingContext = GetContainerInstance()->GetInstancingContext();		
		const FName PackageName = InstancingContext ? InstancingContext->RemapPackage(ActorPackage) : ActorPackage;
		const FPackagePath PackagePath = FPackagePath::FromPackageNameChecked(ActorPackage);

		AsyncLoadID = LoadPackageAsync(PackagePath, PackageName, FLoadPackageAsyncDelegate::CreateLambda([this, ActorPackage](const FName& PackageName, UPackage* Package, EAsyncLoadingResult::Type Result)
		{
			if (AsyncLoadID != INDEX_NONE)
			{
				AsyncLoadFinished(ActorPackage, PackageName, Package, Result == EAsyncLoadingResult::Succeeded);
				AsyncLoadID = INDEX_NONE;
			}
		})
		, PKG_None, INDEX_NONE, 0, InstancingContext);
	}

	return (AsyncLoadID != INDEX_NONE) || ActorPtr.IsValid(false);
}

void FWorldPartitionActorDescInstance::FlushAsyncLoad() const
{
	if (AsyncLoadID != INDEX_NONE)
	{
		// Instead relying on AsyncLoading to call the callback during a flush, we'll do it ourselves here explicitly
		// This is because during a callstack where we are already async loading, we may not get completion callbacks called before returning from this flush.
		// Setting AsyncLoadID to INDEX_NONE before flushing so that we do not end up calling AsyncLoadFinished twice in cases where the completion callback is called during the flush
		int32 IDToFlush = AsyncLoadID;
		AsyncLoadID = INDEX_NONE;
		FlushAsyncLoading(IDToFlush);

		// Now call AsyncLoadFinished to assign ActorPtr
		const FName ActorPackage = GetActorPackage();
		const FLinkerInstancingContext* InstancingContext = GetContainerInstance()->GetInstancingContext();
		const FName PackageName = InstancingContext ? InstancingContext->RemapPackage(ActorPackage) : ActorPackage;
		UPackage* Package = FindPackage(nullptr, *PackageName.ToString());
		AsyncLoadFinished(ActorPackage, PackageName, Package, Package != nullptr);
	}
}

void FWorldPartitionActorDescInstance::AsyncLoadFinished(const FName& ActorPackage, const FName& PackageName, UPackage* Package, bool bSuccessful) const
{
	static FText FailedToLoad(LOCTEXT("FailedToLoadReason", "Failed to load"));

	if (!bSuccessful || !Package)
	{
		UE_LOG(LogWorldPartition, Warning, TEXT("Can't load actor guid `%s` ('%s') from package '%s'"), *GetGuid().ToString(), *GetActorNameString(), *ActorPackage.ToString());
		UnloadedReason = &FailedToLoad;
		return;
	}

	ActorPtr = FindObject<AActor>(nullptr, *GetActorSoftPath().ToString());

	if (!ActorPtr.IsValid())
	{
		UE_LOG(LogWorldPartition, Warning, TEXT("Can't find actor guid `%s` ('%s') in package '%s'"), *GetGuid().ToString(), *GetActorNameString(), *ActorPackage.ToString());
		UnloadedReason = &FailedToLoad;
		return;
	}

	check(ActorPtr->GetPackage() == Package);
}

void FWorldPartitionActorDescInstance::MarkUnload()
{
	FlushAsyncLoad();

	if (AActor* Actor = GetActor())
	{
		// At this point, it can happen that an actor isn't in an external package:
		//
		// PIE travel: 
		//		in this case, actors referenced by the world package (an example is the level script) will be duplicated as part of the PIE world duplication and will end up
		//		not being using an external package, which is fine because in that case they are considered as always loaded.
		//
		// FWorldPartitionCookPackageSplitter:
		//		should mark each FWorldPartitionActorDesc as moved, and the splitter should take responsbility for calling ClearFlags on every object in 
		//		the package when it does the move.

		ActorPtr = nullptr;
	}
}

void FWorldPartitionActorDescInstance::Invalidate()
{
	check(!ChildContainerInstance);
	ContainerInstance = nullptr;
}

const FDataLayerInstanceNames& FWorldPartitionActorDescInstance::GetDataLayerInstanceNames() const
{
	static FDataLayerInstanceNames EmptyDataLayers;
	if (ensure(HasResolvedDataLayerInstanceNames()))
	{
		return ResolvedDataLayerInstanceNames.GetValue();
	}
	return EmptyDataLayers;
}

const FText& FWorldPartitionActorDescInstance::GetUnloadedReason() const
{
	static FText Unloaded(LOCTEXT("UnloadedReason", "Unloaded"));
	return UnloadedReason ? *UnloadedReason : Unloaded;
}

const FString& FWorldPartitionActorDescInstance::GetActorNameString() const
{
	return ActorDesc->GetActorNameString();
}

const FString& FWorldPartitionActorDescInstance::GetActorLabelString() const
{
	return ActorDesc->GetActorLabelString();
}

const FString& FWorldPartitionActorDescInstance::GetDisplayClassNameString() const
{
	return ActorDesc->GetDisplayClassNameString();
}

FString FWorldPartitionActorDescInstance::ToString(FWorldPartitionActorDesc::EToStringMode Mode) const
{
	return ActorDesc->ToString(Mode);
}

void FWorldPartitionActorDescInstance::RegisterChildContainerInstance()
{
	check(IsChildContainerInstance());
	check(!ChildContainerInstance);

	ChildContainerInstance = ActorDesc->CreateChildContainerInstance(this);
	if (ChildContainerInstance)
	{
		ContainerInstance->OnRegisterChildContainerInstance(GetGuid(), ChildContainerInstance);
	}
}

void FWorldPartitionActorDescInstance::UnregisterChildContainerInstance()
{
	if (ChildContainerInstance)
	{
		ContainerInstance->OnUnregisterChildContainerInstance(GetGuid());
		ChildContainerInstance->Uninitialize();
		ChildContainerInstance = nullptr;
	}
}

void FWorldPartitionActorDescInstance::UpdateChildContainerInstance()
{
	// Create before unregistering so that we benefit from shared containers (use GetActorDesc->IsChildContainerInstance as we want to know if our updated desc should be a Container instance or not)
	// ChildContainerInstance member might be non null and we don't want IsChildContainerInstance() to return true in this case if the ActorDesc isn't a Container anymore
	UActorDescContainerInstance* NewChildContainerInstance = ActorDesc->IsChildContainerInstance() ? ActorDesc->CreateChildContainerInstance(this) : nullptr;
	
	// Unregister previous
	if (ChildContainerInstance)
	{
		ContainerInstance->OnUnregisterChildContainerInstance(GetGuid());
		ChildContainerInstance->Uninitialize();
		ChildContainerInstance = nullptr;
	}
	
	// Register new if it is valid
	if (NewChildContainerInstance)
	{
		ChildContainerInstance = NewChildContainerInstance;
		ContainerInstance->OnRegisterChildContainerInstance(GetGuid(), ChildContainerInstance);
	}
}

#undef LOCTEXT_NAMESPACE

#endif