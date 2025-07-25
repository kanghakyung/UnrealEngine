// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GeometryCacheTrack.h"
#include "GeometryCacheMeshData.h"

#include "GeometryCacheTrackTransformAnimation.generated.h"

#define UE_API GEOMETRYCACHE_API

/** Derived GeometryCacheTrack class, used for Transform animation. */
UCLASS(MinimalAPI, collapsecategories, hidecategories = Object, BlueprintType, config = Engine, deprecated)
class UDEPRECATED_GeometryCacheTrack_TransformAnimation : public UGeometryCacheTrack
{
	GENERATED_UCLASS_BODY()
		
	//~ Begin UObject Interface.
	UE_API virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
	//~ End UObject Interface.
	
	//~ Begin UGeometryCacheTrack Interface.
	UE_API virtual const bool UpdateMeshData(const float Time, const bool bLooping, int32& InOutMeshSampleIndex, FGeometryCacheMeshData*& OutMeshData) override;
	//~ End UGeometryCacheTrack Interface.

	/**
	* Sets/updates the MeshData for this track
	*
	* @param NewMeshData - GeometryCacheMeshData instance later used as the rendered mesh	
	*/
	UFUNCTION()
	UE_API void SetMesh(const FGeometryCacheMeshData& NewMeshData);	
private:
	FGeometryCacheMeshData MeshData;
};

#undef UE_API
