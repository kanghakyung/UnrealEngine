// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/ArrayView.h"

#include "Math/Range.h"
#include "Misc/FrameNumber.h"
#include "Misc/FrameTime.h"
#include "Misc/Optional.h"
#include "Channels/MovieSceneChannel.h"
#include "Channels/MovieSceneChannelData.h"
#include "Channels/MovieSceneChannelTraits.h"
#include "Channels/MovieSceneChannelEditorData.h"

#include "MetaHumanMovieSceneChannel.generated.h"


USTRUCT()
struct METAHUMANSEQUENCER_API FMetaHumanMovieSceneChannel : public FMovieSceneChannel
{
	GENERATED_BODY()

	FMetaHumanMovieSceneChannel()
		: DefaultValue(), bHasDefaultValue(false)
	{}

	/**
	 * Access a mutable interface for this channel's data
	 *
	 * @return An object that is able to manipulate this channel's data
	 */
	virtual FORCEINLINE TMovieSceneChannelData<bool> GetData()
	{
		return TMovieSceneChannelData<bool>(&Times, &Values, this, &KeyHandles);
	}

	/**
	 * Access a constant interface for this channel's data
	 *
	 * @return An object that is able to interrogate this channel's data
	 */
	virtual FORCEINLINE TMovieSceneChannelData<const bool> GetData() const
	{
		return TMovieSceneChannelData<const bool>(&Times, &Values);
	}

	/**
	 * Const access to this channel's times
	 */
	FORCEINLINE TArrayView<const FFrameNumber> GetTimes() const
	{
		return Times;
	}

	/**
	 * Const access to this channel's values
	 */
	FORCEINLINE TArrayView<const bool> GetValues() const
	{
		return Values;
	}

	/**
	 * Check whether this channel has any data
	 */
	FORCEINLINE bool HasAnyData() const
	{
		return Times.Num() != 0 || bHasDefaultValue == true;
	}

	/**
	 * Evaluate this channel
	 *
	 * @param InTime     The time to evaluate at
	 * @param OutValue   A value to receive the result
	 * @return true if the channel was evaluated successfully, false otherwise
	 */
	virtual bool Evaluate(FFrameTime InTime, bool& OutValue) const;

public:

	// ~ FMovieSceneChannel Interface
	virtual void GetKeys(const TRange<FFrameNumber>& WithinRange, TArray<FFrameNumber>* OutKeyTimes, TArray<FKeyHandle>* OutKeyHandles) override;
	virtual void GetKeyTimes(TArrayView<const FKeyHandle> InHandles, TArrayView<FFrameNumber> OutKeyTimes) override;
	virtual void SetKeyTimes(TArrayView<const FKeyHandle> InHandles, TArrayView<const FFrameNumber> InKeyTimes) override;
	virtual void DuplicateKeys(TArrayView<const FKeyHandle> InHandles, TArrayView<FKeyHandle> OutNewHandles) override;
	virtual void DeleteKeys(TArrayView<const FKeyHandle> InHandles) override;
	virtual void DeleteKeysFrom(FFrameNumber InTime, bool bDeleteKeysBefore) override;
	virtual void RemapTimes(const UE::MovieScene::IRetimingInterface& Retimer) override;
	virtual TRange<FFrameNumber> ComputeEffectiveRange() const override;
	virtual int32 GetNumKeys() const override;
	virtual void Reset() override;
	virtual void Offset(FFrameNumber DeltaPosition) override;
	virtual void Optimize(const FKeyDataOptimizationParams& InParameters) override;
	virtual void ClearDefault() override;

public:

	/**
	 * Set this channel's default value that should be used when no keys are present
	 *
	 * @param InDefaultValue The desired default value
	 */
	FORCEINLINE void SetDefault(bool InDefaultValue)
	{
		bHasDefaultValue = true;
		DefaultValue = InDefaultValue;
	}

	/**
	 * Get this channel's default value that will be used when no keys are present
	 *
	 * @return (Optional) The channel's default value
	 */
	FORCEINLINE TOptional<bool> GetDefault() const
	{
		return bHasDefaultValue ? TOptional<bool>(DefaultValue) : TOptional<bool>();
	}

	/**
	 * Remove this channel's default value causing the channel to have no effect where no keys are present
	 */
	FORCEINLINE void RemoveDefault()
	{
		bHasDefaultValue = false;
	}

protected:

	UPROPERTY(meta=(KeyTimes))
	TArray<FFrameNumber> Times;

	UPROPERTY()
	bool DefaultValue;

	UPROPERTY()
	bool bHasDefaultValue;

	UPROPERTY(meta=(KeyValues))
	TArray<bool> Values;

	FMovieSceneKeyHandleMap KeyHandles;
};

template<>
struct TMovieSceneChannelTraits<FMetaHumanMovieSceneChannel> : TMovieSceneChannelTraitsBase<FMetaHumanMovieSceneChannel>
{
	enum { SupportsDefaults = false };
	
	typedef TMovieSceneExternalValue<bool> ExtendedEditorDataType;
};
