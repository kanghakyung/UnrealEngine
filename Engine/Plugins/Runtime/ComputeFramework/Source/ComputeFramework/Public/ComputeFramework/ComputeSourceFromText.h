// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeSource.h"
#include "Engine/EngineTypes.h"
#include "ComputeSourceFromText.generated.h"

#define UE_API COMPUTEFRAMEWORK_API

/**
 * Class responsible for loading HLSL text and parsing any metadata available.
 */
UCLASS(MinimalAPI, BlueprintType)
class UComputeSourceFromText : public UComputeSource
{
	GENERATED_BODY()

public:
	/** Filepath to the source file containing the kernel entry points and all options for parsing. */
	UPROPERTY(EditDefaultsOnly, AssetRegistrySearchable, meta = (ContentDir, RelativeToGameContentDir, FilePathFilter = "Unreal Shader File (*.usf)|*.usf"), Category = "Kernel")
	FFilePath SourceFile;

#if WITH_EDITOR
	/** Parse the source to get metadata. */
	UE_API void ReparseSourceText();
#endif

protected:
	//~ Begin UComputeSource Interface.
	FString GetSource() const override
	{
#if WITH_EDITOR
		return SourceText;
#else
		return {};
#endif
	}
	//~ End UComputeSource Interface.

#if WITH_EDITOR
	//~ Begin UObject Interface.
	UE_API void PostLoad() override;
	UE_API void PreEditChange(FEditPropertyChain& PropertyAboutToChange) override;
	UE_API void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	//~ End UObject Interface.
#endif

private:
#if WITH_EDITOR
	FString SourceText;
	FFilePath PrevSourceFile;
#endif
};

#undef UE_API
