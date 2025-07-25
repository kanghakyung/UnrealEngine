// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_EDITOR
#include "CoreMinimal.h"
#include "Serialization/Archive.h"
#include "UObject/TopLevelAssetPath.h"
#include "UObject/FortniteMainBranchObjectVersion.h"

class FWorldPartitionActorDesc;
struct FWorldPartitionAssetDataPatcher;

class FActorDescArchive : public FArchiveProxy
{
public:
	FActorDescArchive(FArchive& InArchive, FWorldPartitionActorDesc* InActorDesc, const FWorldPartitionActorDesc* InBaseActorDesc = nullptr);

	void Init(const FTopLevelAssetPath InClassPath = FTopLevelAssetPath());

	//~ Begin FArchive Interface
	virtual FArchive& operator<<(FName& Value) override { FArchiveProxy::operator<<(Value); return *this; }
	virtual FArchive& operator<<(FText& Value) override { unimplemented(); return *this; }
	virtual FArchive& operator<<(UObject*& Value) override { unimplemented(); return *this; }
	virtual FArchive& operator<<(FLazyObjectPtr& Value) override { unimplemented(); return *this; }
	virtual FArchive& operator<<(FObjectPtr& Value) override { unimplemented(); return *this; }
	virtual FArchive& operator<<(FSoftObjectPtr& Value) override { unimplemented(); return *this; }
	virtual FArchive& operator<<(FSoftObjectPath& Value) override;
	virtual FArchive& operator<<(FWeakObjectPtr& Value) override { unimplemented(); return *this; }
	//~ End FArchive Interface

	using FArchive::operator<<;
	virtual FArchive& operator<<(FTopLevelAssetPath& Value); 

	template <typename DestPropertyType, typename SourcePropertyType>
	struct TDeltaSerializer
	{
		using FDeprecateFunction = TFunction<void(DestPropertyType&, const SourcePropertyType&)>;

		explicit TDeltaSerializer(DestPropertyType& InValue)
			: Value(InValue)
		{}

		explicit TDeltaSerializer(DestPropertyType& InValue, FDeprecateFunction InFunc)
			: Value(InValue)
			, Func(InFunc)
		{}

		friend FArchive& operator<<(FArchive& Ar, const TDeltaSerializer<DestPropertyType, SourcePropertyType>& V)
		{
			FActorDescArchive& ActorDescAr = (FActorDescArchive&)Ar;
			
			check(ActorDescAr.BaseDesc || Ar.IsSaving());
			check(ActorDescAr.ActorDesc);

			Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);

			auto GetBaseDefaultValue = [&V, &ActorDescAr]() -> const DestPropertyType*
			{
				const UPTRINT PropertyOffset = (UPTRINT)&V.Value - *(UPTRINT*)&ActorDescAr.ActorDesc;

				if ((PropertyOffset + sizeof(V.Value)) <= ActorDescAr.BaseDescSizeof)
				{
					const DestPropertyType* RefValue = (const DestPropertyType*)(*(UPTRINT*)&ActorDescAr.BaseDesc + PropertyOffset);
					return RefValue;
				}

				check(ActorDescAr.bIsMissingBaseDesc);
				return nullptr;
			};

			uint8 bSerialize = 1;

			if (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::WorldPartitionActorClassDescSerialize)
			{
				if (Ar.IsSaving() && ActorDescAr.BaseDesc)
				{
					if constexpr (std::is_same_v<DestPropertyType, SourcePropertyType>)
					{
						// When saving, we expect the class descriptor to be the exact type as what we are serializing.
						const DestPropertyType* BaseDefaultValue = GetBaseDefaultValue();
						check(BaseDefaultValue);

						bSerialize = (V.Value != *BaseDefaultValue) ? 1 : 0;
					}
				}

				Ar << bSerialize;
			}

			if (bSerialize)
			{
				if constexpr (std::is_same_v<DestPropertyType, SourcePropertyType>)
				{
					Ar << V.Value;
				}
				else
				{
					check(Ar.IsLoading());

					SourcePropertyType SourceValue;
					Ar << SourceValue;
					V.Func(V.Value, SourceValue);
				}
			}
			else if (Ar.IsLoading())
			{
				// When loading, we need to handle a different class descriptor in case of missing classes, etc.
				if (const DestPropertyType* BaseDefaultValue = GetBaseDefaultValue())
				{
					V.Value = *BaseDefaultValue;
				}
			}

			return Ar;
		}

		DestPropertyType& Value;
		FDeprecateFunction Func;
	};

	FWorldPartitionActorDesc* ActorDesc;
	const FWorldPartitionActorDesc* BaseDesc;
	uint32 BaseDescSizeof;
	bool bIsMissingBaseDesc;
};

class FActorDescArchivePatcher : public FActorDescArchive
{
public:
	FActorDescArchivePatcher(FArchive& InArchive, FWorldPartitionActorDesc* InActorDesc, FArchive& OutArchive, FWorldPartitionAssetDataPatcher* InAssetDataPatcher)
		: FActorDescArchive(InArchive, InActorDesc)
		, OutAr(OutArchive)
		, AssetDataPatcher(InAssetDataPatcher)
		, bIsPatching(false)
	{
		check(AssetDataPatcher);
	}

	//~ Begin FArchive Interface
	virtual FArchive& operator<<(FName& Value) override;
	virtual FArchive& operator<<(FSoftObjectPath& Value) override;
	virtual void Serialize(void* V, int64 Length) override;
	//~ End FArchive Interface

	virtual FArchive& operator<<(FTopLevelAssetPath& Value) override;

private:
	FArchive& OutAr;
	FWorldPartitionAssetDataPatcher* AssetDataPatcher;
	bool bIsPatching;
};

template <typename DestPropertyType, typename SourcePropertyType = DestPropertyType>
using TDeltaSerialize = FActorDescArchive::TDeltaSerializer<DestPropertyType, SourcePropertyType>;
#endif