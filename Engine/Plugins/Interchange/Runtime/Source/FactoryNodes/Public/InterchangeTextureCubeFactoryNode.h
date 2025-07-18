// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangeTextureFactoryNode.h"

#if WITH_ENGINE
#include "Engine/TextureCube.h"
#endif


#include "InterchangeTextureCubeFactoryNode.generated.h"

UCLASS(MinimalAPI, BlueprintType)
class UInterchangeTextureCubeFactoryNode : public UInterchangeTextureFactoryNode
{
	GENERATED_BODY()

private:
	virtual UClass* GetObjectClass() const override
	{
		return UTextureCube::StaticClass();
	}
};
