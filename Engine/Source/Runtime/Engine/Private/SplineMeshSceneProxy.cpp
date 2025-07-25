// Copyright Epic Games, Inc. All Rights Reserved.

#include "SplineMeshSceneProxy.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "MaterialDomain.h"
#include "MeshMaterialShader.h"
#include "Misc/DelayedAutoRegister.h"
#include "StaticMeshComponentLODInfo.h"
#include "StaticMeshResources.h"
#include "RayTracingInstance.h"
#include "SceneInterface.h"
#include "SplineMeshSceneProxyDesc.h"

IMPLEMENT_TYPE_LAYOUT(FSplineMeshVertexFactoryShaderParameters);

static TAutoConsoleVariable<int32> CVarRayTracingSplineMeshes(
	TEXT("r.RayTracing.Geometry.SplineMeshes"),
	1,
	TEXT("Include splines meshes in ray tracing effects (default = 1 (spline meshes enabled in ray tracing))"));

static TAutoConsoleVariable<int32> CVarRayTracingSplineMeshesForceUpdate(
	TEXT("r.RayTracing.Geometry.SplineMeshes.ForceUpdate"),
	0,
	TEXT("Forces ray tracing geometry for spline meshes to be updated every frame"));

bool FSplineMeshVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return (Parameters.MaterialParameters.bIsUsedWithSplineMeshes || Parameters.MaterialParameters.bIsSpecialEngineMaterial)
		&& FLocalVertexFactory::ShouldCompilePermutation(Parameters);
}

/** Modify compile environment to enable spline deformation */
void FSplineMeshVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_SPEEDTREE_WIND"), TEXT("0"));
	FLocalVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);

	OutEnvironment.SetDefine(TEXT("USE_SPLINEDEFORM"), TEXT("1"));
	OutEnvironment.SetDefine(TEXT("USE_SPLINE_MESH_SCENE_RESOURCES"), UseSplineMeshSceneResources(Parameters.Platform));
}

/**
 * FSplineMeshVertexFactory does not support manual vertex fetch yet so worst case element set is returned to make sure the PSO can be compiled
 */
void FSplineMeshVertexFactory::GetPSOPrecacheVertexFetchElements(EVertexInputStreamType VertexInputStreamType, FVertexDeclarationElementList& Elements)
{
	FLocalVertexFactory::GetPSOPrecacheVertexFetchElements(VertexInputStreamType, Elements);
}

FSplineMeshSceneProxy::FSplineMeshSceneProxy(USplineMeshComponent* InComponent) :
	FSplineMeshSceneProxy(FStaticMeshSceneProxyDesc(InComponent), FSplineMeshSceneProxyDesc(InComponent))
{}

FSplineMeshSceneProxy::FSplineMeshSceneProxy(const FStaticMeshSceneProxyDesc& InMeshDesc, const FSplineMeshSceneProxyDesc& InSplineDesc) :
	FStaticMeshSceneProxy(InMeshDesc, false)
{
	bSupportsDistanceFieldRepresentation = false;

	// Mark ourselves as a spline mesh
	bSplineMesh = true;

	// Dynamic draw path without Nanite isn't supported by Lumen
	bVisibleInLumenScene = false;

#if RHI_RAYTRACING
	bHasRayTracingRepresentation = true;
	bDynamicRayTracingGeometry = true;
#endif

	// make sure all the materials are okay to be rendered as a spline mesh
	for (FStaticMeshSceneProxy::FLODInfo& LODInfo : LODs)
	{
		for (FStaticMeshSceneProxy::FLODInfo::FSectionInfo& Section : LODInfo.Sections)
		{
			if (!Section.Material->CheckMaterialUsage_Concurrent(MATUSAGE_SplineMesh))
			{
				Section.Material = UMaterial::GetDefaultMaterial(MD_Surface);
			}
		}
	}

	// Copy spline params from component
	SplineParams = InSplineDesc.CalculateShaderParams();

	// If we're using GPU Scene, we place the spline mesh parameters in the instance data buffer, with the
	// exception of mobile platforms that are unable to pull this data from the structured buffer in the VS
	const ERHIFeatureLevel::Type FeatureLevel = GetScene().GetFeatureLevel();
	if (FeatureLevel > ERHIFeatureLevel::ES3_1 && UseGPUScene(GetScene().GetShaderPlatform(), FeatureLevel))
	{
		SplineMeshInstanceData.Setup(SplineParams);
		SetupInstanceSceneDataBuffers(&SplineMeshInstanceData);
	}

	for (int32 LODIndex = 0; LODIndex < LODs.Num(); LODIndex++)
	{
		FSplineMeshSceneProxyDesc::InitVertexFactory(InMeshDesc.GetStaticMesh(), FeatureLevel, LODIndex, nullptr); // we always need this one for shadows etc
		if (InMeshDesc.LODData.IsValidIndex(LODIndex) && InMeshDesc.LODData[LODIndex].OverrideVertexColors)
		{
			FSplineMeshSceneProxyDesc::InitVertexFactory(InMeshDesc.GetStaticMesh(), FeatureLevel, LODIndex, InMeshDesc.LODData[LODIndex].OverrideVertexColors);
		}
	}

#if RHI_RAYTRACING
	if (bSupportRayTracing)
	{
		const int32 NumRayTracingLods = InMeshDesc.GetStaticMesh() && !InMeshDesc.GetStaticMesh()->GetRenderData()->RayTracingProxy->bUsingRenderingLODs ? InMeshDesc.GetStaticMesh()->GetRenderData()->RayTracingProxy->LODs.Num() : 0;
		for (int32 LODIndex = 0; LODIndex < NumRayTracingLods; LODIndex++)
		{
			FSplineMeshSceneProxyDesc::InitRayTracingProxyVertexFactory(InMeshDesc.GetStaticMesh(), FeatureLevel, LODIndex, nullptr);
		}
	}
#endif
}

