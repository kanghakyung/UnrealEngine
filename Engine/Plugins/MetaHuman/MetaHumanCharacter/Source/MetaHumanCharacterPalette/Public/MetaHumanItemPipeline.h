// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCharacterPipeline.h"

#include "MetaHumanItemPipeline.generated.h"

struct FMetaHumanPaletteBuiltData;
class UMetaHumanItemEditorPipeline;

/** The Wardrobe Item-specific subclass of Character Pipeline */
UCLASS(Abstract, MinimalAPI)
class UMetaHumanItemPipeline : public UMetaHumanCharacterPipeline
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual const UMetaHumanItemEditorPipeline* GetEditorPipeline() const
		PURE_VIRTUAL(UMetaHumanItemPipeline::GetEditorPipeline,return nullptr;);
#endif

	/**
	 * Assembles the item using the build output that was generated by the corresponding 
	 * editor pipeline.
	 * 
	 * Can only be called from a Collection pipeline. Items can't be assembled without a Collection.
	 * 
	 * ItemBuiltData is a filtered view of the built data that only allows access to data belonging
	 * to this item and its sub-items.
	 */
	virtual void AssembleItem(
		const FMetaHumanPaletteItemPath& BaseItemPath,
		const TArray<FMetaHumanPipelineSlotSelectionData>& SlotSelections,
		const FMetaHumanPaletteBuiltData& ItemBuiltData,
		const FInstancedStruct& AssemblyInput,
		TNotNull<UObject*> OuterForGeneratedObjects,
		const FOnAssemblyComplete& OnComplete) const
		PURE_VIRTUAL(UMetaHumanItemPipeline::AssembleItem,);

	METAHUMANCHARACTERPALETTE_API virtual void AssembleItemSynchronous(
		const FMetaHumanPaletteItemPath& BaseItemPath,
		const TArray<FMetaHumanPipelineSlotSelectionData>& SlotSelections,
		const FMetaHumanPaletteBuiltData& ItemBuiltData,
		const FInstancedStruct& AssemblyInput,
		TNotNull<UObject*> OuterForGeneratedObjects,
		FMetaHumanAssemblyOutput& OutAssemblyOutput) const;
};
