// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshVertexSculptTool.h"
#include "Engine/World.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "Intersection/ContainmentQueries3.h"
#include "Intersection/IntrCylinderBox3.h"
#include "ToolDataVisualizer.h"
#include "Async/ParallelFor.h"
#include "Async/Async.h"
#include "Selections/MeshConnectedComponents.h"

#include "MeshWeights.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/MeshIndexUtil.h"
#include "Parameterization/MeshPlanarSymmetry.h"
#include "Util/BufferUtil.h"
#include "AssetUtils/Texture2DUtil.h"
#include "ToolSetupUtil.h"

#include "BaseGizmos/BrushStampIndicator.h"
#include "PreviewMesh.h"
#include "BaseBehaviors/TwoAxisPropertyEditBehavior.h"
#include "Generators/RectangleMeshGenerator.h"

#include "Properties/MeshSculptLayerProperties.h"

#include "Changes/MeshVertexChange.h"
#include "Changes/MeshRegionChange.h"

#include "MeshSculptLayersManagerAPI.h"
#include "Sculpting/KelvinletBrushOp.h"
#include "Sculpting/MeshSmoothingBrushOps.h"
#include "Sculpting/MeshInflateBrushOps.h"
#include "Sculpting/MeshMoveBrushOps.h"
#include "Sculpting/MeshPlaneBrushOps.h"
#include "Sculpting/MeshPinchBrushOps.h"
#include "Sculpting/MeshSculptBrushOps.h"
#include "Sculpting/MeshEraseSculptLayerBrushOps.h"
#include "Sculpting/StampFalloffs.h"
#include "Sculpting/MeshSculptUtil.h"
#include "TargetInterfaces/DynamicMeshCommitter.h"
#include "TargetInterfaces/DynamicMeshProvider.h"

#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "TargetInterfaces/MaterialProvider.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MeshVertexSculptTool)

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UMeshVertexSculptTool"

namespace
{
	// probably should be something defined for the whole tool framework...
#if WITH_EDITOR
	static EAsyncExecution VertexSculptToolAsyncExecTarget = EAsyncExecution::LargeThreadPool;
#else
	static EAsyncExecution VertexSculptToolAsyncExecTarget = EAsyncExecution::ThreadPool;
#endif
}


/*
 * ToolBuilder
 */
UMeshSurfacePointTool* UMeshVertexSculptToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	UMeshVertexSculptTool* SculptTool = NewObject<UMeshVertexSculptTool>(SceneState.ToolManager);
	SculptTool->SetWorld(SceneState.World);
	SculptTool->SetDefaultPrimaryBrushID(DefaultPrimaryBrushID);
	return SculptTool;
}

FToolTargetTypeRequirements UMeshVertexSculptToolBuilder::VSculptTypeRequirements({
	UMaterialProvider::StaticClass(),
	UDynamicMeshProvider::StaticClass(),
	UDynamicMeshCommitter::StaticClass(),
	USceneComponentBackedTarget::StaticClass()
});

const FToolTargetTypeRequirements& UMeshVertexSculptToolBuilder::GetTargetRequirements() const
{
	return VSculptTypeRequirements;
}

/*
 * internal Change classes
 */

class FVertexSculptNonSymmetricChange : public FToolCommandChange
{
public:
	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
};


/*
 * Tool
 */

void UMeshVertexSculptTool::Setup()
{
	UMeshSculptToolBase::Setup();

	SetToolDisplayName(LOCTEXT("ToolName", "Sculpt"));

	// create dynamic mesh component to use for live preview
	check(TargetWorld);
	FActorSpawnParameters SpawnInfo;
	PreviewMeshActor = TargetWorld->SpawnActor<AInternalToolFrameworkActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnInfo);
	DynamicMeshComponent = NewObject<UDynamicMeshComponent>(PreviewMeshActor);

	InitializeSculptMeshComponent(DynamicMeshComponent, PreviewMeshActor);

	// assign materials
	FComponentMaterialSet MaterialSet;
	Cast<IMaterialProvider>(Target)->GetMaterialSet(MaterialSet);
	for (int k = 0; k < MaterialSet.Materials.Num(); ++k)
	{
		DynamicMeshComponent->SetMaterial(k, MaterialSet.Materials[k]);
	}

	DynamicMeshComponent->SetInvalidateProxyOnChangeEnabled(false);
	OnDynamicMeshComponentChangedHandle = DynamicMeshComponent->OnMeshRegionChanged.AddUObject(this, &UMeshVertexSculptTool::OnDynamicMeshComponentChanged);

	FDynamicMesh3* SculptMesh = GetSculptMesh();
	FAxisAlignedBox3d Bounds = SculptMesh->GetBounds(true);
	InitialBoundsMaxDim = Bounds.MaxDim();

	// initialize dynamic octree
	TFuture<void> InitializeOctree = Async(VertexSculptToolAsyncExecTarget, [SculptMesh, Bounds, this]()
	{
		if (SculptMesh->TriangleCount() > 100000)
		{
			Octree.RootDimension = InitialBoundsMaxDim / 10.0;
			Octree.SetMaxTreeDepth(4);
		}
		else
		{
			Octree.RootDimension = InitialBoundsMaxDim / 2.0;
			Octree.SetMaxTreeDepth(8);
		}
		Octree.Initialize(SculptMesh);
		//Octree.CheckValidity(EValidityCheckFailMode::Check, true, true);
		//FDynamicMeshOctree3::FStatistics Stats;
		//Octree.ComputeStatistics(Stats);
		//UE_LOG(LogTemp, Warning, TEXT("Octree Stats: %s"), *Stats.ToString());
	});

	// find mesh connected-component index for each triangle
	TFuture<void> InitializeComponents = Async(VertexSculptToolAsyncExecTarget, [SculptMesh, this]()
	{
		TriangleComponentIDs.SetNum(SculptMesh->MaxTriangleID());
		FMeshConnectedComponents Components(SculptMesh);
		Components.FindConnectedTriangles();
		int32 ComponentIdx = 1;
		for (const FMeshConnectedComponents::FComponent& Component : Components)
		{
			for (int32 TriIdx : Component.Indices)
			{
				TriangleComponentIDs[TriIdx] = ComponentIdx;
			}
			ComponentIdx++;
		}
	});

	TFuture<void> InitializeSymmetry = Async(VertexSculptToolAsyncExecTarget, [SculptMesh, this]()
	{
		TryToInitializeSymmetry();
	});

	// currently only supporting default polygroup set
	TFuture<void> InitializeGroups = Async(VertexSculptToolAsyncExecTarget, [SculptMesh, this]()
	{
		ActiveGroupSet = MakeUnique<UE::Geometry::FPolygroupSet>(SculptMesh);
	});

	// initialize target mesh
	TFuture<void> InitializeBaseMesh = Async(VertexSculptToolAsyncExecTarget, [this]()
	{
		UpdateBaseMesh(nullptr);
		bTargetDirty = false;
	});

	// initialize render decomposition
	TFuture<void> InitializeRenderDecomp = Async(VertexSculptToolAsyncExecTarget, [SculptMesh, &MaterialSet, this]()
	{
		TUniquePtr<FMeshRenderDecomposition> Decomp = MakeUnique<FMeshRenderDecomposition>();
		FMeshRenderDecomposition::BuildChunkedDecomposition(SculptMesh, &MaterialSet, *Decomp);
		Decomp->BuildAssociations(SculptMesh);
		//UE_LOG(LogTemp, Warning, TEXT("Decomposition has %d groups"), Decomp->Num());
		DynamicMeshComponent->SetExternalDecomposition(MoveTemp(Decomp));
	});

	// Wait for above precomputations to finish before continuing
	InitializeOctree.Wait();
	InitializeComponents.Wait();
	InitializeGroups.Wait();
	InitializeBaseMesh.Wait();
	InitializeRenderDecomp.Wait();
	InitializeSymmetry.Wait();

	// initialize brush radius range interval, brush properties
	UMeshSculptToolBase::InitializeBrushSizeRange(Bounds);

	// initialize other properties
	SculptProperties = NewObject<UVertexBrushSculptProperties>(this);
	SculptProperties->Tool = this;
	
	// init state flags flags
	ActiveVertexChange = nullptr;

	InitializeIndicator();

	// initialize our properties
	AddToolPropertySource(UMeshSculptToolBase::BrushProperties);
	UMeshSculptToolBase::BrushProperties->bShowPerBrushProps = false;
	UMeshSculptToolBase::BrushProperties->bShowFalloff = false;
	UMeshSculptToolBase::BrushProperties->BrushSize.bToolSupportsPressureSensitivity = true;
	SculptProperties->RestoreProperties(this, GetPropertyCacheIdentifier());
	
	AddToolPropertySource(SculptProperties);
	CalculateBrushRadius();

	AlphaProperties = NewObject<UVertexBrushAlphaProperties>(this);
	AlphaProperties->RestoreProperties(this, GetPropertyCacheIdentifier());
	AlphaProperties->Tool = this;
	AddToolPropertySource(AlphaProperties);

	SymmetryProperties = NewObject<UMeshSymmetryProperties>(this);
	SymmetryProperties->RestoreProperties(this, GetPropertyCacheIdentifier());
	SymmetryProperties->bSymmetryCanBeEnabled = false;
	AddToolPropertySource(SymmetryProperties);

	if (ISceneComponentBackedTarget* SceneComponentTarget = Cast<ISceneComponentBackedTarget>(Target))
	{
		if (IMeshSculptLayersManager* SculptLayersManager = Cast<IMeshSculptLayersManager>(SceneComponentTarget->GetOwnerSceneComponent()))
		{
			if (SculptLayersManager->HasSculptLayers())
			{
				SculptLayerProperties = NewObject<UMeshSculptLayerProperties>(this);
				SculptLayerProperties->Init(this, SculptLayersManager->NumLockedBaseSculptLayers());
				AddToolPropertySource(SculptLayerProperties);
			}
		}
	}



	this->BaseMeshQueryFunc = [&](int32 VertexID, const FVector3d& Position, double MaxDist, FVector3d& PosOut, FVector3d& NormalOut)
	{
		return GetBaseMeshNearest(VertexID, Position, MaxDist, PosOut, NormalOut);
	};

	RegisterBrushes();

	if (DefaultPrimaryBrushID >= 0 && ensure(SculptProperties->PrimaryBrushID < RegisteredPrimaryBrushTypes.Num()))
	{
		SculptProperties->PrimaryBrushID = DefaultPrimaryBrushID;
	}

	// falloffs
	RegisterStandardFalloffTypes();

	AddToolPropertySource(UMeshSculptToolBase::GizmoProperties);
	SetToolPropertySourceEnabled(UMeshSculptToolBase::GizmoProperties, false);
	// Move the gizmo toward the center of the mesh, without changing the plane it represents
	UMeshSculptToolBase::GizmoProperties->RecenterGizmoIfFar(GetSculptMeshComponent()->GetComponentTransform().TransformPosition(Bounds.Center()), Bounds.MaxDim());

	AddToolPropertySource(UMeshSculptToolBase::ViewProperties);

	// register watchers
	SculptProperties->WatchProperty( SculptProperties->PrimaryBrushID,
		[this](int32 NewType) { UpdateBrushType(NewType); });

	SculptProperties->WatchProperty( SculptProperties->PrimaryFalloffType,
		[this](EMeshSculptFalloffType NewType) { 
			SetPrimaryFalloffType(NewType);
			// Request to have the details panel rebuilt to ensure the new falloff property value is propagated to the details customization
			OnDetailsPanelRequestRebuild.Broadcast();
		});

	SculptProperties->WatchProperty(AlphaProperties->Alpha,
		[this](UTexture2D* NewAlpha) {
			UpdateBrushAlpha(AlphaProperties->Alpha);
			// Request to have the details panel rebuilt to ensure the new alpha property value is propagated to the details customization
			OnDetailsPanelRequestRebuild.Broadcast();
		});

	// must call before updating brush type so that we register all brush properties?
	UMeshSculptToolBase::OnCompleteSetup();

	UpdateBrushType(SculptProperties->PrimaryBrushID);
	SetPrimaryFalloffType(SculptProperties->PrimaryFalloffType);
	UpdateBrushAlpha(AlphaProperties->Alpha);
	SetActiveSecondaryBrushType(0);

	StampRandomStream = FRandomStream(31337);

	// update symmetry state based on validity, and then update internal apply-symmetry state
	SymmetryProperties->bSymmetryCanBeEnabled = bMeshSymmetryIsValid;
	bApplySymmetry = bMeshSymmetryIsValid && SymmetryProperties->bEnableSymmetry;

	SymmetryProperties->WatchProperty(SymmetryProperties->bEnableSymmetry,
		[this](bool bNewValue) { bApplySymmetry = bMeshSymmetryIsValid && bNewValue; });
	SymmetryProperties->WatchProperty(SymmetryProperties->bSymmetryCanBeEnabled,
		[this](bool bNewValue) 
		{
			bApplySymmetry = bMeshSymmetryIsValid && bNewValue && SymmetryProperties->bEnableSymmetry; 
		});
}

