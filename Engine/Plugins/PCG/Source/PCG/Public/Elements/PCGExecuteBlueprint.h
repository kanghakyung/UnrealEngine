// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGContext.h"
#include "PCGSettings.h"



#include "PCGExecuteBlueprint.generated.h"

#define UE_API PCG_API

class UPCGBlueprintElement;
class UPCGMetadata;
class UPCGPointData;
class UPCGSpatialData;
struct FPCGPoint;

class UWorld;

#if WITH_EDITOR
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPCGBlueprintChanged, UPCGBlueprintElement*);

namespace PCGBlueprintHelper
{
	TSet<TObjectPtr<UObject>> GetDataDependencies(UPCGBlueprintElement* InElement);
}
#endif // WITH_EDITOR

/**
 * Helper class which can be used before calling a Blueprint function with a FPCGContext& parameter.
 * 
 * Since BP calls will copy the incoming FPCGContext reference we need to clear out a couple of fields before the BP copy is made to prevent issues when the copy is destroyed.
 */
class FPCGContextBlueprintScope
{
public:
	UE_API explicit FPCGContextBlueprintScope(FPCGContext* InContext);
	UE_API ~FPCGContextBlueprintScope();

private:
	FPCGContext* Context = nullptr;
	TSharedPtr<FPCGContextHandle> ContextHandle = nullptr;
	TWeakPtr<FPCGGraphExecutor> GraphExecutor = nullptr;
};

UCLASS(MinimalAPI, Abstract, BlueprintType, Blueprintable, hidecategories = (Object))
class UPCGBlueprintElement : public UObject
{
	GENERATED_BODY()

public:
	// ~Begin UObject interface
	UE_API virtual void PostLoad() override;
	UE_API virtual void BeginDestroy() override;
	// ~End UObject interface

