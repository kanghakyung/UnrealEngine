// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassSettings.h"
#include "MassGameplaySettings.generated.h"


UCLASS(MinimalAPI, config = Mass, defaultconfig, DisplayName = "Mass Gameplay")
class UMassGameplaySettings : public UMassModuleSettings
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, config, Category = Debug, meta = (ConsoleVariable = "mass.debug.VLogSpawnLocations"))
	bool bLogSpawnLocations = true;
};
