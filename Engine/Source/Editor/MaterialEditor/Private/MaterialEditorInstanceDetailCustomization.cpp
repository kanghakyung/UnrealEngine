// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialEditorInstanceDetailCustomization.h"
#include "Misc/MessageDialog.h"
#include "Misc/Guid.h"
#include "UObject/UnrealType.h"
#include "Layout/Margin.h"
#include "Misc/Attribute.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"
#include "Materials/MaterialInterface.h"
#include "MaterialEditor/DEditorFontParameterValue.h"
#include "MaterialEditor/DEditorMaterialLayersParameterValue.h"
#include "MaterialEditor/DEditorRuntimeVirtualTextureParameterValue.h"
#include "MaterialEditor/DEditorSparseVolumeTextureParameterValue.h"
#include "MaterialEditor/DEditorScalarParameterValue.h"
#include "MaterialEditor/DEditorStaticComponentMaskParameterValue.h"
#include "MaterialEditor/DEditorStaticSwitchParameterValue.h"
#include "MaterialEditor/DEditorTextureParameterValue.h"
#include "MaterialEditor/DEditorVectorParameterValue.h"
#include "MaterialEditor/MaterialEditorInstanceConstant.h"
#include "SMaterialSubstrateTree.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionFontSampleParameter.h"
#include "Materials/MaterialExpressionMaterialAttributeLayers.h"
#include "MaterialShared.h"
#include "EditorSupportDelegates.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "IDetailPropertyRow.h"
#include "DetailLayoutBuilder.h"
#include "IDetailGroup.h"
#include "DetailCategoryBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialPropertyHelpers.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionInstance.h"
#include "Curves/CurveLinearColor.h"
#include "IPropertyUtilities.h"
#include "Engine/Texture.h"
#include "HAL/PlatformApplicationMisc.h"
#include "RenderUtils.h"

#define LOCTEXT_NAMESPACE "MaterialInstanceEditor"



TSharedRef<IDetailCustomization> FMaterialInstanceParameterDetails::MakeInstance(UMaterialEditorInstanceConstant* MaterialInstance, SMaterialLayersFunctionsInstanceWrapper* InMaterialLayersFunctionsInstance, FGetShowHiddenParameters InShowHiddenDelegate)
{
	return MakeShareable(new FMaterialInstanceParameterDetails(MaterialInstance, InMaterialLayersFunctionsInstance, InShowHiddenDelegate));
}

FMaterialInstanceParameterDetails::FMaterialInstanceParameterDetails(UMaterialEditorInstanceConstant* MaterialInstance, SMaterialLayersFunctionsInstanceWrapper* InMaterialLayersFunctionsInstance, FGetShowHiddenParameters InShowHiddenDelegate)
	: MaterialEditorInstance(MaterialInstance)
	, MaterialLayersFunctionsInstance(InMaterialLayersFunctionsInstance) 
	, ShowHiddenDelegate(InShowHiddenDelegate)
{
}

TOptional<float> FMaterialInstanceParameterDetails::OnGetValue(TSharedRef<IPropertyHandle> PropertyHandle)
{
	float Value = 0.0f;
	if (PropertyHandle->GetValue(Value) == FPropertyAccess::Success)
	{
		return TOptional<float>(Value);
	}

	// Value couldn't be accessed. Return an unset value
	return TOptional<float>();
}

void FMaterialInstanceParameterDetails::OnValueCommitted(float NewValue, ETextCommit::Type CommitType, TSharedRef<IPropertyHandle> PropertyHandle)
{	
	// Try setting as float, if that fails then set as int
	ensure(PropertyHandle->SetValue(NewValue) == FPropertyAccess::Success);
}

FString FMaterialInstanceParameterDetails::GetFunctionParentPath() const
{
	FString PathString;
	if (MaterialEditorInstance->SourceFunction)
	{
		PathString = MaterialEditorInstance->SourceFunction->Parent->GetPathName();
	}
	return PathString;
}

void FMaterialInstanceParameterDetails::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	PropertyUtilities = DetailLayout.GetPropertyUtilities();

	// Create a new category for a custom layout for the MIC parameters at the very top
	FName GroupsCategoryName = TEXT("ParameterGroups");
	IDetailCategoryBuilder& GroupsCategory = DetailLayout.EditCategory(GroupsCategoryName, LOCTEXT("MICParamGroupsTitle", "Parameter Groups"));
	TSharedRef<IPropertyHandle> ParameterGroupsProperty = DetailLayout.GetProperty("ParameterGroups");

	// check if tree has any selection, we show parameter properties for selected layer only
	if (MaterialLayersFunctionsInstance != nullptr && MaterialLayersFunctionsInstance->NestedTree->GetNumItemsSelected() > 0)
	{
		// for each selected FSortedParamData item (type stack)
		TSharedPtr<FSortedParamData> SelectedItem = MaterialLayersFunctionsInstance->NestedTree->GetSelectedItems().Last();
		
			// make sure we selected a stack item
		check (SelectedItem->StackDataType == EStackDataType::Stack)

		// we should now gather all sub-stack items to loop through all together
		TArray<TSharedPtr<FSortedParamData>> AssetStacksCollection;
		TArray<uint32> AssetParentNodeCollection;
		MaterialLayersFunctionsInstance->NestedTree->CollectAssetStackItemsRecursively(SelectedItem, AssetStacksCollection, AssetParentNodeCollection);

		// we go through list of assets
		for (int32 Index = 0; Index < AssetStacksCollection.Num(); ++Index)
		{
			TSharedPtr<FSortedParamData> AssetItem = AssetStacksCollection[Index];
			uint32 NodeID = AssetParentNodeCollection[Index];

			for (TSharedPtr<FSortedParamData> GroupParamData : AssetItem->Children)
			{
				if (GroupParamData->StackDataType == EStackDataType::Group)
				{
					int32 GroupIdx = MaterialEditorInstance->ParameterGroups.IndexOfByPredicate(
						[&](const FEditorParameterGroup& Group)
						{
							return Group.GroupName == GroupParamData->Group.GroupName;
						});
					if (GroupIdx != INDEX_NONE)
					{
						FEditorParameterGroup& ParameterGroup = GroupParamData->Group;
						IDetailGroup& DetailGroup = GroupsCategory.AddGroup(ParameterGroup.GroupName, FText::FromName(ParameterGroup.GroupName), false, true);
						TSharedPtr<IPropertyHandle> GroupPropertyHandle = ParameterGroupsProperty->GetChildHandle(GroupIdx);

						CreateSingleGroupWidget(ParameterGroup, GroupPropertyHandle, DetailGroup, GroupParamData->ParameterInfo.Index, true);

						FSimpleDelegate UpdateThumbnails = FSimpleDelegate::CreateLambda([=, this]()
							{
								this->MaterialLayersFunctionsInstance->NestedTree->UpdateThumbnailMaterial(AssetItem->ParameterInfo.Association, NodeID);
							});
						GroupPropertyHandle->SetOnPropertyValueChanged(UpdateThumbnails);
						GroupPropertyHandle->SetOnChildPropertyValueChanged(UpdateThumbnails);
					}
				}
			}
		}
		
		DetailLayout.HideCategory("MaterialEditorInstanceConstant");
		DetailLayout.HideProperty("Parent");
		DetailLayout.HideProperty("PostProcessOverrides");
		DetailLayout.HideProperty("PhysMaterial");
		DetailLayout.HideProperty("LightmassSettings");
		DetailLayout.HideProperty("bUseOldStyleMICEditorGroups");
		DetailLayout.HideProperty("ParameterGroups");
		DetailLayout.HideProperty("RefractionDepthBias");
		DetailLayout.HideProperty("bOverrideSubsurfaceProfile");
		DetailLayout.HideProperty("SubsurfaceProfile");
		DetailLayout.HideProperty("bOverrideSpecularProfile");
		DetailLayout.HideProperty("SpecularProfile");
		DetailLayout.HideProperty("BasePropertyOverrides");
		DetailLayout.HideProperty("MaterialLayersParameterValues");
	}
	else
	{
		CreateGroupsWidget(ParameterGroupsProperty, GroupsCategory);

		// Create default category for class properties
		const FName DefaultCategoryName = NAME_None;
		IDetailCategoryBuilder& DefaultCategory = DetailLayout.EditCategory(DefaultCategoryName);
		DetailLayout.HideProperty("MaterialLayersParameterValues");
		if (MaterialEditorInstance->bIsFunctionPreviewMaterial)
		{
			// Customize Parent property so we can check for recursively set parents
			bool bShowParent = false;
			if(MaterialEditorInstance->SourceFunction->GetMaterialFunctionUsage() != EMaterialFunctionUsage::Default)
			{
				bShowParent = true;
			}
			if (bShowParent)
			{
				TSharedRef<IPropertyHandle> ParentPropertyHandle = DetailLayout.GetProperty("Parent");
				IDetailPropertyRow& ParentPropertyRow = DefaultCategory.AddProperty(ParentPropertyHandle);
				ParentPropertyHandle->MarkResetToDefaultCustomized();

				TSharedPtr<SWidget> NameWidget;
				TSharedPtr<SWidget> ValueWidget;
				FDetailWidgetRow Row;

				ParentPropertyRow.GetDefaultWidgets(NameWidget, ValueWidget, Row);

				ParentPropertyHandle->ClearResetToDefaultCustomized();

				const bool bShowChildren = true;
				ParentPropertyRow.CustomWidget(bShowChildren)
					.NameContent()
					.MinDesiredWidth(Row.NameWidget.MinWidth)
					.MaxDesiredWidth(Row.NameWidget.MaxWidth)
					[
						NameWidget.ToSharedRef()
					]
				.ValueContent()
					.MinDesiredWidth(Row.ValueWidget.MinWidth)
					.MaxDesiredWidth(Row.ValueWidget.MaxWidth)
					[
						SNew(SObjectPropertyEntryBox)
						.ObjectPath(this, &FMaterialInstanceParameterDetails::GetFunctionParentPath)
						.AllowedClass(UMaterialFunctionInterface::StaticClass())
						.ThumbnailPool(DetailLayout.GetThumbnailPool())
						.AllowClear(true)
						.OnObjectChanged(this, &FMaterialInstanceParameterDetails::OnAssetChanged, ParentPropertyHandle)
						.OnShouldSetAsset(this, &FMaterialInstanceParameterDetails::OnShouldSetAsset)
						.NewAssetFactories(TArray<UFactory*>())
					];

				ValueWidget.Reset();


			}
			else
			{
				DetailLayout.HideProperty("Parent");
			}

			DetailLayout.HideProperty("PostProcessOverrides");
			DetailLayout.HideProperty("PhysMaterial");
			DetailLayout.HideProperty("LightmassSettings");
			DetailLayout.HideProperty("bUseOldStyleMICEditorGroups");
			DetailLayout.HideProperty("ParameterGroups");
			DetailLayout.HideProperty("RefractionDepthBias");
			DetailLayout.HideProperty("bOverrideSubsurfaceProfile");
			DetailLayout.HideProperty("SubsurfaceProfile");
			DetailLayout.HideProperty("bOverrideSpecularProfile");
			DetailLayout.HideProperty("SpecularProfile");
			DetailLayout.HideProperty("BasePropertyOverrides");
		}
		else
		{
			DetailLayout.HideProperty("PostProcessOverrides");
			CreatePostProcessOverrideWidgets(DetailLayout);

			// Add PhysMaterial property
			DefaultCategory.AddProperty("PhysMaterial");

			// Customize Parent property so we can check for recursively set parents
			TSharedRef<IPropertyHandle> ParentPropertyHandle = DetailLayout.GetProperty("Parent");
			IDetailPropertyRow& ParentPropertyRow = DefaultCategory.AddProperty(ParentPropertyHandle);

			ParentPropertyHandle->MarkResetToDefaultCustomized();
	
			TSharedPtr<SWidget> NameWidget;
			TSharedPtr<SWidget> ValueWidget;
			FDetailWidgetRow Row;

			ParentPropertyRow.GetDefaultWidgets(NameWidget, ValueWidget, Row);
	
			ParentPropertyHandle->ClearResetToDefaultCustomized();

			const bool bShowChildren = true;
			ParentPropertyRow.CustomWidget(bShowChildren)
				.NameContent()
				.MinDesiredWidth(Row.NameWidget.MinWidth)
				.MaxDesiredWidth(Row.NameWidget.MaxWidth)
				[
					NameWidget.ToSharedRef()
				]
				.ValueContent()
				.MinDesiredWidth(Row.ValueWidget.MinWidth)
				.MaxDesiredWidth(Row.ValueWidget.MaxWidth)
				[
					SNew(SObjectPropertyEntryBox)
					.PropertyHandle(ParentPropertyHandle)
					.AllowedClass(UMaterialInterface::StaticClass())
					.ThumbnailPool(DetailLayout.GetThumbnailPool())
					.AllowClear(true)
					.OnShouldSetAsset(this, &FMaterialInstanceParameterDetails::OnShouldSetAsset)
				];

			ValueWidget.Reset();


			// Add/hide other properties
			DetailLayout.HideProperty("LightmassSettings");
			CreateLightmassOverrideWidgets(DetailLayout);
			DetailLayout.HideProperty("bUseOldStyleMICEditorGroups");
			DetailLayout.HideProperty("ParameterGroups");

			{
				FIsResetToDefaultVisible IsRefractionDepthBiasPropertyResetVisible = FIsResetToDefaultVisible::CreateLambda([this](TSharedPtr<IPropertyHandle> InHandle) {
					float BiasValue;
					float ParentBiasValue;
					return MaterialEditorInstance->SourceInstance->GetRefractionSettings(BiasValue) 
						&& MaterialEditorInstance->Parent->GetRefractionSettings(ParentBiasValue)
						&& BiasValue != ParentBiasValue;
				});
				FResetToDefaultHandler ResetRefractionDepthBiasPropertyHandler = FResetToDefaultHandler::CreateLambda([this](TSharedPtr<IPropertyHandle> InHandle) {
					MaterialEditorInstance->Parent->GetRefractionSettings(MaterialEditorInstance->RefractionDepthBias);
				});
				FResetToDefaultOverride ResetRefractionDepthBiasPropertyOverride = FResetToDefaultOverride::Create(IsRefractionDepthBiasPropertyResetVisible, ResetRefractionDepthBiasPropertyHandler);
				IDetailPropertyRow& PropertyRow = DefaultCategory.AddProperty("RefractionDepthBias");
				PropertyRow.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FMaterialInstanceParameterDetails::ShouldShowMaterialRefractionSettings)));
				PropertyRow.OverrideResetToDefault(ResetRefractionDepthBiasPropertyOverride);
			}

			{
				// Add the material property override group
				static FName GroupName(TEXT("MaterialPropertyOverrideGroup"));
				IDetailGroup& MaterialPropertyOverrideGroup = DefaultCategory.AddGroup(GroupName, LOCTEXT("MaterialPropertyOverrideGroup", "Material Property Overrides"), false, false);
			
				// Hide the originals, these will be recreated manually
				DetailLayout.HideProperty("bOverrideSubsurfaceProfile");
				DetailLayout.HideProperty("SubsurfaceProfile");
				DetailLayout.HideProperty("bOverrideSpecularProfile");
				DetailLayout.HideProperty("SpecularProfile");
				DetailLayout.HideProperty("BasePropertyOverrides");

				// Set up the override logic for the subsurface profile
				{
					TAttribute<bool> IsParamEnabled = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([this](){ return (bool)MaterialEditorInstance->bOverrideSubsurfaceProfile; }));

					IDetailPropertyRow& PropertyRow = MaterialPropertyOverrideGroup.AddPropertyRow(DetailLayout.GetProperty("SubsurfaceProfile"));
					PropertyRow
						.EditCondition(IsParamEnabled, 
							FOnBooleanValueChanged::CreateLambda([this](bool NewValue) {
								MaterialEditorInstance->bOverrideSubsurfaceProfile = (uint32)NewValue;
								MaterialEditorInstance->PostEditChange();
								FEditorSupportDelegates::RedrawAllViewports.Broadcast();
						}))
						.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FMaterialInstanceParameterDetails::ShouldShowSubsurfaceProfile)));
				}

				// Set up the override logic for the specular profile
				if (Substrate::IsSubstrateEnabled())
				{
					TAttribute<bool> IsParamEnabled = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([this](){ return (bool)MaterialEditorInstance->bOverrideSpecularProfile; }));

					IDetailPropertyRow& PropertyRow = MaterialPropertyOverrideGroup.AddPropertyRow(DetailLayout.GetProperty("SpecularProfile"));
					PropertyRow
					.EditCondition(IsParamEnabled, 
								   FOnBooleanValueChanged::CreateLambda([this](bool NewValue) {
									   MaterialEditorInstance->bOverrideSpecularProfile = (uint32)NewValue;
									   MaterialEditorInstance->PostEditChange();
									   FEditorSupportDelegates::RedrawAllViewports.Broadcast();
					}))
					.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FMaterialInstanceParameterDetails::ShouldShowSpecularProfile)));
				}
			
				// Append the base property overrides to the Material Property Override Group
				CreateBasePropertyOverrideWidgets(DetailLayout, MaterialPropertyOverrideGroup);

				// Append the nanite material override.
				MaterialPropertyOverrideGroup.AddPropertyRow(DetailLayout.GetProperty("NaniteOverrideMaterial"));
			}
		}

		// Add the preview mesh property directly from the material instance 
		FName PreviewingCategoryName = TEXT("Previewing");
		IDetailCategoryBuilder& PreviewingCategory = DetailLayout.EditCategory(PreviewingCategoryName, LOCTEXT("MICPreviewingCategoryTitle", "Previewing"));

		TArray<UObject*> ExternalObjects;
		ExternalObjects.Add(MaterialEditorInstance->SourceInstance);

		PreviewingCategory.AddExternalObjectProperty(ExternalObjects, TEXT("PreviewMesh"));

		DefaultCategory.AddExternalObjectProperty(ExternalObjects, TEXT("AssetUserData"), EPropertyLocation::Advanced);
	}
}