	/**
	 * Main execution function that will contain the logic for this PCG Element, with the context as parameter.
	 * @param InContext - Context of the execution
	 * @param Input     - Input collection containing all the data passed as input to the node.
	 * @param Output    - Data collection that will be passed as the output of the node, with pins matching the ones provided during the execution.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "PCG|Execution")
	UE_API void ExecuteWithContext(UPARAM(ref)FPCGContext& InContext, const FPCGDataCollection& Input, FPCGDataCollection& Output);

	/**
	 * Main execution function that will contain the logic for this PCG Element. Use GetContext to have access to the context.
	 * @param Input  - Input collection containing all the data passed as input to the node.
	 * @param Output - Data collection that will be passed as the output of the node, with pins matching the ones provided during the execution.
	 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "PCG|Execution")
	UE_API void Execute(const FPCGDataCollection& Input, FPCGDataCollection& Output);

	/**
	 * Multi-threaded loop that will iterate on all points in InData. All points will be added in the same order than in input. Will be called by Point Loop function.
	 * @param InContext   - Context of the execution
	 * @param InData      - Input point data. Constant, must not be modified.
	 * @param InPoint     - Point for the current iteration. Constant, must not be modified.
	 * @param OutPoint    - Point that will be added to the output data. Can be modified.
	 * @param OutMetadata - Output metadata to write attribute to. Can be modified.
	 * @param Iteration   - Index of the current point. Must only be used to access input data, as this call is multi-threaded. It is not safe to access output data.
	 * @returns True if the point should be kept, False if not.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "PCG|Flow Control")
	UE_API bool PointLoopBody(const FPCGContext& InContext, const UPCGPointData* InData, const FPCGPoint& InPoint, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata, int64 Iteration) const;

	/**
	 * Multi-threaded loop that will be called on all points in InData. Can return a variable number of output points. 
	 * All points will be added in the same order than in input. Will be called by Variable Loop function. 
	 * @param InContext   - Context of the execution
	 * @param InData      - Input point data. Constant, must not be modified.
	 * @param InPoint     - Point for the current iteration. Constant, must not be modified.
	 * @param OutMetadata - Output metadata to write attribute to. Can be modified.
	 * @param Iteration   - Index of the current point. Must only be used to access input data, as this call is multi-threaded. It is not safe to access output data.
	 * @returns Array of new points that will be added to the output point data.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "PCG|Flow Control")
	UE_API TArray<FPCGPoint> VariableLoopBody(const FPCGContext& InContext, const UPCGPointData* InData, const FPCGPoint& InPoint, UPCGMetadata* OutMetadata, int64 Iteration) const;

	/**
	 * Multi-threaded loop that will iterate on all nested loop pairs (e.g. (o, i) for all o in Outer, i in Inner).
	 * All points will be added in the same order than in input (e.g: (0,0), (0,1), (0,2), ...). Will be called by Nested Loop function.
	 * @param InContext      - Context of the execution
	 * @param InOuterData    - Outer point data. Constant, must not be modified.
	 * @param InInnerData    - Inner point data. Constant, must not be modified.
	 * @param InOuterPoint   - Outer Point for the current iteration. Constant, must not be modified.
	 * @param InInnerPoint   - Inner Point for the current iteration. Constant, must not be modified.
	 * @param OutPoint       - Point that will be added to the output data. Can be modified.
	 * @param OutMetadata    - Output metadata to write attribute to. Can be modified.
	 * @param OuterIteration - Index of the current point in outer data. Must only be used to access input data, as this call is multi-threaded. It is not safe to access output data.
	 * @param InnerIteration - Index of the current point in inner data. Must only be used to access input data, as this call is multi-threaded. It is not safe to access output data.
	 * @returns True if the point should be kept, False if not.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "PCG|Flow Control")
	UE_API bool NestedLoopBody(const FPCGContext& InContext, const UPCGPointData* InOuterData, const UPCGPointData* InInnerData, const FPCGPoint& InOuterPoint, const FPCGPoint& InInnerPoint, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata, int64 OuterIteration, int64 InnerIteration) const;

	/**
	 * Multi-threaded loop that will be called N number of times (defined by Iteration Loop parameter NumIterations). 
	 * All points will be added in order (iteration 0 will be before iteration 1 in the final array).
	 * @param InContext   - Context of the execution
	 * @param Iteration   - Index of the current iteration. Must only be used to access input data, as this call is multi-threaded. It is not safe to access output data.
	 * @param InA         - Optional input spatial data, can be null. Constant, must not be modified.
	 * @param InB         - Optional input spatial data, can be null. Constant, must not be modified.
	 * @param OutPoint    - Point that will be added to the output data. Can be modified.
	 * @param OutMetadata - Output metadata to write attribute to. Can be modified.
	 * @returns True if the point should be kept, False if not.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "PCG|Flow Control")
	UE_API bool IterationLoopBody(const FPCGContext& InContext, int64 Iteration, const UPCGSpatialData* InA, const UPCGSpatialData* InB, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const;

	/** Calls the PointLoopBody function on all points */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "PCG|Flow Control", meta = (HideSelfPin = "true"))
	UE_API void PointLoop(UPARAM(ref) FPCGContext& InContext, const UPCGPointData* InData, UPCGPointData*& OutData, UPCGPointData* OptionalOutData = nullptr) const;

	/** Calls the VariableLoopBody function on all points, each call can return a variable number of points */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "PCG|Flow Control", meta = (HideSelfPin = "true"))
	UE_API void VariableLoop(UPARAM(ref) FPCGContext& InContext, const UPCGPointData* InData, UPCGPointData*& OutData, UPCGPointData* OptionalOutData = nullptr) const;

	/** Calls the NestedLoopBody function on all nested loop pairs (e.g. (o, i) for all o in Outer, i in Inner) */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "PCG|FLow Control", meta = (HideSelfPin = "true"))
	UE_API void NestedLoop(UPARAM(ref) FPCGContext& InContext, const UPCGPointData* InOuterData, const UPCGPointData* InInnerData, UPCGPointData*& OutData, UPCGPointData* OptionalOutData = nullptr) const;
	
	/** Calls the IterationLoopBody a fixed number of times, optional parameters are used to potentially initialized the Out Data, but otherwise are used to remove the need to have variables */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "PCG|Flow Control", meta = (HideSelfPin = "true"))
	UE_API void IterationLoop(UPARAM(ref) FPCGContext& InContext, int64 NumIterations, UPCGPointData*& OutData, const UPCGSpatialData* OptionalA = nullptr, const UPCGSpatialData* OptionalB = nullptr, UPCGPointData* OptionalOutData = nullptr) const;

	/** Override for the default node name */
	UFUNCTION(BlueprintNativeEvent, Category = "PCG|Node Customization")
	UE_API FName NodeTitleOverride() const;

	/** Override for the default node color. */
	UFUNCTION(BlueprintNativeEvent, Category = "PCG|Node Customization")
	UE_API FLinearColor NodeColorOverride() const;

	/** Override to change the node type. */
	UFUNCTION(BlueprintNativeEvent, Category = "PCG|Node Customization")
	UE_API EPCGSettingsType NodeTypeOverride() const;

	/** If Dynamic Pins is enabled in the BP settings, override this function to provide the type for the given pin. You can use "GetTypeUnionOfIncidentEdges" from the settings to get the union of input types on a given pin. Use the bitwise OR to combine multiple types together.*/
	UFUNCTION(BlueprintNativeEvent, Category = "PCG|Node Customization")
	UE_API int32 DynamicPinTypesOverride(const UPCGSettings* InSettings, const UPCGPin* InPin) const;

	/** Override for the IsCacheable node property when it depends on the settings in your node. If true, the node will be cached, if not it will always be executed. */
	UFUNCTION(BlueprintNativeEvent, Category = "PCG|Execution")
	UE_API bool IsCacheableOverride() const;

	/** Apply the preconfigured settings specified in the class default. Used to create nodes that are configured with pre-defined settings. Use InPreconfigureInfo index to know which settings it is. */
	UFUNCTION(BlueprintImplementableEvent, Category = "PCG|Preconfigure Settings", meta = (ForceAsFunction))
	UE_API void ApplyPreconfiguredSettings(UPARAM(ref) const FPCGPreConfiguredSettingsInfo& InPreconfigureInfo);

	// Returns the labels of custom input pins only
	UFUNCTION(BlueprintCallable, Category = "PCG|Input & Output")
	UE_API TSet<FName> CustomInputLabels() const;

	// Returns the labels of custom output pins only
	UFUNCTION(BlueprintCallable, Category = "PCG|Input & Output")
	UE_API TSet<FName> CustomOutputLabels() const;

	UFUNCTION(BlueprintCallable, Category = "PCG|Input & Output", meta = (HideSelfPin = "true"))
	UE_API TArray<FPCGPinProperties> GetInputPins() const;

	UFUNCTION(BlueprintCallable, Category = "PCG|Input & Output", meta = (HideSelfPin = "true"))
	UE_API TArray<FPCGPinProperties> GetOutputPins() const;

	/** Returns true if there is an input pin with the matching label. If found, will copy the pin properties in OutFoundPin */
	UFUNCTION(BlueprintCallable, Category = "PCG|Input & Output", meta = (HideSelfPin = "true"))
	UE_API bool GetInputPinByLabel(FName InPinLabel, FPCGPinProperties& OutFoundPin) const;

	/** Returns true if there is an output pin with the matching label. If found, will copy the pin properties in OutFoundPin */
	UFUNCTION(BlueprintCallable, Category = "PCG|Input & Output", meta = (HideSelfPin = "true"))
	UE_API bool GetOutputPinByLabel(FName InPinLabel, FPCGPinProperties& OutFoundPin) const;

	/** Gets the seed from the associated settings & source component */
	UFUNCTION(BlueprintCallable, Category = "PCG|Random")
	UE_API int GetSeed(UPARAM(ref) FPCGContext& InContext) const;

	/** Creates a random stream from the settings & source component */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "PCG|Random")
	UE_API FRandomStream GetRandomStream(UPARAM(ref) FPCGContext& InContext) const;

	/** Called after object creation to setup the object callbacks */
	UE_API void Initialize();

	/** Retrieves the execution context - note that this will not be valid outside of the Execute functions */
	UFUNCTION(BlueprintCallable, Category = "PCG|Advanced", meta = (HideSelfPin = "true"))
	UE_API FPCGContext& GetContext() const;

	/** Called after the element duplication during execution to be able to get the context easily - internal call only */
	UE_API void SetCurrentContext(FPCGContext* InCurrentContext);

	/** Tries to resolve current Context from thread local BP stack */
	static UE_API FPCGContext* ResolveContext();

