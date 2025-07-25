// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreUObjectPluginManager.h"
#include "Interfaces/IPluginManager.h"
#include "UObject/UObjectAllocator.h"
#include "UObject/Package.h"
#include "Templates/Casts.h"
#include "UObject/UObjectHash.h"
#include "Logging/LogMacros.h"
#include "UObject/ObjectRename.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AsciiSet.h"
#include "String/ParseTokens.h"
#include "Misc/WildcardString.h"
#include "UObject/ReferenceChainSearch.h"

namespace UE::CoreUObject::Private
{
	
	static bool GVerifyUnload = true;
	static FAutoConsoleVariableRef CVarVerifyPluginUnload(TEXT("PluginManager.VerifyUnload"),
		GVerifyUnload,
		TEXT("Verify plugin assets are no longer in memory when unloading."),
		ECVF_Default);

	static FAutoConsoleVariableRef CVarVerifyPluginUnloadOldName(TEXT("GameFeaturePlugin.VerifyUnload"),
		GVerifyUnload,
		TEXT("Verify plugin assets are no longer in memory when unloading. Deprecated use PluginManager.VerifyUnload instead"),
		ECVF_Default);

#if WITH_LOW_LEVEL_TESTS
	bool bEnsureOnLeakedPackages = false;
#endif

	static int32 GLeakedAssetTrace_Severity = 2;
	static int32 GLeakedAssetTrace_EditorSeverity = -1;
	static FAutoConsoleVariableRef CVarLeakedAssetTrace_Severity(
#if UE_BUILD_SHIPPING
		TEXT("PluginManager.LeakedAssetTrace.Severity.Shipping"),
#else
		TEXT("PluginManager.LeakedAssetTrace.Severity"),
#endif
		GLeakedAssetTrace_Severity,
		TEXT("Controls severity of logging when the engine detects that assets from an Game Feature Plugin were leaked during unloading or unmounting.\n")
		TEXT("0 - all reference tracing and logging is disabled\n")
		TEXT("1 - logs an error\n")
		TEXT("2 - ensure\n")
		TEXT("3 - fatal error\n"),
		ECVF_Default
	);

	static FAutoConsoleVariableRef CVarLeakedAssetTrace_EditorSeverity(
		TEXT("PluginManager.LeakedAssetTrace.Severity.Editor"),
		GLeakedAssetTrace_EditorSeverity,
		TEXT("Controls severity of logging in Editor when the engine detects that assets from an Game Feature Plugin were leaked during unloading or unmounting. Overrides PluginManager.LeakedAssetTrace.Severity if set.\n")
		TEXT("0 - all reference tracing and logging is disabled\n")
		TEXT("1 - logs an error\n")
		TEXT("2 - ensure\n")
		TEXT("3 - fatal error\n"),
		ECVF_Default
	);

	static FAutoConsoleVariableRef CVarLeakedAssetTrace_SeverityOldName(
#if UE_BUILD_SHIPPING
		TEXT("GameFeaturePlugin.LeakedAssetTrace.Severity.Shipping"),
#else
		TEXT("GameFeaturePlugin.LeakedAssetTrace.Severity"),
#endif
		GLeakedAssetTrace_Severity,
		TEXT("Controls severity of logging when the engine detects that assets from an Game Feature Plugin were leaked during unloading or unmounting. . Deprecated use GameFeaturePlugin.LeakedAssetTrace instead\n")
		TEXT("0 - all reference tracing and logging is disabled\n")
		TEXT("1 - logs an error\n")
		TEXT("2 - ensure\n")
		TEXT("3 - fatal error\n"),
		ECVF_Default
	);

	static bool GRenameLeakedPackages = true;
	static FAutoConsoleVariableRef CVarRenameLeakedPackages(
		TEXT("PluginManager.LeakedAssetTrace.RenameLeakedPackages"),
		GRenameLeakedPackages,
		TEXT("Should packages which are leaked after the Game Feature Plugin is unloaded or unmounted."),
		ECVF_Default
	);

	static FAutoConsoleVariableRef CVarRenameLeakedPackagesOldName(
		TEXT("GameFeaturePlugin.LeakedAssetTrace.RenameLeakedPackages. Deprecated used PluginManager.LeakedAssetTrace.RenameLeakedPackages instead"),
		GRenameLeakedPackages,
		TEXT("Should packages which are leaked after the Game Feature Plugin is unloaded or unmounted."),
		ECVF_Default
	);

