// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVMPropertyPath.h"
#include "MVVMBlueprintPin.h"
#include "View/MVVMViewTypes.h"

#include "MVVMBlueprintViewEvent.generated.h"

struct FEdGraphEditAction;
class UEdGraph;
class UK2Node;
class UMVVMK2Node_AreSourcesValidForEvent;
class UWidgetBlueprint;

/**
 * Binding for an event that MVVM will listen too. Does not imply 
 * the MVVM graph itself will use events.
 * 
 * Ex: UButton::OnClick 
 */
UCLASS(Within = MVVMBlueprintView)
class MODELVIEWVIEWMODELBLUEPRINT_API UMVVMBlueprintViewEvent : public UObject
{
	GENERATED_BODY()

public:
	enum class EMessageType : uint8
	{
		Info,
		Warning,
		Error
	};

	struct FMessage
	{
		FText MessageText;
		EMessageType MessageType;
	};

	/** @return true if the property path contains a multicast delegate property. */
	static bool Supports(const UWidgetBlueprint* WidgetBlueprint, const FMVVMBlueprintPropertyPath& PropertyPath);

public:
	/** Whether the event is enabled or disabled by default. The instance may enable the event at runtime. */
	UPROPERTY(EditAnywhere, Category = "Viewmodel")
	bool bEnabled = true;

	/** The event is visible in the editor, but is not compiled and cannot be used at runtime. */
	UPROPERTY(EditAnywhere, Category = "Viewmodel")
	bool bCompile = true;

public:
	const FMVVMBlueprintPropertyPath& GetEventPath() const
	{
		return EventPath;
	}
	void SetEventPath(FMVVMBlueprintPropertyPath EventPath);

	const FMVVMBlueprintPropertyPath& GetDestinationPath() const
	{
		return DestinationPath;
	}
	void SetDestinationPath(FMVVMBlueprintPropertyPath DestinationPath);

public:
	UEdGraph* GetWrapperGraph() const
	{
		return CachedWrapperGraph;
	}

	FName GetWrapperGraphName() const
	{
		return GraphName;
	}

	enum ERemoveWrapperGraphParam
	{
		RemoveConversionFunctionCurrentValues, // when removing or changing the conversion function, we want to remove all the conversion function parameters
		LeaveConversionFunctionCurrentValues // when we remove the wrapper graph because the event path has changed, we want to keep the conversion function parameters
	};
	void RemoveWrapperGraph(ERemoveWrapperGraphParam ActionForCurrentValues = RemoveConversionFunctionCurrentValues);

	UK2Node* GetWrapperNode() const
	{
		return CachedWrapperNode;
	}

	UEdGraph* GetOrCreateWrapperGraph();

	void RecreateWrapperGraph();

public:
	TArrayView<const FMVVMBlueprintPin> GetPins() const
	{
		return SavedPins;
	}

	/** Generates SavedPins from the wrapper graph, if it exists. */
	void SavePinValues();
	/** Keep the orphaned pins. Add the missing pins. */
	void UpdatePinValues();
	/** Keep the orphaned pins. Add the missing pins. */
	bool HasOrphanedPin() const;
	/** Event sources are tested at runtime to check if they are valid. */
	void UpdateEventKey(FMVVMViewClass_EventKey EventKey);

	UEdGraphPin* GetOrCreateGraphPin(const FMVVMBlueprintPinId& Pin);

	FMVVMBlueprintPropertyPath GetPinPath(const FMVVMBlueprintPinId& Pin) const;
	void SetPinPath(const FMVVMBlueprintPinId& Pin, const FMVVMBlueprintPropertyPath& Path);
	// To set a pin when loading the asset (no graph generation)
	void SetPinPathNoGraphGeneration(const FMVVMBlueprintPinId& Pin, const FMVVMBlueprintPropertyPath& Path);

	FSimpleMulticastDelegate OnWrapperGraphModified;

public:
	TArray<FText> GetCompilationMessages(EMessageType InMessageType) const;
	bool HasCompilationMessage(EMessageType InMessageType) const;
	void AddCompilationToBinding(FMessage MessageToAdd) const;
	void ResetCompilationMessages();

public:
	/** 
	 * Get a string that identifies this event. 
	 */
	FText GetDisplayName(bool bUseDisplayName) const;

	/**
	 * Get a string that identifies this event and is specifically formatted for search. 
	 * This includes the display name and variable name of all fields and widgets, as well as all function keywords.
	 * For use in the UI, use GetDisplayNameString()
	 */
	FString GetSearchableString() const;

public:
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChainEvent) override;

private:
	static const UFunction* GetEventSignature(const UWidgetBlueprint* WidgetBlueprint, const FMVVMBlueprintPropertyPath& PropertyPath);
	const UFunction* GetEventSignature() const;
	void HandleGraphChanged(const FEdGraphEditAction& Action);
	void HandleUserDefinedPinRenamed(UK2Node* InNode, FName OldPinName, FName NewPinName);
	UWidgetBlueprint* GetWidgetBlueprintInternal() const;
	void SetCachedWrapperGraphInternal(UEdGraph* Graph, UK2Node* Node, UMVVMK2Node_AreSourcesValidForEvent* SourceNode);
	UEdGraph* CreateWrapperGraphInternal();
	void LoadPinValuesInternal();
	void UpdateEventKeyInternal();

private:
	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	FMVVMBlueprintPropertyPath EventPath;

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	FMVVMBlueprintPropertyPath DestinationPath;

	/**
	 * The pin that are modified and we saved data.
	 * The data may not be modified. We used the default value of the K2Node in that case.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	TArray<FMVVMBlueprintPin> SavedPins;

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	FName GraphName;

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	FMVVMViewClass_EventKey EventKey;

	mutable TArray<FMessage> Messages;
	bool bLoadingPins = false;

	UPROPERTY(Transient, DuplicateTransient)
	mutable TObjectPtr<UEdGraph> CachedWrapperGraph;

	UPROPERTY(Transient, DuplicateTransient)
	mutable TObjectPtr<UK2Node> CachedWrapperNode;

	UPROPERTY(Transient, DuplicateTransient)
	mutable TObjectPtr<UMVVMK2Node_AreSourcesValidForEvent> CachedSourceValidNode;

	FDelegateHandle OnGraphChangedHandle;
	FDelegateHandle OnUserDefinedPinRenamedHandle;

	bool bNeedsToRegenerateChildren;
};
