// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataflowAdvancedPreviewDetailsTab.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/STextComboBox.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"

#include "AssetViewerSettings.h"

#include "AdvancedPreviewScene.h"
#include "IDetailRootObjectCustomization.h"

#define LOCTEXT_NAMESPACE "SPrettyPreview"

SDataflowAdvancedPreviewDetailsTab::SDataflowAdvancedPreviewDetailsTab()
{
	DefaultSettings = UAssetViewerSettings::Get();
	if (DefaultSettings)
	{
		RefreshDelegate = DefaultSettings->OnAssetViewerSettingsChanged().AddRaw(this, &SDataflowAdvancedPreviewDetailsTab::OnAssetViewerSettingsRefresh);
			
		AddRemoveProfileDelegate = DefaultSettings->OnAssetViewerProfileAddRemoved().AddLambda([this]() { this->Refresh(); });

		PostUndoDelegate = DefaultSettings->OnAssetViewerSettingsPostUndo().AddRaw(this, &SDataflowAdvancedPreviewDetailsTab::OnAssetViewerSettingsPostUndo);
	}
}

SDataflowAdvancedPreviewDetailsTab::~SDataflowAdvancedPreviewDetailsTab()
{
	DefaultSettings = UAssetViewerSettings::Get();
	if (DefaultSettings)
	{
		DefaultSettings->OnAssetViewerSettingsChanged().Remove(RefreshDelegate);
		DefaultSettings->OnAssetViewerProfileAddRemoved().Remove(AddRemoveProfileDelegate);
		DefaultSettings->OnAssetViewerSettingsPostUndo().Remove(PostUndoDelegate);
		DefaultSettings->Save();
	}
}

void SDataflowAdvancedPreviewDetailsTab::Construct(const FArguments& InArgs, const TSharedRef<FAdvancedPreviewScene>& InPreviewScene)
{
	PreviewScenePtr = InPreviewScene;
	DefaultSettings = UAssetViewerSettings::Get();
	ProfileIndexStorage = InArgs._ProfileIndexStorage;
	AdditionalSettings = InArgs._AdditionalSettings;
	DetailCustomizations = InArgs._DetailCustomizations;
	PropertyTypeCustomizations = InArgs._PropertyTypeCustomizations;
	Delegates = InArgs._Delegates;

	ProfileIndex = ProfileIndexStorage->RetrieveProfileIndex();
	ProfileIndex = DefaultSettings->Profiles.IsValidIndex(ProfileIndex) ? ProfileIndex : 0;

	CreateSettingsView();

	this->ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(2.0f, 1.0f, 2.0f, 1.0f)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			[
				SettingsView->AsShared()
			]
		]

		+SVerticalBox::Slot()		
		.Padding(2.0f, 1.0f, 2.0f, 1.0f)
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.Padding(2.0f)
			[
				SNew(SHorizontalBox)
				.ToolTipText(LOCTEXT("SceneProfileComboBoxToolTip", "Allows for switching between scene environment and lighting profiles."))
				+ SHorizontalBox::Slot()
				.Padding(0.0f, 0.0f, 2.0f, 0.0f)
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SceneProfileSettingsLabel", "Profile"))
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Fill)
				[
					SAssignNew(ProfileComboBox, STextComboBox)
					.OptionsSource(&ProfileNames)
					.OnSelectionChanged(this, &SDataflowAdvancedPreviewDetailsTab::ComboBoxSelectionChanged)				
					.IsEnabled_Lambda([this]() -> bool
					{
						return ProfileNames.Num() > 1;
					})
				]
			]
			+ SHorizontalBox::Slot()
			.Padding(2.0f)
			.AutoWidth()
			[
				SNew(SButton)
				.OnClicked(this, &SDataflowAdvancedPreviewDetailsTab::AddProfileButtonClick)
				.Text(LOCTEXT("AddProfileButton", "Add Profile"))
				.ToolTipText(LOCTEXT("SceneProfileAddProfile", "Adds a new profile."))
			]
			+ SHorizontalBox::Slot()
			.Padding(2.0f)
			.AutoWidth()
			[
				SNew(SButton)
				.OnClicked(this, &SDataflowAdvancedPreviewDetailsTab::RemoveOrResetProfileButtonClick)
				.Text_Lambda([this]()
				{
					if (DefaultSettings->Profiles[ProfileIndex].bIsEngineDefaultProfile)
					{
						return LOCTEXT("ResetProfileButton", "Reset Profile");
					}
					return LOCTEXT("RemoveProfileButton", "Remove Profile");
				})
				.ToolTipText_Lambda([this]()
				{
					if (DefaultSettings->Profiles[ProfileIndex].bIsEngineDefaultProfile)
					{
						return LOCTEXT("SceneProfileResetProfile", "Resets this engine profile to default settings. Cannot delete engine profiles.");
					}
					return LOCTEXT("SceneProfileRemoveProfile", "Removes the currently selected profile.");
				})
				.IsEnabled_Lambda([this]()->bool
				{
					return ProfileNames.Num() > 1; 
				})
			]
		]
	];

	UpdateProfileNames();
	UpdateSettingsView();
}

