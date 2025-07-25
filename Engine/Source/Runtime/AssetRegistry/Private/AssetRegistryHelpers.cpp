// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetRegistry/AssetRegistryHelpers.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry.h"
#include "Algo/AnyOf.h"
#include "Algo/RemoveIf.h"
#include "Blueprint/BlueprintSupport.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/LinkerLoad.h"

#if WITH_EDITOR
#include "Misc/RedirectCollector.h"
#endif

namespace UE::AssetRegistry
{

template<typename TLessThan>
static void SortAssets(TArray<FAssetData>& Assets, TLessThan&& Predicate, EAssetRegistrySortOrder SortOrder)
{
	switch (SortOrder)
	{
	case EAssetRegistrySortOrder::Ascending: Assets.Sort(Predicate); break;
	case EAssetRegistrySortOrder::Descending:
		Assets.Sort([&Predicate](const FAssetData& Left, const FAssetData& Right)
		{
			return Predicate(Right, Left);
		});
		break;
	default: checkNoEntry(); break;
	}
}
}

TScriptInterface<IAssetRegistry> UAssetRegistryHelpers::GetAssetRegistry()
{
	return &UAssetRegistryImpl::Get();
}

FAssetData UAssetRegistryHelpers::CreateAssetData(const UObject* InAsset, bool bAllowBlueprintClass /*= false*/)
{
	if (InAsset && InAsset->IsAsset())
	{
		return FAssetData(InAsset, bAllowBlueprintClass);
	}
	else
	{
		return FAssetData();
	}
}

bool UAssetRegistryHelpers::IsValid(const FAssetData& InAssetData)
{
	return InAssetData.IsValid();
}

bool UAssetRegistryHelpers::IsUAsset(const FAssetData& InAssetData)
{
	return InAssetData.IsUAsset();
}

FString UAssetRegistryHelpers::GetFullName(const FAssetData& InAssetData)
{
	return InAssetData.GetFullName();
}

bool UAssetRegistryHelpers::IsRedirector(const FAssetData& InAssetData)
{
	return InAssetData.IsRedirector();
}

FSoftObjectPath UAssetRegistryHelpers::ToSoftObjectPath(const FAssetData& InAssetData) 
{
	return InAssetData.ToSoftObjectPath();
}

UClass* UAssetRegistryHelpers::GetClass(const FAssetData& InAssetData)
{
	return InAssetData.GetClass();
}

UObject* UAssetRegistryHelpers::GetAsset(const FAssetData& InAssetData)
{
	return InAssetData.GetAsset();
}

bool UAssetRegistryHelpers::IsAssetLoaded(const FAssetData& InAssetData)
{
	return InAssetData.IsAssetLoaded();
}

#if WITH_EDITOR
bool UAssetRegistryHelpers::IsAssetCooked(const FAssetData& InAssetData)
{
	return InAssetData.HasAnyPackageFlags(PKG_Cooked);
}

bool UAssetRegistryHelpers::AssetHasEditorOnlyData(const FAssetData& InAssetData)
{
	return !InAssetData.HasAnyPackageFlags(PKG_FilterEditorOnly);
}
#endif	// WITH_EDITOR

FString UAssetRegistryHelpers::GetExportTextName(const FAssetData& InAssetData)
{
	return InAssetData.GetExportTextName();
}

bool UAssetRegistryHelpers::GetTagValue(const FAssetData& InAssetData, const FName& InTagName, FString& OutTagValue)
{
	return InAssetData.GetTagValue(InTagName, OutTagValue);
}

FARFilter UAssetRegistryHelpers::SetFilterTagsAndValues(const FARFilter& InFilter, const TArray<FTagAndValue>& InTagsAndValues)
{
	FARFilter FilterCopy = InFilter;
	for (const FTagAndValue& TagAndValue : InTagsAndValues)
	{
		FilterCopy.TagsAndValues.Add(TagAndValue.Tag, TagAndValue.Value);
	}

	return FilterCopy;
}

UClass* UAssetRegistryHelpers::FindAssetNativeClass(const FAssetData& AssetData)
{
	UClass* AssetClass = AssetData.GetClass();
	if (AssetClass == nullptr)
	{
		const IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
		
		TArray<FTopLevelAssetPath> AncestorClasses;
		AssetRegistry.GetAncestorClassNames(AssetData.AssetClassPath, AncestorClasses);
		for (const FTopLevelAssetPath& AncestorClassPath : AncestorClasses)
		{
			AssetClass = FindObject<UClass>(AncestorClassPath);
			if (AssetClass)
			{
				break;
			}
		}
	}
	while (AssetClass && !AssetClass->HasAnyClassFlags(CLASS_Native))
	{
		AssetClass = AssetClass->GetSuperClass();
	}

	return AssetClass;
}