SIZE_T FSplineMeshSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FSplineMeshSceneProxy::SetupMeshBatchForSpline(int32 InLODIndex, FMeshBatch& OutMeshBatch) const
{
	const FStaticMeshVertexFactories& VFs = RenderData->LODVertexFactories[InLODIndex];
	check(OutMeshBatch.Elements.Num() == 1);
	OutMeshBatch.VertexFactory = OutMeshBatch.Elements[0].bUserDataIsColorVertexBuffer ? VFs.SplineVertexFactoryOverrideColorVertexBuffer : VFs.SplineVertexFactory;
	check(OutMeshBatch.VertexFactory);
	OutMeshBatch.Elements[0].SplineMeshSceneProxy = this;
	OutMeshBatch.Elements[0].bIsSplineProxy = true;
	OutMeshBatch.Elements[0].PrimitiveUniformBuffer = GetUniformBuffer();
	OutMeshBatch.ReverseCulling ^= (SplineParams.StartScale.X < 0) ^ (SplineParams.StartScale.Y < 0);
}

bool FSplineMeshSceneProxy::GetShadowMeshElement(int32 LODIndex, int32 BatchIndex, uint8 InDepthPriorityGroup, FMeshBatch& OutMeshBatch, bool bDitheredLODTransition) const
{
	//checkf(LODIndex == 0, TEXT("Getting spline static mesh element with invalid LOD [%d]"), LODIndex);

	if (FStaticMeshSceneProxy::GetShadowMeshElement(LODIndex, BatchIndex, InDepthPriorityGroup, OutMeshBatch, bDitheredLODTransition))
	{
		SetupMeshBatchForSpline(LODIndex, OutMeshBatch);
		return true;
	}
	return false;
}

bool FSplineMeshSceneProxy::GetMeshElement(int32 LODIndex, int32 BatchIndex, int32 SectionIndex, uint8 InDepthPriorityGroup, bool bUseSelectionOutline, bool bAllowPreCulledIndices, FMeshBatch& OutMeshBatch) const
{
	//checkf(LODIndex == 0 /*&& SectionIndex == 0*/, TEXT("Getting spline static mesh element with invalid params [%d, %d]"), LODIndex, SectionIndex);

	if (FStaticMeshSceneProxy::GetMeshElement(LODIndex, BatchIndex, SectionIndex, InDepthPriorityGroup, bUseSelectionOutline, bAllowPreCulledIndices, OutMeshBatch))
	{
		SetupMeshBatchForSpline(LODIndex, OutMeshBatch);
		return true;
	}
	return false;
}

bool FSplineMeshSceneProxy::GetWireframeMeshElement(int32 LODIndex, int32 BatchIndex, const FMaterialRenderProxy* WireframeRenderProxy, uint8 InDepthPriorityGroup, bool bAllowPreCulledIndices, FMeshBatch& OutMeshBatch) const
{
	//checkf(LODIndex == 0, TEXT("Getting spline static mesh element with invalid LOD [%d]"), LODIndex);

	if (FStaticMeshSceneProxy::GetWireframeMeshElement(LODIndex, BatchIndex, WireframeRenderProxy, InDepthPriorityGroup, bAllowPreCulledIndices, OutMeshBatch))
	{
		SetupMeshBatchForSpline(LODIndex, OutMeshBatch);
		return true;
	}
	return false;
}


