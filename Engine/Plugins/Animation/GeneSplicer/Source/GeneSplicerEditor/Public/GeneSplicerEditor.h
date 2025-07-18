// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenePoolAssetActions.h"
#include "Modules/ModuleManager.h"

class FGeneSplicerEditorModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
private:
	TSharedPtr<FGenePoolAssetTypeActions> GenePoolAssetTypeActions;
};

