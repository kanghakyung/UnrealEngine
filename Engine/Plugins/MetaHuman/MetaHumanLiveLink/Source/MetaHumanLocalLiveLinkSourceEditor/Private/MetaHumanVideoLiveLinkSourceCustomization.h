// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

#include "MetaHumanMediaSourceCustomization.h"



class FMetaHumanVideoLiveLinkSourceCustomization : public IDetailCustomization, public FMetaHumanMediaSourceCustomization
{
public:

	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder) override;
};