void UMeshVertexSculptTool::RegisterBrushes()
{
	RegisterBrushType((int32)EMeshVertexSculptBrushType::Smooth, LOCTEXT("SmoothBrush", "Smooth"),
		MakeUnique<TBasicMeshSculptBrushOpFactory<FSmoothBrushOp>>(),
		NewObject<USmoothBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::SmoothFill, LOCTEXT("SmoothFill", "SmoothFill"),
		MakeUnique<TBasicMeshSculptBrushOpFactory<FSmoothFillBrushOp>>(),
		NewObject<USmoothFillBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::Move, LOCTEXT("Move", "Move"),
		MakeUnique<TBasicMeshSculptBrushOpFactory<FMoveBrushOp>>(),
		NewObject<UMoveBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::Offset, LOCTEXT("Offset", "SculptN"),
		MakeUnique<FLambdaMeshSculptBrushOpFactory>([this]() { return MakeUnique<FSurfaceSculptBrushOp>(BaseMeshQueryFunc); }),
		NewObject<UStandardSculptBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::SculptView, LOCTEXT("SculptView", "SculptV"),
		MakeUnique<FLambdaMeshSculptBrushOpFactory>([this]() { return MakeUnique<FViewAlignedSculptBrushOp>(BaseMeshQueryFunc); }),
		NewObject<UViewAlignedSculptBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::SculptMax, LOCTEXT("SculptMax", "SculptMx"),
		MakeUnique<FLambdaMeshSculptBrushOpFactory>([this]() { return MakeUnique<FSurfaceMaxSculptBrushOp>(BaseMeshQueryFunc); }),
		NewObject<USculptMaxBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::Inflate, LOCTEXT("Inflate", "Inflate"),
		MakeUnique<TBasicMeshSculptBrushOpFactory<FInflateBrushOp>>(),
		NewObject<UInflateBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::Pinch, LOCTEXT("Pinch", "Pinch"),
		MakeUnique<TBasicMeshSculptBrushOpFactory<FPinchBrushOp>>(),
		NewObject<UPinchBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::Flatten, LOCTEXT("Flatten", "Flatten"),
		MakeUnique<TBasicMeshSculptBrushOpFactory<FFlattenBrushOp>>(),
		NewObject<UFlattenBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::Plane, LOCTEXT("Plane", "PlaneN"),
		MakeUnique<TBasicMeshSculptBrushOpFactory<FPlaneBrushOp>>(),
		NewObject<UPlaneBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::PlaneViewAligned, LOCTEXT("PlaneViewAligned", "PlaneV"),
		MakeUnique<TBasicMeshSculptBrushOpFactory<FPlaneBrushOp>>(),
		NewObject<UViewAlignedPlaneBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::FixedPlane, LOCTEXT("FixedPlane", "PlaneW"),
		MakeUnique<TBasicMeshSculptBrushOpFactory<FPlaneBrushOp>>(),
		NewObject<UFixedPlaneBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::ScaleKelvin, LOCTEXT("ScaleKelvin", "Scale"),
		MakeUnique<TBasicMeshSculptBrushOpFactory<FScaleKelvinletBrushOp>>(),
		NewObject<UScaleKelvinletBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::PullKelvin, LOCTEXT("PullKelvin", "Grab"),
		MakeUnique<TBasicMeshSculptBrushOpFactory<FPullKelvinletBrushOp>>(),
		NewObject<UPullKelvinletBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::PullSharpKelvin, LOCTEXT("PullSharpKelvin", "GrabSharp"),
		MakeUnique<TBasicMeshSculptBrushOpFactory<FSharpPullKelvinletBrushOp>>(),
		NewObject<USharpPullKelvinletBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::TwistKelvin, LOCTEXT("TwistKelvin", "Twist"),
		MakeUnique<TBasicMeshSculptBrushOpFactory<FTwistKelvinletBrushOp>>(),
		NewObject<UTwistKelvinletBrushOpProps>(this));

	if (ISceneComponentBackedTarget* SceneComponentTarget = Cast<ISceneComponentBackedTarget>(Target))
	{
		if (IMeshSculptLayersManager* SculptLayersManager = Cast<IMeshSculptLayersManager>(SceneComponentTarget->GetOwnerSceneComponent()))
		{
			if (SculptLayersManager->HasSculptLayers())
			{
				RegisterBrushType((int32)EMeshVertexSculptBrushType::EraseSculptLayer, LOCTEXT("EraseSculptLayer", "EraseSculptLayer"),
					MakeUnique<TBasicMeshSculptBrushOpFactory<FEraseSculptLayerBrushOp>>(),
					NewObject<UEraseSculptLayerBrushOpProps>(this));
			}
		}
	}

	// secondary brushes
	// We activate ID 0 as our default secondary brush, so use that as the registration ID
	RegisterSecondaryBrushType(0, LOCTEXT("Smooth", "Smooth"),
		MakeUnique<TBasicMeshSculptBrushOpFactory<FSmoothBrushOp>>(),
		NewObject<USecondarySmoothBrushOpProps>(this));
}

FString UMeshVertexSculptTool::GetPropertyCacheIdentifier() const
{
	return TEXT("UMeshVertexSculptTool");
}

void UMeshVertexSculptTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (DynamicMeshComponent != nullptr)
	{
		DynamicMeshComponent->OnMeshChanged.Remove(OnDynamicMeshComponentChangedHandle);
	}

	SculptProperties->SaveProperties(this, GetPropertyCacheIdentifier());
	AlphaProperties->SaveProperties(this, GetPropertyCacheIdentifier());
	SymmetryProperties->SaveProperties(this, GetPropertyCacheIdentifier());

	if (PreviewMeshActor != nullptr)
	{
		PreviewMeshActor->Destroy();
		PreviewMeshActor = nullptr;
	}

	// this call will commit result, unregister and destroy DynamicMeshComponent
	UMeshSculptToolBase::Shutdown(ShutdownType);
}


void UMeshVertexSculptTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	CalculateBrushRadius();
}

