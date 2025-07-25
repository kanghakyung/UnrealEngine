// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR
#include "Containers/Map.h"
#include "Delegates/DelegateInstanceInterface.h"
#include "UObject/NameTypes.h"
#include "UObject/UnrealType.h"

/** Used to map properties to member functions that will get executed once a property changes */
template<typename InClassType>
class TActorModifierPropertyChangeDispatcher
{
public:
	using FMemberFunctionType = typename TMemFunPtrType</*bConst*/false, InClassType, void()>::Type;

	TActorModifierPropertyChangeDispatcher(std::initializer_list<TPairInitializer<const FName&, const FMemberFunctionType&>> InCallbacks)
		: PropertyChangedFunctions(InCallbacks)
	{}

	void OnPropertyChanged(InClassType* InObject, const FPropertyChangedEvent& InPropertyChangedEvent) const
	{
		const FName MemberName = InPropertyChangedEvent.GetMemberPropertyName();

		if (const FMemberFunctionType* CallbackFunction = PropertyChangedFunctions.Find(MemberName))
		{
			::Invoke(*CallbackFunction, InObject);
		}
	}

private:
	TMap<FName, FMemberFunctionType> PropertyChangedFunctions;
};
#endif