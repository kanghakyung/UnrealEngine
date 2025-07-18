// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InterchangeFilePickerBase.h"

#include "InterchangeOpenFileDialog.generated.h"

UCLASS(BlueprintType, Blueprintable)
class INTERCHANGEEDITORUTILITIES_API UInterchangeFilePickerGeneric : public UInterchangeFilePickerBase
{
	GENERATED_BODY()

public:

protected:

	virtual bool FilePickerForTranslatorAssetType(const EInterchangeTranslatorAssetType TranslatorAssetType, FInterchangeFilePickerParameters& Parameters, TArray<FString>& OutFilenames) override;
	virtual bool FilePickerForTranslatorType(const EInterchangeTranslatorType TranslatorType, FInterchangeFilePickerParameters& Parameters, TArray<FString>& OutFilenames) override;
};