bool FSplineMeshSceneProxy::GetCollisionMeshElement(int32 LODIndex, int32 BatchIndex, int32 SectionIndex, uint8 InDepthPriorityGroup, const FMaterialRenderProxy* RenderProxy, FMeshBatch& OutMeshBatch) const
{
	//checkf(LODIndex == 0, TEXT("Getting spline static mesh element with invalid LOD [%d]"), LODIndex);

	if (FStaticMeshSceneProxy::GetCollisionMeshElement(LODIndex, BatchIndex, SectionIndex, InDepthPriorityGroup, RenderProxy, OutMeshBatch))
	{
		SetupMeshBatchForSpline(LODIndex, OutMeshBatch);
		return true;
	}
	return false;
}

#if RHI_RAYTRACING
void FSplineMeshSceneProxy::GetDynamicRayTracingInstances(FRayTracingInstanceCollector& Collector)
{
	if (CVarRayTracingSplineMeshes.GetValueOnRenderThread() == 0  || !bSupportRayTracing)
	{
		// TODO: Exclude proxy during ray tracing primitive gather instead of doing this early out here.
		return;
	}

	checkf(!DynamicRayTracingGeometries.IsEmpty(), TEXT("DynamicRayTracingGeometries has not been initialized correctly"));

	ESceneDepthPriorityGroup PrimitiveDPG = GetStaticDepthPriorityGroup();
	const int32 LODIndex = FMath::Max(GetLOD(Collector.GetReferenceView()), (int32)GetCurrentFirstLODIdx_RenderThread());
	const FStaticMeshLODResources& LODModel = RenderData->LODResources[LODIndex];

	FRayTracingGeometry& Geometry = DynamicRayTracingGeometries[LODIndex];

	FRayTracingInstance RayTracingInstance;

	const int32 NumBatches = GetNumMeshBatches();
	const int32 NumRayTracingMaterialEntries = LODModel.Sections.Num() * NumBatches;

	if (NumRayTracingMaterialEntries != CachedRayTracingMaterials.Num() || CachedRayTracingMaterialsLODIndex != LODIndex)
	{
		CachedRayTracingMaterials.Reset();
		CachedRayTracingMaterials.Reserve(NumRayTracingMaterialEntries);

		for (int32 BatchIndex = 0; BatchIndex < NumBatches; BatchIndex++)
		{
			for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
			{
				FMeshBatch& MeshBatch = CachedRayTracingMaterials.AddDefaulted_GetRef();

				bool bResult = GetMeshElement(LODIndex, BatchIndex, SectionIndex, PrimitiveDPG, false, false, MeshBatch);
				if (!bResult)
				{
					// Hidden material
					MeshBatch.MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
					MeshBatch.VertexFactory = &RenderData->LODVertexFactories[LODIndex].VertexFactory;
				}
				MeshBatch.ReverseCulling = bReverseCulling;
				MeshBatch.SegmentIndex = SectionIndex;
				MeshBatch.MeshIdInPrimitive = SectionIndex;
			}
		}

		CachedRayTracingMaterialsLODIndex = LODIndex;
	}
	else
	{
		RayTracingInstance.bInstanceMaskAndFlagsDirty = false;
	}

	RayTracingInstance.Geometry = &Geometry;
	// scene proxies live for the duration of Render(), making array views below safe
	const FMatrix& ThisLocalToWorld = GetLocalToWorld();
	RayTracingInstance.InstanceTransformsView = MakeArrayView(&ThisLocalToWorld, 1);
	RayTracingInstance.MaterialsView = MakeArrayView(CachedRayTracingMaterials);

	const bool bAlwaysUpdate = bAnyMaterialHasWorldPositionOffset || (CVarRayTracingSplineMeshesForceUpdate.GetValueOnRenderThread() != 0);
	const bool bNeedsUpdate = bAlwaysUpdate
		|| bUpdateRayTracingGeometry
		|| (!bAlwaysUpdate && Geometry.DynamicGeometrySharedBufferGenerationID != FRayTracingGeometry::NonSharedVertexBuffers) // was using shared VB but won't use it anymore so update once
		|| !Geometry.IsValid()
		|| Geometry.IsEvicted()
		|| Geometry.GetRequiresBuild();

	FRWBuffer* VertexBuffer = &RayTracingDynamicVertexBuffer;

	if (bAlwaysUpdate)
	{
		// if updating every frame release memory and use shared VB
		VertexBuffer->Release();
		VertexBuffer = nullptr;
	}

	if (bNeedsUpdate && RenderData->LODVertexFactories[LODIndex].VertexFactory.GetType()->SupportsRayTracingDynamicGeometry())
	{
		Collector.AddRayTracingGeometryUpdate(
			FRayTracingDynamicGeometryUpdateParams
			{
				CachedRayTracingMaterials, // TODO: this copy can be avoided if FRayTracingDynamicGeometryUpdateParams supported array views
				false,
				(uint32)LODModel.GetNumVertices(),
				uint32((SIZE_T)LODModel.GetNumVertices() * sizeof(FVector3f)),
				Geometry.Initializer.TotalPrimitiveCount,
				&Geometry,
				VertexBuffer,
				true
			}
		);

		bUpdateRayTracingGeometry = false;
	}

	check(CachedRayTracingMaterials.Num() == RayTracingInstance.GetMaterials().Num());
	checkf(RayTracingInstance.Geometry->Initializer.Segments.Num() == CachedRayTracingMaterials.Num(), TEXT("Segments/Materials mismatch. Number of segments: %d. Number of Materials: %d. LOD Index: %d"),
		RayTracingInstance.Geometry->Initializer.Segments.Num(),
		CachedRayTracingMaterials.Num(),
		LODIndex);

	Collector.AddRayTracingInstance(MoveTemp(RayTracingInstance));
}
#endif // RHI_RAYTRACING