void UAssetRegistryHelpers::SortByPredicate(
	TArray<FAssetData>& Assets,
	FSortingPredicate SortingPredicate,
	EAssetRegistrySortOrder SortOrder
	)
{
	if (SortingPredicate.IsBound())
	{
		UE::AssetRegistry::SortAssets(
			Assets,
			[&SortingPredicate](const FAssetData& Left, const FAssetData& Right)
			{
				return SortingPredicate.Execute(Left, Right);
			}, SortOrder);
	}
}

void UAssetRegistryHelpers::SortByAssetName(TArray<FAssetData>& Assets, EAssetRegistrySortOrder SortOrder)
{
	UE::AssetRegistry::SortAssets(Assets,
		[](const FAssetData& Left, const FAssetData& Right)
		{
			// Summary: String compare needed instead of FName::LexicalLess.
			// Reason: FName::LexicalLess says e.g. FName("Scene_10") < FName("Scene_01") (while: FString::operator< says "Scene_01" < "Scene_10").
			// Explanation:
			// - "Scene_10" has ComparisionIndex of "Scene" and Number = 11,
			// - "Scene_01" has ComparisionIndex of "Scene_01" and number 0
			// - Thus, (FName("Scene_10").LexicalLess(FName("Scene_01")) internally ends up checking "Scene" < "Scene_01" , which is true.
			// - For reference, "Scene_1" has ComparisionIndex of "Scene" and number 2, which, when sorting, we'd "expect" Scene_01 to have, too.
			return Left.AssetName.ToString() < Right.AssetName.ToString();
		}, SortOrder);
}

void UAssetRegistryHelpers::FindReferencersOfAssetOfClass(
	UObject* AssetInstance,
	TConstArrayView<UClass*> InMatchClasses,
	TArray<FAssetData>& OutAssetDatas
	)
{
	FindReferencersOfAssetOfClass(AssetInstance->GetOutermost()->GetFName(), InMatchClasses, OutAssetDatas);
}

void UAssetRegistryHelpers::FindReferencersOfAssetOfClass(
	const FAssetIdentifier& InAssetIdentifier,
	TConstArrayView<UClass*> InMatchClasses,
	TArray<FAssetData>& OutAssetDatas
	)
{
	// If the asset registry is still loading assets, we cant check for referencers, so we must open the rename dialog
	const IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();

	TArray<FAssetIdentifier> Referencers;
	AssetRegistry.GetReferencers(InAssetIdentifier, Referencers);

	for (auto AssetIdentifier : Referencers)
	{
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPackageName(AssetIdentifier.PackageName, Assets);

		for (auto AssetData : Assets)
		{
			if (InMatchClasses.Num() > 0)
			{
				if (Algo::AnyOf(InMatchClasses, [&AssetData](UClass* MatchClass){ return AssetData.IsInstanceOf(MatchClass); }))
				{
					OutAssetDatas.AddUnique(AssetData);
				}
			}
			else
			{
				OutAssetDatas.AddUnique(AssetData);
			}
		}
	}
}

void UAssetRegistryHelpers::GetBlueprintAssets(const FARFilter& InFilter, TArray<FAssetData>& OutAssetData)
{
	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();

	FARFilter Filter(InFilter);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS;
	UE_CLOG(!InFilter.ClassNames.IsEmpty(), LogCore, Error,
		TEXT("ARFilter.ClassNames is not supported by UAssetRegistryHelpers::GetBlueprintAssets and will be ignored."));
	Filter.ClassNames.Empty();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS;

	// Expand list of classes to include derived classes
	TArray<FTopLevelAssetPath> BlueprintParentClassPathRoots = MoveTemp(Filter.ClassPaths);
	TSet<FTopLevelAssetPath> BlueprintParentClassPaths;
	if (Filter.bRecursiveClasses)
	{
		AssetRegistry.GetDerivedClassNames(
			BlueprintParentClassPathRoots, 
			TSet<FTopLevelAssetPath>(),
			 BlueprintParentClassPaths
			 );
	}
	else
	{
		BlueprintParentClassPaths.Append(BlueprintParentClassPathRoots);
	}

	// Search for all blueprints and then check BlueprintParentClassPaths in the results
	Filter.ClassPaths.Reset(1);
	Filter.ClassPaths.Add(FTopLevelAssetPath(FName(TEXT("/Script/Engine")), FName(TEXT("BlueprintCore"))));
	Filter.bRecursiveClasses = true;

	auto FilterLambda = [&OutAssetData, &BlueprintParentClassPaths](const FAssetData& AssetData)
	{
		// Verify blueprint class
		if (BlueprintParentClassPaths.IsEmpty() || IsAssetDataBlueprintOfClassSet(AssetData, BlueprintParentClassPaths))
		{
			OutAssetData.Add(AssetData);
		}
		return true;
	};
	AssetRegistry.EnumerateAssets(Filter, FilterLambda, UE::AssetRegistry::EEnumerateAssetsFlags::None);
}

