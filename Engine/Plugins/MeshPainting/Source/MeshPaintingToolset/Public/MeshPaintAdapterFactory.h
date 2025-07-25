// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

class UMeshComponent;
class IMeshPaintComponentAdapterFactory;
class FReferenceCollector;

class MESHPAINTINGTOOLSET_API FMeshPaintComponentAdapterFactory
{
public:
	static TArray<TSharedPtr<IMeshPaintComponentAdapterFactory>> FactoryList;

public:
	static TSharedPtr<class IMeshPaintComponentAdapter> CreateAdapterForMesh(UMeshComponent* InComponent, int32 InPaintingMeshLODIndex);
	static void InitializeAdapterGlobals();
	static void AddReferencedObjectsGlobals(FReferenceCollector& Collector);
	static void CleanupGlobals();
};
