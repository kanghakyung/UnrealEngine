// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"

#include "DataValidationModule.h"
#include "DataValidationSettings.h"

#include "AssetRegistry/AssetData.h"
#include "UObject/Package.h"
#include "EditorValidatorSubsystem.generated.h"

class UDataValidationChangelist;
class UEditorValidatorBase;
class UEditorValidatorSubsystem;
struct FDirectoryPath;
class FMessageLog;
struct FARFilter;
struct FAssetData;
class ISourceControlChangelist;
typedef TSharedPtr<class ISourceControlChangelist, ESPMode::ThreadSafe> FSourceControlChangelistPtr;

enum class EModuleChangeReason;

namespace EMessageSeverity
{
	enum Type : int;
}

DECLARE_LOG_CATEGORY_EXTERN(LogContentValidation, Log, All);

struct DATAVALIDATION_API FValidateAssetsExternalObject
{
	/** Package Name */
	FName PackageName;

	/** Asset Name */
	FName AssetName;
};

USTRUCT(BlueprintType)
struct DATAVALIDATION_API FValidateAssetsDetails
{
	GENERATED_BODY()

	/** Package Name */
	FName PackageName;

	/** Asset Name */
	FName AssetName;

	/** Validation Result */
	EDataValidationResult Result = EDataValidationResult::Valid;

	/** Validation Errors */
	TArray<FText> ValidationErrors;

	/** Validation Warnings */
	TArray<FText> ValidationWarnings;
	
	/** Rich validation messages including tokens that can generate images or hyperlinks */
	TArray<TSharedRef<FTokenizedMessage>> ValidationMessages;

	/** List of external objects for this asset*/
	TArray<FValidateAssetsExternalObject> ExternalObjects;
};

USTRUCT(BlueprintType)
struct DATAVALIDATION_API FValidateAssetsResults
{
	GENERATED_BODY()

	FValidateAssetsResults() = default;
	
	/** Total amount of assets that were gathered to validate. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int NumRequested = 0;

	/** Amount of tested assets */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int NumChecked = 0;

	/** Amount of assets without errors or warnings */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int NumValid = 0;

	/** Amount of assets with errors */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int NumInvalid = 0;

	/** Amount of assets skipped */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int NumSkipped = 0;

	/** Amount of assets with warnings */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int NumWarnings = 0;

	/** Amount of assets that could not be validated */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int NumUnableToValidate = 0;
	
	/** True if FValidateAssetSettings::MaxAssetsToValidation was reached */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	bool bAssetLimitReached = false;

	/** 
	 * Per asset details
	 * Indexed by object path
	 * Only returned if FValidateAssetsSettings::bCollectPerAssetDetails is true.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	TMap<FString, FValidateAssetsDetails> AssetsDetails;
};

USTRUCT(BlueprintType)
struct DATAVALIDATION_API FValidateAssetsSettings
{
	GENERATED_BODY()

	FValidateAssetsSettings();
	~FValidateAssetsSettings();

	/** If true, will not validate files in excluded directories */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation")
	bool bSkipExcludedDirectories = true;

	/** If true, will add notifications for files with no validation and display even if everything passes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation")
	bool bShowIfNoFailures = true;

	/** If true, will add an FValidateAssetsDetails for each asset to the results */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation")
	bool bCollectPerAssetDetails = false;

	/** The usecase requiring datavalidation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation")
	EDataValidationUsecase ValidationUsecase = EDataValidationUsecase::None;

	/** If false, unloaded assets will get skipped from validation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation")
	bool bLoadAssetsForValidation = true;

	/** If true, will attempt to unload assets which were previously unloaded, and loaded for validation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation")
	bool bUnloadAssetsLoadedForValidation = false;
	
	/** 
	 * If false, external objects (e.g. actors stored separately from maps) will not be loaded when 
	 * validating their associated asset (e.g. the map) 
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation")
	bool bLoadExternalObjectsForValidation = true;

	/** If true, captures log warnings and errors from loading assets for validation and reports them as validation results */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation")
	bool bCaptureAssetLoadLogs = true;

	/** If true, captures log warnings and errors from other operations that happen during validation and adds them to validation results */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation")
	bool bCaptureLogsDuringValidation = true;
	
	/** If true, captured log warnings during validation are added to the validation results as errors (requires bCaptureLogsDuringValidation) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation")
	bool bCaptureWarningsDuringValidationAsErrors = true;

	/** Maximum number of assets to attempt to validate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation")
	int32 MaxAssetsToValidate = MAX_int32;
	
	/** Should we validate referencers of deleted assets in changelists */
	UPROPERTY(EditAnywhere, BlueprintReadwrite, Category = "Validation")
	bool bValidateReferencersOfDeletedAssets = true;

