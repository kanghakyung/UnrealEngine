// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialExpressionFractal3D.h"
#include "MaterialCompiler.h"
#include "Materials/MaterialExpressionCustom.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MaterialExpressionFractal3D)

#define LOCTEXT_NAMESPACE "MaterialExpressionMaterialXFractal3D"

UMaterialExpressionMaterialXFractal3D::UMaterialExpressionMaterialXFractal3D(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_MaterialX;
		FConstructorStatics()
			: NAME_MaterialX(LOCTEXT("MaterialX", "MaterialX"))
		{}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_MaterialX);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionMaterialXFractal3D::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	UMaterialExpressionCustom* MaterialExpressionCustom = NewObject<UMaterialExpressionCustom>();
	MaterialExpressionCustom->Inputs[0].InputName = TEXT("Position");
	MaterialExpressionCustom->Inputs.Add({ TEXT("Octaves") });
	MaterialExpressionCustom->Inputs.Add({ TEXT("Lacunarity") });
	MaterialExpressionCustom->Inputs.Add({ TEXT("Diminish") });
	
	MaterialExpressionCustom->OutputType = ECustomMaterialOutputType::CMOT_Float1; //Noise functions returns only a float
	MaterialExpressionCustom->Code = 
		TEXT(R"(const bool bTiling = false;
        const float RepeatSize = 512;
	
		float3 result = float3(0.0,0.0,0.0);
		float amplitude = 1.0;
		for (int i = 0;  i < Octaves; ++i)
		{
			result += amplitude * GradientNoise3D_TEX(Position, bTiling, RepeatSize);
			amplitude *= Diminish;
			Position *= Lacunarity;
		}
		return result;)");

	int32 IndexPosition = Position.GetTracedInput().Expression ? Position.Compile(Compiler) : Compiler->LocalPosition(EPositionIncludedOffsets::IncludeOffsets, ELocalPositionOrigin::Instance);
	
	if (IndexPosition == INDEX_NONE)
	{
		return Compiler->Errorf(TEXT("Failed to compile Position input."));
	}

	int32 IndexOctaves = Octaves.GetTracedInput().Expression ? Octaves.Compile(Compiler) : Compiler->Constant(ConstOctaves);
	int32 IndexLacunarity = Lacunarity.GetTracedInput().Expression ? Lacunarity.Compile(Compiler) : Compiler->Constant(ConstLacunarity);
	int32 IndexDiminish = Diminish.GetTracedInput().Expression ? Diminish.Compile(Compiler) : Compiler->Constant(ConstDiminish);

	TArray<int32> Inputs{ IndexPosition, IndexOctaves, IndexLacunarity, IndexDiminish };
	int32 IndexFractal = Compiler->CustomExpression(MaterialExpressionCustom, 0, Inputs);

	int32 IndexAmplitude = Amplitude.GetTracedInput().Expression ? Amplitude.Compile(Compiler) : Compiler->Constant(ConstAmplitude);

	return Compiler->Mul(IndexFractal, IndexAmplitude);
}

void UMaterialExpressionMaterialXFractal3D::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("MaterialX Fractal3D"));
}
#endif

#undef LOCTEXT_NAMESPACE 