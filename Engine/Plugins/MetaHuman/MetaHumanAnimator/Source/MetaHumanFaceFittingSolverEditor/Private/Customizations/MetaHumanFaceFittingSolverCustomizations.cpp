// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanFaceFittingSolverCustomizations.h"
#include "MetaHumanFaceFittingSolver.h"
#include "MetaHumanConfig.h"
#include "SMetaHumanConfigCombo.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DetailLayoutBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "Dialogs/Dialogs.h"
#include "AssetThumbnail.h"

#define LOCTEXT_NAMESPACE "MetaHumanAnimator"

TSharedRef<IDetailCustomization> FMetaHumanFaceFittingSolverCustomization::MakeInstance()
{
	return MakeShared<FMetaHumanFaceFittingSolverCustomization>();
}

void FMetaHumanFaceFittingSolverCustomization::CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder)
{
	TSharedRef<IPropertyHandle> DeviceConfigProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanFaceFittingSolver, DeviceConfig));
	IDetailPropertyRow* DeviceConfigRow = InDetailBuilder.EditDefaultProperty(DeviceConfigProperty);
	check(DeviceConfigRow);

	TSharedPtr<FAssetThumbnailPool> ThumbnailPool = MakeShared<FAssetThumbnailPool>(16);

	TSharedPtr<SWidget> NameWidget, ValueWidget;
	DeviceConfigRow->GetDefaultWidgets(NameWidget, ValueWidget);

	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	InDetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);

	if (!ObjectsBeingCustomized.IsEmpty() && ObjectsBeingCustomized[0].IsValid())
	{
		if (UMetaHumanFaceFittingSolver* MetaHumanFaceFittingSolver = Cast<UMetaHumanFaceFittingSolver>(ObjectsBeingCustomized[0]))
		{
			DeviceConfigRow->CustomWidget()
			.NameContent()
			[
				NameWidget.ToSharedRef()
			]
			.ValueContent()
			.MinDesiredWidth(250.0f)
			.MaxDesiredWidth(0.0f)
			[
				// The use of the SMetaHumanConfigCombo custom asset picker is hopefully a temporary measure.
				// Its currently needed since SObjectPropertyEntryBox will not list the MHA plugin content assets in UEFN.
				// The MHA plugin content assets should really be exposed in UEFN, but this will involve enabling the
				// MetaHuman plugin for FortniteGame which is not a step we have time to investigate right now.
				// SMetaHumanConfigCombo works around this problem but is not as user-friendly as a SObjectPropertyEntryBox.
				SNew(SMetaHumanConfigCombo, EMetaHumanConfigType::Fitting, MetaHumanFaceFittingSolver, DeviceConfigProperty)
			];
		}
	}

	TSharedRef<IPropertyHandle> PredictiveSolverProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanFaceFittingSolver, PredictiveSolver));
	IDetailPropertyRow* PredictiveSolverRow = InDetailBuilder.EditDefaultProperty(PredictiveSolverProperty);
	check(PredictiveSolverRow);

	PredictiveSolverRow->GetDefaultWidgets(NameWidget, ValueWidget);

	PredictiveSolverRow->CustomWidget()
		.NameContent()
		[
			NameWidget.ToSharedRef()
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		.MaxDesiredWidth(0.0f)
		[
			SNew(SObjectPropertyEntryBox)
			.PropertyHandle(PredictiveSolverProperty)
			.DisplayThumbnail(true)
			.ThumbnailPool(ThumbnailPool)
			.AllowCreate(false)
			.AllowClear(false)
			.AllowedClass(UMetaHumanConfig::StaticClass())
			.OnShouldFilterAsset_Lambda([](const FAssetData& InAssetData)
			{
				const FAssetDataTagMapSharedView::FFindTagResult Tag = InAssetData.TagsAndValues.FindTag(GET_MEMBER_NAME_CHECKED(UMetaHumanConfig, Type));

				if (Tag.IsSet())
				{
					return Tag.GetValue() != StaticEnum<EMetaHumanConfigType>()->GetNameStringByValue(static_cast<int64>(EMetaHumanConfigType::PredictiveSolver));
				}

				return true;
			})
		];
}

#undef LOCTEXT_NAMESPACE
