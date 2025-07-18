// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "Containers/Array.h"
#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphNodeUtils.h"
#include "Engine/Blueprint.h"
#include "Internationalization/Text.h"
#include "K2Node_Variable.h"
#include "KismetCompilerMisc.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

#include "K2Node_VariableGet.generated.h"

class FArchive;
class FProperty;
class UEdGraph;
class UEdGraphPin;
class UObject;
struct FBPVariableDescription;
struct FEdGraphPinType;

UENUM()
enum class EGetNodeVariation
{
	Pure,
	ValidatedObject,
	Branch,
};

UCLASS()
class BLUEPRINTGRAPH_API UK2Node_VariableGet : public UK2Node_Variable
{
	GENERATED_UCLASS_BODY()

public:

	//~ Begin UObject Interface
	virtual void Serialize(FArchive& Ar) override;
	//~ End UObject Interface

	//~ Begin UEdGraphNode Interface
	virtual void AllocateDefaultPins() override;
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void GetNodeContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	virtual bool IncludeParentNodeContextMenu() const override { return true; }
	virtual void ValidateNodeDuringCompilation(class FCompilerResultsLog& MessageLog) const override;
	//~ End UEdGraphNode Interface

	//~ Begin K2Node Interface
	virtual bool IsNodePure() const override { return CurrentVariation == EGetNodeVariation::Pure; }
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual bool ShouldShowNodeProperties() const override { return true; }
	virtual class FNodeHandlingFunctor* CreateNodeHandler(class FKismetCompilerContext& CompilerContext) const override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	//~ End K2Node Interface

	static FText GetPropertyTooltip(const FProperty* VariableProperty);
	static FText GetBlueprintVarTooltip(const FBPVariableDescription& VarDesc);

private:

	/** Adds pins required for the node to function in a impure manner */
	void CreateImpurePins(TArray<UEdGraphPin*>* InOldPinsPtr);

	/** Flips the node's purity (adding/removing exec pins as needed). */
	void TogglePurity(EGetNodeVariation BoundVariation);

	/** Constructing FText strings can be costly, so we cache the node's title/tooltip */
	FNodeTextCache CachedTooltip;
	FNodeTextCache CachedNodeTitle;

	UPROPERTY()
	bool bIsPureGet_DEPRECATED = true;

	UPROPERTY()
	EGetNodeVariation CurrentVariation;
};

