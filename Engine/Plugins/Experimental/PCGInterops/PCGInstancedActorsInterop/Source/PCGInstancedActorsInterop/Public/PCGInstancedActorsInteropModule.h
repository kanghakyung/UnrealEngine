// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Stats/Stats.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPCGInstancedActorsInterop, Log, All);

class FPCGInstancedActorsInteropModule final : public IModuleInterface
{
public:
	// ~IModuleInterface implementation
	virtual bool SupportsDynamicReloading() override { return true; };
	// ~End IModuleInterface implementation
};
