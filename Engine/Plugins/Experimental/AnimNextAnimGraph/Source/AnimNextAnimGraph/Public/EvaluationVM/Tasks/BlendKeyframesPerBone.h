// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimCurveTypes.h"
#include "Animation/NamedValueArray.h"
#include "CoreMinimal.h"
#include "EvaluationVM/EvaluationTask.h"
#include "EvaluationVM/Tasks/BlendKeyframes.h"
#include "Animation/AttributesContainer.h"

#include "BlendKeyframesPerBone.generated.h"

#define UE_API ANIMNEXTANIMGRAPH_API

struct FBlendSampleData;
class IBlendProfileInterface;
class IInterpolationIndexProvider;
class USkeleton;

/*
 * Blend Overwrite Keyframe Per Bone With Scale Task
 *
 * This pops the top keyframe from the VM keyframe stack, it scales each bone by a factor, and pushes
 * back the result onto the stack.
 * Top = Top * ScaleFactor
 * 
 * If no blend profile is provided, this task behaves like FAnimNextBlendOverwriteKeyframeWithScaleTask
 * Note that rotations will not be normalized after this task.
 */
USTRUCT()
struct FAnimNextBlendOverwriteKeyframePerBoneWithScaleTask : public FAnimNextBlendOverwriteKeyframeWithScaleTask
{
	GENERATED_BODY()

	DECLARE_ANIM_EVALUATION_TASK(FAnimNextBlendOverwriteKeyframePerBoneWithScaleTask)

	static UE_API FAnimNextBlendOverwriteKeyframePerBoneWithScaleTask Make(const IBlendProfileInterface* BlendProfile, const FBlendSampleData& BlendData, float ScaleFactor);

	// Task entry point
	UE_API virtual void Execute(UE::AnimNext::FEvaluationVM& VM) const override;

	// The blend data associated with the keyframe to overwrite
	// Cannot use a UPROPERTY with a pointer to a struct
	//UPROPERTY()
	const FBlendSampleData* BlendData = nullptr;

	// The blend profile to use
	// An optional blend profile provides a blend weight per bone
	const IBlendProfileInterface* BlendProfile = nullptr;

	const USkeleton* SourceSkeleton = nullptr;
};

/*
 * Blend Add Keyframe Per Bone With Scale Task
 *
 * This pops the top two keyframes (A and B) from the VM keyframe stack (let B be at the top).
 * B is our intermediary result that we add on top of; while A is the keyframe we scale.
 * The result is pushed back onto the stack.
 * Top = Top + (Top-1 * ScaleFactor)
 * 
 * If no blend profile is provided, this task behaves like FAnimNextBlendAddKeyframeWithScaleTask
 * Note that rotations will not be normalized after this task.
 */
USTRUCT()
struct FAnimNextBlendAddKeyframePerBoneWithScaleTask : public FAnimNextBlendAddKeyframeWithScaleTask
{
	GENERATED_BODY()

	DECLARE_ANIM_EVALUATION_TASK(FAnimNextBlendAddKeyframePerBoneWithScaleTask)

	static UE_API FAnimNextBlendAddKeyframePerBoneWithScaleTask Make(const IBlendProfileInterface* BlendProfile, const FBlendSampleData& BlendDataA, const FBlendSampleData& BlendDataB, float ScaleFactor);

	// Task entry point
	UE_API virtual void Execute(UE::AnimNext::FEvaluationVM& VM) const override;

	// The blend data associated with the keyframe A
	// Cannot use a UPROPERTY with a pointer to a struct
	//UPROPERTY()
	const FBlendSampleData* BlendDataA = nullptr;

	// The blend data associated with the keyframe B
	// Cannot use a UPROPERTY with a pointer to a struct
	//UPROPERTY()
	const FBlendSampleData* BlendDataB = nullptr;

	// The blend profile to use
	// An optional blend profile provides a blend weight per bone
	const IBlendProfileInterface* BlendProfile = nullptr;
};


USTRUCT()
struct FAnimNextBlendKeyframePerBoneWithScaleTask : public FAnimNextBlendAddKeyframeWithScaleTask
{
	GENERATED_BODY()

	DECLARE_ANIM_EVALUATION_TASK(FAnimNextBlendKeyframePerBoneWithScaleTask)

	struct FMaskedAttributeWeight
	{
		UE::Anim::FAttributeId Attribute;
		float Weight;

		FMaskedAttributeWeight(const UE::Anim::FAttributeId InAttribute, const float InWeight)
			: Attribute(InAttribute)
			, Weight(InWeight)
		{
		}
	};

	static UE_API FAnimNextBlendKeyframePerBoneWithScaleTask Make(TSharedPtr<IInterpolationIndexProvider> BlendProfile, const USkeleton* Skeleton, const TArray<float>& BoneMaskWeights, const UE::Anim::TNamedValueArray<FDefaultAllocator, UE::Anim::FCurveElement>& CurveMaskWeights, const TArray<FMaskedAttributeWeight>& AttributeMaskWeights, float ScaleFactor);

	// Task entry point
	UE_API virtual void Execute(UE::AnimNext::FEvaluationVM& VM) const override;

private:
	TArray<float> BoneMaskWeights;

	UE::Anim::TNamedValueArray<FDefaultAllocator, UE::Anim::FCurveElement> CurveMaskWeights;

	TArray<FMaskedAttributeWeight> AttributeMaskWeights;

	TSharedPtr<IInterpolationIndexProvider> BlendProfile = nullptr;

	const USkeleton* SourceSkeleton = nullptr;
};

#undef UE_API
