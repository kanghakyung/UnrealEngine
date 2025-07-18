// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "USDShadeMaterialTranslator.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
#include "pxr/pxr.h"
#include "USDIncludesEnd.h"

class USDSCHEMAS_API FMaterialXUsdShadeMaterialTranslator : public FUsdShadeMaterialTranslator
{
	using Super = FUsdShadeMaterialTranslator;

public:
	using FUsdShadeMaterialTranslator::FUsdShadeMaterialTranslator;

	virtual void CreateAssets() override;
};

#endif	  // #if USE_USD_SDK