#if WITH_EDITOR
	// ~Begin UObject interface
	UE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	// ~End UObject interface

	/** Used for filtering */
	static UE_API FString GetParentClassName();
#endif

	/** Needed to be able to call certain blueprint functions */
	UE_API virtual UWorld* GetWorld() const override;

#if !WITH_EDITOR
	void SetInstanceWorld(UWorld* World) { InstanceWorld = World; }
#endif

#if WITH_EDITOR
	FOnPCGBlueprintChanged OnBlueprintChangedDelegate;
#endif

	/** Controls whether results can be cached so we can bypass execution if the inputs & settings are the same in a subsequent execution.
	* If you have implemented the IsCacheableOverride function, then this value is ignored.
	* Note that if your node relies on data that is not directly tracked by PCG or creates any kind of artifact (adds components, creates actors, etc.) then it should not be cacheable.
	*/
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, AdvancedDisplay, Category = Settings)
	bool bIsCacheable = false;

	/** In cases where your node is non-cacheable but is likely to yield the same results on subsequent executions, this controls whether we will do a deep & computationally intensive CRC computation (true), 
	* which will allow cache usage in downstream nodes in your graph, or, by default (false), a shallow but quick crc computation which will not be cache-friendly. */
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, AdvancedDisplay, Category = Settings)
	bool bComputeFullDataCrc = false;

	/** Controls whether this node execution can be run from a non-game thread. This is not related to the Loop functions provided/implemented in this class, which should always run on any thread. */
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = Settings)
	bool bRequiresGameThread = true;

