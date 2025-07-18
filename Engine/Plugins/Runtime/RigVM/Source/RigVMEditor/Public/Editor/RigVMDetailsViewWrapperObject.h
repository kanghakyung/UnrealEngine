// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMDetailsViewWrapperObject.generated.h"

class URigVMDetailsViewWrapperObject;

DECLARE_EVENT_ThreeParams(URigVMDetailsViewWrapperObject, FWrappedPropertyChangedChainEvent, URigVMDetailsViewWrapperObject*, const FString&, FPropertyChangedChainEvent&);

UCLASS()
class RIGVMEDITOR_API URigVMDetailsViewWrapperObject : public UObject
{
public:
	GENERATED_BODY()

	URigVMDetailsViewWrapperObject();

	// Creating wrappers from a given struct
	virtual UClass* GetClassForStruct(UScriptStruct* InStruct, bool bCreateIfNeeded = true) const;
	static URigVMDetailsViewWrapperObject* MakeInstance(UClass* InWrapperObjectClass, UObject* InOuter, UScriptStruct* InStruct, uint8* InStructMemory, UObject* InSubject = nullptr);
	UScriptStruct* GetWrappedStruct() const;

	// Creating wrappers from a RigVMNode
	virtual UClass* GetClassForNodes(TArray<URigVMNode*> InNodes, bool bCreateIfNeeded = true) const;
	static URigVMDetailsViewWrapperObject* MakeInstance(UClass* InWrapperObjectClass, UObject* InOuter, TArray<URigVMNode*> InNodes, URigVMNode* InSubject);
	
	static void MarkOutdatedClass(UClass* InClass);
	static bool IsValidClass(UClass* InClass);
	
	FString GetWrappedNodeNotation() const;
	
	bool IsChildOf(const UStruct* InStruct) const
	{
		const UScriptStruct* WrappedStruct = GetWrappedStruct();
		return WrappedStruct && WrappedStruct->IsChildOf(InStruct);
	}

	template<typename T>
	bool IsChildOf() const
	{
		return IsChildOf(T::StaticStruct());
	}

	virtual void SetContent(const uint8* InStructMemory, const UStruct* InStruct);
	virtual void GetContent(uint8* OutStructMemory, const UStruct* InStruct) const;
	void SetContent(URigVMNode* InNode);

	template<typename T>
	T GetContent() const
	{
		check(IsChildOf<T>());
		
		T Result;
		GetContent((uint8*)&Result, T::StaticStruct());
		return Result;
	}

	template<typename T>
	void SetContent(const T& InValue)
	{
		check(IsChildOf<T>());

		SetContent((const uint8*)&InValue, T::StaticStruct());
	}

	UObject* GetSubject() const;
	void SetSubject(UObject* InSubject);

	FWrappedPropertyChangedChainEvent& GetWrappedPropertyChangedChainEvent() { return WrappedPropertyChangedChainEvent; }
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;

private:

	static void CopyPropertiesForUnrelatedStructs(uint8* InTargetMemory, const UStruct* InTargetStruct, const uint8* InSourceMemory, const UStruct* InSourceStruct);
	void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);
	void SetContentForPin(URigVMPin* InPin);

	struct FPerClassInfo
	{
		FString Notation;
		UScriptStruct* ScriptStruct;
		
		FPerClassInfo()
			: Notation()
			, ScriptStruct(nullptr)
		{}

		FPerClassInfo(UScriptStruct* InScriptStruct)
		: Notation()
		, ScriptStruct(InScriptStruct)
		{}

		FPerClassInfo(const FString& InNotation)
		: Notation(InNotation)
		, ScriptStruct(nullptr)
		{}

		friend uint32 GetTypeHash(const FPerClassInfo& Info)
		{
			return HashCombine(GetTypeHash(Info.Notation), GetTypeHash(Info.ScriptStruct));
		}

		bool operator ==(const FPerClassInfo& Other) const
		{
			return Notation == Other.Notation && ScriptStruct == Other.ScriptStruct;
		}

		bool operator !=(const FPerClassInfo& Other) const
		{
			return Notation != Other.Notation || ScriptStruct != Other.ScriptStruct;
		}
	};
	
	static TMap<FPerClassInfo, UClass*> InfoToClass;
	static TMap<UClass*, FPerClassInfo> ClassToInfo;
	static TSet<UClass*> OutdatedClassToRecreate;
	bool bIsSettingValue;

	UPROPERTY()
	TWeakObjectPtr<UObject> SubjectPtr;
	
	FWrappedPropertyChangedChainEvent WrappedPropertyChangedChainEvent;
};
