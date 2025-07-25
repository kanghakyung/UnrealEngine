// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CustomizableObjectNodeDetails.h"
#include "IDetailCustomization.h"

namespace ESelectInfo { enum Type : int; }

class FString;
class IDetailLayoutBuilder;
class IPropertyHandle;

class FCustomizableObjectNodeMeshMorphDetails : public FCustomizableObjectNodeDetails
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** ILayoutDetails interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;

private:

	/** Disables/Enables the tags property widget. */
	bool IsMorphNameSelectorWidgetEnabled() const;

	/** Set the widget tooltip. */
	FText MorphNameSelectorWidgetTooltip() const;

private:

	class UCustomizableObjectNodeMeshMorph* Node = nullptr;
	TArray< TSharedPtr<FString> > MorphTargetComboOptions;

	void OnMorphTargetComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo, TSharedRef<IPropertyHandle> Property);
};
