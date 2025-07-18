// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorUndoClient.h"
#include "IWaveformTransformation.h"
#include "Misc/NotifyHook.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "WaveformEditorTransportController.h"
#include "WaveformEditorZoomController.h"
#include "WaveformTransformationTrimFade.h"
#include "TransformedWaveformView.h"
#include "WaveformTransformationMarkers.h"

class IToolkitHost;
class SDockTab;
class UAudioComponent;
class USoundWave;
class UWaveformEditorTransformationsSettings;

class WAVEFORMEDITOR_API FWaveformEditor 
	: public FAssetEditorToolkit
	, public FEditorUndoClient
	, public FGCObject
	, public FNotifyHook
{
public:
	bool Init(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, USoundWave* SoundWaveToEdit);
	virtual ~FWaveformEditor();

	/** FAssetEditorToolkit interface */
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual FName GetEditorName() const override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual EVisibility GetVisibilityWhileAssetCompiling() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;

	void OnAssetReimport(UObject* ReimportedObject, bool bSuccessfullReimport);

	/** FNotifyHook interface */
	void NotifyPreChange(class FEditPropertyChain* PropertyAboutToChange) override;
	void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, class FEditPropertyChain* PropertyThatChanged) override;

	/** FEditorUndo interface */
	void PostUndo(bool bSuccess) override;
	void PostRedo(bool bSuccess) override;
	bool MatchesContext(const FTransactionContext& InContext, const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjectContexts) const override;
	virtual void InitToolMenuContext(FToolMenuContext& MenuContext) override;