void FMaterialInstanceParameterDetails::CreateGroupsWidget(TSharedRef<IPropertyHandle> ParameterGroupsProperty, IDetailCategoryBuilder& GroupsCategory)
{
	bool bShowSaveButtons = false;
	check(MaterialEditorInstance);
	
	for (int32 GroupIdx = 0; GroupIdx < MaterialEditorInstance->ParameterGroups.Num(); ++GroupIdx)
	{
		FEditorParameterGroup& ParameterGroup = MaterialEditorInstance->ParameterGroups[GroupIdx];
		if (ParameterGroup.GroupAssociation == EMaterialParameterAssociation::GlobalParameter
			&& ParameterGroup.GroupName != FMaterialPropertyHelpers::LayerParamName)
		{
			bShowSaveButtons = true;
			bool bCreateGroup = false;
			for (int32 ParamIdx = 0; ParamIdx < ParameterGroup.Parameters.Num() && !bCreateGroup; ++ParamIdx)
			{
				UDEditorParameterValue* Parameter = ParameterGroup.Parameters[ParamIdx];
				const bool bIsVisible = MaterialEditorInstance->VisibleExpressions.Contains(Parameter->ParameterInfo) && !FMaterialPropertyHelpers::UsesCustomPrimitiveData(Parameter);
				bCreateGroup = bIsVisible && (!MaterialEditorInstance->bShowOnlyOverrides || FMaterialPropertyHelpers::IsOverriddenExpression(Parameter));
			}
		
			if (bCreateGroup)
			{
				IDetailGroup& DetailGroup = GroupsCategory.AddGroup(ParameterGroup.GroupName, FText::FromName(ParameterGroup.GroupName), false, false);
				FUIAction CopyAction(
					FExecuteAction::CreateSP(this, &FMaterialInstanceParameterDetails::OnCopyParameterValues, GroupIdx),
					FCanExecuteAction::CreateSP(this, &FMaterialInstanceParameterDetails::CanCopyParameterValues, GroupIdx));
				FUIAction PasteAction(
					FExecuteAction::CreateSP(this, &FMaterialInstanceParameterDetails::OnPasteParameterValues, GroupIdx),
					FCanExecuteAction::CreateSP(this, &FMaterialInstanceParameterDetails::CanPasteParameterValues, GroupIdx));
				FDetailWidgetRow& HeaderRow = DetailGroup.HeaderRow()
					.CopyAction(CopyAction)
					.PasteAction(PasteAction)
					.NameContent()
					[
						SNew(STextBlock)
						.Text(FText::FromName(DetailGroup.GetGroupName()))
					];

				CreateSingleGroupWidget(ParameterGroup, ParameterGroupsProperty->GetChildHandle(GroupIdx), DetailGroup);

				HeaderRow.AddCustomContextMenuAction(FUIAction(
						FExecuteAction::CreateLambda([&]()mutable 
						{
							EnableGroupParameters(ParameterGroup, true);
						})),
							LOCTEXT("ToggleParametersEnable", "Enable All Parameters"),
							LOCTEXT("ToggleParametersEnableTooltip", "Enable All Parameters in group"),
							FSlateIcon());

				HeaderRow.AddCustomContextMenuAction(FUIAction(
						FExecuteAction::CreateLambda([&]()mutable 
						{
							EnableGroupParameters(ParameterGroup, false);
						})),
							LOCTEXT("ToggleParametersDisable", "Disable All Parameters"),
							LOCTEXT("ToggleParametersDisableTooltip", "Disable All Parameters in group"),
							FSlateIcon());
			}
		}
	}
	
	if (bShowSaveButtons)
	{
		FDetailWidgetRow& SaveInstanceRow = GroupsCategory.AddCustomRow(LOCTEXT("SaveInstances", "Save Instances"));
		FOnClicked OnChildButtonClicked;
		FOnClicked OnSiblingButtonClicked;
		UMaterialInterface* LocalSourceInstance = MaterialEditorInstance->SourceInstance;
		UObject* LocalEditorInstance = MaterialEditorInstance;
		if (!MaterialEditorInstance->bIsFunctionPreviewMaterial)
		{
			OnChildButtonClicked = FOnClicked::CreateStatic(&FMaterialPropertyHelpers::OnClickedSaveNewMaterialInstance, LocalSourceInstance, LocalEditorInstance);
			OnSiblingButtonClicked = FOnClicked::CreateStatic(&FMaterialPropertyHelpers::OnClickedSaveNewMaterialInstance, ToRawPtr(MaterialEditorInstance->SourceInstance->Parent), LocalEditorInstance);
		}
		else
		{
			OnChildButtonClicked = FOnClicked::CreateStatic(&FMaterialPropertyHelpers::OnClickedSaveNewFunctionInstance, 
				ImplicitConv<UMaterialFunctionInterface*>(MaterialEditorInstance->SourceFunction), LocalSourceInstance, LocalEditorInstance);
			OnSiblingButtonClicked = FOnClicked::CreateStatic(&FMaterialPropertyHelpers::OnClickedSaveNewFunctionInstance,
				ImplicitConv<UMaterialFunctionInterface*>(MaterialEditorInstance->SourceFunction->Parent), LocalSourceInstance, LocalEditorInstance);
		}
		SaveInstanceRow.ValueContent()
			.HAlign(HAlign_Fill)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNullWidget::NullWidget
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("SaveSibling", "Save Sibling"))
					.HAlign(HAlign_Center)
					.OnClicked(OnSiblingButtonClicked)
					.ToolTipText(LOCTEXT("SaveToSiblingInstance", "Save to Sibling Instance"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("SaveChild", "Save Child"))
					.HAlign(HAlign_Center)
					.OnClicked(OnChildButtonClicked)
					.ToolTipText(LOCTEXT("SaveToChildInstance", "Save to Child Instance"))
				]
			];
	}
}

