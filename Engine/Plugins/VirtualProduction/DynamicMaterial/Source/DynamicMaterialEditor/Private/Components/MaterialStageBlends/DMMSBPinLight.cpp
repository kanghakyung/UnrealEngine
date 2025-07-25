// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialStageBlends/DMMSBPinLight.h"

#define LOCTEXT_NAMESPACE "DMMaterialStageBlendPinLight"

UDMMaterialStageBlendPinLight::UDMMaterialStageBlendPinLight()
	: UDMMaterialStageBlendFunction(
		LOCTEXT("BlendPinLight", "PinLight"),
		LOCTEXT("BlendPinLightDescription", "Pin Light uses both Darken and Lighten simultaneously."),
		"DM_Blend_PinLight",
		TEXT("/Script/Engine.MaterialFunction'/DynamicMaterial/MaterialFunctions/Blends/MF_DM_Blend_PinLight.MF_DM_Blend_PinLight'")
	)
{
}

#undef LOCTEXT_NAMESPACE
