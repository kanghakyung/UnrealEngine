﻿// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayInteractionContext.h"
#include "GameplayInteractionSmartObjectBehaviorDefinition.h"
#include "Engine/World.h"
#include "StateTreeExecutionContext.h"
#include "VisualLogger/VisualLogger.h"
#include "GameplayInteractionStateTreeSchema.h"
#include "SmartObjectSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameplayInteractionContext)

bool FGameplayInteractionContext::Activate(const UGameplayInteractionSmartObjectBehaviorDefinition& InDefinition)
{
	Definition = &InDefinition;
	check(Definition);
	
	const FStateTreeReference& StateTreeReference = Definition->StateTreeReference;
	const UStateTree* StateTree = StateTreeReference.GetStateTree();

	if (!IsValid())
	{
		UE_LOG(LogGameplayInteractions, Error, TEXT("Failed to activate interaction. Context is not properly setup."));
		return false;
	}
	
	if (StateTree == nullptr)
	{
		UE_VLOG_UELOG(ContextActor, LogGameplayInteractions, Error,
			TEXT("Failed to activate interaction for %s. Definition %s doesn't point to a valid StateTree asset."),
			*GetNameSafe(ContextActor),
			*Definition.GetFullName());
		return false;
	}

	FStateTreeExecutionContext StateTreeContext(*ContextActor, *StateTree, StateTreeInstanceData);

	if (!StateTreeContext.IsValid())
	{
		UE_VLOG_UELOG(ContextActor, LogGameplayInteractions, Error,
			TEXT("Failed to activate interaction for %s. Unable to initialize StateTree execution context for StateTree asset: %s."),
			*GetNameSafe(ContextActor),
			*StateTree->GetFullName());
		return false;
	}

	if (!ValidateSchema(StateTreeContext))
	{
		return false;
	}
	
	if (!SetContextRequirements(StateTreeContext))
	{
		UE_VLOG_UELOG(ContextActor, LogGameplayInteractions, Error,
			TEXT("Failed to activate interaction for %s. Unable to provide all external data views for StateTree asset: %s."),
			*GetNameSafe(ContextActor),
			*StateTree->GetFullName());
		return false;
	}

	// Set slot user actor
	USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(ContextActor->GetWorld());
	check(SmartObjectSubsystem);
	SmartObjectSubsystem->MutateSlotData(ClaimedHandle.SlotHandle, [this, SmartObjectSubsystem](const FSmartObjectSlotView& SlotView)
	{
		if (FGameplayInteractionSlotUserData* UserData = SlotView.GetMutableStateDataPtr<FGameplayInteractionSlotUserData>())
		{
			UserData->UserActor = ContextActor;
		}
		else
		{
			SmartObjectSubsystem->AddSlotData(ClaimedHandle, FConstStructView::Make(FGameplayInteractionSlotUserData(ContextActor)));
		}
	});

	// Start State Tree
	StateTreeContext.Start(&StateTreeReference.GetParameters());
	
	return true;
}

bool FGameplayInteractionContext::Tick(const float DeltaTime)
{
	if (Definition == nullptr || !IsValid())
	{
		return false;
	}
	
	const FStateTreeReference& StateTreeReference = Definition->StateTreeReference;
	const UStateTree* StateTree = StateTreeReference.GetStateTree();
	if (StateTree == nullptr)
	{
		return false;
	}
	
	FStateTreeExecutionContext StateTreeContext(*ContextActor, *StateTree, StateTreeInstanceData);

	EStateTreeRunStatus RunStatus = EStateTreeRunStatus::Unset;
	if (SetContextRequirements(StateTreeContext))
	{
		RunStatus = StateTreeContext.Tick(DeltaTime);
	}

	LastRunStatus = RunStatus;

	return RunStatus == EStateTreeRunStatus::Running;
}

void FGameplayInteractionContext::Deactivate()
{
	if (Definition == nullptr || ContextActor == nullptr)
	{
		return;
	}

	const FStateTreeReference& StateTreeReference = Definition->StateTreeReference;
	const UStateTree* StateTree = StateTreeReference.GetStateTree();
	if (StateTree == nullptr)
	{
		UE_VLOG_UELOG(ContextActor, LogGameplayInteractions, Error,
			TEXT("Failed to deactivate interaction for %s. Definition %s doesn't point to a valid StateTree asset."),
			*GetNameSafe(ContextActor),
			*Definition.GetFullName());
		
		return;
	}
	
	FStateTreeExecutionContext StateTreeContext(ContextActor, StateTree, StateTreeInstanceData);
	if (SetContextRequirements(StateTreeContext))
	{
		StateTreeContext.Stop();
	}

	// Remove user
	const USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(ContextActor->GetWorld());
	check(SmartObjectSubsystem);
	SmartObjectSubsystem->MutateSlotData(ClaimedHandle.SlotHandle, [](const FSmartObjectSlotView& SlotView)
	{
		if (FGameplayInteractionSlotUserData* UserData = SlotView.GetMutableStateDataPtr<FGameplayInteractionSlotUserData>())
		{
			UserData->UserActor = nullptr;
		}
	});
}

