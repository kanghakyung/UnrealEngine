// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Chaos/Real.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Toolkits/BaseToolkit.h"
#include "UnrealEdMisc.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "IDetailCustomization.h"

#include "FractureEditorModeToolkit.generated.h"

class IDetailsView;
class IPropertyHandle;
class IStructureDetailsView;
class SScrollBox;
class FFractureToolContext;
class FGeometryCollection;
struct FPropertyChangedEvent;
class SGeometryCollectionOutliner;
class SGeometryCollectionHistogram;
class SGeometryCollectionStatistics;
struct FGeometryCollectionStatistics;
class AGeometryCollectionActor;
class UGeometryCollectionComponent;
class UGeometryCollection;
class FFractureEditorModeToolkit;
class UFractureActionTool;
class UFractureModalTool;
enum class EMapChangeType : uint8;

namespace GeometryCollection
{
enum class ESelectionMode: uint8;
}

namespace Chaos
{
	template<class T, int d>
	class TParticles;
}

class FFractureViewSettingsCustomization : public IDetailCustomization
{
public:
	FFractureViewSettingsCustomization(FFractureEditorModeToolkit* FractureToolkit);
	static TSharedRef<IDetailCustomization> MakeInstance(FFractureEditorModeToolkit* FractureToolkit);

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FFractureEditorModeToolkit* Toolkit;
};

class FHistogramSettingsCustomization : public IDetailCustomization
{
public:
	FHistogramSettingsCustomization(FFractureEditorModeToolkit* FractureToolkit);
	static TSharedRef<IDetailCustomization> MakeInstance(FFractureEditorModeToolkit* FractureToolkit);

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FFractureEditorModeToolkit* Toolkit;
};

class FOutlinerSettingsCustomization : public IDetailCustomization
{
public:
	FOutlinerSettingsCustomization(FFractureEditorModeToolkit* FractureToolkit);
	static TSharedRef<IDetailCustomization> MakeInstance(FFractureEditorModeToolkit* FractureToolkit);

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FFractureEditorModeToolkit* Toolkit;
};

struct FTextAndSlateColor
{
	FTextAndSlateColor(const FText& InText, const FSlateColor& InColor)
		: Text(InText)
		, Color(InColor)
	{}
	FText Text;
	FSlateColor Color;
};

UENUM(BlueprintType)
enum class EOutlinerColumnMode : uint8
{
	State = 0				UMETA(DisplayName = "State"),
	Damage = 1				UMETA(DisplayName = "Damage"),
	Removal = 2				UMETA(DisplayName = "Removal"),
	Collision = 3			UMETA(DisplayName = "Collision"),
	Size = 4				UMETA(DisplayName = "Size"),
	Geometry = 5			UMETA(DisplayName = "Geometry")
};

class FRACTUREEDITOR_API FFractureEditorModeToolkit : public FModeToolkit, public FGCObject
{
public:

	using FGeometryCollectionPtr = TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe>;

	FFractureEditorModeToolkit();
	~FFractureEditorModeToolkit();
	
	/** FModeToolkit interface */
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode) override;
	/** IToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual class FEdMode* GetEditorMode() const override;
	virtual TSharedPtr<class SWidget> GetInlineContent() const override;

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FFractureEditorModeToolkit");
	}

	void ExecuteAction(UFractureActionTool* InActionTool);
	bool CanExecuteAction(UFractureActionTool* InActionTool) const;

	bool CanSetModalTool(UFractureModalTool* InActiveTool) const;
	void SetActiveTool(UFractureModalTool* InActiveTool);
	UFractureModalTool* GetActiveTool() const;
	bool IsActiveTool(UFractureModalTool* InActiveTool);
	void ShutdownActiveTool();

	void Shutdown();

	void SetOutlinerComponents(const TArray<UGeometryCollectionComponent*>& InNewComponents);
	void SetBoneSelection(UGeometryCollectionComponent* InRootComponent, const TArray<int32>& InSelectedBones, bool bClearCurrentSelection, int32 FocusBoneIdx = -1);

	// View Settings
	float GetExplodedViewValue() const;
	int32 GetLevelViewValue() const;
	bool GetHideUnselectedValue() const;
	void OnSetExplodedViewValue(float NewValue);
	void OnSetLevelViewValue(int32 NewValue);

	void OnExplodedViewValueChanged();
	void OnLevelViewValueChanged();
	void OnHideUnselectedChanged();
	void UpdateHideForComponent(UGeometryCollectionComponent* Component);

	// Update any View Property Changes 
	void OnObjectPostEditChange( UObject* Object, FPropertyChangedEvent& PropertyChangedEvent );

	TSharedRef<SWidget> GetLevelViewMenuContent(TSharedRef<IPropertyHandle> PropertyHandle);
	TSharedRef<SWidget> GetViewMenuContent();

	void ToggleShowBoneColors();

	void ViewUpOneLevel();
	void ViewDownOneLevel();

	// Modal Command Callback
	FReply OnModalClicked();
	bool CanExecuteModal() const;

	static void GetSelectedGeometryCollectionComponents(TSet<UGeometryCollectionComponent*>& GeomCompSelection);

	static void AddAdditionalAttributesIfRequired(UGeometryCollection* GeometryCollectionObject);
	int32 GetLevelCount();

	void GetStatisticsSummary(FGeometryCollectionStatistics& Stats) const;
	FText GetSelectionInfo() const;

	/** Returns the number of Mode specific tabs in the mode toolbar **/ 
	const static TArray<FName> PaletteNames;
	virtual void GetToolPaletteNames(TArray<FName>& InPaletteName) const;
	virtual FText GetToolPaletteDisplayName(FName PaletteName) const; 

	/* Exclusive Tool Palettes only allow users to use tools from one palette at a time */
	virtual bool HasExclusiveToolPalettes() const { return false; }

	/* Integrated Tool Palettes show up in the same panel as their details */
	virtual bool HasIntegratedToolPalettes() const { return false; }

	void SetInitialPalette();
	virtual void BuildToolPalette(FName PaletteName, class FToolBarBuilder& ToolbarBuilder);
	virtual void OnToolPaletteChanged(FName PaletteName) override;

	/** Modes Panel Header Information **/
	virtual FText GetActiveToolDisplayName() const;
	virtual FText GetActiveToolMessage() const;

	void UpdateExplodedVectors(UGeometryCollectionComponent* GeometryCollectionComponent) const;

	void RefreshOutliner();
	
	void RegenerateOutliner();
	void RegenerateHistogram();

	TSharedPtr<SWidget> ExplodedViewWidget;
	TSharedPtr<SWidget> LevelViewWidget;
	TSharedPtr<SWidget> ShowBoneColorsWidget;

	void SetOutlinerColumnMode(EOutlinerColumnMode ColumnMode);

	// function to poll whether the geometry collection data cached by the outliner is stale compared to the current geometry (using quick heuristics such as bone counts)
	bool IsCachedOutlinerGeometryStale(const TArray<TWeakObjectPtr<UGeometryCollectionComponent>>& SelectedComponents) const;

