// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FSvgDistanceFieldModule : public IModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override { }
	virtual void ShutdownModule() override { }

};

IMPLEMENT_MODULE(FSvgDistanceFieldModule, SVGDistanceField);