	/**
	 * Minimum severity of validation messages to make the message log visible after validation. 
	 * Defaults to warning, can be disabled by emptying the optional.
	 */
	TOptional<EMessageSeverity::Type> ShowMessageLogSeverity;

	/** The name of the message log to use for warnings/errors/etc */
	FName MessageLogName;

	/** Title of message log page to use for warnings/errors/etc */
	FText MessageLogPageTitle;

	/** Show progress window */
	bool bSilent = false;
};

/**
 * Utility to disable "validate on save" in UEditorValidatorSubsystem for the lifetime of this object.
 * Calls UEditorValidatorSubsystem::PushDisableValidateOnSave on construction and UEditorValidatorSubsystem::PopDisableValidateOnSave on destruction.
 */
struct DATAVALIDATION_API FScopedDisableValidateOnSave
{
public:
	FScopedDisableValidateOnSave();
	~FScopedDisableValidateOnSave();

	UE_NONCOPYABLE(FScopedDisableValidateOnSave);

private:
	UEditorValidatorSubsystem* EditorValidationSubsystem;
};

/**
 * UEditorValidatorSubsystem manages all the asset validation in the engine. 
 * 
 * The first validation handled is UObject::IsDataValid and its overridden functions.
 * Those validations require custom classes and are most suited to project-specific
 * classes. 
 * 
 * The next validation set is of all registered UEditorValidationBases. These validators 
 * have a function to determine if they can validate a given asset, and if they are 
 * currently enabled. They are good candidates for validating engine classes or 
 * very specific project logic.
 * 
 * Finally, this subsystem may be subclassed to change the overally behavior of
 * validation in your project. If a subclass exist in your project module, it will
 * supercede the engine validation subsystem.
 */
