// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ClothCollisionPrim.h"
#include "Containers/Array.h"
#include "UObject/ObjectMacros.h"
#include "ClothCollisionData.generated.h"

USTRUCT()
struct FClothCollisionData
{
	GENERATED_BODY()

	CLOTHINGSYSTEMRUNTIMEINTERFACE_API void Reset();

	CLOTHINGSYSTEMRUNTIMEINTERFACE_API void Append(const FClothCollisionData& InOther);

	CLOTHINGSYSTEMRUNTIMEINTERFACE_API void AppendTransformed(const FClothCollisionData& InOther, const TArray<FTransform>& BoneTransforms);

	bool IsEmpty() const
	{
		return Spheres.IsEmpty() && SphereConnections.IsEmpty() && Convexes.IsEmpty() && Boxes.IsEmpty();
	}

	// Sphere data
	UPROPERTY(EditAnywhere, Category = Collison)
	TArray<FClothCollisionPrim_Sphere> Spheres;

	// Capsule data
	UPROPERTY(EditAnywhere, Category = Collison)
	TArray<FClothCollisionPrim_SphereConnection> SphereConnections;

	// Convex Data
	UPROPERTY(EditAnywhere, Category = Collison)
	TArray<FClothCollisionPrim_Convex> Convexes;

	// Box data
	UPROPERTY(EditAnywhere, Category = Collison)
	TArray<FClothCollisionPrim_Box> Boxes;
};