void FMaterialInstanceParameterDetails::EnableGroupParameters(FEditorParameterGroup& ParameterGroup, bool ShouldEnable)
{
	// loop through each parameter in the group and toggle to enable/disable them all
	for (int32 ParamIdx = 0; ParamIdx < ParameterGroup.Parameters.Num(); ++ParamIdx)
	{
		UDEditorParameterValue* Parameter = ParameterGroup.Parameters[ParamIdx];
		Parameter->bOverride = ShouldEnable;
	}
}

void FMaterialInstanceParameterDetails::CreateSingleGroupWidget(FEditorParameterGroup& ParameterGroup, TSharedPtr<IPropertyHandle> ParameterGroupProperty, IDetailGroup& DetailGroup, int32 GroupIndex /*= -1*/, bool bForceShowParam /*= false*/)
{
	TSharedPtr<IPropertyHandle> ParametersArrayProperty = ParameterGroupProperty->GetChildHandle("Parameters");

	// Create a custom widget for each parameter in the group
	for (int32 ParamIdx = 0; ParamIdx < ParameterGroup.Parameters.Num(); ++ParamIdx)
	{
		TSharedPtr<IPropertyHandle> ParameterProperty = ParametersArrayProperty->GetChildHandle(ParamIdx);
		UDEditorParameterValue* Parameter = ParameterGroup.Parameters[ParamIdx];
		if (ParameterProperty.IsValid() &&
			(GroupIndex == INDEX_NONE || Parameter->ParameterInfo.Index == GroupIndex))
		{
			UDEditorFontParameterValue* FontParam = Cast<UDEditorFontParameterValue>(Parameter);
			UDEditorMaterialLayersParameterValue* LayersParam = Cast<UDEditorMaterialLayersParameterValue>(Parameter);
			UDEditorScalarParameterValue* ScalarParam = Cast<UDEditorScalarParameterValue>(Parameter);
			UDEditorStaticComponentMaskParameterValue* CompMaskParam = Cast<UDEditorStaticComponentMaskParameterValue>(Parameter);
			UDEditorStaticSwitchParameterValue* SwitchParam = Cast<UDEditorStaticSwitchParameterValue>(Parameter);
			UDEditorTextureParameterValue* TextureParam = Cast<UDEditorTextureParameterValue>(Parameter);
			UDEditorRuntimeVirtualTextureParameterValue* RuntimeVirtualTextureParam = Cast<UDEditorRuntimeVirtualTextureParameterValue>(Parameter);
			UDEditorSparseVolumeTextureParameterValue* SparseVolumeTextureParam = Cast<UDEditorSparseVolumeTextureParameterValue>(Parameter);
			UDEditorVectorParameterValue* VectorParam = Cast<UDEditorVectorParameterValue>(Parameter);

			// Don't display custom primitive data parameters in the details panel.
			// This data is pulled from the primitive and can't be changed on the material.
			if ((VectorParam && VectorParam->bUseCustomPrimitiveData) ||
				(ScalarParam && ScalarParam->bUseCustomPrimitiveData))
			{
				continue;
			}

			if (Parameter->ParameterInfo.Association == EMaterialParameterAssociation::GlobalParameter || bForceShowParam)
			{
				if (VectorParam && VectorParam->bIsUsedAsChannelMask)
				{
					CreateVectorChannelMaskParameterValueWidget(Parameter, ParameterProperty, DetailGroup);
				}
				if (ScalarParam && ScalarParam->AtlasData.bIsUsedAsAtlasPosition)
				{
					CreateScalarAtlasPositionParameterValueWidget(Parameter, ParameterProperty, DetailGroup);
				}
				if (TextureParam && 
					( !TextureParam->ChannelNames.R.IsEmpty()
					|| !TextureParam->ChannelNames.G.IsEmpty()
					|| !TextureParam->ChannelNames.B.IsEmpty()
					|| !TextureParam->ChannelNames.A.IsEmpty()))
				{
					CreateLabeledTextureParameterValueWidget(Parameter, ParameterProperty, DetailGroup);
				}
				else if (LayersParam)
				{
				}
				else if (CompMaskParam)
				{
					CreateMaskParameterValueWidget(Parameter, ParameterProperty, DetailGroup);
				}
				else
				{
					if (ScalarParam && ScalarParam->SliderMax > ScalarParam->SliderMin)
					{
						TSharedPtr<IPropertyHandle> ParameterValueProperty = ParameterProperty->GetChildHandle("ParameterValue");
						ParameterValueProperty->SetInstanceMetaData("UIMin", FString::Printf(TEXT("%f"), ScalarParam->SliderMin));
						ParameterValueProperty->SetInstanceMetaData("UIMax", FString::Printf(TEXT("%f"), ScalarParam->SliderMax));
					}

					if (VectorParam)
					{
						static const FName Red("R");
						static const FName Green("G");
						static const FName Blue("B");
						static const FName Alpha("A");
						if (!VectorParam->ChannelNames.R.IsEmpty())
						{
							ParameterProperty->GetChildHandle(Red)->SetPropertyDisplayName(VectorParam->ChannelNames.R);
						}
						if (!VectorParam->ChannelNames.G.IsEmpty())
						{
							ParameterProperty->GetChildHandle(Green)->SetPropertyDisplayName(VectorParam->ChannelNames.G);
						}
						if (!VectorParam->ChannelNames.B.IsEmpty())
						{
							ParameterProperty->GetChildHandle(Blue)->SetPropertyDisplayName(VectorParam->ChannelNames.B);
						}
						if (!VectorParam->ChannelNames.A.IsEmpty())
						{
							ParameterProperty->GetChildHandle(Alpha)->SetPropertyDisplayName(VectorParam->ChannelNames.A);
						}
					}

					CreateParameterValueWidget(Parameter, ParameterProperty, DetailGroup);
				}
			}
		}
	}
}

void FMaterialInstanceParameterDetails::CreateParameterValueWidget(UDEditorParameterValue* Parameter, TSharedPtr<IPropertyHandle> ParameterProperty, IDetailGroup& DetailGroup)
{
	TSharedPtr<IPropertyHandle> ParameterValueProperty = ParameterProperty->GetChildHandle("ParameterValue");

	if (ParameterValueProperty->IsValidHandle())
	{
		TAttribute<bool> IsParamEnabled = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateStatic(&FMaterialPropertyHelpers::IsOverriddenExpression, Parameter));

		IDetailPropertyRow& PropertyRow = DetailGroup.AddPropertyRow(ParameterValueProperty.ToSharedRef());

		TAttribute<bool> IsResetVisible = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateStatic(&FMaterialPropertyHelpers::ShouldShowResetToDefault, Parameter, MaterialEditorInstance));
		FSimpleDelegate ResetHandler = FSimpleDelegate::CreateStatic(&FMaterialPropertyHelpers::ResetToDefault, Parameter, MaterialEditorInstance);
		FResetToDefaultOverride ResetOverride = FResetToDefaultOverride::Create(IsResetVisible, ResetHandler);

		PropertyRow
			.DisplayName(FText::FromName(Parameter->ParameterInfo.Name))
			.ToolTip(FMaterialPropertyHelpers::GetParameterTooltip(Parameter, MaterialEditorInstance))
			.EditCondition(IsParamEnabled, FOnBooleanValueChanged::CreateStatic(&FMaterialPropertyHelpers::OnOverrideParameter, Parameter, MaterialEditorInstance))
			.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateStatic(&FMaterialPropertyHelpers::ShouldShowExpression, Parameter, MaterialEditorInstance, ShowHiddenDelegate)))
			.OverrideResetToDefault(ResetOverride);

		// Textures need a special widget that filters based on VT or not
		UDEditorTextureParameterValue* TextureParam = Cast<UDEditorTextureParameterValue>(Parameter);
		if (TextureParam != nullptr)
		{
			UMaterial *Material = MaterialEditorInstance->SourceInstance->GetMaterial();
			if (Material != nullptr)
			{
				UMaterialExpressionTextureSampleParameter* Expression = Material->FindExpressionByGUID<UMaterialExpressionTextureSampleParameter>(TextureParam->ExpressionId);
				if (Expression != nullptr)
				{
					TWeakObjectPtr<UMaterialExpressionTextureSampleParameter> SamplerExpression = Expression;

					PropertyRow.CustomWidget()
					.NameContent()
					[
						ParameterValueProperty->CreatePropertyNameWidget()
					]
					.ValueContent()
					.MaxDesiredWidth(TOptional<float>())
					[
						SNew(SObjectPropertyEntryBox)
						.PropertyHandle(ParameterValueProperty)
						.AllowedClass(UTexture::StaticClass())
						.ThumbnailPool(PropertyUtilities.Pin()->GetThumbnailPool())
						.OnShouldFilterAsset_Lambda([SamplerExpression](const FAssetData& AssetData)
						{
							if (SamplerExpression.Get())
							{
								bool VirtualTextured = false;
								AssetData.GetTagValue<bool>("VirtualTextureStreaming", VirtualTextured);

								bool ExpressionIsVirtualTextured = IsVirtualSamplerType(SamplerExpression->SamplerType);

								return VirtualTextured != ExpressionIsVirtualTextured;
							}
							else
							{
								return false;
							}
						})
					];
				}
			}
		}
	}
}