UCLASS(Config = Editor)
class DATAVALIDATION_API UEditorValidatorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UEditorValidatorSubsystem();

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection);
	virtual void Deinitialize();

	/**
	 * Push a new request to temporarily disable "validate on save".
	 * Should be paired with a call to PopDisableValidateOnSave.
	 */
	void PushDisableValidateOnSave();

	/**
	 * Pop a previous request to temporarily disable "validate on save".
	 * Should be paired with a call to PushDisableValidateOnSave.
	 */
	void PopDisableValidateOnSave();

	/**
	 * Called to validate assets from either the UI or a commandlet.
	 * Loads the specified assets and runs all registered validators on them.
	 * Populates the message log with errors and warnings with clickable links.
	 * @param InSettings Structure passing context and settings for ValidateAssetsWithSettings
	 * @param OutResults More detailed information about the results of the validate assets command
	 * @returns Number of assets with validation failures or warnings
	 */
	UFUNCTION(BlueprintCallable, Category = "Asset Validation")
	virtual int32 ValidateAssetsWithSettings(
		const TArray<FAssetData>& AssetDataList,
		const FValidateAssetsSettings& InSettings,
		FValidateAssetsResults& OutResults) const;

	/**
	 * Called to validate assets from either the UI or a commandlet.
	 * Loads the specified assets and runs all registered validators on them.
	 * Populates the message log with errors and warnings with clickable links.
	 * @param InSettings Structure passing context and settings for ValidateAssetsWithSettings
	 * @param OutResults More detailed information about the results of the validate assets command
	 * @returns Validation results for the changelist object itself
	 */
	UFUNCTION(BlueprintCallable, Category = "Asset Validation")
	virtual EDataValidationResult ValidateChangelist(
		UDataValidationChangelist* InChangelist,
		const FValidateAssetsSettings& InSettings,
		FValidateAssetsResults& OutResults) const;

	UFUNCTION(BlueprintCallable, Category = "Asset Validation")
	virtual EDataValidationResult ValidateChangelists(
		const TArray<UDataValidationChangelist*> InChangelists,
		const FValidateAssetsSettings& InSettings,
		FValidateAssetsResults& OutResults) const;

	/*
	* Adds a validator to the list, making sure it is a unique instance
	*/
	UFUNCTION(BlueprintCallable, Category = "Validation")
	void AddValidator(UEditorValidatorBase* InValidator);

	/*
	* Removes a validator
	* Should be called during module shutdown if a validator was added.
	*/
	UFUNCTION(BlueprintCallable, Category = "Validation")
	void RemoveValidator(UEditorValidatorBase* InValidator);

	/**
	 * Iterate the enabled set of validators.
	 * @note Return true to continue iteration, or false to stop.
	 */
	void ForEachEnabledValidator(TFunctionRef<bool(UEditorValidatorBase* Validator)> Callback) const;

	/**
	 * Runs registered validators on the provided object.
	 * Does not add anything to any FMessageLog tabs.
	 * @return Returns Valid if the object contains valid data; returns Invalid if the object contains invalid data; returns NotValidated if no validations was performed on the object
	 */
	UFUNCTION(BlueprintCallable, Category = "Asset Validation")
	virtual EDataValidationResult IsObjectValid(
		UObject* InObject,
		TArray<FText>& ValidationErrors,
		TArray<FText>& ValidationWarnings,
		const EDataValidationUsecase InValidationUsecase) const;

	/**
	 * Runs registered validators on the provided object.
	 * Does not add anything to any FMessageLog tabs.
	 * @return Returns Valid if the object contains valid data; returns Invalid if the object contains invalid data; returns NotValidated if no validations was performed on the object
	 */
	virtual EDataValidationResult IsObjectValidWithContext(
		UObject* InObject,
		FDataValidationContext& InContext) const;

	/**
	 * Loads the object referred to by the provided AssetData and runs registered validators on it.
	 * Does not add anything to any FMessageLog tabs.
	 * @return Returns Valid if the object pointed to by AssetData contains valid data; returns Invalid if the object contains invalid data or does not exist; returns NotValidated if no validations was performed on the object
	 */
	UFUNCTION(BlueprintCallable, Category = "Asset Validation")
	virtual EDataValidationResult IsAssetValid(
		const FAssetData& AssetData,
		TArray<FText>& ValidationErrors,
		TArray<FText>& ValidationWarnings,
		const EDataValidationUsecase InValidationUsecase) const;

	/**
	 * Loads the object referred to by the provided AssetData and runs registered validators on it.
	 * Does not add anything to any FMessageLog tabs.
	 * @return Returns Valid if the object pointed to by AssetData contains valid data; returns Invalid if the object contains invalid data or does not exist; returns NotValidated if no validations was performed on the object
	 */
	virtual EDataValidationResult IsAssetValidWithContext(
		const FAssetData& AssetData,
		FDataValidationContext& InContext) const;

	/**
	 * Called to validate from an interactive save
	 * 
	 * @param AssetDataList List of Assets to validate
	 * @param bProceduralSave True iff this is e.g. a save from the editor, false if it is a procedural change from e.g. cooking
	 */
	virtual void ValidateOnSave(TArray<FAssetData> AssetDataList, bool bProceduralSave) const;

	/**
	 * Schedule a validation of a saved package, this will activate next frame by default so it can combine them
	 *
	 * @param PackageName Package to validate
	 * @param bProceduralSave True iff this is e.g. a save from the editor, false if it is a procedural change from e.g. cooking
	 */
	virtual void ValidateSavedPackage(FName PackageName, bool bProceduralSave);
	
	/** 
	 * From a changelist, return a list of assets to validate.
	 * The base implementation returns assets in modified packages in the changelist.
	 * Subclasses may wish to validate additional assets based on files in the changelist 
	 * (e.g. dependencies, relevant code changes, configuration changes, etc)
	 */
	virtual void GatherAssetsToValidateFromChangelist(
		UDataValidationChangelist* 		InChangelist,
		const FValidateAssetsSettings& 	Settings,
		TSet<FAssetData>& 				OutAssets,
		FDataValidationContext& 		InContext) const;

	
	/** 
	 * Returns true if the given asset should be validated at all.
	 * May be overridden to e.g. skip developer/test assets.
	 * Used to determine which assets should be validated when gathering assets from a source such as a changelist.
	 * Will be ignored when an asset is directly passed for validation.
	 */
	virtual bool ShouldValidateAsset(
		const FAssetData& 				Asset,
		const FValidateAssetsSettings& 	Settings,
		FDataValidationContext& 		InContext) const;

	
	// Utility functions
	
	// Retrieve all assets matching the given filter from the asset registry and then recursively resolve redirectors to produce a single set of assets.
	TArray<FAssetData> GetAssetsResolvingRedirectors(FARFilter& InFilter);