void UMeshVertexSculptTool::UpdateToolMeshes(TFunctionRef<TUniquePtr<FMeshRegionChangeBase>(FDynamicMesh3&, int32 MeshIdx)> UpdateMesh)
{
	if (AllowToolMeshUpdates())
	{
		// have to wait for any outstanding stamp/undo update to finish...
		WaitForPendingStampUpdate();
		WaitForPendingUndoRedo();

		TUniquePtr<FMeshRegionChangeBase> Change = UpdateMesh(*GetSculptMesh(), 0);
		// A change was created -- emit it to the tool manager and update associated data structures, etc
		if (Change)
		{
			// pass through the change to trigger standard mesh updates / octree recomputation
			OnDynamicMeshComponentChanged(DynamicMeshComponent, Change.Get(), false);

			TUniquePtr<TWrappedToolCommandChange<FMeshRegionChangeBase>> NewChange = MakeUnique<TWrappedToolCommandChange<FMeshRegionChangeBase>>();
			NewChange->WrappedChange = MoveTemp(Change);
			NewChange->BeforeModify = [this](bool bRevert)
			{
				this->WaitForPendingUndoRedo();
			};
			// Note this change should be in the context of a larger transaction, so the text isn't that important
			GetToolManager()->EmitObjectChange(DynamicMeshComponent, MoveTemp(NewChange), LOCTEXT("UpdateVertexSculptMesh", "Updated Mesh"));
			
			if (bMeshSymmetryIsValid)
			{
				// Re-validate that the symmetry still holds after the external mesh change
				if (!Symmetry->ValidateSymmetry(*GetSculptMesh()))
				{
					GetToolManager()->EmitObjectChange(this, MakeUnique<FVertexSculptNonSymmetricChange>(), LOCTEXT("InvalidateSymmetryChange", "Invalidate Symmetry"));
					bMeshSymmetryIsValid = false;
					SymmetryProperties->bSymmetryCanBeEnabled = bMeshSymmetryIsValid;
				}
			}
		}
		// No change is ready to emit, just update component rendering
		else
		{
			DynamicMeshComponent->FastNotifyPositionsUpdated();
		}
	}
}

UPreviewMesh* UMeshVertexSculptTool::MakeBrushIndicatorMesh(UObject* Parent, UWorld* World)
{
	UPreviewMesh* PlaneMesh = NewObject<UPreviewMesh>(Parent);
	PlaneMesh->CreateInWorld(World, FTransform::Identity);

	FRectangleMeshGenerator RectGen;
	RectGen.Width = RectGen.Height = 2.0;
	RectGen.WidthVertexCount = RectGen.HeightVertexCount = 1;
	FDynamicMesh3 Mesh(&RectGen.Generate());
	FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->PrimaryUV();
	// configure UVs to be in same space as texture pixels when mapped into brush frame (??)
	for (int32 eid : UVOverlay->ElementIndicesItr())
	{
		FVector2f UV = UVOverlay->GetElement(eid);
		UV.X = 1.0 - UV.X;
		UV.Y = 1.0 - UV.Y;
		UVOverlay->SetElement(eid, UV);
	}
	PlaneMesh->UpdatePreview(&Mesh);

	BrushIndicatorMaterial = ToolSetupUtil::GetDefaultBrushAlphaMaterial(GetToolManager());
	if (BrushIndicatorMaterial)
	{
		PlaneMesh->SetMaterial(BrushIndicatorMaterial);
	}

	// make sure raytracing is disabled on the brush indicator
	Cast<UDynamicMeshComponent>(PlaneMesh->GetRootComponent())->SetEnableRaytracing(false);
	PlaneMesh->SetShadowsEnabled(false);

	return PlaneMesh;
}

void UMeshVertexSculptTool::InitializeIndicator()
{
	UMeshSculptToolBase::InitializeIndicator();
	// want to draw radius
	BrushIndicator->bDrawRadiusCircle = true;
}

void UMeshVertexSculptTool::SetActiveBrushType(int32 Identifier)
{
	if (SculptProperties->PrimaryBrushID != Identifier)
	{
		SculptProperties->PrimaryBrushID = Identifier;
		UpdateBrushType(SculptProperties->PrimaryBrushID);
		SculptProperties->SilentUpdateWatched();
	}

	// this forces full rebuild of properties panel (!!)
	//this->NotifyOfPropertyChangeByTool(SculptProperties);
}

void UMeshVertexSculptTool::SetActiveFalloffType(int32 Identifier)
{
	EMeshSculptFalloffType NewFalloffType = static_cast<EMeshSculptFalloffType>(Identifier);
	if (SculptProperties->PrimaryFalloffType != NewFalloffType)
	{
		SculptProperties->PrimaryFalloffType = NewFalloffType;
		SetPrimaryFalloffType(SculptProperties->PrimaryFalloffType);
		SculptProperties->SilentUpdateWatched();
	}

	// this forces full rebuild of properties panel (!!)
	//this->NotifyOfPropertyChangeByTool(SculptProperties);
}

void UMeshVertexSculptTool::SetRegionFilterType(int32 Identifier)
{
	SculptProperties->BrushFilter = static_cast<EMeshVertexSculptBrushFilterType>(Identifier);
}


void UMeshVertexSculptTool::OnBeginStroke(const FRay& WorldRay)
{
	WaitForPendingUndoRedo();		// cannot start stroke if there is an outstanding undo/redo update

	UpdateBrushPosition(WorldRay);

	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();
	FMeshSculptBrushOp::EReferencePlaneType ReferencePlaneType = UseBrushOp->GetReferencePlaneType();
	if (ReferencePlaneType == FMeshSculptBrushOp::EReferencePlaneType::InitialROI ||
		ReferencePlaneType == FMeshSculptBrushOp::EReferencePlaneType::InitialROI_ViewAligned)
	{
		UpdateROI(GetBrushFrameLocal());
		UpdateStrokeReferencePlaneForROI(GetBrushFrameLocal(), TriangleROIArray,
			ReferencePlaneType == FMeshSculptBrushOp::EReferencePlaneType::InitialROI_ViewAligned);
	}
	else if (ReferencePlaneType == FMeshSculptBrushOp::EReferencePlaneType::WorkPlane)
	{
		UpdateStrokeReferencePlaneFromWorkPlane();
	}

	// initialize first "Last Stamp", so that we can assume all stamps in stroke have a valid previous stamp
	LastStamp.WorldFrame = GetBrushFrameWorld();
	LastStamp.LocalFrame = GetBrushFrameLocal();
	LastStamp.Radius = GetCurrentBrushRadius();
	LastStamp.Falloff = GetCurrentBrushFalloff();
	LastStamp.Direction = GetInInvertStroke() ? -1.0 : 1.0;
	LastStamp.Depth = GetCurrentBrushDepth();
	LastStamp.Power = GetActiveBrushStrength();
	LastStamp.TimeStamp = FDateTime::Now();

	PreviousRayDirection = FVector3d::ZeroVector;

	// If applying symmetry, make sure the stamp is on the "positive" side. 
	if (bApplySymmetry)
	{
		LastStamp.LocalFrame = Symmetry->GetPositiveSideFrame(LastStamp.LocalFrame);
		LastStamp.WorldFrame = LastStamp.LocalFrame;
		LastStamp.WorldFrame.Transform(CurTargetTransform);
	}

	InitialStrokeTriangleID = -1;
	InitialStrokeTriangleID = GetBrushTriangleID();

	FSculptBrushOptions SculptOptions;
	//SculptOptions.bPreserveUVFlow = false; // SculptProperties->bPreserveUVFlow;
	SculptOptions.ConstantReferencePlane = GetCurrentStrokeReferencePlane();

	UseBrushOp->ConfigureOptions(SculptOptions);
	UseBrushOp->BeginStroke(GetSculptMesh(), LastStamp, VertexROI);

	AccumulatedTriangleROI.Reset();

	// begin change here? or wait for first stamp?
	BeginChange();
}

void UMeshVertexSculptTool::OnEndStroke()
{
	// update spatial
	bTargetDirty = true;

	GetActiveBrushOp()->EndStroke(GetSculptMesh(), LastStamp, VertexROI);

	// close change record
	EndChange();
}


void UMeshVertexSculptTool::OnCancelStroke()
{
	GetActiveBrushOp()->CancelStroke();

	delete ActiveVertexChange;
	ActiveVertexChange = nullptr;
}