void FMaterialInstanceParameterDetails::CreateMaskParameterValueWidget(UDEditorParameterValue* Parameter, TSharedPtr<IPropertyHandle> ParameterProperty, IDetailGroup& DetailGroup )
{
	TSharedPtr<IPropertyHandle> ParameterValueProperty = ParameterProperty->GetChildHandle("ParameterValue");
	TSharedPtr<IPropertyHandle> RMaskProperty = ParameterValueProperty->GetChildHandle("R");
	TSharedPtr<IPropertyHandle> GMaskProperty = ParameterValueProperty->GetChildHandle("G");
	TSharedPtr<IPropertyHandle> BMaskProperty = ParameterValueProperty->GetChildHandle("B");
	TSharedPtr<IPropertyHandle> AMaskProperty = ParameterValueProperty->GetChildHandle("A");

	if (ParameterValueProperty->IsValidHandle())
	{
		TAttribute<bool> IsParamEnabled = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateStatic(&FMaterialPropertyHelpers::IsOverriddenExpression, Parameter));

		IDetailPropertyRow& PropertyRow = DetailGroup.AddPropertyRow(ParameterValueProperty.ToSharedRef());
		PropertyRow.EditCondition(IsParamEnabled, FOnBooleanValueChanged::CreateStatic(&FMaterialPropertyHelpers::OnOverrideParameter, Parameter, MaterialEditorInstance));
		// Handle reset to default manually
		PropertyRow.OverrideResetToDefault(FResetToDefaultOverride::Create(FSimpleDelegate::CreateStatic(&FMaterialPropertyHelpers::ResetToDefault, Parameter, MaterialEditorInstance)));
		PropertyRow.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateStatic(&FMaterialPropertyHelpers::ShouldShowExpression, Parameter, MaterialEditorInstance, ShowHiddenDelegate)));

		const FText ParameterName = FText::FromName(Parameter->ParameterInfo.Name);

		FDetailWidgetRow& CustomWidget = PropertyRow.CustomWidget();
		CustomWidget
			.FilterString(ParameterName)
			.NameContent()
			[
				SNew(STextBlock)
				.Text(ParameterName)
				.ToolTipText(FMaterialPropertyHelpers::GetParameterTooltip(Parameter, MaterialEditorInstance))
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			]
		.ValueContent()
			.MaxDesiredWidth(200.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.AutoWidth()
			[
				RMaskProperty->CreatePropertyNameWidget()
			]
		+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.AutoWidth()
			[
				RMaskProperty->CreatePropertyValueWidget()
			]
		+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.Padding(FMargin(10.0f, 0.0f, 0.0f, 0.0f))
			.AutoWidth()
			[
				GMaskProperty->CreatePropertyNameWidget()
			]
		+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.AutoWidth()
			[
				GMaskProperty->CreatePropertyValueWidget()
			]
		+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.Padding(FMargin(10.0f, 0.0f, 0.0f, 0.0f))
			.AutoWidth()
			[
				BMaskProperty->CreatePropertyNameWidget()
			]
		+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.AutoWidth()
			[
				BMaskProperty->CreatePropertyValueWidget()
			]
		+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.Padding(FMargin(10.0f, 0.0f, 0.0f, 0.0f))
			.AutoWidth()
			[
				AMaskProperty->CreatePropertyNameWidget()
			]
		+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.AutoWidth()
			[
				AMaskProperty->CreatePropertyValueWidget()
			]
			]
			];
	}
}

void FMaterialInstanceParameterDetails::CreateVectorChannelMaskParameterValueWidget(UDEditorParameterValue* Parameter, TSharedPtr<IPropertyHandle> ParameterProperty, IDetailGroup& DetailGroup)
{
	TSharedPtr<IPropertyHandle> ParameterValueProperty = ParameterProperty->GetChildHandle("ParameterValue");

	if (ParameterValueProperty->IsValidHandle())
	{
		TAttribute<bool> IsParamEnabled = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateStatic(&FMaterialPropertyHelpers::IsOverriddenExpression, Parameter));

		IDetailPropertyRow& PropertyRow = DetailGroup.AddPropertyRow(ParameterValueProperty.ToSharedRef());
		PropertyRow.EditCondition(IsParamEnabled, FOnBooleanValueChanged::CreateStatic(&FMaterialPropertyHelpers::OnOverrideParameter, Parameter, MaterialEditorInstance));
		// Handle reset to default manually
		PropertyRow.OverrideResetToDefault(FResetToDefaultOverride::Create(FSimpleDelegate::CreateStatic(&FMaterialPropertyHelpers::ResetToDefault, Parameter, MaterialEditorInstance)));
		PropertyRow.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateStatic(&FMaterialPropertyHelpers::ShouldShowExpression, Parameter, MaterialEditorInstance, ShowHiddenDelegate)));

		const FText ParameterName = FText::FromName(Parameter->ParameterInfo.Name);

		// Combo box hooks for converting between our "enum" and colors
		FOnGetPropertyComboBoxStrings GetMaskStrings = FOnGetPropertyComboBoxStrings::CreateStatic(&FMaterialPropertyHelpers::GetVectorChannelMaskComboBoxStrings);
		FOnGetPropertyComboBoxValue GetMaskValue = FOnGetPropertyComboBoxValue::CreateStatic(&FMaterialPropertyHelpers::GetVectorChannelMaskValue, Parameter);
		FOnPropertyComboBoxValueSelected SetMaskValue = FOnPropertyComboBoxValueSelected::CreateStatic(&FMaterialPropertyHelpers::SetVectorChannelMaskValue, ParameterValueProperty, Parameter, (UObject*)MaterialEditorInstance);

		// Widget replaces color picker with combo box
		FDetailWidgetRow& CustomWidget = PropertyRow.CustomWidget();
		CustomWidget
		.FilterString(ParameterName)
		.NameContent()
		[
			SNew(STextBlock)
			.Text(ParameterName)
			.ToolTipText(FMaterialPropertyHelpers::GetParameterTooltip(Parameter, MaterialEditorInstance))
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
		]
		.ValueContent()
		.MaxDesiredWidth(200.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.AutoWidth()
				[
					PropertyCustomizationHelpers::MakePropertyComboBox(ParameterValueProperty, GetMaskStrings, GetMaskValue, SetMaskValue)
				]
			]
		];
	}
}

void FMaterialInstanceParameterDetails::CreateScalarAtlasPositionParameterValueWidget(class UDEditorParameterValue* Parameter, TSharedPtr<IPropertyHandle> ParameterProperty, IDetailGroup& DetailGroup)
{
	TSharedPtr<IPropertyHandle> ParameterValueProperty = ParameterProperty->GetChildHandle("ParameterValue");

	if (ParameterValueProperty->IsValidHandle())
	{
		TAttribute<bool> IsParamEnabled = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateStatic(&FMaterialPropertyHelpers::IsOverriddenExpression, Parameter));

		IDetailPropertyRow& PropertyRow = DetailGroup.AddPropertyRow(ParameterValueProperty.ToSharedRef());
		PropertyRow.EditCondition(IsParamEnabled, FOnBooleanValueChanged::CreateStatic(&FMaterialPropertyHelpers::OnOverrideParameter, Parameter, MaterialEditorInstance));
		// Handle reset to default manually
		PropertyRow.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateStatic(&FMaterialPropertyHelpers::ShouldShowExpression, Parameter, MaterialEditorInstance, ShowHiddenDelegate)));

		const FText ParameterName = FText::FromName(Parameter->ParameterInfo.Name);
		UDEditorScalarParameterValue* AtlasParameter = Cast<UDEditorScalarParameterValue>(Parameter);

		TAttribute<bool> IsResetVisible = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateStatic(&FMaterialPropertyHelpers::ShouldShowResetToDefault, Parameter, MaterialEditorInstance));
		FSimpleDelegate ResetHandler = FSimpleDelegate::CreateStatic(&FMaterialPropertyHelpers::ResetCurveToDefault, Parameter, MaterialEditorInstance);
		FResetToDefaultOverride ResetOverride = FResetToDefaultOverride::Create(IsResetVisible, ResetHandler);

		PropertyRow.OverrideResetToDefault(ResetOverride);

		FDetailWidgetRow& CustomWidget = PropertyRow.CustomWidget();
		CustomWidget
			.FilterString(ParameterName)
			.NameContent()
			[
				SNew(STextBlock)
				.Text(ParameterName)
				.ToolTipText(FMaterialPropertyHelpers::GetParameterTooltip(Parameter, MaterialEditorInstance))
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			]
			.ValueContent()
			.HAlign(HAlign_Fill)
			.MaxDesiredWidth(400.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.ObjectPath(this, &FMaterialInstanceParameterDetails::GetCurvePath, AtlasParameter)
				.AllowedClass(UCurveLinearColor::StaticClass())
				.NewAssetFactories(TArray<UFactory*>())
				.DisplayThumbnail(true)
				.ThumbnailPool(PropertyUtilities.Pin()->GetThumbnailPool())
				.OnShouldFilterAsset(FOnShouldFilterAsset::CreateStatic(&FMaterialPropertyHelpers::OnShouldFilterCurveAsset, AtlasParameter->AtlasData.Atlas))
				.OnShouldSetAsset(FOnShouldSetAsset::CreateStatic(&FMaterialPropertyHelpers::OnShouldSetCurveAsset, AtlasParameter->AtlasData.Atlas))
				.OnObjectChanged(FOnSetObject::CreateStatic(&FMaterialPropertyHelpers::SetPositionFromCurveAsset, AtlasParameter->AtlasData.Atlas, AtlasParameter, ParameterProperty, (UObject*)MaterialEditorInstance))
				.DisplayCompactSize(true)
			];
	}
}