protected:
	bool ShouldValidateOnSave(bool bProceduralSave) const;

	void CleanupValidators();
	
	void WaitForAssetCompilationIfNecessary(EDataValidationUsecase InUsecase, bool bShowProgress = true) const;

	/**
	 * @return Returns true if the current Path should be skipped for validation. Returns false otherwise.
	 */
	virtual bool IsPathExcludedFromValidation(FStringView Path) const;

	/**
	 * Handles validating all pending save packages
	 */
	void ValidateAllSavedPackages();

	void RegisterNativeValidators();

	void RegisterBlueprintValidators();

	/**
	 * Validates changelist before SCC submit operation
	 */
	void ValidateChangelistPreSubmit(FSourceControlChangelistPtr Changelist, EDataValidationResult& OutResult, TArray<FText>& ValidationErrors, TArray<FText>& ValidationWarnings) const;

	/**
	 * Handle native modules being loaded or unloaded (@see NativeModulesPendingLoad and NativeModulesPendingUnload)
	 */
	void OnNativeModulesChanged(FName InModuleName, EModuleChangeReason InModuleChangeReason);

	/**
	 * Handle Blueprint assets being added or removed (@see ValidatorClassesPendingLoad)
	 */
	void OnAssetsAdded(TConstArrayView<FAssetData> InAssets);
	void OnAssetsRemoved(TConstArrayView<FAssetData> InAssets);
	void OnAssetsAddedOrRemoved(TConstArrayView<FAssetData> InAssets, TFunctionRef<void(const FTopLevelAssetPath& BPGC)> Callback);

	/**
	 * Apply any pending changes to the list of active validators (@see ValidatorClassesPendingLoad, NativeModulesPendingLoad, and NativeModulesPendingUnload)
	 */
	void UpdateValidators() const;
	void UpdateValidators();

	/*
	 * Adds a validator to the list making sure it is a unique instance, but wait for the first use to load the class (for Blueprints).
	 */
	void AddValidator(const FTopLevelAssetPath& InValidatorClass);

	/*
	 * Remove any validators (active or pending) for the given class.
	 */
	void RemoveValidator(const FTopLevelAssetPath& InValidatorClass);

	/** 
	 * Validate a set of assets, adding the results to the log/output of a higher level task (e.g. changelist validation).
	 */
	EDataValidationResult ValidateAssetsInternal(
		FMessageLog& 					DataValidationLog,
		TSet<FAssetData>				Assets,
		const FValidateAssetsSettings& 	InSettings,
		FValidateAssetsResults& 		OutResults
	) const;
	
	EDataValidationResult ValidateChangelistsInternal(
		TConstArrayView<UDataValidationChangelist*> InChangelists,
		const FValidateAssetsSettings& 				InSettings,
		FValidateAssetsResults& 					OutResults) const;
	
	void LogAssetValidationSummary(FMessageLog& DataValidationLog, const FValidateAssetsSettings& InSettings, EDataValidationResult Result, const FValidateAssetsResults& Results) const;

	EDataValidationResult ValidateObjectInternal(
		const FAssetData& InAssetData,
		UObject* InObject,
		FDataValidationContext& InContext) const;
protected:
	/**
	 * Directories to ignore for data validation. Useful for test assets
	 */
	UPROPERTY(config)
	TArray<FDirectoryPath> ExcludedDirectories;

	/**
	 * Whether it should validate assets on save inside the editor
	 */
	UPROPERTY(config, meta = (DeprecatedProperty, DeprecationMessage = "Use bValidateOnSave on UDataValidationSettings instead."))
	bool bValidateOnSave;

	/** List of saved package names to validate next frame */
	TArray<FName> SavedPackagesToValidate;

	/**
	 * Active validator instances, mapped from their class.
	 * @note Some instances may be null until UpdateValidators has been called (@see ValidatorClassesPendingLoad)
	 */
	UPROPERTY(Transient)
	TMap<FTopLevelAssetPath, TObjectPtr<UEditorValidatorBase>> Validators;

	/**
	 * Set of Blueprint validator classes (from Validators) that have been discovered since the set of validators was last updated, and need to be loaded by the next call to UpdateValidators.
	 */
	TSet<FTopLevelAssetPath> ValidatorClassesPendingLoad;

	/**
	 * Set of native modules that have been loaded since the set of validators was last updated, and should be queried for new validator classes by the next call to UpdateValidators.
	 */
	TSet<FName> NativeModulesPendingLoad;

	/**
	 * Set of native modules that have been unloaded since the set of validators was last updated, and should be removed from Validators by the next call to UpdateValidators.
	 */
	TSet<FName> NativeModulesPendingUnload;

	bool bHasRegisteredNativeValidators = false;
	bool bHasRegisteredBlueprintValidators = false;

	/** Specifies whether or not to allow Blueprint validators */
	UPROPERTY(config)
	bool bAllowBlueprintValidators;

	/**
	 * Counter used by PushDisableValidateOnSave and PopDisableValidateOnSave to know whether "validate on save" is temporarily disabled.
	 */
	uint8 DisableValidateOnSaveCount = 0;
};
