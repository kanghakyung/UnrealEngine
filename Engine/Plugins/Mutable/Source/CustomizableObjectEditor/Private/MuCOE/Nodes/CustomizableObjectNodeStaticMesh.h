// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeMesh.h"
#include "MuCOE/Nodes/SCustomizableObjectNode.h"

#include "CustomizableObjectNodeStaticMesh.generated.h"

namespace ENodeTitleType { enum Type : int; }

class FArchive;
class FAssetThumbnail;
class FAssetThumbnailPool;
class SOverlay;
class SVerticalBox;
class UCustomizableObjectLayout;
class UCustomizableObjectNodeRemapPins;
class UCustomizableObjectNodeRemapPinsByName;
class UMaterialInterface;
class UObject;
class UStaticMesh;
class UTexture2D;
struct FPropertyChangedEvent;
struct FSlateBrush;


// Class to render the Static Mesh thumbnail of a CustomizableObjectNodeStaticMesh
class SGraphNodeStaticMesh : public SCustomizableObjectNode
{
public:

	SLATE_BEGIN_ARGS(SGraphNodeStaticMesh) {}
	SLATE_END_ARGS();

	SGraphNodeStaticMesh() : SCustomizableObjectNode() {};

	// Builds the SGraphNodeStaticMesh when needed
	void Construct(const FArguments& InArgs, UEdGraphNode* InGraphNode);

	// Calls the needed functions to build the SGraphNode widgets
	void UpdateGraphNode();

	// Overriden functions to build the SGraphNode widgets
	virtual void SetDefaultTitleAreaWidget(TSharedRef<SOverlay> DefaultTitleAreaWidget) override;
	virtual void CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox) override;
	virtual bool ShouldAllowCulling() const override { return false; }

	// Callbacks for the widget
	void OnExpressionPreviewChanged(const ECheckBoxState NewCheckedState);
	ECheckBoxState IsExpressionPreviewChecked() const;
	const FSlateBrush* GetExpressionPreviewArrow() const;
	EVisibility ExpressionPreviewVisibility() const;

public:

	// Single property that only draws the combo box widget of the static mesh
	TSharedPtr<class ISinglePropertyView> StaticMeshSelector;

	// Pointer to the NodeStaticMesh that owns this SGraphNode
	class UCustomizableObjectNodeStaticMesh* NodeStaticMesh = nullptr;

private:

	// Classes needed to get and render the thumbnail of the Static Mesh
	TSharedPtr<FAssetThumbnailPool> AssetThumbnailPool;
	TSharedPtr<FAssetThumbnail> AssetThumbnail;

	//This parameter defines the size of the thumbnail widget inside the Node
	float WidgetSize;

	// This parameter defines the resolution of the thumbnail
	uint32 ThumbnailSize;

};


USTRUCT()
struct CUSTOMIZABLEOBJECTEDITOR_API FCustomizableObjectNodeStaticMeshMaterial
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	TObjectPtr<UEdGraphPin_Deprecated> MeshPin_DEPRECATED;

	UPROPERTY()
	TObjectPtr<UEdGraphPin_Deprecated> LayoutPin_DEPRECATED;

	UPROPERTY()
	TArray< TObjectPtr<UEdGraphPin_Deprecated>> ImagePins_DEPRECATED;

	UPROPERTY()
	FEdGraphPinReference MeshPinRef;

	UPROPERTY()
	FEdGraphPinReference LayoutPinRef;

	UPROPERTY()
	TArray<FEdGraphPinReference> ImagePinsRef;
};


USTRUCT()
struct CUSTOMIZABLEOBJECTEDITOR_API FCustomizableObjectNodeStaticMeshLOD
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TArray<FCustomizableObjectNodeStaticMeshMaterial> Materials;
};


UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeStaticMesh : public UCustomizableObjectNodeMesh
{ 
public:
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	TSoftObjectPtr<UStaticMesh> StaticMesh;

	/** Images */
	UPROPERTY()
	TArray<FCustomizableObjectNodeStaticMeshLOD> LODs;

	/** Default pin when there is no mesh. */
	UPROPERTY()
	FEdGraphPinReference DefaultPin;
	
	// UObject interface.
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void Serialize(FArchive& Ar) override;

	// UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	virtual bool ProvidesCustomPinRelevancyTest() const override { return true; }
	virtual bool IsPinRelevant(const UEdGraphPin* Pin) const override;
	virtual UCustomizableObjectNodeRemapPinsByName* CreateRemapPinsByName() const override;
	virtual bool HasPinViewer() const override;
	virtual bool IsNodeOutDatedAndNeedsRefresh() override;
	virtual FString GetRefreshMessage() const override;

	// UCustomizableObjectNodeMesh interface
	virtual UTexture2D* FindTextureForPin(const UEdGraphPin* Pin) const override;
	virtual TArray<UCustomizableObjectLayout*> GetLayouts(const UEdGraphPin& OutPin) const override;
	virtual TSoftObjectPtr<UObject> GetMesh() const override;
	virtual UEdGraphPin* GetMeshPin(int32 LOD, int32 SectionIndex) const override;
	virtual void GetPinSection(const UEdGraphPin& Pin, int32& OutLODIndex, int32& OutSectionIndex, int32& OutLayoutIndex) const override;

	/** Returns the material associated to the given output pin. */
	UMaterialInterface* GetMaterialFor(const UEdGraphPin* Pin) const;

	// Creates the SGraph Node widget for the thumbnail
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

	// Determines if the Node is collapsed or not
	bool bCollapsed = true;

	// Pointer to the SGraphNodeStaticMesh
	TWeakPtr< SGraphNodeStaticMesh > GraphNodeStaticMesh;

};