void SDataflowAdvancedPreviewDetailsTab::ComboBoxSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	int32 NewSelectionIndex;

	if (ProfileNames.Find(NewSelection, NewSelectionIndex))
	{
		ProfileIndex = NewSelectionIndex;
		ProfileIndexStorage->StoreProfileIndex(ProfileIndex);
		UpdateSettingsView();	
		check(PreviewScenePtr.IsValid());
		if (SelectInfo == ESelectInfo::Type::OnMouseClick)
		{
			PreviewScenePtr.Pin()->SetProfileIndex(ProfileIndex);	
		}
	}
}

void SDataflowAdvancedPreviewDetailsTab::UpdateSettingsView()
{	
	TArray<UObject*> Objects;
	if (AdditionalSettings)
	{
		Objects.Add(AdditionalSettings);
	}
	Objects.Add(DefaultSettings);

	SettingsView->SetObjects(Objects, true);
	//SettingsView->Invalidate(EInvalidateWidgetReason::Paint);
}

void SDataflowAdvancedPreviewDetailsTab::UpdateProfileNames()
{
	checkf(DefaultSettings->Profiles.Num(), TEXT("There should always be at least one profile available"));
	ProfileNames.Empty();
	for (FPreviewSceneProfile& Profile : DefaultSettings->Profiles)
	{
		FString Suffix = TEXT("");
		if (Profile.bSharedProfile)
		{
			Suffix = TEXT(" (Shared)");
		}
		if (Profile.bIsEngineDefaultProfile)
		{
			Suffix = TEXT(" (Engine Default)");
		}
		ProfileNames.Add(TSharedPtr<FString>(new FString(Profile.ProfileName + Suffix)));
	}

	ProfileComboBox->RefreshOptions();
	ProfileComboBox->SetSelectedItem(ProfileNames[ProfileIndex]);
}

FReply SDataflowAdvancedPreviewDetailsTab::AddProfileButtonClick()
{
	const FScopedTransaction Transaction(LOCTEXT("AddSceneProfile", "Adding Preview Scene Profile"));
	DefaultSettings->Modify();

	// Add new profile to settings instance
	ProfileIndex = DefaultSettings->Profiles.AddDefaulted();
	FPreviewSceneProfile& NewProfile = DefaultSettings->Profiles.Last();
	
	// Try to create a valid profile name when one is added
	bool bFoundValidName = false;
	int32 ProfileAppendNum = FMath::Max(0, DefaultSettings->Profiles.Num() - 1);
	FString NewProfileName;
	while (!bFoundValidName)
	{
		NewProfileName = FString::Printf(TEXT("Profile_%i"), ProfileAppendNum);

		bool bValidName = true;
		for (const FPreviewSceneProfile& Profile : DefaultSettings->Profiles)
		{
			if (Profile.ProfileName == NewProfileName)
			{
				bValidName = false;				
				break;
			}
		}

		if (!bValidName)
		{
			++ProfileAppendNum;
		}

		bFoundValidName = bValidName;
	}

	NewProfile.ProfileName = NewProfileName;
	ProfileIndexStorage->StoreProfileIndex(ProfileIndex);
	DefaultSettings->PostEditChange();

	// Change selection to new profile so the user directly sees the profile that was added
	Refresh();
	ProfileComboBox->SetSelectedItem(ProfileNames.Last());
	
	return FReply::Handled();
}

