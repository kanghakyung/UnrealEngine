﻿// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextVariablesTest.h"
#include "CoreMinimal.h"
#include "AnimNextTest.h"
#include "ScopedTransaction.h"
#include "UncookedOnlyUtils.h"
#include "Misc/AutomationTest.h"
#include "Param/ParamType.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Async/ParallelFor.h"
#include "Component/AnimNextComponent.h"
#include "Entries/AnimNextEventGraphEntry.h"
#include "Module/AnimNextModule.h"
#include "Module/AnimNextModule_EditorData.h"
#include "Entries/AnimNextVariableEntry.h"
#include "Factories/Factory.h"
#include "Module/AnimNextModuleFactory.h"
#include "Module/RigUnit_AnimNextModuleEvents.h"
#include "Variables/AnimNextUniversalObjectLocatorBindingData.h"
#include "Module/AnimNextModuleInstance.h"

// AnimNext Parameters Tests

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::AnimNext::Tests
{

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVariableTypesTest, "Animation.AnimNext.VariableTypes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVariableTypesTest::RunTest(const FString& InParameters)
{
	// None is invalid
	FAnimNextParamType ParameterTypeValueNone(FAnimNextParamType::EValueType::None); 
	AddErrorIfFalse(!ParameterTypeValueNone.IsValid(), TEXT("Parameter type None is valid."));

	// None is invalid for all containers
	for(uint8 ContainerType = (uint8)FAnimNextParamType::EContainerType::None; ContainerType <= (uint8)FAnimNextParamType::EContainerType::Array; ++ContainerType)
	{
		FAnimNextParamType ParameterTypeValueContainerNone(FAnimNextParamType::EValueType::None, (FAnimNextParamType::EContainerType)ContainerType); 
		AddErrorIfFalse(!ParameterTypeValueContainerNone.IsValid(), FString::Printf(TEXT("Parameter type None, container type %d is valid."), ContainerType));
	}

	// Null object types
	for(uint8 ObjectValueType = (uint8)FAnimNextParamType::EValueType::Enum; ObjectValueType <= (uint8)FAnimNextParamType::EValueType::SoftClass; ++ObjectValueType)
	{
		for(uint8 ContainerType = (uint8)FAnimNextParamType::EContainerType::None; ContainerType <= (uint8)FAnimNextParamType::EContainerType::Array; ++ContainerType)
		{
			FAnimNextParamType ParameterTypeNullObject((FAnimNextParamType::EValueType)ObjectValueType, (FAnimNextParamType::EContainerType)ContainerType, nullptr); 
			AddErrorIfFalse(!ParameterTypeNullObject.IsValid(), FString::Printf(TEXT("Parameter type %d, container type %d with null object is valid."), ObjectValueType, ContainerType));
		}
	}

	// Non object types
	for(uint8 ValueType = (uint8)FAnimNextParamType::EValueType::Bool; ValueType < (uint8)FAnimNextParamType::EValueType::Enum; ++ValueType)
	{
		for(uint8 ContainerType = (uint8)FAnimNextParamType::EContainerType::None; ContainerType <= (uint8)FAnimNextParamType::EContainerType::Array; ++ContainerType)
		{
			FAnimNextParamType TestValueContainerParameterType((FAnimNextParamType::EValueType)ValueType, (FAnimNextParamType::EContainerType)ContainerType);
			AddErrorIfFalse(TestValueContainerParameterType.IsValid(), FString::Printf(TEXT("Parameter type %d, container type %d is invalid."), ValueType, ContainerType));
		}
	}

	UObject* ExampleValidObjects[(uint8)FAnimNextParamType::EValueType::SoftClass + 1] =
	{
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		FindObjectChecked<UEnum>(nullptr, TEXT("/Script/StructUtils.EPropertyBagPropertyType")),
		FAnimNextParamType::StaticStruct(),
		UObject::StaticClass(),
		UObject::StaticClass(),
		UObject::StaticClass(),
		UObject::StaticClass()
	};

	// Non-null valid object types
	for(uint8 ObjectValueType = (uint8)FAnimNextParamType::EValueType::Enum; ObjectValueType <= (uint8)FAnimNextParamType::EValueType::SoftClass; ++ObjectValueType)
	{
		for(uint8 ContainerType = (uint8)FAnimNextParamType::EContainerType::None; ContainerType <= (uint8)FAnimNextParamType::EContainerType::Array; ++ContainerType)
		{
			FAnimNextParamType TestValueContainerParameterType((FAnimNextParamType::EValueType)ObjectValueType, (FAnimNextParamType::EContainerType)ContainerType, ExampleValidObjects[ObjectValueType]);
			AddErrorIfFalse(TestValueContainerParameterType.IsValid(), FString::Printf(TEXT("Object parameter type %d, container type %d is invalid."), ObjectValueType, ContainerType));
		}
	}

	UObject* ExampleInvalidObjects[(uint8)FAnimNextParamType::EValueType::SoftClass + 1] =
	{
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		FAnimNextParamType::StaticStruct(),
		FindObjectChecked<UEnum>(nullptr, TEXT("/Script/StructUtils.EPropertyBagPropertyType")),
		FAnimNextParamType::StaticStruct(),
		FAnimNextParamType::StaticStruct(),
		FAnimNextParamType::StaticStruct(),
		FAnimNextParamType::StaticStruct()
	};

	// Non-null invalid object types
	for(uint8 ObjectValueType = (uint8)FAnimNextParamType::EValueType::Enum; ObjectValueType <= (uint8)FAnimNextParamType::EValueType::SoftClass; ++ObjectValueType)
	{
		for(uint8 ContainerType = (uint8)FAnimNextParamType::EContainerType::None; ContainerType <= (uint8)FAnimNextParamType::EContainerType::Array; ++ContainerType)
		{
			FAnimNextParamType TestValueContainerParameterType((FAnimNextParamType::EValueType)ObjectValueType, (FAnimNextParamType::EContainerType)ContainerType, ExampleInvalidObjects[ObjectValueType]);
			AddErrorIfFalse(!TestValueContainerParameterType.IsValid(), FString::Printf(TEXT("Object parameter type %d, container type %d is valid."), ObjectValueType, ContainerType));
		}
	}

	// Check type inference
	AddErrorIfFalse(FAnimNextParamType::GetType<bool>().IsValid(), TEXT("bool parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<uint8>().IsValid(), TEXT("uint8 parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<int32>().IsValid(), TEXT("int32 parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<int64>().IsValid(), TEXT("int64 parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<float>().IsValid(), TEXT("float parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<double>().IsValid(), TEXT("double parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<FName>().IsValid(), TEXT("FName parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<FString>().IsValid(), TEXT("FString parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<FText>().IsValid(), TEXT("FText parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<EPropertyBagContainerType>().IsValid(), TEXT("Enum parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<FAnimNextParamType>().IsValid(), TEXT("Struct parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<FVector>().IsValid(), TEXT("Struct parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<FTransform>().IsValid(), TEXT("Struct parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<FQuat>().IsValid(), TEXT("Struct parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<UObject*>().IsValid(), TEXT("UObject parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TObjectPtr<UObject>>().IsValid(), TEXT("TObjectPtr<UObject> parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<UClass*>().IsValid(), TEXT("UClass parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TSubclassOf<UObject>>().IsValid(), TEXT("TSubclassOf<UObject> parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TSoftObjectPtr<UObject>>().IsValid(), TEXT("TSoftObjectPtr<UObject> parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TSoftClassPtr<UObject>>().IsValid(), TEXT("TSoftClassPtr<UObject> parameter is invalid."));

	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<bool>>().IsValid(), TEXT("bool array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<uint8>>().IsValid(), TEXT("uint8 array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<int32>>().IsValid(), TEXT("int32 array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<int64>>().IsValid(), TEXT("int64 array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<float>>().IsValid(), TEXT("float array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<double>>().IsValid(), TEXT("double array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<FName>>().IsValid(), TEXT("FName array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<FString>>().IsValid(), TEXT("FString array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<FText>>().IsValid(), TEXT("FText array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<EPropertyBagContainerType>>().IsValid(), TEXT("Enum array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<FAnimNextParamType>>().IsValid(), TEXT("Struct array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<FVector>>().IsValid(), TEXT("Struct array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<FTransform>>().IsValid(), TEXT("Struct array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<FQuat>>().IsValid(), TEXT("Struct array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<UObject*>>().IsValid(), TEXT("UObject array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<TObjectPtr<UObject>>>().IsValid(), TEXT("TObjectPtr<UObject> array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<UClass*>>().IsValid(), TEXT("UClass array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<TSubclassOf<UObject>>>().IsValid(), TEXT("TSubclassOf<UObject> array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<TSoftObjectPtr<UObject>>>().IsValid(), TEXT("TSoftObjectPtr<UObject> array parameter is invalid."));
	AddErrorIfFalse(FAnimNextParamType::GetType<TArray<TSoftClassPtr<UObject>>>().IsValid(), TEXT("TSoftClassPtr<UObject> array parameter is invalid."));

	// Check type from property
	#define TEST_ANIMNEXT_PROPERTY(Type, Property) AddErrorIfFalse(FAnimNextParamType::FromProperty(FAnimNextParamTypeTestStruct::StaticStruct()->FindPropertyByName(#Property)) == FAnimNextParamType::GetType<Type>(), #Type " param type is invalid")
	#define TEST_ANIMNEXT_PROPERTY_ARRAY(Type, Property) AddErrorIfFalse(FAnimNextParamType::FromProperty(FAnimNextParamTypeTestStruct::StaticStruct()->FindPropertyByName(#Property"Array")) == FAnimNextParamType::GetType<TArray<Type>>(), #Type " array param type is invalid")

	TEST_ANIMNEXT_PROPERTY(bool, bBool);
	TEST_ANIMNEXT_PROPERTY(uint8, Uint8);
	TEST_ANIMNEXT_PROPERTY(int32, Int32);
	TEST_ANIMNEXT_PROPERTY(int64, Int64);
	TEST_ANIMNEXT_PROPERTY(float, Float);
	TEST_ANIMNEXT_PROPERTY(double, Double);
	TEST_ANIMNEXT_PROPERTY(FName, Name);
	TEST_ANIMNEXT_PROPERTY(FString, String);
	TEST_ANIMNEXT_PROPERTY(FText, Text);
	TEST_ANIMNEXT_PROPERTY(EPropertyBagContainerType, Enum);
	TEST_ANIMNEXT_PROPERTY(FAnimNextParamType, Struct);
	TEST_ANIMNEXT_PROPERTY(FVector, Vector);
	TEST_ANIMNEXT_PROPERTY(FTransform, Transform);
	TEST_ANIMNEXT_PROPERTY(TObjectPtr<UObject>, Object);
	TEST_ANIMNEXT_PROPERTY(TObjectPtr<UClass>, Class);
	TEST_ANIMNEXT_PROPERTY(TSubclassOf<UObject>, SubclassOf);
	TEST_ANIMNEXT_PROPERTY(TSoftObjectPtr<UObject>, SoftObjectPtr);
	TEST_ANIMNEXT_PROPERTY(TSoftClassPtr<UObject>, SoftClassPtr);

	TEST_ANIMNEXT_PROPERTY_ARRAY(bool, Bool);
	TEST_ANIMNEXT_PROPERTY_ARRAY(uint8, Uint8);
	TEST_ANIMNEXT_PROPERTY_ARRAY(int32, Int32);
	TEST_ANIMNEXT_PROPERTY_ARRAY(int64, Int64);
	TEST_ANIMNEXT_PROPERTY_ARRAY(float, Float);
	TEST_ANIMNEXT_PROPERTY_ARRAY(double, Double);
	TEST_ANIMNEXT_PROPERTY_ARRAY(FName, Name);
	TEST_ANIMNEXT_PROPERTY_ARRAY(FString, String);
	TEST_ANIMNEXT_PROPERTY_ARRAY(FText, Text);
	TEST_ANIMNEXT_PROPERTY_ARRAY(EPropertyBagContainerType, Enum);
	TEST_ANIMNEXT_PROPERTY_ARRAY(FAnimNextParamType, Struct);
	TEST_ANIMNEXT_PROPERTY_ARRAY(FVector, Vector);
	TEST_ANIMNEXT_PROPERTY_ARRAY(FTransform, Transform);
	TEST_ANIMNEXT_PROPERTY_ARRAY(TObjectPtr<UObject>, Object);
	TEST_ANIMNEXT_PROPERTY_ARRAY(TObjectPtr<UClass>, Class);
	TEST_ANIMNEXT_PROPERTY_ARRAY(TSubclassOf<UObject>, SubclassOf);
	TEST_ANIMNEXT_PROPERTY_ARRAY(TSoftObjectPtr<UObject>, SoftObjectPtr);
	TEST_ANIMNEXT_PROPERTY_ARRAY(TSoftClassPtr<UObject>, SoftClassPtr);

	#undef TEST_ANIMNEXT_PROPERTY
	#undef TEST_ANIMNEXT_PROPERTY_ARRAY

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVariables, "Animation.AnimNext.Variables", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVariables::RunTest(const FString& InParameters)
{
	ON_SCOPE_EXIT{ FUtils::CleanupAfterTests(); };

	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), UAnimNextModuleFactory::StaticClass());
	UAnimNextRigVMAsset* Asset = CastChecked<UAnimNextRigVMAsset>(Factory->FactoryCreateNew(UAnimNextModule::StaticClass(), GetTransientPackage(), TEXT("TestAsset"), RF_Transient, nullptr, nullptr, NAME_None));
	UE_RETURN_ON_ERROR(Asset != nullptr, "FVariables -> Failed to create asset");

	UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(Asset);
	UE_RETURN_ON_ERROR(EditorData != nullptr, "FVariables -> Asset has no editor data.");

	// Add variables
	UAnimNextVariableEntry* OperandAEntry = EditorData->AddVariable(TEXT("A"), FAnimNextParamType::GetType<int32>(), TEXT("1"));
	UE_RETURN_ON_ERROR(OperandAEntry != nullptr, TEXT("Could not create new variable in graph."));
	UAnimNextVariableEntry* OperandBEntry = EditorData->AddVariable(TEXT("B"), FAnimNextParamType::GetType<int32>(), TEXT("2"));
	UE_RETURN_ON_ERROR(OperandBEntry != nullptr, TEXT("Could not create new variable in graph."));
	UAnimNextVariableEntry* ResultEntry = EditorData->AddVariable(TEXT("Result"), FAnimNextParamType::GetType<int32>(), TEXT("12"));
	UE_RETURN_ON_ERROR(ResultEntry != nullptr, TEXT("Could not create new variable in graph."));

	// Get event graph
	UAnimNextEventGraphEntry* EventGraph = Cast<UAnimNextEventGraphEntry>(EditorData->FindEntry(TEXT("PrePhysics")));
	if(EventGraph == nullptr)
	{
		EventGraph = EditorData->AddEventGraph(TEXT("PrePhysics"), FRigUnit_AnimNextPrePhysicsEvent::StaticStruct());
	}
	UE_RETURN_ON_ERROR(EventGraph != nullptr, TEXT("Could not create new event graph in asset."));
	
	URigVMGraph* RigVMGraph = EventGraph->GetRigVMGraph();
	UE_RETURN_ON_ERROR(RigVMGraph->GetNodes().Num() == 1, TEXT("Unexpected number of nodes in new event graph."));

	URigVMNode* EventNode = RigVMGraph->GetNodes()[0];
	check(EventNode);
	URigVMPin* ExecutePin = EventNode->FindPin("ExecuteContext");
	UE_RETURN_ON_ERROR(ExecutePin != nullptr, TEXT("Could find initial execute pin."));

	URigVMController* Controller = EditorData->GetController(EventGraph->GetRigVMGraph());

	URigVMVariableNode* VariableANode = Controller->AddVariableNode("A", RigVMTypeUtils::Int32Type, nullptr, true, TEXT(""));
	UE_RETURN_ON_ERROR(VariableANode != nullptr, TEXT("Could not add get variable node."));
	URigVMVariableNode* VariableBNode = Controller->AddVariableNode("B", RigVMTypeUtils::Int32Type, nullptr, true, TEXT(""));
	UE_RETURN_ON_ERROR(VariableBNode != nullptr, TEXT("Could not add get variable node."));
	URigVMVariableNode* SetResultNode = Controller->AddVariableNode("Result", RigVMTypeUtils::Int32Type, nullptr, false, TEXT(""));
	UE_RETURN_ON_ERROR(SetResultNode != nullptr, TEXT("Could not add set variable node."));

	URigVMUnitNode* TestOpUnitNode = Controller->AddUnitNode(FAnimNextTests_TestOperation::StaticStruct());
	bool bLinkAAdded = Controller->AddLink(VariableANode->FindPin("Value"), TestOpUnitNode->FindPin("A"));
	UE_RETURN_ON_ERROR(bLinkAAdded, TEXT("Could not link variable node."));
	bool bLinkBAdded = Controller->AddLink(VariableBNode->FindPin("Value"), TestOpUnitNode->FindPin("B"));
	UE_RETURN_ON_ERROR(bLinkBAdded, TEXT("Could not link variable node."));
	bool bLinkResultAdded = Controller->AddLink(TestOpUnitNode->FindPin("Result"), SetResultNode->FindPin("Value"));
	UE_RETURN_ON_ERROR(bLinkResultAdded, TEXT("Could not link variable node."));

	bool bLinkExec1Added = Controller->AddLink(EventNode->FindPin(FRigVMStruct::ExecuteContextName.ToString()), TestOpUnitNode->FindPin(FRigVMStruct::ExecuteContextName.ToString()));
	UE_RETURN_ON_ERROR(bLinkExec1Added, TEXT("Could not link variable node exec."));

	bool bLinkExec2Added = Controller->AddLink(TestOpUnitNode->FindPin(FRigVMStruct::ExecuteContextName.ToString()), SetResultNode->FindPin(FRigVMStruct::ExecuteContextName.ToString()));
	UE_RETURN_ON_ERROR(bLinkExec2Added, TEXT("Could not link variable node exec."));

	URigVMUnitNode* PrintResultUnitNode = Controller->AddUnitNode(FAnimNextTests_PrintResult::StaticStruct());
	bool bLinkExec3Added = Controller->AddLink(SetResultNode->FindPin(FRigVMStruct::ExecuteContextName.ToString()), PrintResultUnitNode->FindPin(FRigVMStruct::ExecuteContextName.ToString()));
	UE_RETURN_ON_ERROR(bLinkExec3Added, TEXT("Could not link variable node exec."));

	URigVMVariableNode* GetResultNode = Controller->AddVariableNode("Result", RigVMTypeUtils::Int32Type, nullptr, true, TEXT(""));
	UE_RETURN_ON_ERROR(GetResultNode != nullptr, TEXT("Could not add get variable node."));
	bool bLinkResult2Added = Controller->AddLink(GetResultNode->FindPin("Value"), PrintResultUnitNode->FindPin("Result"));
	UE_RETURN_ON_ERROR(bLinkResult2Added, TEXT("Could not link variable node."));

	TArray<FString> Messages;
	FRigVMRuntimeSettings RuntimeSettings;
	RuntimeSettings.SetLogFunction([&Messages](const FRigVMLogSettings& InLogSettings, const FRigVMExecuteContext* InContext, const FString& Message)
	{
		Messages.Add(Message);
	});
	Asset->GetRigVMExtendedExecuteContext().SetRuntimeSettings(RuntimeSettings);

	Asset->GetVM()->ExecuteVM(Asset->GetRigVMExtendedExecuteContext(), FRigUnit_AnimNextPrePhysicsEvent::DefaultEventName);

	UE_RETURN_ON_ERROR(Messages.Num() == 1, TEXT("unexpected number of messages"));
	UE_RETURN_ON_ERROR(Messages[0] == TEXT("Result = 3"), TEXT("unexpected result message"));
	
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVariables_UOLBindings, "Animation.AnimNext.Variables.UOLBindings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVariables_UOLBindings::RunTest(const FString& InParameters)
{
	ON_SCOPE_EXIT{ FUtils::CleanupAfterTests(); };

	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), UAnimNextModuleFactory::StaticClass());
	UAnimNextModule* Asset = CastChecked<UAnimNextModule>(Factory->FactoryCreateNew(UAnimNextModule::StaticClass(), GetTransientPackage(), TEXT("TestAsset"), RF_Transient, nullptr, nullptr, NAME_None));
	UE_RETURN_ON_ERROR(Asset != nullptr, "FVariables_UOLBindings -> Failed to create asset");

	UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(Asset);
	UE_RETURN_ON_ERROR(EditorData != nullptr, "FVariables_UOLBindings -> Asset has no editor data.");

	UniversalObjectLocator::FParseStringParams ParseStringParams;
	ParseStringParams.Flags |= UniversalObjectLocator::EParseStringFlags::ErrorMessaging;

	// Add variables
	UAnimNextVariableEntry* PropertyEntry = EditorData->AddVariable(TEXT("Property"), FAnimNextParamType::GetType<int32>(), TEXT("1"));
	UE_RETURN_ON_ERROR(PropertyEntry != nullptr, TEXT("Could not create new variable in graph."));
	TInstancedStruct<FAnimNextUniversalObjectLocatorBindingData> BindingA = TInstancedStruct<FAnimNextUniversalObjectLocatorBindingData>::Make();
	FAnimNextUniversalObjectLocatorBindingData& BindingDataA = BindingA.GetMutable<FAnimNextUniversalObjectLocatorBindingData>();
	BindingDataA.Type = FAnimNextUniversalObjectLocatorBindingType::Property;
	BindingDataA.Property = UAnimNextTestFuncLib::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UAnimNextTestFuncLib, ValueA));
	UE_RETURN_ON_ERROR(BindingDataA.Property != nullptr, TEXT("Could not find property."));
	UE::UniversalObjectLocator::FParseStringResult ResultA = BindingDataA.Locator.TryParseString(TEXT("uobj://animobjfunc?&payload0=/Script/AnimNextTestSuite.AnimNextTestFuncLib:GetObj"), ParseStringParams);
	UE_RETURN_ON_ERROR(ResultA.bSuccess, TEXT("Could not parse UOL."));
	PropertyEntry->SetBinding(MoveTemp(BindingA));

	UAnimNextVariableEntry* ObjectAccessorEntry = EditorData->AddVariable(TEXT("ObjectAccessor"), FAnimNextParamType::GetType<int32>(), TEXT("2"));
	UE_RETURN_ON_ERROR(ObjectAccessorEntry != nullptr, TEXT("Could not create new variable in graph."));
	TInstancedStruct<FAnimNextUniversalObjectLocatorBindingData> BindingB = TInstancedStruct<FAnimNextUniversalObjectLocatorBindingData>::Make();
	FAnimNextUniversalObjectLocatorBindingData& BindingDataB = BindingB.GetMutable<FAnimNextUniversalObjectLocatorBindingData>();
	BindingDataB.Type = FAnimNextUniversalObjectLocatorBindingType::Function;
	BindingDataB.Function = UAnimNextTestFuncLib::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UAnimNextTestFuncLib, GetValueB));
	UE_RETURN_ON_ERROR(BindingDataB.Function != nullptr, TEXT("Could not find function."));
	UE::UniversalObjectLocator::FParseStringResult ResultB = BindingDataB.Locator.TryParseString(TEXT("uobj://animobjfunc?&payload0=/Script/AnimNextTestSuite.AnimNextTestFuncLib:GetObj"), ParseStringParams);
	UE_RETURN_ON_ERROR(ResultB.bSuccess, TEXT("Could not parse UOL."));
	ObjectAccessorEntry->SetBinding(MoveTemp(BindingB));

	UAnimNextVariableEntry* HoistedAccessorEntry = EditorData->AddVariable(TEXT("HoistedAccessor"), FAnimNextParamType::GetType<int32>(), TEXT("12"));
	UE_RETURN_ON_ERROR(HoistedAccessorEntry != nullptr, TEXT("Could not create new variable in graph."));
	TInstancedStruct<FAnimNextUniversalObjectLocatorBindingData> BindingC = TInstancedStruct<FAnimNextUniversalObjectLocatorBindingData>::Make();
	FAnimNextUniversalObjectLocatorBindingData& BindingDataC = BindingC.GetMutable<FAnimNextUniversalObjectLocatorBindingData>();
	BindingDataC.Type = FAnimNextUniversalObjectLocatorBindingType::HoistedFunction;
	BindingDataC.Function = UAnimNextTestFuncLib::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UAnimNextTestFuncLib, GetValueC));
	UE_RETURN_ON_ERROR(BindingDataC.Function != nullptr, TEXT("Could not find function."));
	UE::UniversalObjectLocator::FParseStringResult ResultC = BindingDataC.Locator.TryParseString(TEXT("uobj://animobjfunc?&payload0=/Script/AnimNextTestSuite.AnimNextTestFuncLib:GetObj"), ParseStringParams);
	UE_RETURN_ON_ERROR(ResultC.bSuccess, TEXT("Could not parse UOL."));
	HoistedAccessorEntry->SetBinding(MoveTemp(BindingC));

	// Get event graph
	UAnimNextEventGraphEntry* EventGraph = Cast<UAnimNextEventGraphEntry>(EditorData->FindEntry(TEXT("PrePhysics")));
	if(EventGraph == nullptr)
	{
		EventGraph = EditorData->AddEventGraph(TEXT("PrePhysics"), FRigUnit_AnimNextPrePhysicsEvent::StaticStruct());
	}
	UE_RETURN_ON_ERROR(EventGraph != nullptr, TEXT("Could not create new event graph in asset."));
	
	URigVMGraph* RigVMGraph = EventGraph->GetRigVMGraph();
	UE_RETURN_ON_ERROR(RigVMGraph->GetNodes().Num() == 1, TEXT("Unexpected number of nodes in new event graph."));

	URigVMNode* EventNode = RigVMGraph->GetNodes()[0];
	check(EventNode);
	URigVMPin* ExecutePin = EventNode->FindPin("ExecuteContext");
	UE_RETURN_ON_ERROR(ExecutePin != nullptr, TEXT("Could find initial execute pin."));

	URigVMController* Controller = EditorData->GetController(EventGraph->GetRigVMGraph());

	URigVMVariableNode* PropertyEntryNode = Controller->AddVariableNode("Property", RigVMTypeUtils::Int32Type, nullptr, true, TEXT(""));
	UE_RETURN_ON_ERROR(PropertyEntryNode != nullptr, TEXT("Could not add get variable node."));
	URigVMVariableNode* ObjectAccessorEntryNode = Controller->AddVariableNode("ObjectAccessor", RigVMTypeUtils::Int32Type, nullptr, true, TEXT(""));
	UE_RETURN_ON_ERROR(ObjectAccessorEntryNode != nullptr, TEXT("Could not add get variable node."));
	URigVMVariableNode* HoistedAccessorEntryNode = Controller->AddVariableNode("HoistedAccessor", RigVMTypeUtils::Int32Type, nullptr, true, TEXT(""));
	UE_RETURN_ON_ERROR(HoistedAccessorEntryNode != nullptr, TEXT("Could not add get variable node."));

	URigVMUnitNode* PrintPropertyUnitNode = Controller->AddUnitNode(FAnimNextTests_PrintResult::StaticStruct());
	bool bLinkAAdded = Controller->AddLink(PropertyEntryNode->FindPin("Value"), PrintPropertyUnitNode->FindPin("Result"));
	UE_RETURN_ON_ERROR(bLinkAAdded, TEXT("Could not link variable node."));

	bool bLinkExec1Added = Controller->AddLink(EventNode->FindPin(FRigVMStruct::ExecuteContextName.ToString()), PrintPropertyUnitNode->FindPin(FRigVMStruct::ExecuteContextName.ToString()));
	UE_RETURN_ON_ERROR(bLinkExec1Added, TEXT("Could not link variable node exec."));
	
	URigVMUnitNode* PrintObjectAccessorUnitNode = Controller->AddUnitNode(FAnimNextTests_PrintResult::StaticStruct());
	bool bLinkBAdded = Controller->AddLink(ObjectAccessorEntryNode->FindPin("Value"), PrintObjectAccessorUnitNode->FindPin("Result"));
	UE_RETURN_ON_ERROR(bLinkBAdded, TEXT("Could not link variable node."));
	
	bool bLinkExec2Added = Controller->AddLink(PrintPropertyUnitNode->FindPin(FRigVMStruct::ExecuteContextName.ToString()), PrintObjectAccessorUnitNode->FindPin(FRigVMStruct::ExecuteContextName.ToString()));
	UE_RETURN_ON_ERROR(bLinkExec2Added, TEXT("Could not link variable node exec."));

	URigVMUnitNode* PrintHoistedAccessorUnitNode = Controller->AddUnitNode(FAnimNextTests_PrintResult::StaticStruct());
	bool bLinkCAdded = Controller->AddLink(HoistedAccessorEntryNode->FindPin("Value"), PrintHoistedAccessorUnitNode->FindPin("Result"));
	UE_RETURN_ON_ERROR(bLinkCAdded, TEXT("Could not link variable node."));

	bool bLinkExec3Added = Controller->AddLink(PrintObjectAccessorUnitNode->FindPin(FRigVMStruct::ExecuteContextName.ToString()), PrintHoistedAccessorUnitNode->FindPin(FRigVMStruct::ExecuteContextName.ToString()));
	UE_RETURN_ON_ERROR(bLinkExec3Added, TEXT("Could not link variable node exec."));

	TArray<FString> Messages;
	FRigVMRuntimeSettings RuntimeSettings;
	RuntimeSettings.SetLogFunction([&Messages](const FRigVMLogSettings& InLogSettings, const FRigVMExecuteContext* InContext, const FString& Message)
	{
		Messages.Add(Message);
	});
	Asset->GetRigVMExtendedExecuteContext().SetRuntimeSettings(RuntimeSettings);
	
	FAnimNextExecuteContext& AnimNextContext = Asset->GetRigVMExtendedExecuteContext().GetPublicDataSafe<FAnimNextExecuteContext>();
	FAnimNextModuleInstance Instance(Asset, NewObject<UAnimNextComponent>(), nullptr, nullptr, EAnimNextModuleInitMethod::None);
	FAnimNextModuleContextData ContextData(&Instance);
	FScopedExecuteContextData ContextDataScope(AnimNextContext, ContextData);

	// Execute bindings to fetch data
	Asset->GetVM()->ExecuteVM(Asset->GetRigVMExtendedExecuteContext(), FRigUnit_AnimNextExecuteBindings_GT::EventName);

	// Run PrePhysics event to print messages
	Asset->GetVM()->ExecuteVM(Asset->GetRigVMExtendedExecuteContext(), FRigUnit_AnimNextPrePhysicsEvent::DefaultEventName);

	UE_RETURN_ON_ERROR(Messages.Num() == 3, TEXT("unexpected number of messages"));
	UE_RETURN_ON_ERROR(Messages[0] == TEXT("Result = 23"), TEXT("unexpected result message"));
	UE_RETURN_ON_ERROR(Messages[1] == TEXT("Result = 42"), TEXT("unexpected result message"));
	UE_RETURN_ON_ERROR(Messages[2] == TEXT("Result = 12345"), TEXT("unexpected result message"));

	return true;
}

}

#endif	// WITH_DEV_AUTOMATION_TESTS

FAnimNextTests_TestOperation_Execute()
{
	Result = A + B;
}

FAnimNextTests_PrintResult_Execute()
{
	ExecuteContext.Logf(EMessageSeverity::Info, TEXT("Result = %d"), Result);
}
