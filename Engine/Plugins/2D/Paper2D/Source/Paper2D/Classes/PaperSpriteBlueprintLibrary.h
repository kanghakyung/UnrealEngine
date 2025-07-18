// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "PaperSpriteBlueprintLibrary.generated.h"

struct FSlateBrush;

class UPaperSprite;

/**
 *
 */
UCLASS(meta=(ScriptName="PaperSpriteLibrary"))
class UPaperSpriteBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="Widget|Brush")
	static FSlateBrush MakeBrushFromSprite(UPaperSprite* Sprite, int32 Width, int32 Height);
};
