// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UObject/Class.h"
#include "UObject/UnrealType.h"

#include "TraitCore/TraitUID.h"
#include "TraitCore/NodeTemplate.h"
#include "TraitCore/NodeTemplateRegistry.h"

class UAnimNextAnimationGraph;

namespace UE::AnimNext
{
	struct FNodeHandle;

	// Converts a property value into its string representation using UE reflection
	template<class TraitSharedDataType, typename PropertyType>
	static FString ToString(const FString& PropertyName, PropertyType PropertyValue)
	{
		const UScriptStruct* SharedDataStruct = TraitSharedDataType::StaticStruct();
		if (const FProperty* Property = SharedDataStruct->FindPropertyByName(*PropertyName))
		{
			void* PropertyDefaults = Property->AllocateAndInitializeValue();

			FString Result;

			if constexpr (std::is_pointer_v<PropertyType>)
			{
				// C-style array properties aren't handled by ExportText, we need to handle it manually

				const bool bIsCArray = Property->ArrayDim > 1;
				if (bIsCArray)
				{
					Result += TEXT("(");
				}

				for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
				{
					Property->ExportText_Direct(Result, PropertyValue + Index, (PropertyType)PropertyDefaults + Index, nullptr, PPF_None, nullptr);

					if (Index + 1 < Property->ArrayDim)
					{
						Result += TEXT(",");
					}
				}

				if (bIsCArray)
				{
					Result += TEXT(")");
				}
			}
			else
			{
				Property->ExportText_Direct(Result, &PropertyValue, PropertyDefaults, nullptr, PPF_None);
			}

			Property->DestroyAndFreeValue(PropertyDefaults);

			return Result;
		}

		return FString();
	}

	// Creates a temporary empty node template registry instance and swaps it for the current one
	struct ANIMNEXTANIMGRAPHTESTSUITE_API FScopedClearNodeTemplateRegistry final
	{
		FScopedClearNodeTemplateRegistry(const FScopedClearNodeTemplateRegistry&) = delete;
		FScopedClearNodeTemplateRegistry& operator=(const FScopedClearNodeTemplateRegistry&) = delete;

		FScopedClearNodeTemplateRegistry();
		~FScopedClearNodeTemplateRegistry();

		FNodeTemplateRegistry TmpRegistry;
	};

	struct ANIMNEXTANIMGRAPHTESTSUITE_API FTestUtils final
	{
		// Loads the module data from the provided archive buffer and returns true on success, false otherwise
		// On success, we resolve every node handle provided as argument
		static bool LoadFromArchiveBuffer(UAnimNextAnimationGraph& AnimationGraph, TArray<FNodeHandle>& NodeHandles, const TArray<uint8>& SharedDataArchiveBuffer);
	};
}

#endif
