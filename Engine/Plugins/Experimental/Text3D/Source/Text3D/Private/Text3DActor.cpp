// Copyright Epic Games, Inc. All Rights Reserved.

#include "Text3DActor.h"
#include "Text3DComponent.h"

AText3DActor::AText3DActor()
{
	Text3DComponent = CreateDefaultSubobject<UText3DComponent>(TEXT("Text3DComponent"));
	RootComponent = Text3DComponent;
}

#if WITH_EDITOR
FString AText3DActor::GetDefaultActorLabel() const
{
	return TEXT("Text3DActor");
}
#endif