// The first part of UpdateROI, which updates TriangleROIArray to be triangles
//  in our region of interest, and VertexROI to be vertices in our region of
//  interest.
void UMeshVertexSculptTool::UpdateRangeQueryTriBuffer(const FFrame3d& LocalFrame)
{
	if (RequireConnectivityToHitPointInStamp()
		// It's possible for LastBrushTriangleID to be null if we started a stroke and brushed off the
		//  edge of the mesh.
		&& LastBrushTriangleID == IndexConstants::InvalidID)
	{
		// If we're requiring connectivity, and we didn't hit a triangle to start with, then we shouldn't
		//  move any triangles.
		VertexROI.Reset();
		TriangleROIArray.Reset();
		SymmetricVertexROI.Reset();
		return;
	}

	FDynamicMesh3* Mesh = GetSculptMesh();
	FVector3d BrushPos = LocalFrame.Origin;

	// By default, our brush is a sphere, and we affect vertices inside it
	float RadiusSqr = GetCurrentBrushRadius() * GetCurrentBrushRadius();
	// This function gets called when first gathering the triangles that might intersect our brush
	//  from the octree's cells
	TFunction<void()> GatherOverlappingCells = [this, &BrushPos]()
	{
		FAxisAlignedBox3d BrushBox(
			BrushPos - GetCurrentBrushRadius() * FVector3d::One(),
			BrushPos + GetCurrentBrushRadius() * FVector3d::One());
		Octree.ParallelRangeQuery(BrushBox, RangeQueryTriBuffer);
	};
	// This is used to filter the gathered verts for ones that are actually in the brush.
	TFunction<bool(int32 Vid)> IsVertInBrush = [Mesh, &BrushPos, RadiusSqr](int32 Vid)
	{
		return DistanceSquared(BrushPos, Mesh->GetVertexRef(Vid)) < RadiusSqr;
	};

	// Some brush types want their brush to be a cylinder, so we need to change how we evaluate
	//  cells/vertices that are within reach
	TUniquePtr<FMeshSculptBrushOp>& CurrentBrush = GetActiveBrushOp();
	if (CurrentBrush &&
		(CurrentBrush->GetBrushRegionType() == FMeshSculptBrushOp::EBrushRegionType::InfiniteCylinder
		|| CurrentBrush->GetBrushRegionType() == FMeshSculptBrushOp::EBrushRegionType::CylinderOnSphere))
	{
		double CylinderRadius = GetCurrentBrushRadius();
		double CylinderHeight = TNumericLimits<double>::Max();

		FVector3d CylinderCenter, CylinderAxis;
		if (CurrentBrush->GetBrushRegionType() == FMeshSculptBrushOp::EBrushRegionType::InfiniteCylinder)
		{
			CylinderCenter = BrushPos;
			CylinderAxis = LocalFrame.Z();

			// Since cylinder is infinite, just have to check distance from line for the actual vert containment function
			FLine3d CylinderLine(CylinderCenter, CylinderAxis);
			IsVertInBrush = [Mesh, CylinderLine, RadiusSqr](int32 Vid)
			{
				return CylinderLine.DistanceSquared(Mesh->GetVertexRef(Vid)) < RadiusSqr;
			};
		}
		else // if cylinder on sphere
		{
			FVector3d SphereCenter = GizmoProperties ? CurTargetTransform.InverseTransformPosition(GizmoProperties->Position) 
				: FVector3d::ZeroVector;
			CylinderAxis = BrushPos - SphereCenter;
			if (!CylinderAxis.Normalize())
			{
				CylinderAxis = FVector3d::UnitZ();
			}
			// We want the bottom of our cylinder to be at the sphere center, and the top to go infinitely up, 
			//  but we need a non-infinite position for the center. So lets pick our height to be based on mesh bounds,
			//  with some arbitrary minimum instead.
			CylinderHeight = FMath::Max(InitialBoundsMaxDim, 1000);
			CylinderCenter = SphereCenter + CylinderAxis * (CylinderHeight / 2);

			IsVertInBrush = [Mesh, CylinderCenter, CylinderAxis, CylinderHeight, CylinderRadius](int32 Vid)
			{
				return UE::Geometry::DoesCylinderContainPoint(CylinderCenter, CylinderAxis, 
					CylinderRadius, CylinderHeight, Mesh->GetVertexRef(Vid));
			};
		}

		auto DoesCellIntersectBrush = [CylinderCenter, CylinderAxis, CylinderRadius, CylinderHeight](const FAxisAlignedBox3d& CellBounds)
		{
			return UE::Geometry::DoesCylinderIntersectBox(CellBounds,
				CylinderCenter, CylinderAxis, CylinderRadius, CylinderHeight);
		};
		
		FAxisAlignedBox3d ConservativeCylinderBounds;
		ConservativeCylinderBounds.Contain(CylinderCenter + CylinderAxis * (CylinderHeight / 2));
		ConservativeCylinderBounds.Contain(CylinderCenter - CylinderAxis * (CylinderHeight / 2));
		ConservativeCylinderBounds.Expand(CylinderRadius);

		GatherOverlappingCells = [this, ConservativeCylinderBounds, BoundsOverlapFn = MoveTemp(DoesCellIntersectBrush)]()
		{
			Octree.ParallelRangeQuery(ConservativeCylinderBounds, BoundsOverlapFn, RangeQueryTriBuffer);
		};
	}

	// Do a parallel range query to find those triangles that may intersect with
	//  our brush bounds. This grabs all triangles of intersecting cells, so we
	//  will need to do additional filtering afterward.
	RangeQueryTriBuffer.Reset();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_UpdateROI_RangeQuery);
		GatherOverlappingCells();
	}

	int32 ActiveComponentID = -1;
	int32 ActiveGroupID = -1;
	if (SculptProperties->BrushFilter == EMeshVertexSculptBrushFilterType::Component)
	{
		ActiveComponentID = (InitialStrokeTriangleID >= 0 && InitialStrokeTriangleID <= TriangleComponentIDs.Num()) ?
			TriangleComponentIDs[InitialStrokeTriangleID] : -1;
	}
	else if (SculptProperties->BrushFilter == EMeshVertexSculptBrushFilterType::PolyGroup)
	{
		ActiveGroupID = Mesh->IsTriangle(InitialStrokeTriangleID) ? ActiveGroupSet->GetGroup(InitialStrokeTriangleID) : -1;
	}

#if 1
	// in this path we use more memory but this lets us do more in parallel

	// Construct array of inside/outside flags for each triangle's vertices. If no
	// vertices are inside, clear the triangle ID from the range query buffer.
	// This can be done in parallel and it's cheaper to do repeated distance computations
	// than to try to do it inside the ROI building below (todo: profile this some more?)
	TriangleROIInBuf.SetNum(RangeQueryTriBuffer.Num(), EAllowShrinking::No);
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DynamicMeshSculptTool_UpdateROI_TriVerts);
		ParallelFor(RangeQueryTriBuffer.Num(), [&](int k)
		{
			// check various triangle ROI filters
			int32 tid = RangeQueryTriBuffer[k];
			bool bDiscardTriangle = false;
			if (ActiveComponentID >= 0 && TriangleComponentIDs[tid] != ActiveComponentID)
			{
				bDiscardTriangle = true;
			}
			if (ActiveGroupID >= 0 && ActiveGroupSet->GetGroup(tid) != ActiveGroupID)
			{
				bDiscardTriangle = true;
			}
			if (bDiscardTriangle)
			{
				TriangleROIInBuf[k].A = TriangleROIInBuf[k].B = TriangleROIInBuf[k].C = 0;
				RangeQueryTriBuffer[k] = -1;
				return;
			}

			const FIndex3i& TriV = Mesh->GetTriangleRef(tid);
			TriangleROIInBuf[k].A = IsVertInBrush(TriV.A) ? 1 : 0;
			TriangleROIInBuf[k].B = IsVertInBrush(TriV.B) ? 1 : 0;
			TriangleROIInBuf[k].C = IsVertInBrush(TriV.C) ? 1 : 0;
			if (TriangleROIInBuf[k].A + TriangleROIInBuf[k].B + TriangleROIInBuf[k].C == 0)
			{
				RangeQueryTriBuffer[k] = -1;
			}
		});
	}

	// Build up vertex and triangle ROIs from the remaining range-query triangles.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DynamicMeshSculptTool_UpdateROI_3Collect);
		VertexROIBuilder.Initialize(Mesh->MaxVertexID());
		TriangleROIBuilder.Initialize(Mesh->MaxTriangleID());
		int32 N = RangeQueryTriBuffer.Num();
		for ( int32 k = 0; k < N; ++k )
		{
			int32 tid = RangeQueryTriBuffer[k];
			if (tid == -1) continue;		// triangle was deleted in previous step
			const FIndex3i& TriV = Mesh->GetTriangleRef(RangeQueryTriBuffer[k]);
			const FIndex3i& Inside = TriangleROIInBuf[k];
			int InsideCount = 0;
			for (int j = 0; j < 3; ++j)
			{
				if (Inside[j])
				{
					VertexROIBuilder.Add(TriV[j]);
					InsideCount++;
				}
			}
			if (InsideCount > 0)
			{
				TriangleROIBuilder.Add(tid);
			}
		}

		// See if we need to filter our vertices based on connectivity to hit location (used to avoid affecting
		//  hidden regions of a mesh that might be in the volume of the brush)
		if (RequireConnectivityToHitPointInStamp()
			&& ensure(LastBrushTriangleID != IndexConstants::InvalidID))
		{
			FIndex3i HitTriVids = Mesh->GetTriangle(LastBrushTriangleID);
			TArray<int32> SeedVids;
			for (int i = 0; i < 3; ++i)
			{
				if (VertexROIBuilder.Contains(HitTriVids[i]))
				{
					SeedVids.Add(HitTriVids[i]);
				}
			}
			
			TSet<int32> ConnectedROIVids;
				FMeshConnectedComponents Components(Mesh);
				Components.GrowToConnectedVertices(*Mesh, SeedVids, ConnectedROIVids, nullptr,
					[this](int32 Vid, int32 Tid) { return VertexROIBuilder.Contains(Vid); });

			// We'll need to update TriangleROIBuilder based on the vertices too
			TArray<int32> TidsToFilter = TriangleROIBuilder.TakeValues();
			TriangleROIBuilder.Initialize(Mesh->MaxTriangleID());
			for (int32 Tid : TidsToFilter)
			{
				FIndex3i TriVids = Mesh->GetTriangle(Tid);
				for (int i = 0; i < 3; ++i)
				{
					if (ConnectedROIVids.Contains(TriVids[i]))
					{
						TriangleROIBuilder.Add(Tid);
						break; // continue to next Tid
					}
				}
			}
			VertexROI = ConnectedROIVids.Array();
		}
		else
		{
			VertexROIBuilder.SwapValuesWith(VertexROI);
		}

		if (bApplySymmetry)
		{
			// Find symmetric Vertex ROI. This will overlap with VertexROI in many cases.
			SymmetricVertexROI.Reset();
			Symmetry->GetMirrorVertexROI(VertexROI, SymmetricVertexROI, true);
			// expand the Triangle ROI to include the symmetric vertex one-rings
			for (int32 VertexID : SymmetricVertexROI)
			{
				if (Mesh->IsVertex(VertexID))
				{
					Mesh->EnumerateVertexTriangles(VertexID, [&](int32 tid)
					{
						TriangleROIBuilder.Add(tid);
					});
				}
			}
		}

		TriangleROIBuilder.SwapValuesWith(TriangleROIArray);
	}