private:
	bool InitializeAudioComponent();
	bool CreateTransportController();
	bool InitializeZoom();
	bool BindDelegates();
	bool SetUpAssetReimport();
	void ExecuteReimport();
	void ExecuteOverwriteTransformations();

	/**	Sets the wave editor layout */
	const TSharedRef<FTabManager::FLayout> SetupStandaloneLayout();

	/**	Toolbar Setup */
	bool RegisterToolbar();
	bool BindCommands();
	TSharedRef<SWidget> GenerateFadeInOptionsMenu();
	TSharedRef<SWidget> GenerateFadeOutOptionsMenu();
	TSharedRef<SWidget> GenerateExportOptionsMenu();
	TSharedRef<SWidget> GenerateImportOptionsMenu();
	bool CanExecuteReimport() const;

	/**	Details tabs set up */
	TSharedRef<SDockTab> SpawnTab_Properties(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Transformations(const FSpawnTabArgs& Args);
	bool CreateDetailsViews();

	/**	Waveform view tab setup */
	TSharedRef<SDockTab> SpawnTab_WaveformDisplay(const FSpawnTabArgs& Args);
	bool CreateWaveformView();
	bool CreateTransportCoordinator();
	void BindWaveformViewDelegates(FWaveformEditorSequenceDataProvider& ViewDataProvider, STransformedWaveformViewPanel& ViewWidget);
	void RemoveWaveformViewDelegates(FWaveformEditorSequenceDataProvider& ViewDataProvider, STransformedWaveformViewPanel& ViewWidget);

	/** Playback delegates handlers */
	void HandlePlaybackPercentageChange(const UAudioComponent* InComponent, const USoundWave* InSoundWave, const float InPlaybackPercentage);
	void HandleAudioComponentPlayStateChanged(const UAudioComponent* InAudioComponent, EAudioComponentPlayState NewPlayState);
	void HandlePlayheadScrub(const float InTargetPlayBackRatio, const bool bIsMoving);

	/* Data View Delegates Handlers*/
	void HandleRenderDataUpdate();
	void HandleDisplayRangeUpdate(const TRange<double>);

	/** FGCObject overrides */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;
	bool CanPressPlayButton() const;
	bool CreateWaveWriter();
	void ExportWaveform();
	const UWaveformEditorTransformationsSettings* GetWaveformEditorTransformationsSettings() const;
	void AddDefaultTransformations();

	/** Transformation Functions*/
	void NotifyPostTransformationChange(const EPropertyChangeType::Type& PropertyChangeType = EPropertyChangeType::ArrayAdd);
	TObjectPtr<UWaveformTransformationTrimFade> GetOrAddTrimFadeTransformation();

	TObjectPtr<UWaveformTransformationMarkers> GetOrAddMarkerTransformation();
	TObjectPtr<UWaveformTransformationMarkers> GetMarkerTransformation();
	TObjectPtr<UWaveformTransformationMarkers> AddMarkerTransformation();
	void ToggleFadeIn();
	bool CanFadeIn();

	void ToggleFadeOut();
	bool CanFadeOut();
	void CreateMarker(bool bIsLoopRegion);
	void DeleteMarker();
	void SkipToNextMarker(); // Skips the playhead to the next marker
	void RegenerateTransformations();
	void UpdateTransportState();

	void ModifyMarkerLoopRegion(ELoopModificationControls Modification);
	void CycleMarkerLoopRegion(ELoopModificationControls Modification);

	FTransformedWaveformView  WaveformView;

	/** Exports the edited waveform to a new asset */
	TSharedPtr<class FWaveformEditorWaveWriter> WaveWriter = nullptr;

	/** Manages Transport info in waveform panel */
	TSharedPtr<class FSparseSampledSequenceTransportCoordinator> TransportCoordinator = nullptr;

	/** Controls Transport of the audio component  */
	TSharedPtr<FWaveformEditorTransportController> TransportController = nullptr;

	/** Controls and propagates zoom level */
	TSharedPtr<FWaveformEditorZoomController> ZoomManager = nullptr;

	/** Properties tab */
	TSharedPtr<IDetailsView> PropertiesDetails;

	/** Transformations tab */
	TSharedPtr<IDetailsView> TransformationsDetails;

	/** Settings Editor App Identifier */
	static const FName AppIdentifier;

	/** Tab Ids */
	static const FName PropertiesTabId;
	static const FName TransformationsTabId;
	static const FName WaveformDisplayTabId;
	static const FName EditorName;
	static const FName ToolkitFName;
	TObjectPtr<USoundWave> SoundWave = nullptr;
	TObjectPtr<UAudioComponent> AudioComponent = nullptr;
	bool bWasPlayingBeforeScrubbing = false;
	bool bIsInteractingWithTransformations = false;
	bool bWasPlayingBeforeChange = false;
	float LastReceivedPlaybackPercent = 0.f;
	EAudioComponentPlayState TransformInteractionPlayState = EAudioComponentPlayState::Stopped;
	float PlaybackTimeBeforeTransformInteraction = 0.f;
	float StartTimeBeforeTransformInteraction = 0.f;
	FWaveTransformUObjectConfiguration TransformationChainConfig;

	static constexpr float DefaultFadeInAmount = 0.5f;
	float CachedFadeInAmount = 0.f;
	float FadeInCurve = UWaveformTransformationTrimFade::FadeModeToCurveValueMap[EWaveEditorFadeMode::Exponetial];

	static constexpr float DefaultFadeOutAmount = 0.5f;
	float CachedFadeOutAmount = 0.f;
	float FadeOutCurve = UWaveformTransformationTrimFade::FadeModeToCurveValueMap[EWaveEditorFadeMode::Exponetial];

	// Tracking bool to ensure we detect wave cue changes and regenerate transformations
	// Checking the event is bound may have issues if other systems need to subscribe for wave cue array changes
	bool bCueChangeRegisteredByWaveformEditor = false;

	enum class EWaveEditorReimportMode : uint8
	{
		SameFile = 0,
		SameFileOverwrite,
		SelectFile,
		COUNT

	} ReimportMode;

	FText GetReimportButtonToolTip() const;
	FText GetExportButtonToolTip() const;

	FDelegateHandle OnMapOpenedHandle;
	TSharedPtr<SNotificationItem> ReopenNotificationItem;
};