void FSplineMeshSceneProxy::OnTransformChanged(FRHICommandListBase& RHICmdList)
{
	// Call parent implementation
	FStaticMeshSceneProxy::OnTransformChanged(RHICmdList);

	// NOTE: The proxy's local bounds have already been padded for WPO/Displacement
	SplineMeshInstanceData.UpdateDefaultInstance(GetLocalToWorld(), GetLocalBounds());
}

void FSplineMeshSceneInstanceDataBuffers::Setup(const FSplineMeshShaderParams& InSplineMeshShaderParams)
{
	InstancePayloadExtension.SetNumUninitialized(SPLINE_MESH_PARAMS_FLOAT4_SIZE);
	Flags.bHasPerInstancePayloadExtension = true;
	Update(InSplineMeshShaderParams);
}

bool FSplineMeshSceneInstanceDataBuffers::Update(const FSplineMeshShaderParams& InSplineMeshShaderParams)
{
	if (!InstancePayloadExtension.IsEmpty())
	{
		PackSplineMeshParams(InSplineMeshShaderParams, InstancePayloadExtension);
		return true;
	}
	return false;
}

FNaniteSplineMeshSceneProxy::FNaniteSplineMeshSceneProxy(const Nanite::FMaterialAudit& NaniteMaterials, USplineMeshComponent* InComponent) :
	FNaniteSplineMeshSceneProxy(NaniteMaterials, FStaticMeshSceneProxyDesc(InComponent), FSplineMeshSceneProxyDesc(InComponent))
{}

FNaniteSplineMeshSceneProxy::FNaniteSplineMeshSceneProxy(const Nanite::FMaterialAudit& NaniteMaterials, const FStaticMeshSceneProxyDesc& InMeshDesc, const FSplineMeshSceneProxyDesc& InSplineDesc) :
	Nanite::FSceneProxy(NaniteMaterials, InMeshDesc)
{
	bSupportsDistanceFieldRepresentation = false;
	
	// Mark ourselves as a spline mesh
	bSplineMesh = true;

	// Make sure all the materials are okay to be rendered as a spline mesh or reset them
 	bool bAnyReset = false;
	for (auto& Section : GetMaterialSections())
	{
		if (Section.ShadingMaterialProxy)
		{
			UMaterialInterface* ShadingMaterial = Section.ShadingMaterialProxy->GetMaterialInterface();
			if (!ShadingMaterial->CheckMaterialUsage_Concurrent(MATUSAGE_SplineMesh))
			{
				Section.ResetToDefaultMaterial();
				bAnyReset = true;
			}
		}
	}

	if (bAnyReset)
	{
		// Update any data that is dependent upon shading materials
		OnMaterialsUpdated();
	}

	// Copy spline params from component
	SplineParams = InSplineDesc.CalculateShaderParams();
	SplineMeshInstanceData.Setup(SplineParams);
	SetupInstanceSceneDataBuffers(&SplineMeshInstanceData);

#if RHI_RAYTRACING
	if (bSupportRayTracing)
	{
		bHasRayTracingRepresentation = true;
		bDynamicRayTracingGeometry = true;

		// We only need to init vertex factories for Nanite spline meshes if they can be ray traced
		const ERHIFeatureLevel::Type FeatureLevel = GetScene().GetFeatureLevel();
		for (int32 LODIndex = 0; LODIndex < RenderData->LODResources.Num(); LODIndex++)
		{
			FSplineMeshSceneProxyDesc::InitVertexFactory(InMeshDesc.GetStaticMesh(), FeatureLevel, LODIndex, nullptr); // we always need this one for shadows etc
			if (InMeshDesc.LODData.IsValidIndex(LODIndex) && InMeshDesc.LODData[LODIndex].OverrideVertexColors)
			{
				FSplineMeshSceneProxyDesc::InitVertexFactory(InMeshDesc.GetStaticMesh(), FeatureLevel, LODIndex, InMeshDesc.LODData[LODIndex].OverrideVertexColors);
			}
		}

		const int32 NumRayTracingLods = InMeshDesc.GetStaticMesh() && !InMeshDesc.GetStaticMesh()->GetRenderData()->RayTracingProxy->bUsingRenderingLODs ? InMeshDesc.GetStaticMesh()->GetRenderData()->RayTracingProxy->LODs.Num() : 0;
		for (int32 LODIndex = 0; LODIndex < NumRayTracingLods; LODIndex++)
		{
			FSplineMeshSceneProxyDesc::InitRayTracingProxyVertexFactory(InMeshDesc.GetStaticMesh(), FeatureLevel, LODIndex, nullptr);
		}
	}
#endif
}