#else
	// In this path we combine everything into one loop. Does fewer distance checks
	// but nothing can be done in parallel (would change if ROIBuilders had atomic-try-add)

	// TODO would need to support these, this branch is likely dead though
	ensure(SculptProperties->BrushFilter == EMeshVertexSculptBrushFilterType::None);
	ensure(!bApplySymmetry);
	ensure(!RequireConnectivityToHitPointInStamp());

	// collect set of vertices and triangles inside brush sphere, from range query result
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DynamicMeshSculptTool_UpdateROI_2Collect);
		VertexROIBuilder.Initialize(Mesh->MaxVertexID());
		TriangleROIBuilder.Initialize(Mesh->MaxTriangleID());
		for (int32 TriIdx : RangeQueryTriBuffer)
		{
			FIndex3i TriV = Mesh->GetTriangle(TriIdx);
			int InsideCount = 0;
			for (int j = 0; j < 3; ++j)
			{
				if (VertexROIBuilder.Contains(TriV[j]))
				{
					InsideCount++;
				} 
				else if (IsVertInBrush(TriV[j]))
				{
					VertexROIBuilder.Add(TriV[j]);
					InsideCount++;
				}
			}
			if (InsideCount > 0)
			{
				TriangleROIBuilder.Add(tid);
			}
		}
		VertexROIBuilder.SwapValuesWith(VertexROI);
		TriangleROIBuilder.SwapValuesWith(TriangleROIArray);
	}
#endif
}

// Second part of UpdateROI, which fills out ROIPrevPositionBuffer, prepares ROIPositionBuffer,
//  and prepares the symmetry buffers if relevant.
void UMeshVertexSculptTool::PrepROIVertPositionBuffers()
{
	FDynamicMesh3* Mesh = GetSculptMesh();
	// set up and populate position buffers for Vertex ROI
	TRACE_CPUPROFILER_EVENT_SCOPE(DynamicMeshSculptTool_UpdateROI_4ROI);
	int32 ROISize = VertexROI.Num();
	ROIPositionBuffer.SetNum(ROISize, EAllowShrinking::No);
	ROIPrevPositionBuffer.SetNum(ROISize, EAllowShrinking::No);
	ParallelFor(ROISize, [&](int i)
	{
		ROIPrevPositionBuffer[i] = Mesh->GetVertexRef(VertexROI[i]);
	});
	// do the same for the Symmetric Vertex ROI
	if (bApplySymmetry)
	{
		SymmetricROIPositionBuffer.SetNum(ROISize, EAllowShrinking::No);
		SymmetricROIPrevPositionBuffer.SetNum(ROISize, EAllowShrinking::No);
		ParallelFor(ROISize, [&](int i)
		{
			if ( Mesh->IsVertex(SymmetricVertexROI[i]) )
			{
				SymmetricROIPrevPositionBuffer[i] = Mesh->GetVertexRef(SymmetricVertexROI[i]);
			}
		});
	}
}

void UMeshVertexSculptTool::UpdateROI(const FVector3d& BrushPos)
{
	UpdateROI(FFrame3d(BrushPos));
}

void UMeshVertexSculptTool::UpdateROI(const FFrame3d& LocalFrame)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_UpdateROI);

	UpdateRangeQueryTriBuffer(LocalFrame);
	PrepROIVertPositionBuffers();
}

/*
 * Updates CurrentStamp, LastStamp, bMouseMoved, and LastMovedStamp (if bMouseMoved is true)
 * 
 * @return false if this ray can be ignored because it did not move and brush ignores zero movement.
 */
bool UMeshVertexSculptTool::UpdateStampPosition(const FRay& WorldRay)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_UpdateStampPosition);

	CalculateBrushRadius();

	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();

	ESculptBrushOpTargetType TargetType = UseBrushOp->GetBrushTargetType();
	switch (TargetType)
	{
	case ESculptBrushOpTargetType::SculptMesh:
		UpdateBrushPositionOnSculptMesh(WorldRay, true);
		break;
	case ESculptBrushOpTargetType::TargetMesh:
		UpdateBrushPositionOnTargetMesh(WorldRay, true);
		break;
	case ESculptBrushOpTargetType::ActivePlane:
		UpdateBrushPositionOnActivePlane(WorldRay);
		break;
	}

	// Adjust stamp alignment if needed
	RealignBrush(UseBrushOp->GetStampAlignmentType());

	CurrentStamp = LastStamp;
	//CurrentStamp.DeltaTime = FMathd::Min((FDateTime::Now() - LastStamp.TimeStamp).GetTotalSeconds(), 1.0);
	CurrentStamp.DeltaTime = 0.03;		// 30 fps - using actual time is no good now that we support variable stamps!
	CurrentStamp.WorldFrame = GetBrushFrameWorld();
	CurrentStamp.Radius = GetActiveBrushRadius();
	CurrentStamp.LocalFrame = GetBrushFrameLocal();
	CurrentStamp.Power = GetActiveBrushStrength();

	if (bHaveBrushAlpha && (AlphaProperties->RotationAngle != 0 || AlphaProperties->bRandomize))
	{
		float UseAngle = AlphaProperties->RotationAngle;
		if (AlphaProperties->bRandomize)
		{
			UseAngle += (StampRandomStream.GetFraction() - 0.5f) * 2.0f * AlphaProperties->RandomRange;
		}

		// possibly should be done in base brush...
		CurrentStamp.WorldFrame.Rotate(FQuaterniond(CurrentStamp.WorldFrame.Z(), UseAngle, true));
		CurrentStamp.LocalFrame.Rotate(FQuaterniond(CurrentStamp.LocalFrame.Z(), UseAngle, true));
	}

	if (bApplySymmetry)
	{
		CurrentStamp.LocalFrame = Symmetry->GetPositiveSideFrame(CurrentStamp.LocalFrame);
		CurrentStamp.WorldFrame = CurrentStamp.LocalFrame;
		CurrentStamp.WorldFrame.Transform(CurTargetTransform);
	}

	CurrentStamp.PrevLocalFrame = LastStamp.LocalFrame;
	CurrentStamp.PrevWorldFrame = LastStamp.WorldFrame;

	bMouseMoved = (PreviousRayDirection - WorldRay.Direction).SquaredLength() > FMathd::ZeroTolerance;
	if (bMouseMoved)
	{
		LastMovedStamp = CurrentStamp;
		PreviousRayDirection = WorldRay.Direction;
	}
	return bMouseMoved || !UseBrushOp->IgnoreZeroMovements();
}

// Adjusts brush alignment (assumes that currently brush is aligned to hit normal)
void UMeshVertexSculptTool::RealignBrush(FMeshSculptBrushOp::EStampAlignmentType AlignmentType)
{
	switch (AlignmentType)
	{
	case FMeshSculptBrushOp::EStampAlignmentType::HitNormal:
		// Assume this is already aligned
		break;
	case FMeshSculptBrushOp::EStampAlignmentType::Camera:
		AlignBrushToView();
		break;
	case FMeshSculptBrushOp::EStampAlignmentType::ReferencePlane:
		// Note for this and reference sphere: GetCurrentStrokeReferencePlane is not (necessarily)
		//  what we want because we may not have done UpdateStrokeReferencePlaneFromWorkPlane.
		UpdateBrushFrameWorld(GetBrushFrameWorld().Origin, 
			GizmoProperties ? GizmoProperties->Rotation.GetAxisZ() : FVector3d::UnitZ());
		break;
	case FMeshSculptBrushOp::EStampAlignmentType::ReferenceSphere:
	{
		FVector3d BrushLocation = GetBrushFrameWorld().Origin;
		FVector3d SphereCenter = GizmoProperties ? GizmoProperties->Position : FVector3d::ZeroVector;
		FVector3d NormalToUse = BrushLocation - SphereCenter;
		if (!NormalToUse.Normalize())
		{
			NormalToUse = FVector3d::UnitZ();
		}
		UpdateBrushFrameWorld(BrushLocation, NormalToUse);
		break;
	}
	}
}

bool UMeshVertexSculptTool::CanUpdateBrushType() const
{
	return DefaultPrimaryBrushID == -1;
}

