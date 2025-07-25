// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialStageBlends/DMMSBLighten.h"

#define LOCTEXT_NAMESPACE "DMMaterialStageBlendLighten"

UDMMaterialStageBlendLighten::UDMMaterialStageBlendLighten()
	: UDMMaterialStageBlendFunction(
		LOCTEXT("BlendLighten", "Lighten"),
		LOCTEXT("BlendLightenDescription", "Lighten chooses the color with the highest luminance between the base and blend layer."),
		"DM_Blend_Lighten",
		TEXT("/Script/Engine.MaterialFunction'/DynamicMaterial/MaterialFunctions/Blends/MF_DM_Blend_Lighten.MF_DM_Blend_Lighten'")
	)
{
}

#undef LOCTEXT_NAMESPACE