	static int32 GLeakedAssetTrace_TraceMode = (UE_BUILD_SHIPPING ? 0 : 1);
	static FAutoConsoleVariableRef CVarLeakedAssetTrace_TraceMode(
#if UE_BUILD_SHIPPING
		TEXT("PluginManager.LeakedAssetTrace.TraceMode.Shipping"),
#else
		TEXT("PluginManager.LeakedAssetTrace.TraceMode"),
#endif
		GLeakedAssetTrace_TraceMode,
		TEXT("Controls detail level of reference tracing when the engine detects that assets from a Game Feature Plugin were leaked during unloading or unmounting.\n")
		TEXT("0 - direct references only\n")
		TEXT("1 - full reference trace"),
		ECVF_Default
	);

	static FAutoConsoleVariableRef CVarLeakedAssetTrace_TraceModeOldName(
#if UE_BUILD_SHIPPING
		TEXT("GameFeaturePlugin.LeakedAssetTrace.TraceMode.Shipping"),
#else
		TEXT("GameFeaturePlugin.LeakedAssetTrace.TraceMode"),
#endif
		GLeakedAssetTrace_TraceMode,
		TEXT("Controls detail level of reference tracing when the engine detects that assets from a Game Feature Plugin were leaked during unloading or unmounting. Deprecated used PluginManager.LeakedAssetTrace.TraceMode instead\n")
		TEXT("0 - direct references only\n")
		TEXT("1 - full reference trace"),
		ECVF_Default
	);

	static int32 GLeakedAssetTrace_MaxReportCount = 10;
	static FAutoConsoleVariableRef CVarLeakedAssetTrace_MaxReportCount(
		TEXT("PluginManager.LeakedAssetTrace.MaxReportCount"),
		GLeakedAssetTrace_MaxReportCount,
		TEXT("Max number of assets to report when we find leaked assets.\n"),
		ECVF_Default
	);

	static FAutoConsoleVariableRef CVarLeakedAssetTrace_MaxReportCountOldName(
		TEXT("GameFeaturePlugin.LeakedAssetTrace.MaxReportCount. Deprecated use PluginManager.LeakedAssetTrace.MaxReportCount instead"),
		GLeakedAssetTrace_MaxReportCount,
		TEXT("Max number of assets to report when we find leaked assets.\n"),
		ECVF_Default
	);

	DEFINE_LOG_CATEGORY_STATIC(PluginHandlerLog, Log, All);

	PluginHandler GPluginHandler;

	void PluginHandler::Install()
	{
		UE::PluginManager::Private::SetCoreUObjectPluginManager(GPluginHandler);
	}

	// Check if any assets from the plugin mount point have leaked, and if so trace them.
	// Then rename the assets and mark them as garbage to allow new copies of them to be loaded. 
	void HandlePossibleAssetLeaks(const FString& PluginName)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(HandlePossibleAssetLeaks);

		// Allow the editor to customize it's own severity during development.
#if WITH_EDITOR && !UE_BUILD_SHIPPING
		const int32 LeakedAssetSeverity = GLeakedAssetTrace_EditorSeverity >= 0 ? GLeakedAssetTrace_EditorSeverity : GLeakedAssetTrace_Severity;
#else
		const int32 LeakedAssetSeverity = GLeakedAssetTrace_Severity;
#endif
		const bool bFindLeakedPackages = GVerifyUnload && (LeakedAssetSeverity != 0 || GRenameLeakedPackages);
		if (!bFindLeakedPackages)
		{
			return;
		}

		TArray<UPackage*> LeakedPackages;
		TStringBuilder<512> Prefix;
		Prefix << '/' << PluginName << '/';

		TStringBuilder<NAME_SIZE> NameBuffer;
		{
			// If the UObject hash knew about package mount roots, we could avoid this loop
			TRACE_CPUPROFILER_EVENT_SCOPE(PackageLoop);
			FPermanentObjectPoolExtents PermanentPool;
			ForEachObjectOfClass(UPackage::StaticClass(), [&](UObject* Obj)
				{
					if (UPackage* Package = CastChecked<UPackage>(Obj, ECastCheckedType::NullAllowed))
					{
						if (PermanentPool.Contains(Package))
						{
							return;
						}
						NameBuffer.Reset();
						Package->GetFName().GetDisplayNameEntry()->AppendNameToString(NameBuffer);
						if (NameBuffer.ToView().StartsWith(Prefix, ESearchCase::IgnoreCase))
						{
							LeakedPackages.Add(Package);
						}
					}
				});
		}