TFuture<void> UMeshVertexSculptTool::ApplyStamp()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_ApplyStamp);

	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();

	FSculptBrushStamp* StampToUse = &CurrentStamp;
	// If we haven't moved our stamp, we might want to consider it to be at the same location
	//  (depending on the brush we're using).
	// TODO: It would be nice to have the brush visualization reflect this (currently it is still
	//  centered on the ray hit), but requires more changes to the base tool. Also this sometimes
	//  doesn't work super well with RequireConnectivityToHitPointInStamp because the hit location
	//  may be outside the stamp, but it's not terrible.
	if (!bMouseMoved && UseBrushOp->UseLastStampFrameOnZeroMovement())
	{
		StampToUse = &LastMovedStamp;
	}

	// compute region plane if necessary. This may currently be expensive?
	if (UseBrushOp->WantsStampRegionPlane())
	{
		StampToUse->RegionPlane = ComputeStampRegionPlane(StampToUse->LocalFrame, TriangleROIArray, true, false, false);
	}

	// set up alpha function if we have one
	if (bHaveBrushAlpha)
	{
		StampToUse->StampAlphaFunc = [this](const FSculptBrushStamp& Stamp, const FVector3d& Position)
		{
			return this->SampleBrushAlpha(Stamp, Position);
		};
	}

	// apply the stamp, which computes new positions
	FDynamicMesh3* Mesh = GetSculptMesh();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_ApplyStamp_Apply);
		UseBrushOp->ApplyStamp(Mesh, *StampToUse, VertexROI, ROIPositionBuffer);
	}

	// can discard alpha now
	StampToUse->StampAlphaFunc = nullptr;

	// if we are applying symmetry, we need to update the on-plane positions as they
	// will not be in the SymmetricVertexROI
	if (bApplySymmetry)
	{
		// update position of vertices that are on the symmetry plane
		Symmetry->ApplySymmetryPlaneConstraints(VertexROI, ROIPositionBuffer);

		// currently something gross is that VertexROI/ROIPositionBuffer may have both a vertex and it's mirror vertex,
		// each with a different position. We somehow need to be able to resolve this, but we don't have the mapping 
		// between the two locations in VertexROI, and we have no way to figure out the 'new' position of that mirror vertex
		// until we can look it up by VertexID, not array-index. So, we are going to bake in the new vertex positions for now.
		const int32 NumV = ROIPositionBuffer.Num();
		ParallelFor(NumV, [&](int32 k)
		{
			int VertIdx = VertexROI[k];
			const FVector3d& NewPos = ROIPositionBuffer[k];
			Mesh->SetVertex(VertIdx, NewPos, false);
		});

		// compute all the mirror vertex positions
		Symmetry->ComputeSymmetryConstrainedPositions(VertexROI, SymmetricVertexROI, ROIPositionBuffer, SymmetricROIPositionBuffer);
	}

	// once stamp is applied, we can start updating vertex change, which can happen async as we saved all necessary info
	TFuture<void> SaveVertexFuture;
	if (ActiveVertexChange != nullptr)
	{
		SaveVertexFuture = Async(VertexSculptToolAsyncExecTarget, [this]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_SyncMeshWithPositionBuffer_UpdateChange);
			const int32 NumV = ROIPositionBuffer.Num();
			for (int k = 0; k < NumV; ++k)
			{
				int VertIdx = VertexROI[k];
				ActiveVertexChange->UpdateVertex(VertIdx, ROIPrevPositionBuffer[k], ROIPositionBuffer[k]);
			}

			if (bApplySymmetry)
			{
				int32 NumSymV = SymmetricVertexROI.Num();
				for (int32 k = 0; k < NumSymV; ++k)
				{
					if (SymmetricVertexROI[k] >= 0)
					{
						ActiveVertexChange->UpdateVertex(SymmetricVertexROI[k], SymmetricROIPrevPositionBuffer[k], SymmetricROIPositionBuffer[k]);
					}
				}
			}

		});
	}

	// now actually update the mesh, which happens on the game thread
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_ApplyStamp_Sync);
		const int32 NumV = ROIPositionBuffer.Num();

		// If we are applying symmetry, we already baked these positions in in the branch above and
		// can skip it now, otherwise we update the mesh (todo: profile ParallelFor here, is it helping or hurting?)
		if (bApplySymmetry == false)
		{
			ParallelFor(NumV, [&](int32 k)
			{
				int VertIdx = VertexROI[k];
				const FVector3d& NewPos = ROIPositionBuffer[k];
				Mesh->SetVertex(VertIdx, NewPos, false);
			});
		}

		// if applying symmetry, bake in new symmetric positions
		if (bApplySymmetry)
		{
			ParallelFor(NumV, [&](int32 k)
			{
				int VertIdx = SymmetricVertexROI[k];
				if (Mesh->IsVertex(VertIdx))
				{
					const FVector3d& NewPos = SymmetricROIPositionBuffer[k];
					Mesh->SetVertex(VertIdx, NewPos, false);
				}
			});
		}

		Mesh->UpdateChangeStamps(true, false);
	}

	LastStamp = *StampToUse;
	LastStamp.TimeStamp = FDateTime::Now();

	// let caller wait for this to finish
	return SaveVertexFuture;
}




bool UMeshVertexSculptTool::IsHitTriangleBackFacing(int32 TriangleID, const FDynamicMesh3* QueryMesh)
{
	if (TriangleID != IndexConstants::InvalidID)
	{
		FViewCameraState StateOut;
		GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(StateOut);
		FVector3d LocalEyePosition(CurTargetTransform.InverseTransformPosition((FVector3d)StateOut.Position));

		FVector3d Normal, Centroid;
		double Area;
		QueryMesh->GetTriInfo(TriangleID, Normal, Area, Centroid);

		return (Normal.Dot((Centroid - LocalEyePosition)) >= 0);
	}
	return false;
}


int32 UMeshVertexSculptTool::FindHitSculptMeshTriangle(const FRay3d& LocalRay)
{
	// need this to finish before we can touch Octree
	WaitForPendingStampUpdate();

	int32 HitTID = Octree.FindNearestHitObject(LocalRay);
	if (GetBrushCanHitBackFaces() == false && IsHitTriangleBackFacing(HitTID, GetSculptMesh()))
	{
		HitTID = IndexConstants::InvalidID;
	}
	return HitTID;
}

int32 UMeshVertexSculptTool::FindHitTargetMeshTriangle(const FRay3d& LocalRay)
{
	int32 HitTID = BaseMeshSpatial.FindNearestHitObject(LocalRay);
	if (GetBrushCanHitBackFaces() == false && IsHitTriangleBackFacing(HitTID, GetBaseMesh()))
	{
		HitTID = IndexConstants::InvalidID;
	}
	return HitTID;
}



bool UMeshVertexSculptTool::UpdateBrushPosition(const FRay& WorldRay)
{
	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();

	bool bHit = false; 
	ESculptBrushOpTargetType TargetType = UseBrushOp->GetBrushTargetType();
	switch (TargetType)
	{
	case ESculptBrushOpTargetType::SculptMesh:
		bHit = UpdateBrushPositionOnSculptMesh(WorldRay, false);
		break;
	case ESculptBrushOpTargetType::TargetMesh:
		bHit = UpdateBrushPositionOnTargetMesh(WorldRay, false);
		break;
	case ESculptBrushOpTargetType::ActivePlane:
		//UpdateBrushPositionOnActivePlane(WorldRay);
		bHit = UpdateBrushPositionOnSculptMesh(WorldRay, false);
		break;
	}

	if (bHit)
	{
		RealignBrush(UseBrushOp->GetStampAlignmentType());
	}

	return bHit;
}




void UMeshVertexSculptTool::UpdateHoverStamp(const FFrame3d& StampFrameWorld)
{
	FFrame3d HoverFrame = StampFrameWorld;
	if (bHaveBrushAlpha && (AlphaProperties->RotationAngle != 0))
	{
		HoverFrame.Rotate(FQuaterniond(HoverFrame.Z(), AlphaProperties->RotationAngle, true));
	}
	UMeshSculptToolBase::UpdateHoverStamp(HoverFrame);
}

bool UMeshVertexSculptTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	// 4.26 HOTFIX: update LastWorldRay position so that we have it for updating WorkPlane position
	UMeshSurfacePointTool::LastWorldRay = DevicePos.WorldRay;

	PendingStampBrushID = SculptProperties->PrimaryBrushID;
	if(ensure(InStroke() == false))
	{
		UpdateBrushPosition(DevicePos.WorldRay);

		if (BrushIndicatorMaterial)
		{
			BrushIndicatorMaterial->SetScalarParameterValue(TEXT("FalloffRatio"), GetCurrentBrushFalloff());

			switch (SculptProperties->PrimaryFalloffType)
			{
			default:
			case EMeshSculptFalloffType::Smooth:
			case EMeshSculptFalloffType::BoxSmooth:
				BrushIndicatorMaterial->SetScalarParameterValue(TEXT("FalloffMode"), 0.0f);
				break;
			case EMeshSculptFalloffType::Linear:
			case EMeshSculptFalloffType::BoxLinear:
				BrushIndicatorMaterial->SetScalarParameterValue(TEXT("FalloffMode"), 0.3333333f);
				break;
			case EMeshSculptFalloffType::Inverse:
			case EMeshSculptFalloffType::BoxInverse:
				BrushIndicatorMaterial->SetScalarParameterValue(TEXT("FalloffMode"), 0.6666666f);
				break;
			case EMeshSculptFalloffType::Round:
			case EMeshSculptFalloffType::BoxRound:
				BrushIndicatorMaterial->SetScalarParameterValue(TEXT("FalloffMode"), 1.0f);
				break;
			}

			switch (SculptProperties->PrimaryFalloffType)
			{
			default:
			case EMeshSculptFalloffType::Smooth:
			case EMeshSculptFalloffType::Linear:
			case EMeshSculptFalloffType::Inverse:
			case EMeshSculptFalloffType::Round:
				BrushIndicatorMaterial->SetScalarParameterValue(TEXT("FalloffShape"), 0.0f);
				break;
			case EMeshSculptFalloffType::BoxSmooth:
			case EMeshSculptFalloffType::BoxLinear:
			case EMeshSculptFalloffType::BoxInverse:
			case EMeshSculptFalloffType::BoxRound:
				BrushIndicatorMaterial->SetScalarParameterValue(TEXT("FalloffShape"), 1.0f);
			}
			
		}
	}

	return true;
}


void UMeshVertexSculptTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UMeshSculptToolBase::Render(RenderAPI);

	// draw a dot for the symmetric brush stamp position
	if (bApplySymmetry)
	{
		FToolDataVisualizer Visualizer;
		Visualizer.BeginFrame(RenderAPI);
		FVector3d MirrorPoint = CurTargetTransform.TransformPosition(
			Symmetry->GetMirroredPosition(HoverStamp.LocalFrame.Origin));
		Visualizer.DrawPoint(MirrorPoint, FLinearColor(1.0, 0.1, 0.1, 1), 5.0f, false);
		Visualizer.EndFrame();
	}
}


void UMeshVertexSculptTool::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	Super::DrawHUD(Canvas, RenderAPI);
	if (BrushEditBehavior.IsValid())
	{
		BrushEditBehavior->DrawHUD(Canvas, RenderAPI);	
	}
}