void FGameplayInteractionContext::SendEvent(const FGameplayTag Tag, const FConstStructView Payload, const FName Origin)
{
	if (Definition == nullptr || !IsValid())
	{
		return;
	}

	const FStateTreeReference& StateTreeReference = Definition->StateTreeReference;
	const UStateTree* StateTree = StateTreeReference.GetStateTree();
	if (!StateTree)
	{
		return;
	}
	FStateTreeMinimalExecutionContext StateTreeContext(ContextActor, StateTree, StateTreeInstanceData);
	StateTreeContext.SendEvent(Tag, Payload, Origin);
}

bool FGameplayInteractionContext::ValidateSchema(const FStateTreeExecutionContext& StateTreeContext) const
{
	// Ensure that the actor and smart object match the schema.
	const UGameplayInteractionStateTreeSchema* Schema = Cast<UGameplayInteractionStateTreeSchema>(StateTreeContext.GetStateTree()->GetSchema());
	if (Schema == nullptr)
	{
		UE_VLOG_UELOG(ContextActor, LogGameplayInteractions, Error,
			TEXT("Failed to activate interaction for %s. Expecting %s schema for StateTree asset: %s."),
			*GetNameSafe(ContextActor),
			*GetNameSafe(UGameplayInteractionStateTreeSchema::StaticClass()),
			*GetFullNameSafe(StateTreeContext.GetStateTree()));

		return false;
	}
	if (!ContextActor || !ContextActor->IsA(Schema->GetContextActorClass()))
	{
		UE_VLOG_UELOG(ContextActor, LogGameplayInteractions, Error,
			TEXT("Failed to activate interaction for %s. Expecting Actor to be of type %s (found %s) for StateTree asset: %s."),
			*GetNameSafe(ContextActor),
			*GetNameSafe(Schema->GetContextActorClass()),
			*GetNameSafe(ContextActor ? ContextActor->GetClass() : nullptr),
			*GetFullNameSafe(StateTreeContext.GetStateTree()));

		return false;
	}
	if (!SmartObjectActor || !SmartObjectActor->IsA(Schema->GetSmartObjectActorClass()))
	{
		UE_VLOG_UELOG(ContextActor, LogGameplayInteractions, Error,
			TEXT("Failed to activate interaction for %s. SmartObject Actor to be of type %s (found %s) for StateTree asset: %s."),
			*GetNameSafe(ContextActor),
			*GetNameSafe(Schema->GetSmartObjectActorClass()),
			*GetNameSafe(SmartObjectActor ? SmartObjectActor->GetClass() : nullptr),
			*GetFullNameSafe(StateTreeContext.GetStateTree()));

		return false;
	}

	return true;
}

bool FGameplayInteractionContext::SetContextRequirements(FStateTreeExecutionContext& StateTreeContext)
{
	if (!StateTreeContext.IsValid())
	{
		return false;
	}

	if (!::IsValid(Definition))
	{
		return false;
	}
	
	StateTreeContext.SetContextDataByName(UE::GameplayInteraction::Names::ContextActor, FStateTreeDataView(ContextActor));
	StateTreeContext.SetContextDataByName(UE::GameplayInteraction::Names::SmartObjectActor, FStateTreeDataView(SmartObjectActor));
    StateTreeContext.SetContextDataByName(UE::GameplayInteraction::Names::SmartObjectClaimedHandle, FStateTreeDataView(FStructView::Make(ClaimedHandle)));
	StateTreeContext.SetContextDataByName(UE::GameplayInteraction::Names::SlotEntranceHandle, FStateTreeDataView(FStructView::Make(SlotEntranceHandle)));
	StateTreeContext.SetContextDataByName(UE::GameplayInteraction::Names::AbortContext, FStateTreeDataView(FStructView::Make(AbortContext)));

	checkf(ContextActor != nullptr, TEXT("Should never reach this point with an invalid ContextActor since it is required to get a valid StateTreeContext."));
	const UWorld* World = ContextActor->GetWorld();
	
	StateTreeContext.SetCollectExternalDataCallback(FOnCollectStateTreeExternalData::CreateLambda(
		[World, ContextActor = ContextActor]
		(const FStateTreeExecutionContext& Context, const UStateTree* StateTree, TArrayView<const FStateTreeExternalDataDesc> ExternalDescs, TArrayView<FStateTreeDataView> OutDataViews)
		{
			check(ExternalDescs.Num() == OutDataViews.Num());
			for (int32 Index = 0; Index < ExternalDescs.Num(); Index++)
			{
				const FStateTreeExternalDataDesc& Desc = ExternalDescs[Index];
				if (Desc.Struct != nullptr)
				{
					if (World != nullptr && Desc.Struct->IsChildOf(UWorldSubsystem::StaticClass()))
					{
						UWorldSubsystem* Subsystem = World->GetSubsystemBase(Cast<UClass>(const_cast<UStruct*>(ToRawPtr(Desc.Struct))));
						OutDataViews[Index] = FStateTreeDataView(Subsystem);
					}
					else if (Desc.Struct->IsChildOf(AActor::StaticClass()))
					{
						OutDataViews[Index] = FStateTreeDataView(ContextActor);
					}
				}
			}
				
			return true;
		})
	);

	return StateTreeContext.AreContextDataViewsValid();
}
