// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeState.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeConditionBase.h"
#include "StateTreeConsiderationBase.h"
#include "StateTreeTaskBase.h"
#include "StateTreeDelegates.h"
#include "StateTreePropertyHelpers.h"
#include "Customizations/StateTreeEditorNodeUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeState)


//////////////////////////////////////////////////////////////////////////
// FStateTreeStateParameters

void FStateTreeStateParameters::RemoveUnusedOverrides()
{
	// Remove overrides that do not exists anymore
	if (!PropertyOverrides.IsEmpty())
	{
		if (const UPropertyBag* Bag = Parameters.GetPropertyBagStruct())
		{
			for (TArray<FGuid>::TIterator It = PropertyOverrides.CreateIterator(); It; ++It)
			{
				if (!Bag->FindPropertyDescByID(*It))
				{
					It.RemoveCurrentSwap();
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// FStateTreeTransition

FStateTreeTransition::FStateTreeTransition(const EStateTreeTransitionTrigger InTrigger, const EStateTreeTransitionType InType, const UStateTreeState* InState)
	: Trigger(InTrigger)
{
	State = InState ? InState->GetLinkToState() : FStateTreeStateLink(InType);
}

FStateTreeTransition::FStateTreeTransition(const EStateTreeTransitionTrigger InTrigger, const FGameplayTag InEventTag, const EStateTreeTransitionType InType, const UStateTreeState* InState)
	: Trigger(InTrigger)
	, RequiredEvent{InEventTag}
{
	State = InState ? InState->GetLinkToState() : FStateTreeStateLink(InType);
}

void FStateTreeTransition::PostSerialize(const FArchive& Ar)
{
#if WITH_EDITORONLY_DATA
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (EventTag_DEPRECATED.IsValid())
	{
		RequiredEvent.Tag = EventTag_DEPRECATED;
		EventTag_DEPRECATED = FGameplayTag();
	}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif // WITH_EDITORONLY_DATA
}

//////////////////////////////////////////////////////////////////////////
// UStateTreeState

UStateTreeState::UStateTreeState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ID(FGuid::NewGuid())
{
	Parameters.ID = FGuid::NewGuid();
}

UStateTreeState::~UStateTreeState()
{
	UE::StateTree::Delegates::OnPostCompile.RemoveAll(this);
}

void UStateTreeState::PostInitProperties()
{
	Super::PostInitProperties();
	
	UE::StateTree::Delegates::OnPostCompile.AddUObject(this, &UStateTreeState::OnTreeCompiled);
}

void UStateTreeState::OnTreeCompiled(const UStateTree& StateTree)
{
	if (&StateTree == LinkedAsset)
	{
		UpdateParametersFromLinkedSubtree();
	}
}

void UStateTreeState::PreEditChange(FEditPropertyChain& PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	const FStateTreeEditPropertyPath PropertyChainPath(PropertyAboutToChange);

	static const FStateTreeEditPropertyPath StateTypePath(UStateTreeState::StaticClass(), TEXT("Type"));

	if (PropertyChainPath.IsPathExact(StateTypePath))
	{
		// If transitioning from linked state, reset the parameters
		if (Type == EStateTreeStateType::Linked
			|| Type == EStateTreeStateType::LinkedAsset)
		{
			Parameters.ResetParametersAndOverrides();
		}
	}
}

void UStateTreeState::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	const FStateTreeEditPropertyPath ChangePropertyPath(PropertyChangedEvent);

	static const FStateTreeEditPropertyPath StateNamePath(UStateTreeState::StaticClass(), TEXT("Name"));
	static const FStateTreeEditPropertyPath StateTypePath(UStateTreeState::StaticClass(), TEXT("Type"));
	static const FStateTreeEditPropertyPath SelectionBehaviorPath(UStateTreeState::StaticClass(), TEXT("SelectionBehavior"));
	static const FStateTreeEditPropertyPath StateLinkedSubtreePath(UStateTreeState::StaticClass(), TEXT("LinkedSubtree"));
	static const FStateTreeEditPropertyPath StateLinkedAssetPath(UStateTreeState::StaticClass(), TEXT("LinkedAsset"));
	static const FStateTreeEditPropertyPath StateParametersPath(UStateTreeState::StaticClass(), TEXT("Parameters"));
	static const FStateTreeEditPropertyPath StateTasksPath(UStateTreeState::StaticClass(), TEXT("Tasks"));
	static const FStateTreeEditPropertyPath StateEnterConditionsPath(UStateTreeState::StaticClass(), TEXT("EnterConditions"));
	static const FStateTreeEditPropertyPath StateConsiderationsPath(UStateTreeState::StaticClass(), TEXT("Considerations"));
	static const FStateTreeEditPropertyPath StateTransitionsPath(UStateTreeState::StaticClass(), TEXT("Transitions"));
	static const FStateTreeEditPropertyPath StateTransitionsConditionsPath(UStateTreeState::StaticClass(), TEXT("Transitions.Conditions"));
	static const FStateTreeEditPropertyPath StateTransitionsIDPath(UStateTreeState::StaticClass(), TEXT("Transitions.ID"));


	// Broadcast name changes so that the UI can update.
	if (ChangePropertyPath.IsPathExact(StateNamePath))
	{
		const UStateTree* StateTree = GetTypedOuter<UStateTree>();
		if (ensure(StateTree))
		{
			UE::StateTree::Delegates::OnIdentifierChanged.Broadcast(*StateTree);
		}
	}

	if (ChangePropertyPath.IsPathExact(SelectionBehaviorPath))
	{
		// Broadcast selection type changes so that the UI can update.
		const UStateTree* StateTree = GetTypedOuter<UStateTree>();
		if (ensure(StateTree))
		{
			UE::StateTree::Delegates::OnIdentifierChanged.Broadcast(*StateTree);
		}
	}
	
	if (ChangePropertyPath.IsPathExact(StateTypePath))
	{
		// Reset Selection Behavior back to Try Enter State for group and linked types
		if (Type == EStateTreeStateType::Group || Type == EStateTreeStateType::Linked || Type == EStateTreeStateType::LinkedAsset)
		{
			SelectionBehavior = EStateTreeStateSelectionBehavior::TryEnterState;
		}

		// Remove any tasks when they are not used.
		if (Type == EStateTreeStateType::Group || Type == EStateTreeStateType::Linked || Type == EStateTreeStateType::LinkedAsset)
		{
			Tasks.Reset();
		}

		// If transitioning from linked state, reset the linked state.
		if (Type != EStateTreeStateType::Linked)
		{
			LinkedSubtree = FStateTreeStateLink();
		}
		if (Type != EStateTreeStateType::LinkedAsset)
		{
			LinkedAsset = nullptr;
		}

		if (Type == EStateTreeStateType::Linked
			|| Type == EStateTreeStateType::LinkedAsset)
		{
			// Linked parameter layout is fixed, and copied from the linked target state.
			Parameters.bFixedLayout = true;
			UpdateParametersFromLinkedSubtree();
		}
		else
		{
			// Other layouts can be edited
			Parameters.bFixedLayout = false;
		}
	}

	// When switching to new state, update the parameters.
	if (ChangePropertyPath.IsPathExact(StateLinkedSubtreePath))
	{
		if (Type == EStateTreeStateType::Linked)
		{
			UpdateParametersFromLinkedSubtree();
		}
	}
	
	if (ChangePropertyPath.IsPathExact(StateLinkedAssetPath))
	{
		if (Type == EStateTreeStateType::LinkedAsset)
		{
			UpdateParametersFromLinkedSubtree();
		}
	}

	// Broadcast subtree parameter layout edits so that the linked states can adapt, and bindings can update.
	if (ChangePropertyPath.IsPathExact(StateParametersPath))
	{
		const UStateTree* StateTree = GetTypedOuter<UStateTree>();
		if (ensure(StateTree))
		{
			UE::StateTree::Delegates::OnStateParametersChanged.Broadcast(*StateTree, ID);
		}
	}

	// Reset delay on completion transitions
	if (ChangePropertyPath.ContainsPath(StateTransitionsPath))
	{
		const int32 TransitionsIndex = ChangePropertyPath.GetPropertyArrayIndex(StateTransitionsPath);
		if (Transitions.IsValidIndex(TransitionsIndex))
		{
			FStateTreeTransition& Transition = Transitions[TransitionsIndex];

			if (EnumHasAnyFlags(Transition.Trigger, EStateTreeTransitionTrigger::OnStateCompleted))
			{
				Transition.bDelayTransition = false;
			}
		}
	}

	// Set default state to root and Id on new transitions.
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd)
	{
		if (ChangePropertyPath.IsPathExact(StateTransitionsPath))
		{
			const int32 TransitionsIndex = ChangePropertyPath.GetPropertyArrayIndex(StateTransitionsPath);
			if (Transitions.IsValidIndex(TransitionsIndex))
			{
				FStateTreeTransition& Transition = Transitions[TransitionsIndex];
				Transition.Trigger = EStateTreeTransitionTrigger::OnStateCompleted;
				const UStateTreeState* RootState = GetRootState();
				Transition.State = RootState->GetLinkToState();
				Transition.ID = FGuid::NewGuid();
			}
		}
	}

	if (UStateTreeEditorData* TreeData = GetTypedOuter<UStateTreeEditorData>())
	{
		UE::StateTree::PropertyHelpers::DispatchPostEditToNodes(*this, PropertyChangedEvent, *TreeData);
	}
}

void UStateTreeState::PostLoad()
{
	Super::PostLoad();

	// Make sure state has transactional flags to make it work with undo (to fix a bug where root states were created without this flag).
	if (!HasAnyFlags(RF_Transactional))
	{
		SetFlags(RF_Transactional);
	}

#if WITH_EDITORONLY_DATA
	const int32 CurrentVersion = GetLinkerCustomVersion(FStateTreeCustomVersion::GUID);
	if (CurrentVersion < FStateTreeCustomVersion::AddedTransitionIds)
	{
		// Make guids for transitions. These need to be deterministic when upgrading because of cooking.
		for (int32 Index = 0; Index < Transitions.Num(); Index++)
		{
			FStateTreeTransition& Transition = Transitions[Index];
			Transition.ID = FGuid::NewDeterministicGuid(GetPathName(), Index);
		}
	}

	if (CurrentVersion < FStateTreeCustomVersion::OverridableStateParameters)
	{
		// In earlier versions, all parameters were overwritten.
		if (const UPropertyBag* Bag = Parameters.Parameters.GetPropertyBagStruct())
		{
			for (const FPropertyBagPropertyDesc& Desc : Bag->GetPropertyDescs())
			{
				Parameters.PropertyOverrides.Add(Desc.ID);
			}
		}
	}

	if (CurrentVersion < FStateTreeCustomVersion::AddedCheckingParentsPrerequisites)
	{
		bCheckPrerequisitesWhenActivatingChildDirectly = false;
	}
	
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
	for (FStateTreeEditorNode& EnterConditionEditorNode : EnterConditions)
	{
		if (FStateTreeNodeBase* ConditionNode = EnterConditionEditorNode.Node.GetMutablePtr<FStateTreeNodeBase>())
		{
			UE::StateTreeEditor::EditorNodeUtils::ConditionalUpdateNodeInstanceData(EnterConditionEditorNode, *this);
			ConditionNode->PostLoad(EnterConditionEditorNode.GetInstance());
		}
	}

	for (FStateTreeEditorNode& ConsiderationEditorNode : Considerations)
	{
		if (FStateTreeNodeBase* ConsiderationNode = ConsiderationEditorNode.Node.GetMutablePtr<FStateTreeNodeBase>())
		{
			UE::StateTreeEditor::EditorNodeUtils::ConditionalUpdateNodeInstanceData(ConsiderationEditorNode, *this);
			ConsiderationNode->PostLoad(ConsiderationEditorNode.GetInstance());
		}
	}

	for (FStateTreeEditorNode& TaskEditorNode : Tasks)
	{
		if (FStateTreeNodeBase* TaskNode = TaskEditorNode.Node.GetMutablePtr<FStateTreeNodeBase>())
		{
			UE::StateTreeEditor::EditorNodeUtils::ConditionalUpdateNodeInstanceData(TaskEditorNode, *this);
			TaskNode->PostLoad(TaskEditorNode.GetInstance());
		}
	}

	if (FStateTreeNodeBase* SingleTaskNode = SingleTask.Node.GetMutablePtr<FStateTreeNodeBase>())
	{
		UE::StateTreeEditor::EditorNodeUtils::ConditionalUpdateNodeInstanceData(SingleTask, *this);
		SingleTaskNode->PostLoad(SingleTask.GetInstance());
	}

	for (FStateTreeTransition& Transition : Transitions)
	{
		for (FStateTreeEditorNode& TransitionConditionEditorNode : Transition.Conditions)
		{
			if (FStateTreeNodeBase* ConditionNode = TransitionConditionEditorNode.Node.GetMutablePtr<FStateTreeNodeBase>())
			{
				UE::StateTreeEditor::EditorNodeUtils::ConditionalUpdateNodeInstanceData(TransitionConditionEditorNode, *this);
				ConditionNode->PostLoad(TransitionConditionEditorNode.GetInstance());
			}
		}
	}
#endif // WITH_EDITOR
}

void UStateTreeState::UpdateParametersFromLinkedSubtree()
{
	if (const FInstancedPropertyBag* DefaultParameters = GetDefaultParameters())
	{
		Parameters.Parameters.MigrateToNewBagInstanceWithOverrides(*DefaultParameters, Parameters.PropertyOverrides);
		Parameters.RemoveUnusedOverrides();
	}
	else
	{
		Parameters.ResetParametersAndOverrides();
	}
}

void UStateTreeState::SetParametersPropertyOverridden(const FGuid PropertyID, const bool bIsOverridden)
{
	if (bIsOverridden)
	{
		Parameters.PropertyOverrides.AddUnique(PropertyID);
	}
	else
	{
		Parameters.PropertyOverrides.Remove(PropertyID);
		UpdateParametersFromLinkedSubtree();

		// Remove binding when override is removed.
		if (UStateTreeEditorData* EditorData = GetTypedOuter<UStateTreeEditorData>())
		{
			if (FStateTreeEditorPropertyBindings* Bindings = EditorData->GetPropertyEditorBindings())
			{
				if (const UPropertyBag* ParametersBag = Parameters.Parameters.GetPropertyBagStruct())
				{
					if (const FPropertyBagPropertyDesc* Desc = ParametersBag->FindPropertyDescByID(PropertyID))
					{
						check(Desc->CachedProperty);

						EditorData->Modify();

						FPropertyBindingPath Path(Parameters.ID, Desc->CachedProperty->GetFName());
						Bindings->RemoveBindings(Path);
					}
				}
			}
		}
	}
}

const FInstancedPropertyBag* UStateTreeState::GetDefaultParameters() const
{
	if (Type == EStateTreeStateType::Linked)
	{
		if (const UStateTreeEditorData* TreeData = GetTypedOuter<UStateTreeEditorData>())
		{
			if (const UStateTreeState* LinkTargetState = TreeData->GetStateByID(LinkedSubtree.ID))
			{
				return &LinkTargetState->Parameters.Parameters;
			}
		}
	}
	else if (Type == EStateTreeStateType::LinkedAsset)
	{
		if (LinkedAsset)
		{
			return &LinkedAsset->GetDefaultParameters();
		}
	}

	return nullptr;
}

const UStateTreeState* UStateTreeState::GetRootState() const
{
	const UStateTreeState* RootState = this;
	while (RootState->Parent != nullptr)
	{
		RootState = RootState->Parent;
	}
	return RootState;
}

const UStateTreeState* UStateTreeState::GetNextSiblingState() const
{
	if (!Parent)
	{
		return nullptr;
	}
	for (int32 ChildIdx = 0; ChildIdx < Parent->Children.Num(); ChildIdx++)
	{
		if (Parent->Children[ChildIdx] == this)
		{
			const int NextIdx = ChildIdx + 1;

			// Select the next enabled sibling
			if (NextIdx < Parent->Children.Num() && Parent->Children[NextIdx]->bEnabled)
			{
				return Parent->Children[NextIdx];
			}
			break;
		}
	}
	return nullptr;
}

const UStateTreeState* UStateTreeState::GetNextSelectableSiblingState() const
{
	if (!Parent)
	{
		return nullptr;
	}

	const int32 StartChildIndex = Parent->Children.IndexOfByKey(this);
	if (StartChildIndex == INDEX_NONE)
	{
		return nullptr;
	}
	
	for (int32 ChildIdx = StartChildIndex + 1; ChildIdx < Parent->Children.Num(); ChildIdx++)
	{
		// Select the next enabled and selectable sibling
		const UStateTreeState* State =Parent->Children[ChildIdx];
		if (State->SelectionBehavior != EStateTreeStateSelectionBehavior::None
			&& State->bEnabled)
		{
			return State;
		}
	}
	
	return nullptr;
}

FString UStateTreeState::GetPath() const
{
	TArray<const UStateTreeState*> States;
	for (const UStateTreeState* CurrState = this; CurrState; CurrState = CurrState->Parent)
	{
		States.Add(CurrState);
	}
	Algo::Reverse(States);
	
	FStringBuilderBase Result;
	for (const UStateTreeState* CurrState : States)
	{
		if (Result.Len() > 0)
		{
			Result.Append(TEXT("/"));
		}
		Result.Append(CurrState->Name.ToString());
	}

	return Result.ToString();
}

FStateTreeStateLink UStateTreeState::GetLinkToState() const
{
	FStateTreeStateLink Link(EStateTreeTransitionType::GotoState);
	Link.Name = Name;
	Link.ID = ID;
	return Link;
}

TSubclassOf<UStateTreeSchema> UStateTreeState::GetSchema() const
{
	if (const UStateTreeEditorData* EditorData = GetTypedOuter<UStateTreeEditorData>())
	{
		if (EditorData->Schema)
		{
			return EditorData->Schema->GetClass();
		}
	}
	return nullptr;
}

void UStateTreeState::SetLinkedState(FStateTreeStateLink InStateLink)
{
	check(Type == EStateTreeStateType::Linked);
	LinkedSubtree = InStateLink;

	Tasks.Reset();
	LinkedAsset = nullptr;
	Parameters.bFixedLayout = true;
	UpdateParametersFromLinkedSubtree();
	SelectionBehavior = EStateTreeStateSelectionBehavior::TryEnterState;
}

void UStateTreeState::SetLinkedStateAsset(UStateTree* InLinkedAsset)
{
	check(Type == EStateTreeStateType::LinkedAsset);
	LinkedAsset = InLinkedAsset;

	Tasks.Reset();
	LinkedSubtree = FStateTreeStateLink();
	Parameters.bFixedLayout = true;
	UpdateParametersFromLinkedSubtree();
	SelectionBehavior = EStateTreeStateSelectionBehavior::TryEnterState;
}