#if WITH_EDITORONLY_DATA
	/** This property is deprecated, please use 'Requires Game Thread' instead. */
	UPROPERTY()
	bool bCanBeMultithreaded_DEPRECATED = false;

	UPROPERTY(BlueprintGetter=CustomInputLabels, Category = "Settings|Input & Output", meta = (DeprecatedProperty, DeprecatedMessage = "Input Pin Labels are deprecated - use Input Labels instead."))
	TSet<FName> InputPinLabels_DEPRECATED;

	UPROPERTY(BlueprintGetter=CustomOutputLabels, Category = "Settings|Input & Output")
	TSet<FName> OutputPinLabels_DEPRECATED;
#endif

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Settings|Input & Output")
	TArray<FPCGPinProperties> CustomInputPins;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Settings|Input & Output")
	TArray<FPCGPinProperties> CustomOutputPins;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Settings|Input & Output")
	bool bHasDefaultInPin = true;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Settings|Input & Output")
	bool bHasDefaultOutPin = true;

	/** If enabled, by default, the Out pin type will have the union of In pin types. Default only works if the pins are In and Out. For custom behavior, implement DynamicPinTypesOverride. */
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Settings|Input & Output")
	bool bHasDynamicPins = false;

#if WITH_EDITORONLY_DATA
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = AssetInfo, AssetRegistrySearchable)
	bool bExposeToLibrary = false;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "AssetInfo|Preconfigured Settings", AssetRegistrySearchable)
	bool bEnablePreconfiguredSettings = false;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "AssetInfo|Preconfigured Settings", AssetRegistrySearchable, meta = (EditCondition = bEnablePreconfiguredSettings, EditConditionHides))
	bool bOnlyExposePreconfiguredSettings = false;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "AssetInfo|Preconfigured Settings", meta = (EditCondition = bEnablePreconfiguredSettings, EditConditionHides))
	TArray<FPCGPreConfiguredSettingsInfo> PreconfiguredInfo;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = AssetInfo, AssetRegistrySearchable)
	FText Category;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = AssetInfo, AssetRegistrySearchable)
	FText Description;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, AdvancedDisplay, Category = "Settings")
	int32 DependencyParsingDepth = 1;
#endif

protected:
#if WITH_EDITOR
	UE_API void OnDependencyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);
	UE_API void UpdateDependencies();
	TSet<TWeakObjectPtr<UObject>> DataDependencies;
#endif

#if !WITH_EDITORONLY_DATA
	UWorld* InstanceWorld = nullptr;
#endif

	// Since we duplicate the blueprint elements prior to execution, they will be unique
	// and have a 1:1 match with their context, which allows us to store it here
	FPCGContext* CurrentContext = nullptr;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural))
class UPCGBlueprintSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UE_API UPCGBlueprintSettings();

	friend class FPCGExecuteBlueprintElement;

	// ~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("ExecuteBlueprint")); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGBlueprintSettings", "NodeTitle", "Execute Blueprint"); }
	UE_API virtual FLinearColor GetNodeTitleColor() const override;
	UE_API virtual EPCGSettingsType GetType() const override;
	UE_API virtual void GetStaticTrackedKeys(FPCGSelectionKeyToSettingsMap& OutKeysToSettings, TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const override;
	UE_API virtual UObject* GetJumpTargetForDoubleClick() const override;
	UE_API virtual void ApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	UE_API virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	UE_API virtual bool OnlyExposePreconfiguredSettings() const override;
#endif
	virtual bool UseSeed() const override { return true; }
	UE_API virtual bool HasDynamicPins() const override;
	UE_API virtual EPCGDataType GetCurrentPinTypes(const UPCGPin* InPin) const override;

	UE_API virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& InPreconfiguredsInfo) override;
	UE_API virtual FString GetAdditionalTitleInformation() const override;
	virtual bool HasFlippedTitleLines() const override { return true; }
	// This node may have side effects, don't assume we can cull even when unwired.
	virtual bool CanCullTaskIfUnwired() const { return false; }
	UE_API virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	UE_API virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

	/** To be removed when we support automatic override of BP params. For now always return true to force params pin. */
	virtual bool HasOverridableParams() const override { return true; }
