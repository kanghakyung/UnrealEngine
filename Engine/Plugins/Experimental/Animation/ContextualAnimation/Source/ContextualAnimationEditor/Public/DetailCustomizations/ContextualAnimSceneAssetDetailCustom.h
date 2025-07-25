// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class IDetailsView;
class UContextualAnimSceneAsset;
class FContextualAnimViewModel;

class FContextualAnimSceneAssetDetailCustom : public IDetailCustomization
{
public:

	static TSharedRef<IDetailCustomization> MakeInstance(TSharedRef<FContextualAnimViewModel> ViewModelRef);

	FContextualAnimSceneAssetDetailCustom(TSharedRef<FContextualAnimViewModel> ViewModelRef);

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

protected:

	TWeakPtr<FContextualAnimViewModel> ViewModelPtr;
};