void FMaterialInstanceParameterDetails::CreateLabeledTextureParameterValueWidget(class UDEditorParameterValue* Parameter, TSharedPtr<IPropertyHandle> ParameterProperty, IDetailGroup& DetailGroup)
{
	TSharedPtr<IPropertyHandle> ParameterValueProperty = ParameterProperty->GetChildHandle("ParameterValue");

	if (ParameterValueProperty->IsValidHandle())
	{
		UDEditorTextureParameterValue* TextureParam = Cast<UDEditorTextureParameterValue>(Parameter);
		if (TextureParam)
		{
			UMaterial *Material = MaterialEditorInstance->SourceInstance->GetMaterial();
			if (Material != nullptr)
			{
				UMaterialExpressionTextureSampleParameter* Expression = Material->FindExpressionByGUID<UMaterialExpressionTextureSampleParameter>(TextureParam->ExpressionId);
				if (Expression != nullptr)
				{
					TWeakObjectPtr<UMaterialExpressionTextureSampleParameter> SamplerExpression = Expression;
					TAttribute<bool> IsParamEnabled = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateStatic(&FMaterialPropertyHelpers::IsOverriddenExpression, Parameter));

					IDetailPropertyRow& PropertyRow = DetailGroup.AddPropertyRow(ParameterValueProperty.ToSharedRef());

					TAttribute<bool> IsResetVisible = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateStatic(&FMaterialPropertyHelpers::ShouldShowResetToDefault, Parameter, MaterialEditorInstance));
					FSimpleDelegate ResetHandler = FSimpleDelegate::CreateStatic(&FMaterialPropertyHelpers::ResetToDefault, Parameter, MaterialEditorInstance);
					FResetToDefaultOverride ResetOverride = FResetToDefaultOverride::Create(IsResetVisible, ResetHandler);

					PropertyRow
						.DisplayName(FText::FromName(Parameter->ParameterInfo.Name))
						.EditCondition(IsParamEnabled, FOnBooleanValueChanged::CreateStatic(&FMaterialPropertyHelpers::OnOverrideParameter, Parameter, MaterialEditorInstance))
						.ToolTip(FMaterialPropertyHelpers::GetParameterTooltip(Parameter, MaterialEditorInstance))
						.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateStatic(&FMaterialPropertyHelpers::ShouldShowExpression, Parameter, MaterialEditorInstance, ShowHiddenDelegate)));


					TSharedPtr<SWidget> NameWidget;
					TSharedPtr<SWidget> ValueWidget;
					FDetailWidgetRow Row;
					PropertyRow.GetDefaultWidgets(NameWidget, ValueWidget, Row);

					FDetailWidgetRow &DetailWidgetRow = PropertyRow.CustomWidget();
					TSharedPtr<SVerticalBox> NameVerticalBox;
					DetailWidgetRow.NameContent()
						[
							SAssignNew(NameVerticalBox, SVerticalBox)
							+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromName(Parameter->ParameterInfo.Name))
						.ToolTipText(FMaterialPropertyHelpers::GetParameterTooltip(Parameter, MaterialEditorInstance))
						.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						]
						];
					DetailWidgetRow.ValueContent()
						.MinDesiredWidth(Row.ValueWidget.MinWidth)
						.MaxDesiredWidth(Row.ValueWidget.MaxWidth)
						[
							SNew(SObjectPropertyEntryBox)
							.PropertyHandle(ParameterValueProperty)
							.AllowedClass(UTexture::StaticClass())
							.ThumbnailPool(PropertyUtilities.Pin()->GetThumbnailPool())
							.OnShouldFilterAsset_Lambda([SamplerExpression](const FAssetData& AssetData)
							{
								if (SamplerExpression.Get())
								{
									bool VirtualTextured = false;
									AssetData.GetTagValue<bool>("VirtualTextureStreaming", VirtualTextured);

									bool ExpressionIsVirtualTextured = IsVirtualSamplerType(SamplerExpression->SamplerType);

									return VirtualTextured != ExpressionIsVirtualTextured;
								}
								else
								{
									return false;
								}
							})
						];

					DetailWidgetRow.OverrideResetToDefault(ResetOverride);

					static const FName Red("R");
					static const FName Green("G");
					static const FName Blue("B");
					static const FName Alpha("A");

					if (!TextureParam->ChannelNames.R.IsEmpty())
					{
						NameVerticalBox->AddSlot()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.Padding(20.0, 2.0, 4.0, 2.0)
								[
									SNew(STextBlock)
									.Text(FText::FromName(Red))
									.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))
								]
								+ SHorizontalBox::Slot()
								.HAlign(HAlign_Left)
								.Padding(4.0, 2.0)
								[
									SNew(STextBlock)
									.Text(TextureParam->ChannelNames.R)
									.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
								]
							];
					}
					if (!TextureParam->ChannelNames.G.IsEmpty())
					{
						NameVerticalBox->AddSlot()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
							.Padding(20.0, 2.0, 4.0, 2.0)
							.AutoWidth()
							[
								SNew(STextBlock)
								.Text(FText::FromName(Green))
							.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))
							]
						+ SHorizontalBox::Slot()
							.HAlign(HAlign_Left)
							.Padding(4.0, 2.0)
							[
								SNew(STextBlock)
								.Text(TextureParam->ChannelNames.G)
							.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
							]
							];
					}
					if (!TextureParam->ChannelNames.B.IsEmpty())
					{
						NameVerticalBox->AddSlot()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
							.Padding(20.0, 2.0, 4.0, 2.0)
							.AutoWidth()
							[
								SNew(STextBlock)
								.Text(FText::FromName(Blue))
							.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))
							]
						+ SHorizontalBox::Slot()
							.HAlign(HAlign_Left)
							.Padding(4.0, 2.0)
							[
								SNew(STextBlock)
								.Text(TextureParam->ChannelNames.B)
							.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
							]
							];
					}
					if (!TextureParam->ChannelNames.A.IsEmpty())
					{
						NameVerticalBox->AddSlot()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
							.Padding(20.0, 2.0, 4.0, 2.0)
							.AutoWidth()
							[
								SNew(STextBlock)
								.Text(FText::FromName(Alpha))
							.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))
							]
						+ SHorizontalBox::Slot()
							.HAlign(HAlign_Left)
							.Padding(4.0, 2.0)
							[
								SNew(STextBlock)
								.Text(TextureParam->ChannelNames.A)
							.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
							]
							];
					}
				}
			}
		}
	}
}

FString FMaterialInstanceParameterDetails::GetCurvePath(UDEditorScalarParameterValue* Parameter) const
{
	FString Path = Parameter->AtlasData.Curve->GetPathName();
	return Path;
}

bool FMaterialInstanceParameterDetails::IsVisibleExpression(UDEditorParameterValue* Parameter)
{
	return MaterialEditorInstance->VisibleExpressions.Contains(Parameter->ParameterInfo);
}

EVisibility FMaterialInstanceParameterDetails::ShouldShowExpression(UDEditorParameterValue* Parameter) const
{
	return FMaterialPropertyHelpers::ShouldShowExpression(Parameter, MaterialEditorInstance, ShowHiddenDelegate);
}

bool FMaterialInstanceParameterDetails::OnShouldSetAsset(const FAssetData& AssetData) const
{
	if (MaterialEditorInstance->bIsFunctionPreviewMaterial)
	{
		if (MaterialEditorInstance->SourceFunction->GetMaterialFunctionUsage() == EMaterialFunctionUsage::Default)
		{
			return false;
		}
		else
		{
			UMaterialFunctionInstance* FunctionInstance = Cast<UMaterialFunctionInstance>(AssetData.GetAsset());
			if (FunctionInstance != nullptr)
			{
				bool bIsChild = FunctionInstance->IsDependent(MaterialEditorInstance->SourceFunction);
				if (bIsChild)
				{
					FMessageDialog::Open(
						EAppMsgType::Ok,
						FText::Format(LOCTEXT("CannotSetExistingChildFunctionAsParent", "Cannot set {0} as a parent as it is already a child of this material function instance."), FText::FromName(AssetData.AssetName)));
				}
				return !bIsChild;
			}
		}
	}

	UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(AssetData.GetAsset());

	if (MaterialInstance != nullptr)
	{
		bool bIsChild = MaterialInstance->IsChildOf(MaterialEditorInstance->SourceInstance);
		if (bIsChild)
		{
			FMessageDialog::Open(
				EAppMsgType::Ok,
				FText::Format(LOCTEXT("CannotSetExistingChildAsParent", "Cannot set {0} as a parent as it is already a child of this material instance."), FText::FromName(AssetData.AssetName)));
		}

		if (bIsChild)
		{
			return false;
		}
	}

	return true;
}

void FMaterialInstanceParameterDetails::OnAssetChanged(const FAssetData & InAssetData, TSharedRef<IPropertyHandle> InHandle)
{
	if (MaterialEditorInstance->bIsFunctionPreviewMaterial &&
		MaterialEditorInstance->SourceFunction->GetMaterialFunctionUsage() != EMaterialFunctionUsage::Default)
	{
		UMaterialFunctionInterface* NewParent = Cast<UMaterialFunctionInterface>(InAssetData.GetAsset());
		if (NewParent != nullptr)
		{
			MaterialEditorInstance->SourceFunction->SetParent(NewParent);
			FPropertyChangedEvent ParentChanged = FPropertyChangedEvent(InHandle->GetProperty());
			MaterialEditorInstance->PostEditChangeProperty(ParentChanged);
		}
	}
}

EVisibility FMaterialInstanceParameterDetails::ShouldShowMaterialRefractionSettings() const
{
	return (MaterialEditorInstance->SourceInstance->GetMaterial()->bUsesDistortion && IsTranslucentBlendMode(*MaterialEditorInstance->SourceInstance)) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FMaterialInstanceParameterDetails::ShouldShowSubsurfaceProfile() const
{
	FMaterialShadingModelField ShadingModels = MaterialEditorInstance->SourceInstance->GetShadingModels();

	return UseSubsurfaceProfile(ShadingModels) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FMaterialInstanceParameterDetails::ShouldShowSpecularProfile() const
{
	const bool bHasProfiles = Substrate::IsSubstrateEnabled() && MaterialEditorInstance->Parent && MaterialEditorInstance->Parent->SpecularProfiles.Num() > 0;
	return bHasProfiles ? EVisibility::Visible : EVisibility::Collapsed;
}

void FMaterialInstanceParameterDetails::OnCopyParameterValues(int32 ParameterGroupIndex)
{
	if (!MaterialEditorInstance || !MaterialEditorInstance->ParameterGroups.IsValidIndex(ParameterGroupIndex))
	{
		return;
	}
	FEditorParameterGroup& ParameterGroup = MaterialEditorInstance->ParameterGroups[ParameterGroupIndex];

	TStringBuilder<4096> CombinedValue;

	const int32 NumParams = ParameterGroup.Parameters.Num();
	for (int32 ParamIdx = 0; ParamIdx < NumParams; ++ParamIdx)
	{
		UDEditorParameterValue* Parameter = ParameterGroup.Parameters[ParamIdx];
		const FName ParamName = Parameter->ParameterInfo.Name;

		const TCHAR* Prefix = (ParamIdx == 0) ? TEXT("") : TEXT(",");

		// Include the value in the result entry only if the parameter is overridden.
		const bool bOverride = FMaterialPropertyHelpers::IsOverriddenExpression(Parameter);
		if (bOverride)
		{
			FProperty* ParameterValueProperty = Parameter->GetClass()->FindPropertyByName("ParameterValue");
			if (ParameterValueProperty != nullptr)
			{
				FString ParameterValueString;
				if (ParameterValueProperty->ExportText_InContainer(0, ParameterValueString, Parameter, Parameter, Parameter, PPF_Copy))
				{
					CombinedValue.Appendf(TEXT("%s%s.Override=True,%s.Value=\"%s\""),
						Prefix,
						*(ParamName.ToString()),
						*(ParamName.ToString()),
						*(ParameterValueString.ReplaceCharWithEscapedChar()));
				}
			}
		}
		else
		{
			CombinedValue.Appendf(TEXT("%s%s.Override=False"), Prefix, *(ParamName.ToString()));
		}
	}

	if (CombinedValue.Len())
	{
		// Copy.
		FPlatformApplicationMisc::ClipboardCopy(*CombinedValue);
	}
}

bool FMaterialInstanceParameterDetails::CanCopyParameterValues(int32 ParameterGroupIndex)
{
	return MaterialEditorInstance && MaterialEditorInstance->ParameterGroups.IsValidIndex(ParameterGroupIndex)
		&& (MaterialEditorInstance->ParameterGroups[ParameterGroupIndex].Parameters.Num() > 0);
}

void FMaterialInstanceParameterDetails::OnPasteParameterValues(int32 ParameterGroupIndex)
{
	if (!MaterialEditorInstance || !MaterialEditorInstance->ParameterGroups.IsValidIndex(ParameterGroupIndex))
	{
		return;
	}

	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);
	if (!ClipboardContent.IsEmpty())
	{
		FScopedTransaction Transaction(LOCTEXT("PasteMaterialInstanceParameters", "Paste Material Instance Parameters"));

		MaterialEditorInstance->Modify();

		for (int32 ParamIdx = 0; ParamIdx < MaterialEditorInstance->ParameterGroups[ParameterGroupIndex].Parameters.Num(); ++ParamIdx)
		{
			UDEditorParameterValue* Parameter = MaterialEditorInstance->ParameterGroups[ParameterGroupIndex].Parameters[ParamIdx];
			Parameter->Modify();

			const FName ParamName = Parameter->ParameterInfo.Name;

			const FString OverrideKey = FString::Printf(TEXT("%s.Override="), *(ParamName.ToString()));
			bool bParsedOverride = false;
			if (FParse::Bool(*ClipboardContent, *OverrideKey, bParsedOverride))
			{
				Parameter->bOverride = bParsedOverride;
				if (bParsedOverride)
				{
					// Paste value.
					const FString ValueKey = FString::Printf(TEXT("%s.Value="), *(ParamName.ToString()));
					FString ParsedValueString;
					if (FParse::Value(*ClipboardContent, *ValueKey, ParsedValueString))
					{
						ParsedValueString = ParsedValueString.ReplaceEscapedCharWithChar();
						FProperty* ParameterValueProperty = Parameter->GetClass()->FindPropertyByName("ParameterValue");
						if (ParameterValueProperty != nullptr)
						{
							ParameterValueProperty->ImportText_InContainer(*ParsedValueString, Parameter, Parameter, PPF_Copy);
						}
					}
				}
			}
		}

		MaterialEditorInstance->PostEditChange();
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();
	}
}

bool FMaterialInstanceParameterDetails::CanPasteParameterValues(int32 ParameterGroupIndex)
{
	// First check the same criteria as copying.
	if (!CanCopyParameterValues(ParameterGroupIndex))
	{
		return false;
	}

	// Now see if there's anything to paste from the clipboard.
	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);
	return !ClipboardContent.IsEmpty();
}

