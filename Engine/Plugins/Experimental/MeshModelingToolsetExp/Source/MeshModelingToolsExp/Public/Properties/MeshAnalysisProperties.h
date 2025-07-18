// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveToolBuilder.h"
#include "GeometryBase.h"
#include "MeshAnalysisProperties.generated.h"

#define UE_API MESHMODELINGTOOLSEXP_API


// predeclarations
PREDECLARE_USE_GEOMETRY_CLASS(FDynamicMesh3);



UCLASS(MinimalAPI)
class UMeshAnalysisProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	/** In meters squared */
	UPROPERTY(VisibleAnywhere, Category = MeshAnalysis, meta = (NoResetToDefault))
	FString SurfaceArea;

	/** In cubic meters */
	UPROPERTY(VisibleAnywhere, Category = MeshAnalysis, meta = (NoResetToDefault))
	FString Volume;

	UE_API void Update(const FDynamicMesh3& Mesh, const FTransform& Transform);
};

#undef UE_API