protected:
	UE_API virtual FPCGElementPtr CreateElement() const override;
#if WITH_EDITOR
	virtual EPCGChangeType GetChangeTypeForProperty(const FName& InPropertyName) const override { return Super::GetChangeTypeForProperty(InPropertyName) | EPCGChangeType::Cosmetic; }
	UE_API virtual TArray<FPCGSettingsOverridableParam> GatherOverridableParams() const override;
#endif // WITH_EDITOR
	UE_API virtual void FixingOverridableParamPropertyClass(FPCGSettingsOverridableParam& Param) const override;
	// ~End UPCGSettings interface

public:
	// ~Begin UObject interface
	UE_API virtual void PostLoad() override;
	UE_API virtual void BeginDestroy() override;
#if WITH_EDITOR
	UE_API virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	UE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	// ~End UObject interface
#endif

	UFUNCTION(BlueprintCallable, Category = "Settings|Template", meta=(DeterminesOutputType="InElementType", DynamicOutputParam = "ElementInstance"))
	UE_API void SetElementType(TSubclassOf<UPCGBlueprintElement> InElementType, UPCGBlueprintElement*& ElementInstance);

	UFUNCTION(BlueprintCallable, Category = "Settings|Template")
	TSubclassOf<UPCGBlueprintElement> GetElementType() const { return BlueprintElementType; }

#if WITH_EDITOR
	TObjectPtr<UPCGBlueprintElement> GetElementInstance() const { return BlueprintElementInstance; }
#endif

protected:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TSubclassOf<UPCGBlueprintElement> BlueprintElement_DEPRECATED;
#endif

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Template)
	TSubclassOf<UPCGBlueprintElement> BlueprintElementType;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Instanced, Category = "Instance", meta = (ShowOnlyInnerProperties))
	TObjectPtr<UPCGBlueprintElement> BlueprintElementInstance;

#if WITH_EDITORONLY_DATA
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TArray<FName> TrackedActorTags;

	/** If this is checked, found actors that are outside component bounds will not trigger a refresh. Only works for tags for now in editor. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings)
	bool bTrackActorsOnlyWithinBounds = false;

	UPROPERTY()
	bool bCanBeMultithreaded_DEPRECATED = false;
#endif

protected:
#if WITH_EDITOR
	UE_DEPRECATED(5.6, "No longer supported")
	void OnBlueprintChanged(UBlueprint* InBlueprint) {}
	UE_API void OnBlueprintElementChanged(UPCGBlueprintElement* InElement);
	UE_API void OnObjectsReplaced(const TMap<UObject*, UObject*>& InOldToNewInstances);
#endif

	UE_API void RefreshBlueprintElement();
	UE_API void SetupBlueprintEvent();
	UE_API void TeardownBlueprintEvent();
	UE_API void SetupBlueprintElementEvent();
	UE_API void TeardownBlueprintElementEvent();
};

struct FPCGBlueprintExecutionContext : public FPCGContext
{
	TObjectPtr<UPCGBlueprintElement> BlueprintElementInstance = nullptr;

protected:
	virtual void AddExtraStructReferencedObjects(FReferenceCollector& Collector) override;
	virtual UObject* GetExternalContainerForOverridableParam(const FPCGSettingsOverridableParam& InParam) override { return BlueprintElementInstance; }
};

class FPCGExecuteBlueprintElement : public IPCGElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override;
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override;
	virtual bool ShouldComputeFullOutputDataCrc(FPCGContext* Context) const override;
	/** Set it to true by default, if there is a performance concern, we can expose a bool in the element class. */
	virtual bool ShouldVerifyIfOutputsAreUsedMultipleTimes(const UPCGSettings* InSettings) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual void PostExecuteInternal(FPCGContext* Context) const override;
	virtual FPCGContext* Initialize(const FPCGInitializeElementParams& InParams) override;	
};

#undef UE_API