SIZE_T FNaniteSplineMeshSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FNaniteSplineMeshSceneProxy::OnTransformChanged(FRHICommandListBase& RHICmdList)
{
	// Call Nanite parent implementation
	Nanite::FSceneProxy::OnTransformChanged(RHICmdList);

	// NOTE: The proxy's local bounds have already been padded for WPO/Displacement
	SplineMeshInstanceData.UpdateDefaultInstance(GetLocalToWorld(), GetLocalBounds());
}

#if RHI_RAYTRACING

void FNaniteSplineMeshSceneProxy::GetDynamicRayTracingInstances(FRayTracingInstanceCollector& Collector)
{
	if (CVarRayTracingSplineMeshes.GetValueOnRenderThread() == 0)
	{
		return;
	}

	const bool bAlwaysUpdate = CombinedMaterialRelevance.bUsesWorldPositionOffset || (CVarRayTracingSplineMeshesForceUpdate.GetValueOnRenderThread() != 0);

	FRWBuffer* DynamicVertexBuffer = &RayTracingDynamicVertexBuffer;
	if (bAlwaysUpdate)
	{
		// if updating every frame release memory and use shared VB
		DynamicVertexBuffer->Release();
		DynamicVertexBuffer = nullptr;
	}	

	bUpdateRayTracingGeometry |= bAlwaysUpdate;

	Nanite::FSceneProxy::GetDynamicRayTracingInstances_Internal(Collector, DynamicVertexBuffer, bUpdateRayTracingGeometry);

	bUpdateRayTracingGeometry = false;
}

void FNaniteSplineMeshSceneProxy::SetupFallbackRayTracingMaterials(int32 LODIndex, TArray<FMeshBatch>& OutMaterials) const
{
	Nanite::FSceneProxy::SetupFallbackRayTracingMaterials(LODIndex, OutMaterials);

	// set up the vertex factories
	const FStaticMeshVertexFactories& VFs = (*RenderData->RayTracingProxy->LODVertexFactories)[LODIndex];
	for (auto& MeshBatch : OutMaterials)
	{
		MeshBatch.VertexFactory = MeshBatch.Elements[0].bUserDataIsColorVertexBuffer ? VFs.SplineVertexFactoryOverrideColorVertexBuffer : VFs.SplineVertexFactory;
		check(MeshBatch.VertexFactory);
		MeshBatch.ReverseCulling ^= (SplineParams.StartScale.X < 0) ^ (SplineParams.StartScale.Y < 0);
	}
}

#endif // RHI_RAYTRACING


void UpdateSplineMeshParams_RenderThread(FPrimitiveSceneProxy* SceneProxy, const FSplineMeshShaderParams& Params)
{
	check(SceneProxy->IsSplineMesh());

	if (SceneProxy->IsNaniteMesh())
	{
		static_cast<FNaniteSplineMeshSceneProxy*>(SceneProxy)->UpdateSplineMeshParams_RenderThread(Params);
	}
	else
	{
		static_cast<FSplineMeshSceneProxy*>(SceneProxy)->UpdateSplineMeshParams_RenderThread(Params);
	}
}