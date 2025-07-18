// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AcquiredResources.h"
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "Layout/Visibility.h"
#include "ISceneOutlinerColumn.h"
#include "UObject/ObjectKey.h"
#include "ISequencer.h"
#include "Engine/EngineBaseTypes.h"
#include "LevelEditorSequencerIntegration.generated.h"

class AActor;
class FExtender;
class FMenuBuilder;
class FSequencer;
class FObjectPostSaveContext;
class FObjectPreSaveContext;
class SLevelViewport;
class FUICommandList;
class IAssetViewport;
class ISequencer;
class ULevel;
class UToolMenu;
struct FPropertyAndParent;
struct FPilotedSpawnable;

struct FLevelEditorSequencerIntegrationOptions
{
	FLevelEditorSequencerIntegrationOptions()
		: bRequiresLevelEvents(true)
		, bRequiresActorEvents(false)
		, bForceRefreshDetails(true)
		, bAttachOutlinerColumns(true)
		, bActivateSequencerEdMode(true)
		, bSyncBindingsToActorLabels(true)
	{}

	bool bRequiresLevelEvents : 1;
	bool bRequiresActorEvents : 1;
	bool bForceRefreshDetails : 1;
	bool bAttachOutlinerColumns : 1;
	bool bActivateSequencerEdMode : 1;
	bool bSyncBindingsToActorLabels : 1;
};


class FLevelEditorSequencerBindingData : public TSharedFromThis<FLevelEditorSequencerBindingData>
{
public:
	FLevelEditorSequencerBindingData() 
		: bActorBindingsDirty(true)
		, bPropertyBindingsDirty(true)
	{}

	DECLARE_MULTICAST_DELEGATE(FActorBindingsDataChanged);
	DECLARE_MULTICAST_DELEGATE(FPropertyBindingsDataChanged);

	FActorBindingsDataChanged& OnActorBindingsDataChanged() { return ActorBindingsDataChanged; }
	FPropertyBindingsDataChanged& OnPropertyBindingsDataChanged() { return PropertyBindingsDataChanged; }

	FString GetLevelSequencesForActor(TWeakPtr<FSequencer> Sequencer, const AActor*);
	bool GetIsPropertyBound(TWeakPtr<FSequencer> Sequencer, const struct FPropertyAndParent&);

	bool bActorBindingsDirty;
	bool bPropertyBindingsDirty;

private:
	void UpdateActorBindingsData(TWeakPtr<FSequencer> InSequencer);
	void UpdatePropertyBindingsData(TWeakPtr<FSequencer> InSequencer);

	TMap< FObjectKey, FString > ActorBindingsMap;
	TMap< FObjectKey, TArray<FString> > PropertyBindingsMap;

	FActorBindingsDataChanged ActorBindingsDataChanged;
	FPropertyBindingsDataChanged PropertyBindingsDataChanged;
};

/**
* Tick function whose sole purpose is to update the gizmo position after any root motion may have run on selected characters at the end of the frame.
**/
USTRUCT()
struct FLevelEditorSequencerUpdateGizmoTickFunction : public FTickFunction
{
	GENERATED_USTRUCT_BODY()

	bool bSequencerEvaluated = false;

	/**
	* Abstract function to execute the tick.
	* @param DeltaTime - frame time to advance, in seconds.
	* @param TickType - kind of tick for this frame.
	* @param CurrentThread - thread we are executing on, useful to pass along as new tasks are created.
	* @param MyCompletionGraphEvent - completion event for this task. Useful for holding the completetion of this task until certain child tasks are complete.
	*/
	virtual void ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	/** Abstract function to describe this tick. Used to print messages about illegal cycles in the dependency graph. */
	virtual FString DiagnosticMessage() override;
	/** Function used to describe this tick for active tick reporting. **/
	virtual FName DiagnosticContext(bool bDetailed) override;
};

template<>
struct TStructOpsTypeTraits<FLevelEditorSequencerUpdateGizmoTickFunction> : public TStructOpsTypeTraitsBase2<FLevelEditorSequencerUpdateGizmoTickFunction>
{
	enum
	{
		WithCopy = false
	};
};


class SEQUENCER_API FLevelEditorSequencerIntegration
{
public:

	static FLevelEditorSequencerIntegration& Get();

	void Initialize(const FLevelEditorSequencerIntegrationOptions& Options);

	void AddSequencer(TSharedRef<ISequencer> InSequencer, const FLevelEditorSequencerIntegrationOptions& Options);

	void OnSequencerReceivedFocus(TSharedRef<ISequencer> InSequencer);

	void RemoveSequencer(TSharedRef<ISequencer> InSequencer);

	TArray<TWeakPtr<ISequencer>> GetSequencers();
	DECLARE_MULTICAST_DELEGATE(FOnSequencersChanged);
	FOnSequencersChanged& GetOnSequencersChanged() { return OnSequencersChanged; };

private:

	/** Called before the world is going to be saved. The sequencer puts everything back to its initial state. */
	void OnPreSaveWorld(UWorld* World, FObjectPreSaveContext ObjectSaveContext);

