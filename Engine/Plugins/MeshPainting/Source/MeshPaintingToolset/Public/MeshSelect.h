// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseTools/SingleClickTool.h"
#include "MeshPaintInteractions.h"
#include "MeshSelect.generated.h"

class IMeshPaintComponentAdapter;
class AActor;
class UMeshComponent;
class UMeshPaintSelectionMechanic;

/**
 * Builder for UVertexAdapterClickTool
 */
UCLASS()
class MESHPAINTINGTOOLSET_API UVertexAdapterClickToolBuilder : public USingleClickToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};


/**
 * Builder for UTextureColorAdapterClickTool
 */
UCLASS()
class MESHPAINTINGTOOLSET_API UTextureColorAdapterClickToolBuilder : public USingleClickToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};


/**
 * Builder for UTextureAssetAdapterClickTool
 */
UCLASS()
class MESHPAINTINGTOOLSET_API UTextureAssetAdapterClickToolBuilder : public USingleClickToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};


/**
 * USingleClickTool is perhaps the simplest possible interactive tool. It simply
 * reacts to default primary button clicks for the active device (eg left-mouse clicks).
 *
 * The function ::IsHitByClick() determines what is clickable by this Tool. The default is
 * to return true, which means the click will activate anywhere (the Tool itself has no
 * notion of Actors, Components, etc). You can override this function to, for example,
 * filter out clicks that don't hit a target object, etc.
 *
 * The function ::OnClicked() implements the action that will occur when a click happens.
 * You must override this to implement any kind of useful behavior.
 */
UCLASS(Abstract)
class MESHPAINTINGTOOLSET_API UMeshClickTool : public USingleClickTool, public IMeshPaintSelectionInterface
{
	GENERATED_BODY()

public:
	UMeshClickTool();

	// USingleClickTool overrides
	virtual	void Setup() override;
	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;
	virtual bool IsMeshAdapterSupported(TSharedPtr<IMeshPaintComponentAdapter> MeshAdapter) const override;
	virtual bool AllowsMultiselect() const override { return false; }

protected:
	// flags used to identify modifier keys/buttons
	static const int AdditiveSelectionModifier = 1;


	UPROPERTY(Transient)
	TObjectPtr<UMeshPaintSelectionMechanic> SelectionMechanic;
};

UCLASS()
class MESHPAINTINGTOOLSET_API UVertexAdapterClickTool : public UMeshClickTool
{
	GENERATED_BODY()

public:
	virtual bool AllowsMultiselect() const override { return true; }
};

UCLASS()
class MESHPAINTINGTOOLSET_API UTextureColorAdapterClickTool : public UMeshClickTool
{
	GENERATED_BODY()

public:
	virtual bool AllowsMultiselect() const override { return true; }
};

UCLASS()
class MESHPAINTINGTOOLSET_API UTextureAssetAdapterClickTool : public UMeshClickTool
{
	GENERATED_BODY()
};