void UMeshVertexSculptTool::OnTick(float DeltaTime)
{
	UMeshSculptToolBase::OnTick(DeltaTime);

	TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Tick);

	// process the undo update
	if (bUndoUpdatePending)
	{
		// wait for updates
		WaitForPendingUndoRedo();

		// post rendering update
		DynamicMeshComponent->FastNotifyTriangleVerticesUpdated(AccumulatedTriangleROI,
			EMeshRenderAttributeFlags::Positions | EMeshRenderAttributeFlags::VertexNormals);
		GetToolManager()->PostInvalidation();

		// ignore stamp and wait for next tick to do anything else
		bUndoUpdatePending = false;
		return;
	}

	// if user changed to not-frozen, we need to reinitialize the target
	if (bCachedFreezeTarget != SculptProperties->bFreezeTarget)
	{
		UpdateBaseMesh(nullptr);
		bTargetDirty = false;
	}

	if (InStroke())
	{
		//UE_LOG(LogTemp, Warning, TEXT("dt is %.3f, tick fps %.2f - roi size %d/%d"), DeltaTime, 1.0 / DeltaTime, VertexROI.Num(), TriangleROI.Num());
		TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Tick_StrokeUpdate);
		FDynamicMesh3* Mesh = GetSculptMesh();

		// update brush position
		if (UpdateStampPosition(GetPendingStampRayWorld()) == false)
		{
			return;
		}
		UpdateStampPendingState();
		if (IsStampPending() == false)
		{
			return;
		}

		// need to make sure previous stamp finished
		WaitForPendingStampUpdate();

		// update sculpt ROI
		UpdateROI(CurrentStamp.LocalFrame);

		// Append updated ROI to modified region (async). For some reason this is very expensive,
		// maybe because of TSet? but we have a lot of time to do it.
		TFuture<void> AccumulateROI = Async(VertexSculptToolAsyncExecTarget, [this]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Tick_AccumROI);
			for (int32 tid : TriangleROIArray)
			{
				AccumulatedTriangleROI.Add(tid);
			}
		});

		// Start precomputing the normals ROI. This is currently the most expensive single thing we do next
		// to Octree re-insertion, despite it being almost trivial. Why?!?
		bool bUsingOverlayNormalsOut = false;
		TFuture<void> NormalsROI = Async(VertexSculptToolAsyncExecTarget, [Mesh, &bUsingOverlayNormalsOut, this]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Tick_NormalsROI);

			//UE::SculptUtil::PrecalculateNormalsROI(Mesh, TriangleROIArray, NormalsROIBuilder, bUsingOverlayNormals, false);
			UE::SculptUtil::PrecalculateNormalsROI(Mesh, TriangleROIArray, NormalsFlags, bUsingOverlayNormalsOut, false);
		});

		// NOTE: you might try to speculatively do the octree remove here, to save doing it later on Reinsert().
		// This will not improve things, as Reinsert() checks if it needs to actually re-insert, which avoids many
		// removes, and does much of the work of Remove anyway.

		// Apply the stamp. This will return a future that is updating the vertex-change record, 
		// which can run until the end of the frame, as it is using cached information
		TFuture<void> UpdateChangeFuture = ApplyStamp();

		// begin octree rebuild calculation
		StampUpdateOctreeFuture = Async(VertexSculptToolAsyncExecTarget, [this]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Tick_OctreeReinsert);
			Octree.ReinsertTrianglesParallel(TriangleROIArray, OctreeUpdateTempBuffer, OctreeUpdateTempFlagBuffer);
		});
		bStampUpdatePending = true;
		//TFuture<void> OctreeRebuild = Async(VertexSculptToolAsyncExecTarget, [&]()
		//{
		//	TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Tick_OctreeReinsert);
		//	Octree.ReinsertTriangles(TriangleROIArray);
		//});

		// TODO: first step of RecalculateROINormals() is to convert TriangleROI into vertex or element ROI.
		// We can do this while we are computing stamp!

		// precompute dynamic mesh update info
		TArray<int32> RenderUpdateSets; FAxisAlignedBox3d RenderUpdateBounds;
		TFuture<bool> RenderUpdatePrecompute = DynamicMeshComponent->FastNotifyTriangleVerticesUpdated_TryPrecompute(
				TriangleROIArray, RenderUpdateSets, RenderUpdateBounds);

		// recalculate normals. This has to complete before we can update component
		// (in fact we could do it per-chunk...)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Tick_RecalcNormals);
			NormalsROI.Wait();
			UE::SculptUtil::RecalculateROINormals(Mesh, NormalsFlags, bUsingOverlayNormalsOut);
			//UE::SculptUtil::RecalculateROINormals(Mesh, NormalsROIBuilder.Indices(), bUsingOverlayNormals);
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Tick_UpdateMesh);
			RenderUpdatePrecompute.Wait();
			DynamicMeshComponent->FastNotifyTriangleVerticesUpdated_ApplyPrecompute(TriangleROIArray,
				EMeshRenderAttributeFlags::Positions | EMeshRenderAttributeFlags::VertexNormals,
				RenderUpdatePrecompute, RenderUpdateSets, RenderUpdateBounds);

			GetToolManager()->PostInvalidation();
		}

		// we don't really need to wait for these to happen to end Tick()...
		UpdateChangeFuture.Wait();
		AccumulateROI.Wait();
	} 
	else if (bTargetDirty)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Tick_UpdateTarget);
		check(InStroke() == false);

		// this spawns futures that we could allow to run while other things happen...
		UpdateBaseMesh(&AccumulatedTriangleROI);
		AccumulatedTriangleROI.Reset();

		bTargetDirty = false;
	}

}



void UMeshVertexSculptTool::WaitForPendingStampUpdate()
{
	if (bStampUpdatePending)
	{
		StampUpdateOctreeFuture.Wait();
		bStampUpdatePending = false;
	}
}



void UMeshVertexSculptTool::UpdateBaseMesh(const TSet<int32>* TriangleSet)
{
	if (SculptProperties != nullptr)
	{
		bCachedFreezeTarget = SculptProperties->bFreezeTarget;
		if (SculptProperties->bFreezeTarget)
		{
			return;   // do not update frozen target
		}
	}

	const FDynamicMesh3* SculptMesh = GetSculptMesh();
	if ( ! TriangleSet )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Target_FullUpdate);
		BaseMesh.Copy(*SculptMesh, false, false, false, false);
		BaseMesh.EnableVertexNormals(FVector3f::UnitZ());
		FMeshNormals::QuickComputeVertexNormals(BaseMesh);
		BaseMeshSpatial.SetMaxTreeDepth(8);
		BaseMeshSpatial = FDynamicMeshOctree3();   // need to clear...
		BaseMeshSpatial.Initialize(&BaseMesh);
	}
	else
	{
		BaseMeshIndexBuffer.Reset();
		for ( int32 tid : *TriangleSet)
		{ 
			FIndex3i Tri = BaseMesh.GetTriangle(tid);
			BaseMesh.SetVertex(Tri.A, SculptMesh->GetVertex(Tri.A));
			BaseMesh.SetVertex(Tri.B, SculptMesh->GetVertex(Tri.B));
			BaseMesh.SetVertex(Tri.C, SculptMesh->GetVertex(Tri.C));
			BaseMeshIndexBuffer.Add(tid);
		}
		auto UpdateBaseNormals = Async(VertexSculptToolAsyncExecTarget, [this]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Target_UpdateBaseNormals);
			FMeshNormals::QuickComputeVertexNormalsForTriangles(BaseMesh, BaseMeshIndexBuffer);
		});
		auto ReinsertTriangles = Async(VertexSculptToolAsyncExecTarget, [TriangleSet, this]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Target_Reinsert);
			BaseMeshSpatial.ReinsertTriangles(*TriangleSet);
		});
		UpdateBaseNormals.Wait();
		ReinsertTriangles.Wait();
	}
}


bool UMeshVertexSculptTool::GetBaseMeshNearest(int32 VertexID, const FVector3d& Position, double SearchRadius, FVector3d& TargetPosOut, FVector3d& TargetNormalOut)
{
	TargetPosOut = BaseMesh.GetVertex(VertexID);
	TargetNormalOut = (FVector3d)BaseMesh.GetVertexNormal(VertexID);
	return true;
}






void UMeshVertexSculptTool::IncreaseBrushSpeedAction()
{
	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();
	float CurStrength = UseBrushOp->PropertySet->GetStrength();
	float NewStrength = FMath::Clamp(CurStrength + 0.05f, 0.0f, 1.0f);
	UseBrushOp->PropertySet->SetStrength(NewStrength);
	NotifyOfPropertyChangeByTool(UseBrushOp->PropertySet.Get());
}

void UMeshVertexSculptTool::DecreaseBrushSpeedAction()
{
	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();
	float CurStrength = UseBrushOp->PropertySet->GetStrength();
	float NewStrength = FMath::Clamp(CurStrength - 0.05f, 0.0f, 1.0f);
	UseBrushOp->PropertySet->SetStrength(NewStrength);
	NotifyOfPropertyChangeByTool(UseBrushOp->PropertySet.Get());
}

void UMeshVertexSculptTool::UpdateBrushAlpha(UTexture2D* NewAlpha)
{
	if (this->BrushAlpha != NewAlpha)
	{
		this->BrushAlpha = NewAlpha;
		if (this->BrushAlpha != nullptr)
		{
			TImageBuilder<FVector4f> AlphaValues;

			constexpr bool bPreferPlatformData = false;
			const bool bReadOK = UE::AssetUtils::ReadTexture(this->BrushAlpha, AlphaValues, bPreferPlatformData);
			if (bReadOK)
			{
				BrushAlphaValues = MoveTemp(AlphaValues);
				BrushAlphaDimensions = AlphaValues.GetDimensions();
				bHaveBrushAlpha = true;

				BrushIndicatorMaterial->SetTextureParameterValue(TEXT("BrushAlpha"), NewAlpha);
				BrushIndicatorMaterial->SetScalarParameterValue(TEXT("AlphaPower"), 1.0);

				return;
			}
		}
		bHaveBrushAlpha = false;
		BrushAlphaValues = TImageBuilder<FVector4f>();
		BrushAlphaDimensions = FImageDimensions();

		BrushIndicatorMaterial->SetTextureParameterValue(TEXT("BrushAlpha"), nullptr);
		BrushIndicatorMaterial->SetScalarParameterValue(TEXT("AlphaPower"), 0.0);
	}
}


