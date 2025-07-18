// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_ClothAsset.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothEditor.h"
#include "ChaosClothAsset/ColorScheme.h"
#include "Dataflow/DataflowObject.h"
#include "Misc/MessageDialog.h"
#include "Misc/FileHelper.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#include "ThumbnailRendering/SceneThumbnailInfo.h"
#include "Misc/WarnIfAssetsLoadedInScope.h"
#include "Dialog/SMessageDialog.h"
#include "ChaosClothAsset/TerminalNode.h"
#include "Dataflow/DataflowEditor.h"
#include "Dataflow/DataflowSNode.h"
#include "Toolkits/SimpleAssetEditor.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_ClothAsset"

namespace UE::Chaos::ClothAsset::Private
{
static int32 bEnableClothDataflowEditor = false;
static FAutoConsoleVariableRef CVarEnableClothDataflowEditor(TEXT("p.ChaosCloth.EnableDataflowEditor"), bEnableClothDataflowEditor, TEXT("Enable the use of the core dataflow editor for cloth asset (WIP)"));
}

namespace ClothAssetDefinitionHelpers
{
	// Return true if we should proceed, false if we should re-open the dialog
	bool CreateNewDataflowAsset(const UChaosClothAsset* ClothAsset, UObject*& OutDataflowAsset)
	{
		const UClass* const DataflowClass = UDataflow::StaticClass();

		FSaveAssetDialogConfig NewDataflowAssetDialogConfig;
		{
			const FString PackageName = ClothAsset->GetOutermost()->GetName();
			NewDataflowAssetDialogConfig.DefaultPath = FPackageName::GetLongPackagePath(PackageName);
			const FString ClothName = ClothAsset->GetName();
			NewDataflowAssetDialogConfig.DefaultAssetName = ClothName + "_Dataflow";
			NewDataflowAssetDialogConfig.AssetClassNames.Add(DataflowClass->GetClassPathName());
			NewDataflowAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
			NewDataflowAssetDialogConfig.DialogTitleOverride = LOCTEXT("NewDataflowAssetDialogTitle", "Save Dataflow Asset As");
		}

		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

		FString NewPackageName = FPaths::Combine(NewDataflowAssetDialogConfig.DefaultPath, NewDataflowAssetDialogConfig.DefaultAssetName);
		FText OutError;

		while (!FFileHelper::IsFilenameValidForSaving(NewPackageName, OutError) ||
			LoadObject<UObject>(nullptr, *NewPackageName, nullptr, LOAD_NoWarn | LOAD_Quiet))  // Check if an object with this name already exists
		{
			const FString AssetSavePath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(NewDataflowAssetDialogConfig);
			if (AssetSavePath.IsEmpty())
			{
				OutDataflowAsset = nullptr;
				return false;
			}
			NewPackageName = FPackageName::ObjectPathToPackageName(AssetSavePath);
		}

		const FName NewAssetName(FPackageName::GetLongPackageAssetName(NewPackageName));
		UPackage* const NewPackage = CreatePackage(*NewPackageName);

		UDataflow* const ClothAssetTemplate = LoadObject<UDataflow>(NewPackage, TEXT("/ChaosClothAssetEditor/ClothAssetTemplate.ClothAssetTemplate"));
		UDataflow* const NewAsset = DuplicateObject(ClothAssetTemplate, NewPackage, NewAssetName);

		NewAsset->MarkPackageDirty();

		// Notify the asset registry
		FAssetRegistryModule::AssetCreated(NewAsset);

		OutDataflowAsset = NewAsset;
		return true;
	}


	// Return true if we should proceed, false if we should re-open the dialog
	bool OpenDataflowAsset(const UChaosClothAsset* ClothAsset, UObject*& OutDataflowAsset)
	{
		const UClass* const DataflowClass = UDataflow::StaticClass();

		FOpenAssetDialogConfig NewDataflowAssetDialogConfig;
		{
			const FString PackageName = ClothAsset->GetOutermost()->GetName();
			NewDataflowAssetDialogConfig.DefaultPath = FPackageName::GetLongPackagePath(PackageName);
			NewDataflowAssetDialogConfig.AssetClassNames.Add(DataflowClass->GetClassPathName());
			NewDataflowAssetDialogConfig.bAllowMultipleSelection = false;
			NewDataflowAssetDialogConfig.DialogTitleOverride = LOCTEXT("OpenDataflowAssetDialogTitle", "Open Dataflow Asset");
		}

		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		TArray<FAssetData> AssetData = ContentBrowserModule.Get().CreateModalOpenAssetDialog(NewDataflowAssetDialogConfig);

		if (AssetData.Num() == 1)
		{
			OutDataflowAsset = AssetData[0].GetAsset();
			return true;
		}

		return false;
	}