		if (LeakedPackages.Num() == 0)
		{
			return;
		}

#if WITH_LOW_LEVEL_TESTS
		if (bEnsureOnLeakedPackages)
		{
			check(LeakedPackages.Num() == 0);
		}
#endif

		if (LeakedAssetSeverity != 0)
		{
			EPrintStaleReferencesOptions Options = EPrintStaleReferencesOptions::None;
			switch (LeakedAssetSeverity)
			{
			case 3:
				Options = EPrintStaleReferencesOptions::Fatal;
				break;
			case 2:
				Options = EPrintStaleReferencesOptions::Ensure | EPrintStaleReferencesOptions::Error;
				break;
			case 1:
			default:
				Options = EPrintStaleReferencesOptions::Error;
				break;
			}

			if (GLeakedAssetTrace_TraceMode == 0)
			{
				Options |= EPrintStaleReferencesOptions::Minimal;
			}

			// Make sure we are running on the game thread because we are going to be modifying
			// object flags to improve the stale reference reporting later on.
			check(IsInGameThread());

			TSet<UObject*> ObjectsThatHadStandaloneCleared;
			TArray<UObject*> ObjectsInPackage;

			// Sort even if we don't limit the count, so that high priority leaks appear first 
			int32 OmittedCount = FMath::Max(0, LeakedPackages.Num() - GLeakedAssetTrace_MaxReportCount);
			TArray<UPackage*> PackagesToSearchFor;
			int32 i = 0;
			for (UPackage* Package : LeakedPackages)
			{
				ObjectsInPackage.Reset();

				// To improve the reporting for stale references we clear out the RF_Standalone flag on
				// every object in the packages we will check. The flags will be restored after the Find.
				GetObjectsWithPackage(Package, ObjectsInPackage, false);

				for (UObject* Object : ObjectsInPackage)
				{
					if (Object->HasAnyFlags(RF_Standalone))
					{
						Object->ClearFlags(RF_Standalone);
						ObjectsThatHadStandaloneCleared.Add(Object);
					}
				}

				if (i++ < GLeakedAssetTrace_MaxReportCount)
				{
					PackagesToSearchFor.Add(Package);
				}
			}

			UE_LOG(PluginHandlerLog, Display, TEXT("Searching for references to %d leaked packages (%d omitted for speed) from plugin %s"), LeakedPackages.Num(), OmittedCount, *PluginName);
			FReferenceChainSearch::FindAndPrintStaleReferencesToObjects(MakeArrayView((UObject**)PackagesToSearchFor.GetData(), PackagesToSearchFor.Num()), Options);

			for (UObject* Object : ObjectsThatHadStandaloneCleared)
			{
				Object->SetFlags(RF_Standalone);
			}
		}

		// Rename the packages that we are streaming out so that we can possibly reload another copy of them
		for (UPackage* Package : LeakedPackages)
		{
			UE_LOG(PluginHandlerLog, Warning, TEXT("Marking leaking package %s as Garbage"), *Package->GetName());
			ForEachObjectWithPackage(Package, [](UObject* Object)
				{
					Object->MarkAsGarbage();
					return true;
				}, false);

			Package->MarkAsGarbage();

			if (!UObject::IsGarbageEliminationEnabled()
#if WITH_LOW_LEVEL_TESTS
				|| GRenameLeakedPackages
#endif
				)
			{
				UE::Object::RenameLeakedPackage(Package);
			}
		}
	}

	void PluginHandler::OnPluginUnload(IPlugin& Plugin)
	{
		check(IsInGameThread());

		if (SuppressGCRefCount > 0)
		{
			DeferredPluginsToGC.Add(Plugin.GetName());
		}
		else
		{
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
			HandlePossibleAssetLeaks(Plugin.GetName());
		}
	}

	void PluginHandler::SuppressPluginUnloadGC()
	{
		check(IsInGameThread());

		++SuppressGCRefCount;
	}

	void PluginHandler::ResumePluginUnloadGC()
	{
		check(IsInGameThread());

		--SuppressGCRefCount;

		ensure(SuppressGCRefCount >= 0);

		if (SuppressGCRefCount == 0)
		{
			if (!DeferredPluginsToGC.IsEmpty())
			{
				CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);

				for (FString& PluginName : DeferredPluginsToGC)
				{
					HandlePossibleAssetLeaks(PluginName);
				}

				DeferredPluginsToGC.Empty();
			}
		}
	}
}