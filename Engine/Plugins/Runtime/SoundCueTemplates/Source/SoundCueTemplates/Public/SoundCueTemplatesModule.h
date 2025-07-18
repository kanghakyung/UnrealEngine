// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"
#include "Modules/ModuleInterface.h"

SOUNDCUETEMPLATES_API DECLARE_LOG_CATEGORY_EXTERN(SoundCueTemplates, Log, All);

class FSoundCueTemplatesModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
