// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "NetObjectConnectionFilter.generated.h"

// Stub used when we are not compiling with iris to workaround UHT dependencies
static_assert(UE_WITH_IRIS == 0, "IrisStub module should not be used when iris is enabled");

UCLASS(Transient, MinimalAPI)
class UNetObjectConnectionFilterConfig : public UObject
{
	GENERATED_BODY()
};

UCLASS(Transient, MinimalAPI)
class UNetObjectConnectionFilter : public UObject
{
	GENERATED_BODY()
};