	// Return true if we should proceed, false if we should re-open the dialog
	bool NewOrOpenDialog(const UChaosClothAsset* ClothAsset, UObject*& OutDataflowAsset)
	{
		TSharedRef<SMessageDialog> ConfirmDialog = SNew(SMessageDialog)
			.Title(FText(LOCTEXT("ClothDataflow_WindowTitle", "Create or Open Dataflow graph?")))
			.Message(LOCTEXT("ClothDataflow_WindowText", "This Cloth Asset currently has no Dataflow graph"))
			.Buttons({
				SMessageDialog::FButton(LOCTEXT("ClothDataflow_NewText", "Create new Dataflow")),
				SMessageDialog::FButton(LOCTEXT("ClothDataflow_OpenText", "Open existing Dataflow")),
				SMessageDialog::FButton(LOCTEXT("ClothDataflow_ContinueText", "Continue without Dataflow")),
			});

		const int32 ResultButtonIdx = ConfirmDialog->ShowModal();
		switch(ResultButtonIdx)
		{
		case 0:
			return CreateNewDataflowAsset(ClothAsset, OutDataflowAsset);
		case 1:
			return OpenDataflowAsset(ClothAsset, OutDataflowAsset);
		default:
			break;
		}

		return true;
	}
}


// Create a new UDataflow if one doesn't already exist for the Cloth Asset
UObject* UAssetDefinition_ClothAsset::NewOrOpenDataflowAsset(const UChaosClothAsset* ClothAsset)
{
	UObject* DataflowAsset = nullptr;
	bool bDialogDone = false;
	while (!bDialogDone)
	{
		bDialogDone = ClothAssetDefinitionHelpers::CreateNewDataflowAsset(ClothAsset, DataflowAsset);
	}

	return DataflowAsset;
}

FText UAssetDefinition_ClothAsset::GetAssetDisplayName() const
{
	return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_ClothAsset", "ClothAsset");
}

TSoftClassPtr<UObject> UAssetDefinition_ClothAsset::GetAssetClass() const
{
	return UChaosClothAsset::StaticClass();
}

FLinearColor UAssetDefinition_ClothAsset::GetAssetColor() const
{
	return UE::Chaos::ClothAsset::FColorScheme::Asset;
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_ClothAsset::GetAssetCategories() const
{
	static const auto Categories = { EAssetCategoryPaths::Physics };
	return Categories;
}

UThumbnailInfo* UAssetDefinition_ClothAsset::LoadThumbnailInfo(const FAssetData& InAsset) const
{
	return UE::Editor::FindOrCreateThumbnailInfo(InAsset.GetAsset(), USceneThumbnailInfo::StaticClass());
}

EAssetCommandResult UAssetDefinition_ClothAsset::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	TArray<UChaosClothAsset*> ClothObjects = OpenArgs.LoadObjects<UChaosClothAsset>();

	// For now the cloth editor only works on one asset at a time
	ensure(ClothObjects.Num() == 0 || ClothObjects.Num() == 1);

	if (ClothObjects.Num() > 0)
	{
		UAssetEditorSubsystem* const AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if(!UE::Chaos::ClothAsset::Private::bEnableClothDataflowEditor)
		{
			UChaosClothAssetEditor* const AssetEditor = NewObject<UChaosClothAssetEditor>(AssetEditorSubsystem, NAME_None, RF_Transient);
			AssetEditor->Initialize({ClothObjects[0] });
		}
		else
		{
			// Check if the cloth asset has a Dataflow asset
			UChaosClothAsset* const ClothAsset = CastChecked<UChaosClothAsset>(ClothObjects[0]);
			if (!ClothAsset->GetDataflow())
			{
				// No Dataflow asset, just open a basic properties panel
				FSimpleAssetEditor::CreateEditor(EToolkitMode::Standalone, OpenArgs.ToolkitHost, ClothObjects[0] );
			}
			else
			{
				UDataflowEditor* const AssetEditor = NewObject<UDataflowEditor>(AssetEditorSubsystem, NAME_None, RF_Transient);
				const TSubclassOf<AActor> ActorClass = StaticLoadClass(AActor::StaticClass(), nullptr,
					TEXT("/ChaosClothAssetEditor/BP_ClothPreview.BP_ClothPreview_C"), nullptr, LOAD_None, nullptr);
				AssetEditor->RegisterToolCategories({"General", "Cloth"});
				AssetEditor->Initialize({ ClothObjects[0] }, ActorClass);
			}
		}

		return EAssetCommandResult::Handled;
	}

	return EAssetCommandResult::Unhandled;
}


#undef LOCTEXT_NAMESPACE
