// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
/**
* Per Project Settings for the Mirroring, in particular what axis to use to mirror and the matching strings.
*/
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "ControlRigPoseMirrorSettings.generated.h"

#define UE_API CONTROLRIG_API


UCLASS(MinimalAPI, config = EditorPerProjectUserSettings)
class UControlRigPoseMirrorSettings : public UObject
{

public:
	GENERATED_BODY()

	UE_API UControlRigPoseMirrorSettings();

	/** The String to denote Controls on the Right Side */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Mirror Settings", meta = (ToolTip = "String To Denote Right Side"))
	FString RightSide;

	/** The String to denote Controls on the Left Side */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Mirror Settings", meta = (ToolTip = "String To Denote Left Side"))
	FString LeftSide;

	// the axis to mirror translations against
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Mirror Settings", meta = (ToolTip = "Axis to Mirror Translations"))
	TEnumAsByte<EAxis::Type> MirrorAxis;

	// the axis to flip for rotations
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Mirror Settings", meta = (ToolTip = "Axis to Flip for Rotations"))
	TEnumAsByte<EAxis::Type> AxisToFlip;

};


#undef UE_API