void FMaterialInstanceParameterDetails::CreateLightmassOverrideWidgets(IDetailLayoutBuilder& DetailLayout)
{
	IDetailCategoryBuilder& DetailCategory = DetailLayout.EditCategory(NAME_None);

	static FName GroupName(TEXT("LightmassSettings"));
	IDetailGroup& LightmassSettingsGroup = DetailCategory.AddGroup(GroupName, LOCTEXT("LightmassSettingsGroup", "Lightmass Settings"), false, false);

	TAttribute<bool> IsOverrideCastShadowAsMaskedEnabled = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([this] { return (bool)MaterialEditorInstance->LightmassSettings.CastShadowAsMasked.bOverride; }));
	TAttribute<bool> IsOverrideEmissiveBoostEnabled = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([this] { return (bool)MaterialEditorInstance->LightmassSettings.EmissiveBoost.bOverride; }));
	TAttribute<bool> IsOverrideDiffuseBoostEnabled = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([this] { return (bool)MaterialEditorInstance->LightmassSettings.DiffuseBoost.bOverride; }));
	TAttribute<bool> IsOverrideExportResolutionScaleEnabled = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([this] { return (bool)MaterialEditorInstance->LightmassSettings.ExportResolutionScale.bOverride; }));
	
	TSharedRef<IPropertyHandle> LightmassSettings = DetailLayout.GetProperty("LightmassSettings");
	TSharedPtr<IPropertyHandle> CastShadowAsMaskedProperty = LightmassSettings->GetChildHandle("CastShadowAsMasked");
	TSharedPtr<IPropertyHandle> EmissiveBoostProperty = LightmassSettings->GetChildHandle("EmissiveBoost");
	TSharedPtr<IPropertyHandle> DiffuseBoostProperty = LightmassSettings->GetChildHandle("DiffuseBoost");
	TSharedPtr<IPropertyHandle> ExportResolutionScaleProperty = LightmassSettings->GetChildHandle("ExportResolutionScale");

	
	FIsResetToDefaultVisible IsCastShadowAsMaskedPropertyResetVisible = FIsResetToDefaultVisible::CreateLambda([this](TSharedPtr<IPropertyHandle> InHandle) {
		return MaterialEditorInstance->Parent != nullptr ? MaterialEditorInstance->LightmassSettings.CastShadowAsMasked.ParameterValue != MaterialEditorInstance->Parent->GetCastShadowAsMasked() : false;
	});
	FResetToDefaultHandler ResetCastShadowAsMaskedPropertyHandler = FResetToDefaultHandler::CreateLambda([this](TSharedPtr<IPropertyHandle> InHandle) {
		if (MaterialEditorInstance->Parent != nullptr)
		{
			MaterialEditorInstance->LightmassSettings.CastShadowAsMasked.ParameterValue = MaterialEditorInstance->Parent->GetCastShadowAsMasked();
		}
	});
	FResetToDefaultOverride ResetCastShadowAsMaskedPropertyOverride = FResetToDefaultOverride::Create(IsCastShadowAsMaskedPropertyResetVisible, ResetCastShadowAsMaskedPropertyHandler);
	IDetailPropertyRow& CastShadowAsMaskedPropertyRow = LightmassSettingsGroup.AddPropertyRow(CastShadowAsMaskedProperty->GetChildHandle(0).ToSharedRef());
	CastShadowAsMaskedPropertyRow
		.DisplayName(CastShadowAsMaskedProperty->GetPropertyDisplayName())
		.ToolTip(CastShadowAsMaskedProperty->GetToolTipText())
		.EditCondition(IsOverrideCastShadowAsMaskedEnabled, FOnBooleanValueChanged::CreateLambda([this](bool NewValue) {
		MaterialEditorInstance->LightmassSettings.CastShadowAsMasked.bOverride = (uint32)NewValue;
		MaterialEditorInstance->PostEditChange();
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();
	}))
		.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FMaterialInstanceParameterDetails::IsOverriddenAndVisible, IsOverrideCastShadowAsMaskedEnabled)))
		.OverrideResetToDefault(ResetCastShadowAsMaskedPropertyOverride);

	FIsResetToDefaultVisible IsEmissiveBoostPropertyResetVisible = FIsResetToDefaultVisible::CreateLambda([this](TSharedPtr<IPropertyHandle> InHandle) {
		return MaterialEditorInstance->Parent != nullptr ? MaterialEditorInstance->LightmassSettings.EmissiveBoost.ParameterValue != MaterialEditorInstance->Parent->GetEmissiveBoost() : false;
	});
	FResetToDefaultHandler ResetEmissiveBoostPropertyHandler = FResetToDefaultHandler::CreateLambda([this](TSharedPtr<IPropertyHandle> InHandle) {
		if (MaterialEditorInstance->Parent != nullptr)
		{
			MaterialEditorInstance->LightmassSettings.EmissiveBoost.ParameterValue = MaterialEditorInstance->Parent->GetEmissiveBoost();
		}
	});
	FResetToDefaultOverride ResetEmissiveBoostPropertyOverride = FResetToDefaultOverride::Create(IsEmissiveBoostPropertyResetVisible, ResetEmissiveBoostPropertyHandler);
	IDetailPropertyRow& EmissiveBoostPropertyRow = LightmassSettingsGroup.AddPropertyRow(EmissiveBoostProperty->GetChildHandle(0).ToSharedRef());
	EmissiveBoostPropertyRow
		.DisplayName(EmissiveBoostProperty->GetPropertyDisplayName())
		.ToolTip(EmissiveBoostProperty->GetToolTipText())
		.EditCondition(IsOverrideEmissiveBoostEnabled, FOnBooleanValueChanged::CreateLambda([this](bool NewValue) {
		MaterialEditorInstance->LightmassSettings.EmissiveBoost.bOverride = (uint32)NewValue;
		MaterialEditorInstance->PostEditChange();
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();
	}))
		.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FMaterialInstanceParameterDetails::IsOverriddenAndVisible, IsOverrideEmissiveBoostEnabled)))
		.OverrideResetToDefault(ResetEmissiveBoostPropertyOverride);

	FIsResetToDefaultVisible IsDiffuseBoostPropertyResetVisible = FIsResetToDefaultVisible::CreateLambda([this](TSharedPtr<IPropertyHandle> InHandle) {
		return MaterialEditorInstance->Parent != nullptr ? MaterialEditorInstance->LightmassSettings.DiffuseBoost.ParameterValue != MaterialEditorInstance->Parent->GetDiffuseBoost() : false;
	});
	FResetToDefaultHandler ResetDiffuseBoostPropertyHandler = FResetToDefaultHandler::CreateLambda([this](TSharedPtr<IPropertyHandle> InHandle) {
		if (MaterialEditorInstance->Parent != nullptr)
		{
			MaterialEditorInstance->LightmassSettings.DiffuseBoost.ParameterValue = MaterialEditorInstance->Parent->GetDiffuseBoost();
		}
	});
	FResetToDefaultOverride ResetDiffuseBoostPropertyOverride = FResetToDefaultOverride::Create(IsDiffuseBoostPropertyResetVisible, ResetDiffuseBoostPropertyHandler);
	IDetailPropertyRow& DiffuseBoostPropertyRow = LightmassSettingsGroup.AddPropertyRow(DiffuseBoostProperty->GetChildHandle(0).ToSharedRef());
	DiffuseBoostPropertyRow
		.DisplayName(DiffuseBoostProperty->GetPropertyDisplayName())
		.ToolTip(DiffuseBoostProperty->GetToolTipText())
		.EditCondition(IsOverrideDiffuseBoostEnabled, FOnBooleanValueChanged::CreateLambda([this](bool NewValue) {
		MaterialEditorInstance->LightmassSettings.DiffuseBoost.bOverride = (uint32)NewValue;
		MaterialEditorInstance->PostEditChange();
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();
	}))
		.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FMaterialInstanceParameterDetails::IsOverriddenAndVisible, IsOverrideDiffuseBoostEnabled)))
		.OverrideResetToDefault(ResetDiffuseBoostPropertyOverride);

	FIsResetToDefaultVisible IsExportResolutionScalePropertyResetVisible = FIsResetToDefaultVisible::CreateLambda([this](TSharedPtr<IPropertyHandle> InHandle) {
		return MaterialEditorInstance->Parent != nullptr ? MaterialEditorInstance->LightmassSettings.ExportResolutionScale.ParameterValue != MaterialEditorInstance->Parent->GetDiffuseBoost() : false;
	});
	FResetToDefaultHandler ResetExportResolutionScalePropertyHandler = FResetToDefaultHandler::CreateLambda([this](TSharedPtr<IPropertyHandle> InHandle) {
		if (MaterialEditorInstance->Parent != nullptr)
		{
			MaterialEditorInstance->LightmassSettings.ExportResolutionScale.ParameterValue = MaterialEditorInstance->Parent->GetDiffuseBoost();
		}
	});
	FResetToDefaultOverride ResetExportResolutionScalePropertyOverride = FResetToDefaultOverride::Create(IsExportResolutionScalePropertyResetVisible, ResetExportResolutionScalePropertyHandler);
	IDetailPropertyRow& ExportResolutionScalePropertyRow = LightmassSettingsGroup.AddPropertyRow(ExportResolutionScaleProperty->GetChildHandle(0).ToSharedRef());
	ExportResolutionScalePropertyRow
		.DisplayName(ExportResolutionScaleProperty->GetPropertyDisplayName())
		.ToolTip(ExportResolutionScaleProperty->GetToolTipText())
		.EditCondition(IsOverrideExportResolutionScaleEnabled, FOnBooleanValueChanged::CreateLambda([this](bool NewValue) {
		MaterialEditorInstance->LightmassSettings.ExportResolutionScale.bOverride = (uint32)NewValue;
		MaterialEditorInstance->PostEditChange();
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();
	}))
		.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FMaterialInstanceParameterDetails::IsOverriddenAndVisible, IsOverrideExportResolutionScaleEnabled)))
		.OverrideResetToDefault(ResetExportResolutionScalePropertyOverride);
}