protected:
	/** FModeToolkit interface */
	virtual void RequestModeUITabs() override;
	virtual void InvokeUI() override;

	TSharedRef<SDockTab> CreateHierarchyTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> CreateStatisticsTab(const FSpawnTabArgs& Args);
	static bool IsGeometryCollectionSelected();
	static bool IsSelectedActorsInEditorWorld();	

	void InvalidateCachedDetailPanelState(UObject* ChangedObject);

	// Invalidate the hit proxies for all level viewports; we need to do this after updating geometry collection(s)
	void InvalidateHitProxies();

private:
	static void UpdateGeometryComponentAttributes(UGeometryCollectionComponent* Component);

	void OnOutlinerBoneSelectionChanged(UGeometryCollectionComponent* RootComponent, TArray<int32>& SelectedBones);
	void OnHistogramBoneSelectionChanged(UGeometryCollectionComponent* RootComponent, TArray<int32>& SelectedBones);
	void BindCommands();

	void SetHideForUnselected(UGeometryCollectionComponent* GCComp);

	/** Callback for map changes. */
	void HandleMapChanged(UWorld* NewWorld, EMapChangeType MapChangeType);

	FReply OnRefreshOutlinerButtonClicked();

	TSharedRef<SWidget> MakeMenu_FractureModeConfigSettings();
	void UpdateAssetLocationMode(TSharedPtr<FString> NewString);
	void UpdateAssetPanelFromSettings();
	void OnProjectSettingsModified();

	void UpdateOutlinerHeader();

	void CreateVariableOverrideDetailView();
	void RefreshVariableOverrideDetailView(const UGeometryCollection* RestCollection);

	FReply OnDataflowOverridesUpdateAsset();
	bool CanDataflowOverridesUpdateAsset() const;
	bool GetDataflowOverridesUpdateAssetEnabled() const;
private:
	TObjectPtr<UFractureModalTool> ActiveTool;

	// called when PIE is about to start, shuts down active tools
	FDelegateHandle BeginPIEDelegateHandle;
	// calls with the project settings are modified; used to keep the quick settings up to date
	FDelegateHandle ProjectSettingsModifiedHandle;

	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<IDetailsView> FractureSettingsDetailsView;
	TSharedPtr<IStructureDetailsView> OverridesDetailsView;
	TSharedPtr<IDetailsView> HistogramDetailsView;
	TSharedPtr<IDetailsView> OutlinerDetailsView;
	TSharedPtr<SWidget> ToolkitWidget;
	TSharedPtr<SGeometryCollectionOutliner> OutlinerView;
	TSharedPtr<SGeometryCollectionHistogram> HistogramView;
	TWeakPtr<SDockTab> HierarchyTab;
	FMinorTabConfig HierarchyTabInfo;
	TWeakPtr<SDockTab> StatisticsTab;
	FMinorTabConfig StatisticsTabInfo;
	TSharedPtr<SGeometryCollectionStatistics> StatisticsView;
	TArray<TSharedPtr<FString>> AssetLocationModes;
	TSharedPtr<STextComboBox> AssetLocationMode;

	// Simple cached statistics to allow use to quickly/heuristically check for stale geometry collection data
	int64 OutlinerCachedBoneCount = 0, OutlinerCachedVertexCount = 0, OutlinerCachedHullCount = 0;
};
