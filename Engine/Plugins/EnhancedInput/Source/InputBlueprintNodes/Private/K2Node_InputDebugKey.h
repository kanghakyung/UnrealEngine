// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BlueprintNodeSpawner.h"
#include "EdGraph/EdGraphNodeUtils.h"
#include "K2Node.h"
#include "K2Node_EventNodeInterface.h"

#include "K2Node_InputDebugKey.generated.h"

namespace ENodeTitleType { enum Type : int; }
struct FBlueprintNodeSignature;

class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;

/**
 * A node spawner which will check if there is already adebug key event node (UInputDebugKeyEventNodeSpawner)
 * before spawning a new one in the graph. This makes the behavior of debug key nodes the same as other
 * event nodes. 
 */
UCLASS(Transient)
class UInputDebugKeyEventNodeSpawner final : public UBlueprintNodeSpawner
{
	GENERATED_BODY()
public:
	static UInputDebugKeyEventNodeSpawner* Create(TSubclassOf<UEdGraphNode> const NodeClass, const FKey& DebugKey);
private:
	virtual UEdGraphNode* Invoke(UEdGraph* ParentGraph, FBindingSet const& Bindings, FVector2D const Location) const override;

	UK2Node* FindExistingNode(const UBlueprint* Blueprint) const;

	UPROPERTY()
	FKey DebugKey = EKeys::Invalid;
};

UCLASS()
class INPUTBLUEPRINTNODES_API UK2Node_InputDebugKey : public UK2Node, public IK2Node_EventNodeInterface
{
	GENERATED_UCLASS_BODY()

	// The key that is bound to this debug event. Pressing this key while the game is running will trigger this node's events.
	UPROPERTY(VisibleDefaultsOnly, Category="Input")
	FKey InputKey;

	// Should the binding execute even when the game is paused
	UPROPERTY(EditAnywhere, Category="Input")
	uint32 bExecuteWhenPaused:1;

	// Does this binding require the control key on PC or the command key on Mac to be held
	UPROPERTY(EditAnywhere, Category="Modifier")
	uint32 bControl:1;

	// Does this binding require the alt key to be held
	UPROPERTY(EditAnywhere, Category="Modifier")
	uint32 bAlt:1;

	// Does this binding require the shift key to be held
	UPROPERTY(EditAnywhere, Category="Modifier")
	uint32 bShift:1;

	// Does this binding require the windows key on PC or the control key on Mac to be held
	UPROPERTY(EditAnywhere, Category="Modifier")
	uint32 bCommand:1;

	//~ Begin UObject Interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject Interface

	//~ Begin UK2Node Interface.
	virtual bool ShouldShowNodeProperties() const override { return true; }
	virtual void ValidateNodeDuringCompilation(class FCompilerResultsLog& MessageLog) const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual FBlueprintNodeSignature GetSignature() const override;
	//~ End UK2Node Interface

	//~ Begin UEdGraphNode Interface.
	virtual void AllocateDefaultPins() override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual bool IsCompatibleWithGraph(UEdGraph const* Graph) const override;
	//~ End UEdGraphNode Interface.

	//~ Begin IK2Node_EventNodeInterface Interface.
	virtual TSharedPtr<FEdGraphSchemaAction> GetEventNodeAction(const FText& ActionCategory) override;
	//~ End IK2Node_EventNodeInterface Interface.


	FText GetModifierText() const;

	FName GetModifierName() const;

	FText GetKeyText() const;

	/** Get the 'pressed' input pin */
	UEdGraphPin* GetPressedPin() const;

	/** Get the 'released' input pin */
	UEdGraphPin* GetReleasedPin() const;

	/** Get the 'Action Value' input pin */
	UEdGraphPin* GetActionValuePin() const;

private:
	/** Constructing FText strings can be costly, so we cache the node's title/tooltip */
	FNodeTextCache CachedTooltip;
	FNodeTextCache CachedNodeTitle;
};