void FMaterialInstanceParameterDetails::CreatePostProcessOverrideWidgets(IDetailLayoutBuilder& DetailLayout)
{
	if (MaterialEditorInstance->PostProcessOverrides.bIsOverrideable)
	{
		FName PostProcessCategoryName = TEXT("PostProcessOverrides");
		IDetailCategoryBuilder& PostProcessCategory = DetailLayout.EditCategory(PostProcessCategoryName, LOCTEXT("MICPostProcessOverridesTitle", "Post Process Overrides"));
		PostProcessCategory.InitiallyCollapsed(true);

		TAttribute<bool> IsOverrideLocationEnabled = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([this] { return (bool)MaterialEditorInstance->PostProcessOverrides.bOverrideBlendableLocation; }));
		TAttribute<bool> IsOverridePriorityEnabled = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([this] { return (bool)MaterialEditorInstance->PostProcessOverrides.bOverrideBlendablePriority; }));

		TSharedRef<IPropertyHandle> PostProcessOverridesProperty = DetailLayout.GetProperty("PostProcessOverrides");
		TSharedPtr<IPropertyHandle> BlendableLocationProperty = PostProcessOverridesProperty->GetChildHandle("BlendableLocationOverride");
		TSharedPtr<IPropertyHandle> BlendablePriorityProperty = PostProcessOverridesProperty->GetChildHandle("BlendablePriorityOverride");
		TSharedPtr<IPropertyHandle> UserSceneTextureOutputProperty = PostProcessOverridesProperty->GetChildHandle("UserSceneTextureOutput");

		FIsResetToDefaultVisible IsBlendableLocationPropertyResetVisible = FIsResetToDefaultVisible::CreateLambda([this](TSharedPtr<IPropertyHandle> InHandle) {
			return MaterialEditorInstance->Parent && MaterialEditorInstance->Parent->GetMaterial() ?
				MaterialEditorInstance->PostProcessOverrides.BlendableLocationOverride != MaterialEditorInstance->Parent->GetMaterial()->BlendableLocation : false;
			});
		FResetToDefaultHandler ResetBlendableLocationPropertyHandler = FResetToDefaultHandler::CreateLambda([this](TSharedPtr<IPropertyHandle> InHandle) {
			if (MaterialEditorInstance->Parent && MaterialEditorInstance->Parent->GetMaterial())
			{
				MaterialEditorInstance->PostProcessOverrides.BlendableLocationOverride = MaterialEditorInstance->Parent->GetMaterial()->BlendableLocation;
			}
			});
		FResetToDefaultOverride ResetBlendableLocationPropertyOverride = FResetToDefaultOverride::Create(IsBlendableLocationPropertyResetVisible, ResetBlendableLocationPropertyHandler);

		IDetailPropertyRow& BlendableLocationPropertyRow = PostProcessCategory.AddProperty(BlendableLocationProperty);
		BlendableLocationPropertyRow
			.DisplayName(BlendableLocationProperty->GetPropertyDisplayName())
			.ToolTip(BlendableLocationProperty->GetToolTipText())
			.EditCondition(IsOverrideLocationEnabled, FOnBooleanValueChanged::CreateLambda([this](bool NewValue) {
				MaterialEditorInstance->PostProcessOverrides.bOverrideBlendableLocation = NewValue;
				MaterialEditorInstance->PostEditChange();
				FEditorSupportDelegates::RedrawAllViewports.Broadcast();
			}))
			.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FMaterialInstanceParameterDetails::IsOverriddenAndVisible, IsOverrideLocationEnabled)))
			.OverrideResetToDefault(ResetBlendableLocationPropertyOverride);

		FIsResetToDefaultVisible IsBlendablePriorityPropertyResetVisible = FIsResetToDefaultVisible::CreateLambda([this](TSharedPtr<IPropertyHandle> InHandle) {
			return MaterialEditorInstance->Parent && MaterialEditorInstance->Parent->GetMaterial() ?
				MaterialEditorInstance->PostProcessOverrides.BlendablePriorityOverride != MaterialEditorInstance->Parent->GetMaterial()->BlendablePriority : false;
			});
		FResetToDefaultHandler ResetBlendablePriorityPropertyHandler = FResetToDefaultHandler::CreateLambda([this](TSharedPtr<IPropertyHandle> InHandle) {
			if (MaterialEditorInstance->Parent && MaterialEditorInstance->Parent->GetMaterial())
			{
				MaterialEditorInstance->PostProcessOverrides.BlendablePriorityOverride = MaterialEditorInstance->Parent->GetMaterial()->BlendablePriority;
			}
			});
		FResetToDefaultOverride ResetBlendablePriorityPropertyOverride = FResetToDefaultOverride::Create(IsBlendablePriorityPropertyResetVisible, ResetBlendablePriorityPropertyHandler);

		IDetailPropertyRow& BlendablePriorityPropertyRow = PostProcessCategory.AddProperty(BlendablePriorityProperty);
		BlendablePriorityPropertyRow
			.DisplayName(BlendablePriorityProperty->GetPropertyDisplayName())
			.ToolTip(BlendablePriorityProperty->GetToolTipText())
			.EditCondition(IsOverridePriorityEnabled, FOnBooleanValueChanged::CreateLambda([this](bool NewValue) {
				MaterialEditorInstance->PostProcessOverrides.bOverrideBlendablePriority = NewValue;
				MaterialEditorInstance->PostEditChange();
				FEditorSupportDelegates::RedrawAllViewports.Broadcast();
			}))
			.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FMaterialInstanceParameterDetails::IsOverriddenAndVisible, IsOverridePriorityEnabled)))
			.OverrideResetToDefault(ResetBlendablePriorityPropertyOverride);

		if (MaterialEditorInstance->PostProcessOverrides.UserSceneTextureInputs.Num())
		{
			static FName GroupName(TEXT("UserSceneTextures"));
			IDetailGroup& UserSceneTexturesGroup = PostProcessCategory.AddGroup(GroupName, LOCTEXT("UserSceneTextureInputsGroup", "User Scene Texture Inputs"), false, true);

			TSharedPtr<IPropertyHandle> UserSceneTexturesArrayProperty = PostProcessOverridesProperty->GetChildHandle("UserSceneTextureInputs");

			for (int32 UserSceneTextureIndex = 0; UserSceneTextureIndex < MaterialEditorInstance->PostProcessOverrides.UserSceneTextureInputs.Num(); ++UserSceneTextureIndex)
			{
				TSharedPtr<IPropertyHandle> UserSceneTextureItemProperty = UserSceneTexturesArrayProperty->GetChildHandle(UserSceneTextureIndex);
				TSharedPtr<IPropertyHandle> UserSceneTextureValueProperty = UserSceneTextureItemProperty->GetChildHandle("Value");

				IDetailPropertyRow& PropertyRow = UserSceneTexturesGroup.AddPropertyRow(UserSceneTextureValueProperty.ToSharedRef());

				PropertyRow.CustomWidget()
					.NameContent()
					[
						SNew(STextBlock)
						.Text(FText::FromName(MaterialEditorInstance->PostProcessOverrides.UserSceneTextureInputs[UserSceneTextureIndex].Key))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
					.ValueContent()
					[
						UserSceneTextureValueProperty->CreatePropertyValueWidget()
					];
			}
		}

		PostProcessCategory.AddProperty(UserSceneTextureOutputProperty);
	}
}

UEnum* GetBlendModeEnum();

void FMaterialInstanceParameterDetails::CreateBasePropertyOverrideWidgets(IDetailLayoutBuilder& DetailLayout, IDetailGroup& MaterialPropertyOverrideGroup)
{
	IDetailGroup& BasePropertyOverrideGroup = MaterialPropertyOverrideGroup;
	TSharedRef<IPropertyHandle> BasePropertyOverrideProperty = DetailLayout.GetProperty("BasePropertyOverrides");
	FMaterialInstanceBasePropertyOverrides& BaseOverrides = MaterialEditorInstance->BasePropertyOverrides;
	TObjectPtr<UMaterialInterface> ParentMat = MaterialEditorInstance->Parent;
	const FText ParameterDisabledToolTipString = FText::FromString(TEXT("This material instance parent restricts the creation of new shader permutations. Overriding this parameter would result in the generation of additional shader permutations."));
	const bool bStaticParametersOverrideDisabled = MaterialEditorInstance->SourceInstance->bDisallowStaticParameterPermutations;

	auto CreateBaseOverrideRow = [&] (
		const char* PropertyName,
		auto&& OverrideBoolEnabledMemberFn,
		auto&& OverrideBoolChangedMemberFn,
		auto&& IsResetPropertyVisibleLambda,
		auto&& ResetPropertyHandlerLambda,
		auto&& OverrideAndVisibleMemberFn
	)
	{
		TSharedPtr<IPropertyHandle> ValueProperty = BasePropertyOverrideProperty->GetChildHandle(PropertyName);
		check(ValueProperty.IsValid());
		TAttribute<bool> OverrideBoolAttr = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, OverrideBoolEnabledMemberFn));
		FIsResetToDefaultVisible IsResetPropertyVisible = FIsResetToDefaultVisible::CreateLambda(IsResetPropertyVisibleLambda);
		FResetToDefaultHandler ResetPropertyHandler = FResetToDefaultHandler::CreateLambda(ResetPropertyHandlerLambda);
		FResetToDefaultOverride ResetPropertyOverride = FResetToDefaultOverride::Create(IsResetPropertyVisible, ResetPropertyHandler);
		IDetailPropertyRow& PropertyRow = BasePropertyOverrideGroup.AddPropertyRow(ValueProperty.ToSharedRef())
			.DisplayName(ValueProperty->GetPropertyDisplayName())
			.ToolTip(bStaticParametersOverrideDisabled ? ParameterDisabledToolTipString : ValueProperty->GetToolTipText())
			.EditCondition(OverrideBoolAttr, FOnBooleanValueChanged::CreateSP(this, OverrideBoolChangedMemberFn))
			.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, OverrideAndVisibleMemberFn, OverrideBoolAttr)))
			.OverrideResetToDefault(ResetPropertyOverride);
	};

