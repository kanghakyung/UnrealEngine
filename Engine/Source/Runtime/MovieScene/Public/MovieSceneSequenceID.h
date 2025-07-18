// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Class.h"
#include "EntitySystem/MovieSceneComponentDebug.h"
#include "MovieSceneSequenceID.generated.h"

USTRUCT()
struct FMovieSceneSequenceID
{
	GENERATED_BODY()

	constexpr FMovieSceneSequenceID()
		: Value(-1)
	{}

	constexpr explicit FMovieSceneSequenceID(uint32 InValue)
		: Value(InValue)
	{}

	FORCEINLINE friend bool operator==(FMovieSceneSequenceID LHS, FMovieSceneSequenceID RHS)
	{
		return LHS.Value == RHS.Value;
	}
	
	FORCEINLINE friend bool operator!=(FMovieSceneSequenceID LHS, FMovieSceneSequenceID RHS)
	{
		return LHS.Value != RHS.Value;
	}

	FORCEINLINE friend bool operator<(FMovieSceneSequenceID LHS, FMovieSceneSequenceID RHS)
	{
		return LHS.Value < RHS.Value;
	}

	FORCEINLINE friend bool operator>(FMovieSceneSequenceID LHS, FMovieSceneSequenceID RHS)
	{
		return LHS.Value > RHS.Value;
	}

	FORCEINLINE friend uint32 GetTypeHash(FMovieSceneSequenceID In)
	{
		return GetTypeHash(In.Value);
	}

	FORCEINLINE FMovieSceneSequenceID AccumulateParentID(FMovieSceneSequenceID InParentID) const
	{
		return Value == 0 ? InParentID : FMovieSceneSequenceID(HashCombine(Value, InParentID.Value));
	}

	FORCEINLINE bool Serialize(FArchive& Ar)
	{
		Ar << Value;
		return true;
	}

	FORCEINLINE friend FArchive& operator<<(FArchive& Ar, FMovieSceneSequenceID& SequenceID)
	{
		SequenceID.Serialize(Ar);
		return Ar;
	}

	FORCEINLINE uint32 GetInternalValue() const
	{
		return Value;
	}

	FORCEINLINE bool IsValid() const
	{
		return Value != -1;
	}

private:

	UPROPERTY()
	uint32 Value;
};

template<>
struct TStructOpsTypeTraits<FMovieSceneSequenceID> : public TStructOpsTypeTraitsBase2<FMovieSceneSequenceID>
{
	enum
	{
		WithSerializer = true,
		WithCopy = true
	};
	static constexpr EPropertyObjectReferenceType WithSerializerObjectReferences = EPropertyObjectReferenceType::None;
};

typedef TCallTraits<FMovieSceneSequenceID>::ParamType FMovieSceneSequenceIDRef;

namespace MovieSceneSequenceID
{
	inline constexpr FMovieSceneSequenceID Invalid = FMovieSceneSequenceID(-1);
	inline constexpr FMovieSceneSequenceID Root = FMovieSceneSequenceID(0);
}

inline FString LexToString(const FMovieSceneSequenceID& SequenceID)
{
	return *FString::Printf(TEXT("SeqID(%d)"), SequenceID.GetInternalValue());
}
