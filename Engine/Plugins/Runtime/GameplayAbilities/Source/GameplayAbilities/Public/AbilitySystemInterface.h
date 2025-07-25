// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Interface.h"
#include "AbilitySystemInterface.generated.h"

#define UE_API GAMEPLAYABILITIES_API

class UAbilitySystemComponent;

/** Interface for actors that expose access to an ability system component */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UAbilitySystemInterface : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

class IAbilitySystemInterface
{
	GENERATED_IINTERFACE_BODY()

	/** Returns the ability system component to use for this actor. It may live on another actor, such as a Pawn using the PlayerState's component */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const = 0;
};

#undef UE_API