#define CREATE_BASE_OVERRIDE_ROW_CUSTOM(PropertyName, PropertyVariableName, IsResetPropertyVisibleLambda, ResetPropertyHandlerLambda, IsOverriddenAndVisibleFn) \
	CreateBaseOverrideRow ( \
		#PropertyVariableName, \
		&FMaterialInstanceParameterDetails::Override ## PropertyName ## Enabled, \
		&FMaterialInstanceParameterDetails::OnOverride ## PropertyName ## Changed, \
		IsResetPropertyVisibleLambda, \
		ResetPropertyHandlerLambda, \
		IsOverriddenAndVisibleFn \
		)

#define CREATE_BASE_OVERRIDE_ROW_OVERRIDEFN(PropertyName, PropertyVariableName, ValueGetterName, IsOverriddenAndVisibleFn) \
	CREATE_BASE_OVERRIDE_ROW_CUSTOM( \
		PropertyName, \
		PropertyVariableName, \
		[this] (TSharedPtr<IPropertyHandle> InHandle) { \
			if (TObjectPtr<UMaterialInterface> ParentMat = MaterialEditorInstance->Parent) \
			{ \
				return MaterialEditorInstance->BasePropertyOverrides.PropertyVariableName != ParentMat->ValueGetterName(); \
			} \
			else \
			{ \
				return false; \
			} \
		}, \
		[this] (TSharedPtr<IPropertyHandle> InHandle) { \
			if (TObjectPtr<UMaterialInterface> ParentMat = MaterialEditorInstance->Parent) \
			{ \
				MaterialEditorInstance->BasePropertyOverrides.PropertyVariableName = ParentMat->ValueGetterName(); \
			} \
		}, \
		IsOverriddenAndVisibleFn \
	)

#define CREATE_BASE_OVERRIDE_ROW(PropertyName, PropertyVariableName, ValueGetterName) \
		CREATE_BASE_OVERRIDE_ROW_OVERRIDEFN(PropertyName, PropertyVariableName, ValueGetterName, &FMaterialInstanceParameterDetails::IsOverriddenAndVisible)

#define CREATE_BASE_OVERRIDE_ROW_BASIC(PropertyName) \
	CREATE_BASE_OVERRIDE_ROW(PropertyName, PropertyName, Get ## PropertyName)
#define CREATE_BASE_OVERRIDE_ROW_BOOL(PropertyName, GetterName) \
	CREATE_BASE_OVERRIDE_ROW(PropertyName, b ## PropertyName, GetterName)
#define CREATE_BASE_OVERRIDE_ROW_BASIC_BOOL(PropertyName) \
	CREATE_BASE_OVERRIDE_ROW(PropertyName, b ## PropertyName, PropertyName)

	CREATE_BASE_OVERRIDE_ROW_BASIC(OpacityMaskClipValue);
	CREATE_BASE_OVERRIDE_ROW_BASIC(BlendMode);
	CREATE_BASE_OVERRIDE_ROW_CUSTOM(ShadingModel, ShadingModel,
		[this](TSharedPtr<IPropertyHandle> InHandle) {
			if (TObjectPtr<UMaterialInterface> ParentMat = MaterialEditorInstance->Parent)
			{
				if (ParentMat->IsShadingModelFromMaterialExpression())
				{
					return MaterialEditorInstance->BasePropertyOverrides.ShadingModel != MSM_FromMaterialExpression;
				}
				else
				{
					return MaterialEditorInstance->BasePropertyOverrides.ShadingModel != ParentMat->GetShadingModels().GetFirstShadingModel();
				}
			}
			else
			{
				return false;
			}
		},
		[this](TSharedPtr<IPropertyHandle> InHandle) {
			if (TObjectPtr<UMaterialInterface> ParentMat = MaterialEditorInstance->Parent)
			{
				if (ParentMat->IsShadingModelFromMaterialExpression())
				{
					MaterialEditorInstance->BasePropertyOverrides.ShadingModel = MSM_FromMaterialExpression;
				}
				else
				{
					MaterialEditorInstance->BasePropertyOverrides.ShadingModel = ParentMat->GetShadingModels().GetFirstShadingModel();
				}
			}
		},
		&FMaterialInstanceParameterDetails::IsOverriddenAndVisibleShadingModels
	);
	CREATE_BASE_OVERRIDE_ROW(TwoSided, TwoSided, IsTwoSided);
	CREATE_BASE_OVERRIDE_ROW_OVERRIDEFN(IsThinSurface, bIsThinSurface, IsThinSurface, &FMaterialInstanceParameterDetails::IsOverriddenAndVisibleSubstrateOnly);
	CREATE_BASE_OVERRIDE_ROW(DitheredLODTransition, DitheredLODTransition, IsDitheredLODTransition);
	CREATE_BASE_OVERRIDE_ROW_BOOL(OutputTranslucentVelocity, IsTranslucencyWritingVelocity);
	CREATE_BASE_OVERRIDE_ROW_BASIC_BOOL(HasPixelAnimation);
	CREATE_BASE_OVERRIDE_ROW_BOOL(EnableTessellation, IsTessellationEnabled);
	CREATE_BASE_OVERRIDE_ROW_BASIC(DisplacementScaling);
	CREATE_BASE_OVERRIDE_ROW_BOOL(EnableDisplacementFade, IsDisplacementFadeEnabled);
	CREATE_BASE_OVERRIDE_ROW_BASIC(DisplacementFadeRange);
	CREATE_BASE_OVERRIDE_ROW_BASIC(MaxWorldPositionOffsetDisplacement);
	CREATE_BASE_OVERRIDE_ROW_BOOL(CastDynamicShadowAsMasked, GetCastDynamicShadowAsMasked);
	CREATE_BASE_OVERRIDE_ROW_BOOL(CompatibleWithLumenCardSharing, IsCompatibleWithLumenCardSharing);

#undef CREATE_BASE_OVERRIDE_ROW_BASIC_BOOL
#undef CREATE_BASE_OVERRIDE_ROW_BOOL
#undef CREATE_BASE_OVERRIDE_ROW_BASIC
#undef CREATE_BASE_OVERRIDE_ROW
#undef CREATE_BASE_OVERRIDE_ROW_CUSTOM
}

EVisibility FMaterialInstanceParameterDetails::IsOverriddenAndVisible(TAttribute<bool> IsOverridden) const
{
	bool bShouldBeVisible = true;
	if (MaterialEditorInstance->bShowOnlyOverrides)
	{
		bShouldBeVisible = IsOverridden.Get();
	}
	return bShouldBeVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FMaterialInstanceParameterDetails::IsOverriddenAndVisibleShadingModels(TAttribute<bool> IsOverridden) const
{
	bool bShouldBeVisible = true;
	if (MaterialEditorInstance->bShowOnlyOverrides)
	{
		bShouldBeVisible = IsOverridden.Get();
	}
	// If Substrate is enabled, only allows ShadingModel to be visible if the parent allows it
	if (Substrate::IsSubstrateEnabled())
	{
		const UMaterial* ParentMaterial = MaterialEditorInstance->Parent ? MaterialEditorInstance->Parent->GetMaterial() : nullptr;
		bShouldBeVisible = ParentMaterial ? ParentMaterial->SupportsShadingModelOverride() : false;
	}
	return bShouldBeVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FMaterialInstanceParameterDetails::IsOverriddenAndVisibleSubstrateOnly(TAttribute<bool> IsOverridden) const
{
	return Substrate::IsSubstrateEnabled() ? EVisibility::Visible : EVisibility::Collapsed;
}

/** Helper function used by some parameters to verify that they are allowed to be overridden. This
  * must be prevented if the source material instance disallows the creation of new static parameter
  * permutations as that would trigger a new shader creation. */
static bool DoesSourceMaterialInstanceDisallowStaticParameterPermutation(const UMaterialEditorInstanceConstant* mi, bool NewValue)
{
	if (NewValue && mi->SourceInstance->bDisallowStaticParameterPermutations)
	{
		return true;
	}
	return false;
}

#define IMPLEMENT_OVERRIDE_MEMBER_FUNCS_COMMON(PropertyName, PropertyVariableName, bRequiresPermutation) \
	bool FMaterialInstanceParameterDetails::Override ## PropertyName ## Enabled() const \
	{ \
		return MaterialEditorInstance->BasePropertyOverrides.bOverride_ ## PropertyVariableName ; \
	} \
	void FMaterialInstanceParameterDetails::OnOverride ## PropertyName ## Changed(bool NewValue) \
	{ \
		if (DoesSourceMaterialInstanceDisallowStaticParameterPermutation(MaterialEditorInstance, NewValue) && \
			bRequiresPermutation) \
		{ \
			return; \
		} \
		MaterialEditorInstance->BasePropertyOverrides.bOverride_ ## PropertyVariableName = NewValue; \
		MaterialEditorInstance->PostEditChange(); \
		FEditorSupportDelegates::RedrawAllViewports.Broadcast(); \
	}
#define IMPLEMENT_OVERRIDE_MEMBER_FUNCS(PropertyName, bRequiresPermutation) \
	IMPLEMENT_OVERRIDE_MEMBER_FUNCS_COMMON(PropertyName, PropertyName, bRequiresPermutation)
#define IMPLEMENT_OVERRIDE_MEMBER_FUNCS_BOOL(PropertyName, bRequiresPermutation) \
	IMPLEMENT_OVERRIDE_MEMBER_FUNCS_COMMON(PropertyName, b ## PropertyName, bRequiresPermutation)

IMPLEMENT_OVERRIDE_MEMBER_FUNCS(OpacityMaskClipValue, true)
IMPLEMENT_OVERRIDE_MEMBER_FUNCS(BlendMode, true)
IMPLEMENT_OVERRIDE_MEMBER_FUNCS(ShadingModel, true)
IMPLEMENT_OVERRIDE_MEMBER_FUNCS(TwoSided, true)
IMPLEMENT_OVERRIDE_MEMBER_FUNCS_BOOL(IsThinSurface, true)
IMPLEMENT_OVERRIDE_MEMBER_FUNCS(DitheredLODTransition, true)
IMPLEMENT_OVERRIDE_MEMBER_FUNCS(OutputTranslucentVelocity, true)
IMPLEMENT_OVERRIDE_MEMBER_FUNCS_BOOL(HasPixelAnimation, true)
IMPLEMENT_OVERRIDE_MEMBER_FUNCS_BOOL(EnableTessellation, true)
IMPLEMENT_OVERRIDE_MEMBER_FUNCS(DisplacementScaling, false)
IMPLEMENT_OVERRIDE_MEMBER_FUNCS_BOOL(EnableDisplacementFade, false)
IMPLEMENT_OVERRIDE_MEMBER_FUNCS(DisplacementFadeRange, false)
IMPLEMENT_OVERRIDE_MEMBER_FUNCS(MaxWorldPositionOffsetDisplacement, false)
IMPLEMENT_OVERRIDE_MEMBER_FUNCS(CastDynamicShadowAsMasked, true)
IMPLEMENT_OVERRIDE_MEMBER_FUNCS(CompatibleWithLumenCardSharing, false)

#undef IMPLEMENT_OVERRIDE_MEMBER_FUNCS_BOOL
#undef IMPLEMENT_OVERRIDE_MEMBER_FUNCS
#undef IMPLEMENT_OVERRIDE_MEMBER_FUNCS_COMMON

#undef LOCTEXT_NAMESPACE