double UMeshVertexSculptTool::SampleBrushAlpha(const FSculptBrushStamp& Stamp, const FVector3d& Position) const
{
	if (! bHaveBrushAlpha) return 1.0;

	static const FVector4f InvalidValue(0, 0, 0, 0);

	FVector2d AlphaUV = Stamp.LocalFrame.ToPlaneUV(Position, 2);
	double u = AlphaUV.X / Stamp.Radius;
	u = 1.0 - (u + 1.0) / 2.0;
	double v = AlphaUV.Y / Stamp.Radius;
	v = 1.0 - (v + 1.0) / 2.0;
	if (u < 0 || u > 1) return 0.0;
	if (v < 0 || v > 1) return 0.0;
	FVector4f AlphaValue = BrushAlphaValues.BilinearSampleUV<float>(FVector2d(u, v), InvalidValue);
	return FMathd::Clamp(AlphaValue.X, 0.0, 1.0);
}


void UMeshVertexSculptTool::TryToInitializeSymmetry()
{
	// Attempt to find symmetry, favoring the X axis, then Y axis, if a single symmetry plane is not immediately found
	// Uses local mesh surface (angle sum, normal) to help disambiguate final matches, but does not require exact topology matches across the plane

	FAxisAlignedBox3d Bounds = GetSculptMesh()->GetBounds(true);

	TArray<FVector3d> PreferAxes;
	PreferAxes.Add(this->InitialTargetTransform.GetRotation().AxisX());
	PreferAxes.Add(this->InitialTargetTransform.GetRotation().AxisY());

	FMeshPlanarSymmetry FindSymmetry;
	FFrame3d FoundPlane;
	if (FindSymmetry.FindPlaneAndInitialize(GetSculptMesh(), Bounds, FoundPlane, PreferAxes))
	{
		Symmetry = MakePimpl<FMeshPlanarSymmetry>();
		*Symmetry = MoveTemp(FindSymmetry);
		bMeshSymmetryIsValid = true;
	}
}


//
// Change Tracking
//
void UMeshVertexSculptTool::BeginChange()
{
	check(ActiveVertexChange == nullptr);
	ActiveVertexChange = new FMeshVertexChangeBuilder();
	LongTransactions.Open(LOCTEXT("VertexSculptChange", "Brush Stroke"), GetToolManager());
}

void UMeshVertexSculptTool::EndChange()
{
	check(ActiveVertexChange);

	TUniquePtr<TWrappedToolCommandChange<FMeshVertexChange>> NewChange = MakeUnique<TWrappedToolCommandChange<FMeshVertexChange>>();
	NewChange->WrappedChange = MoveTemp(ActiveVertexChange->Change);
	NewChange->BeforeModify = [this](bool bRevert)
	{
		this->WaitForPendingUndoRedo();
	};

	GetToolManager()->EmitObjectChange(DynamicMeshComponent, MoveTemp(NewChange), LOCTEXT("VertexSculptChange", "Brush Stroke"));
	if (bMeshSymmetryIsValid && bApplySymmetry == false)
	{
		// if we end a stroke while symmetry is possible but disabled, we now have to assume that symmetry is no longer possible
		GetToolManager()->EmitObjectChange(this, MakeUnique<FVertexSculptNonSymmetricChange>(), LOCTEXT("DisableSymmetryChange", "Disable Symmetry"));
		bMeshSymmetryIsValid = false;
		SymmetryProperties->bSymmetryCanBeEnabled = bMeshSymmetryIsValid;
	}
	LongTransactions.Close(GetToolManager());

	delete ActiveVertexChange;
	ActiveVertexChange = nullptr;
}

void UMeshVertexSculptTool::SetDefaultPrimaryBrushID(const int32 InPrimaryBrushID)
{
	DefaultPrimaryBrushID = InPrimaryBrushID;
}


void UMeshVertexSculptTool::WaitForPendingUndoRedo()
{
	if (bUndoUpdatePending)
	{
		UndoNormalsFuture.Wait();
		UndoUpdateOctreeFuture.Wait();
		UndoUpdateBaseMeshFuture.Wait();
		bUndoUpdatePending = false;
	}
}

void UMeshVertexSculptTool::OnDynamicMeshComponentChanged(UDynamicMeshComponent* Component, const FMeshRegionChangeBase* Change, bool bRevert)
{
	// have to wait for any outstanding stamp update to finish...
	WaitForPendingStampUpdate();
	// wait for previous Undo to finish (possibly never hit because the change records do it?)
	WaitForPendingUndoRedo();

	FDynamicMesh3* Mesh = GetSculptMesh();

	// figure out the set of modified triangles
	AccumulatedTriangleROI.Reset();
	Change->ProcessChangeVertices(Mesh, [this, &Mesh](TConstArrayView<int32> Vertices)
	{
		UE::Geometry::VertexToTriangleOneRing(Mesh, Vertices, AccumulatedTriangleROI);
	}, bRevert);

	// start the normal recomputation
	UndoNormalsFuture = Async(VertexSculptToolAsyncExecTarget, [this, Mesh]()
	{
		UE::SculptUtil::RecalculateROINormals(Mesh, AccumulatedTriangleROI, NormalsROIBuilder);
		return true;
	});

	// start the octree update
	UndoUpdateOctreeFuture = Async(VertexSculptToolAsyncExecTarget, [this, Mesh]()
	{
		Octree.ReinsertTriangles(AccumulatedTriangleROI);
		return true;
	});

	// start the base mesh update
	UndoUpdateBaseMeshFuture = Async(VertexSculptToolAsyncExecTarget, [this, Mesh]()
	{
		UpdateBaseMesh(&AccumulatedTriangleROI);
		return true;
	});

	// note that we have a pending update
	bUndoUpdatePending = true;
}






void UMeshVertexSculptTool::UpdateBrushType(EMeshVertexSculptBrushType BrushType)
{
	UpdateBrushType((int32)BrushType);
}

void UMeshVertexSculptTool::UpdateBrushType(int32 BrushID)
{	
	static const FText BaseMessage = LOCTEXT("OnStartSculptTool", "Hold Shift to Smooth, Ctrl to Invert (where applicable). [/] and S/D change Size (+Shift to small-step), W/E changes Strength.");
	FTextBuilder Builder;
	Builder.AppendLine(BaseMessage);

	SetActivePrimaryBrushType(BrushID);

	if (BrushEditBehavior.IsValid())
	{
		// todo: Handle Kelvinlet brush props better. At the moment we are just disabling strength editing for Kelvinlet brush ops.
		auto PropertySetSupportsStrength = [this]()
		{
			return PrimaryBrushOp->PropertySet.IsValid() && Cast<UBaseKelvinletBrushOpProps>(PrimaryBrushOp->PropertySet.Get()) == nullptr;
		};
	
		if (PropertySetSupportsStrength())
		{
			BrushEditBehavior->VerticalProperty.Name = LOCTEXT("BrushStrength", "Strength");
            BrushEditBehavior->VerticalProperty.GetValueFunc = [this](){ return PrimaryBrushOp->PropertySet->GetStrength(); };
            BrushEditBehavior->VerticalProperty.SetValueFunc = [this](float NewValue){ PrimaryBrushOp->PropertySet->SetStrength(FMath::Clamp(NewValue, 0.f, 1.f)); };
            BrushEditBehavior->VerticalProperty.bEnabled = true;
		}
		else
		{
			BrushEditBehavior->VerticalProperty.bEnabled = false;
		}
	}
	
	if (ensure(SculptProperties))
	{
		SculptProperties->bCanFreezeTarget = 
			BrushID == (int32)EMeshVertexSculptBrushType::Offset
			|| BrushID == (int32)EMeshVertexSculptBrushType::SculptMax || BrushID == (int32)EMeshVertexSculptBrushType::SculptView
			|| BrushID == (int32)EMeshVertexSculptBrushType::Pinch;
	}

	SetToolPropertySourceEnabled(GizmoProperties, false);
	if (PrimaryBrushOp && 
		(PrimaryBrushOp->GetReferencePlaneType() == FMeshSculptBrushOp::EReferencePlaneType::WorkPlane
		|| PrimaryBrushOp->GetStampAlignmentType() == FMeshSculptBrushOp::EStampAlignmentType::ReferencePlane
		|| PrimaryBrushOp->GetStampAlignmentType() == FMeshSculptBrushOp::EStampAlignmentType::ReferenceSphere))
	{
		Builder.AppendLine(LOCTEXT("FixedPlaneTip", "Use T to reposition Work Plane at cursor, Shift+T to align to Normal, Ctrl+Shift+T to align to View"));
		SetToolPropertySourceEnabled(GizmoProperties, true);
	}

	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();
	bool bEnableAlpha = UseBrushOp && UseBrushOp->UsesAlpha();
	SetToolPropertySourceEnabled(AlphaProperties, bEnableAlpha);

	GetToolManager()->DisplayMessage(Builder.ToText(), EToolMessageLevel::UserNotification);
}



void UMeshVertexSculptTool::UndoRedo_RestoreSymmetryPossibleState(bool bSetToValue)
{
	bMeshSymmetryIsValid = bSetToValue;
	SymmetryProperties->bSymmetryCanBeEnabled = bMeshSymmetryIsValid;
}





void FVertexSculptNonSymmetricChange::Apply(UObject* Object)
{
	if (Cast<UMeshVertexSculptTool>(Object))
	{
		Cast<UMeshVertexSculptTool>(Object)->UndoRedo_RestoreSymmetryPossibleState(false);
	}
}
void FVertexSculptNonSymmetricChange::Revert(UObject* Object)
{
	if (Cast<UMeshVertexSculptTool>(Object))
	{
		Cast<UMeshVertexSculptTool>(Object)->UndoRedo_RestoreSymmetryPossibleState(true);
	}
}

#undef LOCTEXT_NAMESPACE