bool UAssetRegistryHelpers::IsAssetDataBlueprintOfClassSet(const FAssetData& AssetData, const TSet<FTopLevelAssetPath>& ClassNameSet)
{
	const FString ParentClassFromData = AssetData.GetTagValueRef<FString>(FBlueprintTags::ParentClassPath);
	if (!ParentClassFromData.IsEmpty())
	{
		const FTopLevelAssetPath ClassObjectPath(FPackageName::ExportTextPathToObjectPath(ParentClassFromData));
		const FName ClassName = ClassObjectPath.GetAssetName();

		TArray<FTopLevelAssetPath> ValidNames;
		ValidNames.Add(ClassObjectPath);
		// Check for redirected name
		FTopLevelAssetPath RedirectedName = FTopLevelAssetPath(FLinkerLoad::FindNewPathNameForClass(ClassObjectPath.ToString(), false));
		if (!RedirectedName.IsNull())
		{
			ValidNames.Add(RedirectedName);
		}
		for (const FTopLevelAssetPath& ValidName : ValidNames)
		{
			if (ClassNameSet.Contains(ValidName))
			{
				// Our parent class is in the class name set
				return true;
			}
		}
	}
	return false;
}

UAssetRegistryHelpers::FTemporaryCachingModeScope::FTemporaryCachingModeScope(bool InTempCachingMode)
{
	PreviousCachingMode = UAssetRegistryHelpers::GetAssetRegistry()->GetTemporaryCachingMode();
	UAssetRegistryHelpers::GetAssetRegistry()->SetTemporaryCachingMode(InTempCachingMode);
}

UAssetRegistryHelpers::FTemporaryCachingModeScope::~FTemporaryCachingModeScope()
{
	UAssetRegistryHelpers::GetAssetRegistry()->SetTemporaryCachingMode(PreviousCachingMode);
}

void UAssetRegistryHelpers::FixupRedirectedAssetPath(FSoftObjectPath& InOutSoftObjectPath)
{
	if (InOutSoftObjectPath.IsNull())
	{
		return;
	}

	FSoftObjectPath FoundRedirection;
	InOutSoftObjectPath.FixupCoreRedirects();

#if WITH_EDITOR
	FoundRedirection = GRedirectCollector.GetAssetPathRedirection(InOutSoftObjectPath);
	if (FoundRedirection.IsValid())
	{
		InOutSoftObjectPath = FoundRedirection;
		return;
	}
#endif

	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
	FoundRedirection = AssetRegistry.GetRedirectedObjectPath(InOutSoftObjectPath.GetWithoutSubPath());
	InOutSoftObjectPath = FSoftObjectPath(FoundRedirection.GetAssetPath(), InOutSoftObjectPath.GetSubPathString());
}

void UAssetRegistryHelpers::FixupRedirectedAssetPath(FName& InOutAssetPath)
{
	if (InOutAssetPath.IsNone())
	{
		return;
	}

	FSoftObjectPath SoftObjectPath(InOutAssetPath.ToString());
	FixupRedirectedAssetPath(SoftObjectPath);
	InOutAssetPath = FName(*SoftObjectPath.ToString());
}

#if WITH_EDITOR
TArray<FAssetData> UAssetRegistryHelpers::GetAssetsWithOuterForPaths(const TArray<FName>& InPackagePaths, FName InOuterPath, bool bInRecursivePaths, bool bInIncludeOnlyOnDiskAssets, bool bInExactOuter)
{
	FARFilter Filter;
	Filter.PackagePaths = InPackagePaths;
	Filter.bRecursivePaths = bInRecursivePaths;
	Filter.bIncludeOnlyOnDiskAssets = bInIncludeOnlyOnDiskAssets;

	TArray<FString> PathsToScan;
	PathsToScan.Reserve(Filter.PackagePaths.Num());
	Algo::Transform(Filter.PackagePaths, PathsToScan, [](FName InPackagePath) { return InPackagePath.ToString(); });

	TArray<FAssetData> Assets;
	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
	AssetRegistry.ScanSynchronous(PathsToScan, TArray<FString>());
	AssetRegistry.GetAssets(Filter, Assets);

	Assets.SetNum(
		Algo::RemoveIf(Assets, [InOuterPath, bInExactOuter](const FAssetData& InAssetData)
		{
			if (InAssetData.GetOptionalOuterPathName() == InOuterPath)
			{
				return false;
			}

			if (!bInExactOuter && InAssetData.GetOptionalOuterPathName().ToString().StartsWith(InOuterPath.ToString()))
			{
				return false;
			}

			return true;
		}));
	Assets.Sort();

	return Assets;
}
#endif