FReply SDataflowAdvancedPreviewDetailsTab::RemoveOrResetProfileButtonClick()
{
	const FPreviewSceneProfile& CurrentProfile = DefaultSettings->Profiles[ProfileIndex];
	
	if (CurrentProfile.bIsEngineDefaultProfile)
	{
		const FScopedTransaction Transaction(LOCTEXT("ResetSceneProfile", "Reset Preview Scene Profile"));
		DefaultSettings->Modify();

		const FPreviewSceneProfile* DefaultEditorProfile = GetMutableDefault<UDefaultEditorProfiles>()->GetProfile(CurrentProfile.ProfileName);
		if (DefaultEditorProfile)
		{
			// Reset currently selected profile 
			DefaultSettings->Profiles[ProfileIndex] = *DefaultEditorProfile;
			DefaultSettings->PostEditChange();
			return FReply::Handled();
		}

		// if we get here, it means an engine-provided default profile was removed from the engine,
		// in which case we should remove it...
	}

	const FScopedTransaction Transaction(LOCTEXT("RemoveSceneProfile", "Remove Preview Scene Profile"));
	DefaultSettings->Modify();

	// Remove currently selected profile 
	DefaultSettings->Profiles.RemoveAt(ProfileIndex);
	ProfileIndex = DefaultSettings->Profiles.IsValidIndex(ProfileIndex - 1 ) ? ProfileIndex - 1 : 0;
	ProfileIndexStorage->StoreProfileIndex(ProfileIndex);
	DefaultSettings->PostEditChange();
	
	return FReply::Handled();
}

void SDataflowAdvancedPreviewDetailsTab::OnAssetViewerSettingsRefresh(const FName& InPropertyName)
{
	if (!PreviewScenePtr.IsValid())
	{
		// this callback can fire when the editor is forcibly closed and tool modes revert the active profile
		// when this happens, the preview scene is null even though this details tab hasn't been destroyed yet (and unregistered this delegate)
		return;
	}
	
	if (InPropertyName == GET_MEMBER_NAME_CHECKED(FPreviewSceneProfile, ProfileName) || InPropertyName == GET_MEMBER_NAME_CHECKED(FPreviewSceneProfile, bSharedProfile))
	{
		Refresh();
	}
}

void SDataflowAdvancedPreviewDetailsTab::CreateSettingsView()
{	
	// Create a property view
	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
	DetailsViewArgs.bShowOptions = false;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;

	SettingsView = EditModule.CreateDetailView(DetailsViewArgs);

	for (const FAdvancedPreviewSceneModule::FDetailCustomizationInfo& DetailCustomizationInfo : DetailCustomizations)
	{
		SettingsView->RegisterInstancedCustomPropertyLayout(DetailCustomizationInfo.Struct, DetailCustomizationInfo.OnGetDetailCustomizationInstance);
	}

	for (const FAdvancedPreviewSceneModule::FPropertyTypeCustomizationInfo& PropertyTypeCustomizationInfo : PropertyTypeCustomizations)
	{
		SettingsView->RegisterInstancedCustomPropertyTypeLayout(PropertyTypeCustomizationInfo.StructName, PropertyTypeCustomizationInfo.OnGetPropertyTypeCustomizationInstance);
	}

	for (FAdvancedPreviewSceneModule::FDetailDelegates& DetailDelegate : Delegates)
	{
		DetailDelegate.OnPreviewSceneChangedDelegate.AddSP(this, &SDataflowAdvancedPreviewDetailsTab::OnPreviewSceneChanged);
	}

	UpdateSettingsView();
}

void SDataflowAdvancedPreviewDetailsTab::Refresh()
{	
	const int32 StoredIndex = ProfileIndexStorage->RetrieveProfileIndex();
	ProfileIndex = DefaultSettings->Profiles.IsValidIndex(StoredIndex) ? StoredIndex : 0;
	ProfileIndexStorage->StoreProfileIndex(ProfileIndex);
	UpdateProfileNames();
	PreviewScenePtr.Pin()->SetProfileIndex(ProfileIndex);
	UpdateSettingsView();
}

void SDataflowAdvancedPreviewDetailsTab::OnAssetViewerSettingsPostUndo()
{
	Refresh();
	PreviewScenePtr.Pin()->UpdateScene(DefaultSettings->Profiles[ProfileIndex]);
}

void SDataflowAdvancedPreviewDetailsTab::OnPreviewSceneChanged(TSharedRef<FAdvancedPreviewScene> PreviewScene)
{
  	PreviewScenePtr = PreviewScene;
	Refresh();

}

#undef LOCTEXT_NAMESPACE