	/** Called after the world has been saved. The sequencer updates to the animated state. */
	void OnPostSaveWorld(UWorld* World, FObjectPostSaveContext ObjectSaveContext);

	/** Called before any number of external actors are going to be saved. The sequencer puts everything back to its initial state. */
	void OnPreSaveExternalActors(UWorld* World);

	/** Called after any number of external actors has been saved. The sequencer puts everything back to its initial state. */
	void OnPostSaveExternalActors(UWorld* World);

	/** Called before asset validation is run on assets. The sequencer puts everything back to its initial state. */
	void OnPreAssetValidation();
	
	/** Called after asset validation has finished. The sequencer re-evaluates to hide the fact we did this from users. */
	void OnPostAssetValidation();

	/** Called after a level has been added */
	void OnLevelAdded(ULevel* InLevel, UWorld* InWorld);

	/** Called after a level has been removed */
	void OnLevelRemoved(ULevel* InLevel, UWorld* InWorld);

	/** Called after a new level has been created. The sequencer editor mode needs to be enabled. */
	void OnNewCurrentLevel();

	/** Called after a map has been opened. The sequencer editor mode needs to be enabled. */
	void OnMapOpened(const FString& Filename, bool bLoadAsTemplate);

	/** Called when new actors are dropped in the viewport. */
	void OnNewActorsDropped(const TArray<UObject*>& DroppedObjects, const TArray<AActor*>& DroppedActors);

	/** Called when viewport tab content changes. */
	void OnTabContentChanged();

	/** Called when the map is changed. */
	void OnMapChanged(UWorld* World, EMapChangeType MapChangeType);

	/** Called before a PIE session begins. */
	void OnPreBeginPIE(bool bIsSimulating);

	/** Called after a PIE session ends. */
	void OnEndPIE(bool bIsSimulating);

	/** Called after PIE session ends and maps have been cleaned up */
	void OnEndPlayMap();

	/** Handles the actor selection changing externally .*/
	void OnActorSelectionChanged( UObject* );

	/** Called when an actor label has changed */
	void OnActorLabelChanged(AActor* ChangedActor);

	/** Called when sequencer has been evaluated */
	void OnSequencerEvaluated();

	/** Called when bindings have changed */
	void OnMovieSceneBindingsChanged();

	/** Called when data has changed */
	void OnMovieSceneDataChanged(EMovieSceneDataChangeType DataChangeType);

	/** Called when allow edits mode has changed */
	void OnAllowEditsModeChanged(EAllowEditsMode AllowEditsMode);

	/** Called when the user begins playing/scrubbing */
	void OnBeginDeferUpdates();

	/** Called when the user stops playing/scrubbing */
	void OnEndDeferUpdates();

	/** Called to determine whether a binding is visible in the tree view */
	bool IsBindingVisible(const FMovieSceneBinding& InBinding);

	void RegisterMenus();

	void MakeBrowseToSelectedActorSubMenu(UToolMenu* Menu);
	void BrowseToSelectedActor(AActor* Actor, FSequencer* Sequencer, FMovieSceneSequenceID SequenceId);

	bool IsPropertyReadOnly(const FPropertyAndParent& InPropertyAndParent);

private:

	void ActivateSequencerEditorMode();
	void DeactivateSequencerEditorMode();
	void AddLevelViewportMenuExtender();
	void BindDetailHandler(const FLevelEditorSequencerIntegrationOptions& Options);
	void ActivateDetailHandler();
	void AttachOutlinerColumn();
	void DetachOutlinerColumn();
	void ActivateRealtimeViewports();
	void RestoreRealtimeViewports();
	void RestoreToSavedState(UWorld* World);
	void ResetToAnimatedState(UWorld* World);

	void BackupSpawnablePilotData();
	void RestoreSpawnablePilotData();

	struct FSequencerAndOptions
	{
		TWeakPtr<FSequencer> Sequencer;
		FLevelEditorSequencerIntegrationOptions Options;
		FAcquiredResources AcquiredResources;
		TSharedRef<FLevelEditorSequencerBindingData> BindingData;
	};
	TArray<FSequencerAndOptions> BoundSequencers;

public:
	
	TSharedRef< ISceneOutlinerColumn > CreateSequencerInfoColumn( ISceneOutliner& SceneOutliner ) const;
	TSharedRef< ISceneOutlinerColumn > CreateSequencerSpawnableColumn( ISceneOutliner& SceneOutliner ) const;

private:

	void IterateAllSequencers(TFunctionRef<void(FSequencer&, const FLevelEditorSequencerIntegrationOptions& Options)>) const;
	void UpdateDetails(bool bForceRefresh = false);

	FLevelEditorSequencerIntegration();
	~FLevelEditorSequencerIntegration();

private:
	FAcquiredResources AcquiredResources;

	TSharedPtr<class FDetailKeyframeHandlerWrapper> KeyFrameHandler;

	TArray<FPilotedSpawnable> PilotedSpawnables;

	bool bDeferUpdates;

	FOnSequencersChanged OnSequencersChanged;

	FLevelEditorSequencerUpdateGizmoTickFunction UpdateGizmoTickFunction;

};
