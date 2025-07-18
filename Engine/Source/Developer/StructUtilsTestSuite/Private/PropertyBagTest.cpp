// Copyright Epic Games, Inc. All Rights Reserved.

#include "AITestsCommon.h"
#include "StructUtilsTestTypes.h"

#define LOCTEXT_NAMESPACE "StructUtilsTests"

namespace FPropertyBagTest
{

struct FTest_CreatePropertyBag : FAITestBase
{
	virtual bool InstantTest() override
	{
		static const FName IsHotName(TEXT("bIsHot"));
		static const FName TemperatureName(TEXT("Temperature"));
		static const FName CountName(TEXT("Count"));
		static const FName UInt32Name(TEXT("Unsigned32"));
		static const FName UInt64Name(TEXT("Unsigned64"));

		FInstancedPropertyBag Bag;

		Bag.AddProperty(IsHotName, EPropertyBagPropertyType::Bool);
		AITEST_TRUE(TEXT("Should have bIsHot property"), Bag.FindPropertyDescByName(IsHotName) != nullptr);
		AITEST_TRUE(TEXT("Set bIsHot should succeed"), Bag.SetValueBool(IsHotName, true) == EPropertyBagResult::Success);

		// Amend the bag with new properties.
		Bag.AddProperties({
			{ TemperatureName, EPropertyBagPropertyType::Float },
			{ CountName, EPropertyBagPropertyType::Int32 },
			{ UInt32Name, EPropertyBagPropertyType::UInt32 },
			{ UInt64Name, EPropertyBagPropertyType::UInt64 }
			});
		AITEST_TRUE(TEXT("Set Temperature should succeed"), Bag.SetValueFloat(TemperatureName, 451.0f) == EPropertyBagResult::Success);
		AITEST_TRUE(TEXT("Set Count should succeed"), Bag.SetValueFloat(CountName, 42) == EPropertyBagResult::Success);

		AITEST_TRUE(TEXT("Set UInt32 should succeed"), Bag.SetValueUInt32(UInt32Name, MAX_uint32) == EPropertyBagResult::Success);
		AITEST_TRUE(TEXT("Set UInt64 should succeed"), Bag.SetValueUInt64(UInt64Name, MAX_uint64) == EPropertyBagResult::Success);

		AITEST_TRUE(TEXT("UInt32 value could not be retrieved"), Bag.GetValueUInt32(UInt32Name).HasError() == false);
		AITEST_TRUE(TEXT("UInt32 value not correct"), Bag.GetValueUInt32(UInt32Name).GetValue() == MAX_uint32);

		AITEST_TRUE(TEXT("UInt64 value could not be retrieved"), Bag.GetValueUInt64(UInt64Name).HasError() == false);
		AITEST_TRUE(TEXT("UInt64 value not correct"), Bag.GetValueUInt64(UInt64Name).GetValue() == MAX_uint64);

		Bag.RemovePropertyByName(IsHotName);
		AITEST_TRUE(TEXT("Should not have bIsHot property"), Bag.FindPropertyDescByName(IsHotName) == nullptr);
		AITEST_TRUE(TEXT("Set bIsHot should not succeed"), Bag.SetValueBool(IsHotName, true) != EPropertyBagResult::Success);
		
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FTest_CreatePropertyBag, "System.StructUtils.PropertyBag.CreateBag");

struct FTest_MovePropertyBag : FAITestBase
{
	virtual bool InstantTest() override
	{
		static const FName IsHotName(TEXT("bIsHot"));
		static const FName TemperatureName(TEXT("Temperature"));

		FInstancedPropertyBag Bag;

		Bag.AddProperty(IsHotName, EPropertyBagPropertyType::Bool);
		AITEST_TRUE(TEXT("Bag should have bIsHot property"), Bag.FindPropertyDescByName(IsHotName) != nullptr);
		AITEST_TRUE(TEXT("Bag set bIsHot should succeed"), Bag.SetValueBool(IsHotName, true) == EPropertyBagResult::Success);

		FInstancedPropertyBag Bag2(Bag);
		Bag2.AddProperty(TemperatureName, EPropertyBagPropertyType::Float);
		AITEST_TRUE(TEXT("Bag should have bIsHot property"), Bag.FindPropertyDescByName(IsHotName) != nullptr);
		AITEST_TRUE(TEXT("Bag2 should have bIsHot property"), Bag2.FindPropertyDescByName(IsHotName) != nullptr);
		AITEST_TRUE(TEXT("Bag2 should have Temperature property"), Bag2.FindPropertyDescByName(TemperatureName) != nullptr);

		FInstancedPropertyBag Bag3(MoveTemp(Bag));
		AITEST_TRUE(TEXT("Bag should not have bIsHot property"), Bag.FindPropertyDescByName(IsHotName) == nullptr);
		AITEST_TRUE(TEXT("Bag3 should have bIsHot property"), Bag3.FindPropertyDescByName(IsHotName) != nullptr);

		Bag = Bag2;
		AITEST_TRUE(TEXT("Bag should have bIsHot property"), Bag.FindPropertyDescByName(IsHotName) != nullptr);
		AITEST_TRUE(TEXT("Bag should have Temperature property"), Bag.FindPropertyDescByName(TemperatureName) != nullptr);
		AITEST_TRUE(TEXT("Bag2 should have bIsHot property"), Bag2.FindPropertyDescByName(IsHotName) != nullptr);
		AITEST_TRUE(TEXT("Bag2 should have Temperature property"), Bag2.FindPropertyDescByName(TemperatureName) != nullptr);

		Bag = MoveTemp(Bag2);
		AITEST_TRUE(TEXT("Bag should have bIsHot property"), Bag.FindPropertyDescByName(IsHotName) != nullptr);
		AITEST_TRUE(TEXT("Bag should have Temperature property"), Bag.FindPropertyDescByName(TemperatureName) != nullptr);
		AITEST_TRUE(TEXT("Bag2 should not have bIsHot property"), Bag2.FindPropertyDescByName(IsHotName) == nullptr);
		AITEST_TRUE(TEXT("Bag2 should not have Temperature property"), Bag2.FindPropertyDescByName(TemperatureName) == nullptr);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FTest_MovePropertyBag, "System.StructUtils.PropertyBag.MoveBag");

struct FTest_MigrateProperty : FAITestBase
{
	virtual bool InstantTest() override
	{
		static const FName TemperatureName(TEXT("Temperature"));

		FInstancedPropertyBag Bag;
		Bag.AddProperty(TemperatureName, EPropertyBagPropertyType::Float);
		AITEST_TRUE(TEXT("Bag should have Temperature property"), Bag.FindPropertyDescByName(TemperatureName) != nullptr);

		TValueOrError<float, EPropertyBagResult> FloatDefaultRes = Bag.GetValueFloat(TemperatureName);
		AITEST_TRUE(TEXT("Bag getting Temperature default value should succeed"), FloatDefaultRes.IsValid());
		AITEST_TRUE(TEXT("Bag Temperature default value should be 0"), FMath::IsNearlyEqual(FloatDefaultRes.GetValue(), 0.0f));
		
		AITEST_TRUE(TEXT("Bag set Temperature as float should succeed"), Bag.SetValueFloat(TemperatureName, 451.0f) == EPropertyBagResult::Success);
		TValueOrError<float, EPropertyBagResult> FloatRes = Bag.GetValueFloat(TemperatureName);
		AITEST_TRUE(TEXT("Bag Temperature as float should be 451"), FloatRes.IsValid() && FMath::IsNearlyEqual(FloatRes.GetValue(), 451.0f));

		AITEST_TRUE(TEXT("Bag set Temperature as int should succeed"), Bag.SetValueInt32(TemperatureName, 451) == EPropertyBagResult::Success);
		FloatRes = Bag.GetValueFloat(TemperatureName);
		AITEST_TRUE(TEXT("Bag Temperature as float should be 451"), FloatRes.IsValid() && FMath::IsNearlyEqual(FloatRes.GetValue(), 451.0f));
		TValueOrError<int64, EPropertyBagResult> Int64Res = Bag.GetValueInt64(TemperatureName);
		AITEST_TRUE(TEXT("Bag Temperature as int64 should be 451"), Int64Res.IsValid() && Int64Res.GetValue() == 451);

		Bag.AddProperty(TemperatureName, EPropertyBagPropertyType::Int32);
		const FPropertyBagPropertyDesc* TempDesc = Bag.FindPropertyDescByName(TemperatureName);
		AITEST_TRUE(TEXT("Temperature property should be int32"), TempDesc != nullptr && TempDesc->ValueType == EPropertyBagPropertyType::Int32);

		TValueOrError<int32, EPropertyBagResult> Int32Res = Bag.GetValueInt32(TemperatureName);
		AITEST_TRUE(TEXT("Bag Temperature as int32 should be 451"), Int32Res.IsValid() && Int32Res.GetValue() == 451);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FTest_MigrateProperty, "System.StructUtils.PropertyBag.MigrateProperty");

struct FTest_Object : FAITestBase
{
	virtual bool InstantTest() override
	{
		static const FName ObjectName(TEXT("Object"));

		UBagTestObject1* Test1 = NewObject<UBagTestObject1>();
		UBagTestObject2* Test2 = NewObject<UBagTestObject2>();
		UBagTestObject1Derived* Test1Derived = NewObject<UBagTestObject1Derived>();

		FInstancedPropertyBag Bag;
		Bag.AddProperty(ObjectName, EPropertyBagPropertyType::Object, UBagTestObject1::StaticClass());
		AITEST_TRUE(TEXT("Bag should have Object property"), Bag.FindPropertyDescByName(ObjectName) != nullptr);

		AITEST_TRUE(TEXT("Bag set Object to Test1Derived should succeed"), Bag.SetValueObject(ObjectName, Test1Derived) == EPropertyBagResult::Success);
		AITEST_FALSE(TEXT("Bag set Object to Test2 should fail"), Bag.SetValueObject(ObjectName, Test2) == EPropertyBagResult::Success);
		AITEST_TRUE(TEXT("Bag set Object to Test1 should succeed"), Bag.SetValueObject(ObjectName, Test1) == EPropertyBagResult::Success);

		TValueOrError<UBagTestObject1*, EPropertyBagResult> Test1Res = Bag.GetValueObject<UBagTestObject1>(ObjectName);
		TValueOrError<UBagTestObject1Derived*, EPropertyBagResult> Test1DerivedRes = Bag.GetValueObject<UBagTestObject1Derived>(ObjectName);
		
		AITEST_TRUE(TEXT("Bag get Object as Test1 should succeed"), Test1Res.IsValid());
		AITEST_FALSE(TEXT("Bag get Object as Test1Derived should fail"), Test1DerivedRes.IsValid()); // Note: the current value is Test1, and Cast should fail.

		// Test conversion from Object to SoftObject
		Bag.AddProperty(ObjectName, EPropertyBagPropertyType::SoftObject, UBagTestObject1::StaticClass());
		TValueOrError<UBagTestObject1*, EPropertyBagResult> Test1Res2 = Bag.GetValueObject<UBagTestObject1>(ObjectName);
		AITEST_TRUE(TEXT("Bag get Object as Test1 should succeed after migration soft object"), Test1Res2.IsValid());
		AITEST_TRUE(TEXT("Bag get Object Test1 should be Test1 after migration soft object"), Test1Res2.GetValue() == Test1);

		// Test conversion from SoftObject to Object
		Bag.AddProperty(ObjectName, EPropertyBagPropertyType::Object, UBagTestObject1::StaticClass());
		TValueOrError<UBagTestObject1*, EPropertyBagResult> Test1Res3 = Bag.GetValueObject<UBagTestObject1>(ObjectName);
		AITEST_TRUE(TEXT("Bag get Object as Test1 should succeed after migration object"), Test1Res3.IsValid());
		AITEST_TRUE(TEXT("Bag get Object Test1 should be Test1 after migration object"), Test1Res3.GetValue() == Test1);

		// Test conversion from different type
		Bag.AddProperty(ObjectName, EPropertyBagPropertyType::Object, UBagTestObject2::StaticClass());
		TValueOrError<UBagTestObject1*, EPropertyBagResult> Test1Res4 = Bag.GetValueObject<UBagTestObject1>(ObjectName);
		TValueOrError<UBagTestObject2*, EPropertyBagResult> Test2Res = Bag.GetValueObject<UBagTestObject2>(ObjectName);
		AITEST_FALSE(TEXT("Bag get Object as Test1 should fail after migration to test2"), Test1Res4.IsValid());
		AITEST_TRUE(TEXT("Bag get Object as Test1 should succeed after migration to test2"), Test2Res.IsValid());
		AITEST_TRUE(TEXT("Bag get Object Test2 should be null after migration to test2"), Test2Res.GetValue() == nullptr);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FTest_Object, "System.StructUtils.PropertyBag.Object");

struct FTest_Struct : FAITestBase
{
	virtual bool InstantTest() override
	{
		static const FName StructName(TEXT("Struct"));

		FInstancedPropertyBag Bag;
		Bag.AddProperty(StructName, EPropertyBagPropertyType::Struct, FTestStructSimple::StaticStruct());
		AITEST_TRUE(TEXT("Bag should have Struct property"), Bag.FindPropertyDescByName(StructName) != nullptr);

		FTestStructSimple Value;
		Value.Float = 42.0f;

		FTestStructComplex Value2;

		AITEST_TRUE(TEXT("Bag set Struct as struct view should succeed"), Bag.SetValueStruct(StructName, FConstStructView::Make(Value)) == EPropertyBagResult::Success);
		AITEST_TRUE(TEXT("Bag set Struct with template should succeed"), Bag.SetValueStruct(StructName, Value) == EPropertyBagResult::Success);
		AITEST_FALSE(TEXT("Bag set Struct to complex as struct view should succeed"), Bag.SetValueStruct(StructName, FConstStructView::Make(Value2)) == EPropertyBagResult::Success);
		AITEST_FALSE(TEXT("Bag set Struct to complex  with template should succeed"), Bag.SetValueStruct(StructName, Value2) == EPropertyBagResult::Success);
		
		TValueOrError<FStructView, EPropertyBagResult> Res1 = Bag.GetValueStruct(StructName);
		TValueOrError<FTestStructSimple*, EPropertyBagResult> Res2 = Bag.GetValueStruct<FTestStructSimple>(StructName);
		TValueOrError<FTestStructSimpleBase*, EPropertyBagResult> Res3 = Bag.GetValueStruct<FTestStructSimpleBase>(StructName);
		TValueOrError<FTestStructComplex*, EPropertyBagResult> Res4 = Bag.GetValueStruct<FTestStructComplex>(StructName);
		
		AITEST_TRUE(TEXT("Bag get Struct as struct view should succeed"), Res1.IsValid());
		AITEST_TRUE(TEXT("Bag get Struct as simple should succeed"), Res2.IsValid());
		AITEST_TRUE(TEXT("Bag result value should be 42"), FMath::IsNearlyEqual(Res2.GetValue()->Float, 42.0f));
		AITEST_TRUE(TEXT("Bag get Struct as simple base should succeed"), Res3.IsValid());
		AITEST_FALSE(TEXT("Bag get Struct as complex should succeed"), Res4.IsValid());

		Bag.AddProperty(StructName, EPropertyBagPropertyType::Bool);
		TValueOrError<FStructView, EPropertyBagResult> MigRes1 = Bag.GetValueStruct(StructName);
		TValueOrError<FTestStructSimple*, EPropertyBagResult> MigRes2 = Bag.GetValueStruct<FTestStructSimple>(StructName);
		
		AITEST_FALSE(TEXT("Bag get Struct as struct view should fail after migration"), MigRes1.IsValid());
		AITEST_FALSE(TEXT("Bag get Struct as simple should succeed after migration"), MigRes2.IsValid());

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FTest_Struct, "System.StructUtils.PropertyBag.Struct");

struct FTest_Class : FAITestBase
{
	virtual bool InstantTest() override
	{
		static const FName ClassName(TEXT("Class"));

		FInstancedPropertyBag Bag;
		Bag.AddProperty(ClassName, EPropertyBagPropertyType::Class, UBagTestObject1::StaticClass());
		AITEST_TRUE(TEXT("Bag should have Class property"), Bag.FindPropertyDescByName(ClassName) != nullptr);

		AITEST_TRUE(TEXT("Bag set Class to UBagTestObject1 should succeed"), Bag.SetValueClass(ClassName, UBagTestObject1::StaticClass()) == EPropertyBagResult::Success);
		AITEST_FALSE(TEXT("Bag set Class to UBagTestObject2 should fail"), Bag.SetValueClass(ClassName, UBagTestObject2::StaticClass()) == EPropertyBagResult::Success);
		AITEST_TRUE(TEXT("Bag set Class to UBagTestObject1Derived should succeed"), Bag.SetValueClass(ClassName, UBagTestObject1Derived::StaticClass()) == EPropertyBagResult::Success);

		TValueOrError<UClass*, EPropertyBagResult> Res1 = Bag.GetValueClass(ClassName);
		AITEST_TRUE(TEXT("Bag get Class should succeed"), Res1.IsValid());
		AITEST_TRUE(TEXT("Bag Class result should be UBagTestObject1Derived"), Res1.GetValue() == UBagTestObject1Derived::StaticClass());

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FTest_Class, "System.StructUtils.PropertyBag.Class");

struct FTest_SubclassOf : FAITestBase
{
	virtual bool InstantTest() override
	{
		static const FName PropertyName = GET_MEMBER_NAME_CHECKED(FTestStructWithSubClassOf, ClassProperty);
		const UScriptStruct& Struct = *FTestStructWithSubClassOf::StaticStruct();
		const FClassProperty& Property = *CastFieldChecked<FClassProperty>(Struct.FindPropertyByName(PropertyName));

		FInstancedPropertyBag Bag;
		Bag.AddProperty(PropertyName, &Property);
		const FPropertyBagPropertyDesc* BagPropertyDesc = Bag.FindPropertyDescByName(PropertyName);
		AITEST_NOT_NULL(TEXT("Expecting to find inserted property"), BagPropertyDesc);

#if WITH_EDITOR // FPropertyBagPropertyDesc::MetaClass is in WITH_EDITORONLY_DATA
		AITEST_NOT_NULL(TEXT("Created bag property MetaClass pointer"), BagPropertyDesc->MetaClass.Get());
		AITEST_EQUAL(TEXT("Created bag property MetaClass type"), BagPropertyDesc->MetaClass->GetFName(), Property.MetaClass->GetFName());
#endif // WITH_EDITOR

		AITEST_NOT_NULL(TEXT("Created bag property ValueTypeObject pointer"), BagPropertyDesc->ValueTypeObject.Get());
		AITEST_EQUAL(TEXT("Created bag property ValueTypeObject type"), BagPropertyDesc->ValueTypeObject->GetFName(), Property.MetaClass->GetFName());

		const FString ResultSuccess = UEnum::GetValueAsString(EPropertyBagResult::Success);

		EPropertyBagResult SetResult = Bag.SetValueClass(PropertyName, UBagTestObject1::StaticClass());
		AITEST_EQUAL(TEXT("SetValueClass result for allowed class")
			, FStringView(UEnum::GetValueAsString(SetResult))
			, FStringView(ResultSuccess));

		SetResult = Bag.SetValueClass(PropertyName, UClass::StaticClass());
		AITEST_NOT_EQUAL(TEXT("SetValueClass result for non-allowed class")
			, FStringView(UEnum::GetValueAsString(SetResult))
			, FStringView(ResultSuccess));

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FTest_SubclassOf, "System.StructUtils.PropertyBag.SubclassOf");

struct FTest_SoftClassPtr : FAITestBase
{
	virtual bool InstantTest() override
	{
		static const FName PropertyName = GET_MEMBER_NAME_CHECKED(FTestStructWithSubClassOf, SoftClassProperty);
		const UScriptStruct& Struct = *FTestStructWithSubClassOf::StaticStruct();
		const FSoftClassProperty& Property = *CastFieldChecked<FSoftClassProperty>(Struct.FindPropertyByName(PropertyName));

		FInstancedPropertyBag Bag;
		Bag.AddProperty(PropertyName, &Property);
		const FPropertyBagPropertyDesc* BagPropertyDesc = Bag.FindPropertyDescByName(PropertyName);
		AITEST_NOT_NULL(TEXT("Expecting to find inserted property"), BagPropertyDesc);

#if WITH_EDITOR // FPropertyBagPropertyDesc::MetaClass is in WITH_EDITORONLY_DATA
		AITEST_NOT_NULL(TEXT("Created bag property MetaClass pointer"), BagPropertyDesc->MetaClass.Get());
		AITEST_EQUAL(TEXT("Created bag property MetaClass type"), BagPropertyDesc->MetaClass->GetFName(), Property.MetaClass->GetFName());
#endif // WITH_EDITOR

		AITEST_NOT_NULL(TEXT("Created bag property ValueTypeObject pointer"), BagPropertyDesc->ValueTypeObject.Get());
		AITEST_EQUAL(TEXT("Created bag property ValueTypeObject type"), BagPropertyDesc->ValueTypeObject->GetFName(), Property.MetaClass->GetFName());

		const FString ResultSuccess = UEnum::GetValueAsString(EPropertyBagResult::Success);

		EPropertyBagResult SetResult = Bag.SetValueSoftPath(PropertyName, UBagTestObject1::StaticClass());
		AITEST_EQUAL(TEXT("SetValueSoftPath result for allowed class")
			, FStringView(UEnum::GetValueAsString(SetResult))
			, FStringView(ResultSuccess));

		SetResult = Bag.SetValueSoftPath(PropertyName, UClass::StaticClass());
		AITEST_NOT_EQUAL(TEXT("SetValueSoftPath result for non-allowed class")
			, FStringView(UEnum::GetValueAsString(SetResult))
			, FStringView(ResultSuccess));

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FTest_SoftClassPtr, "System.StructUtils.PropertyBag.SoftClassPtr");

struct FTest_Enum : FAITestBase
{
	virtual bool InstantTest() override
	{
		static const FName EnumName(TEXT("Enum"));

		FInstancedPropertyBag Bag;
		Bag.AddProperty(EnumName, EPropertyBagPropertyType::Enum, StaticEnum<EPropertyBagTest1>());
		AITEST_TRUE(TEXT("Bag should have Enum property"), Bag.FindPropertyDescByName(EnumName) != nullptr);

		AITEST_TRUE(TEXT("Bag set Enum to Foo should succeed"), Bag.SetValueEnum(EnumName, EPropertyBagTest1::Foo) == EPropertyBagResult::Success);
		AITEST_FALSE(TEXT("Bag set Enum to Bongo should fail"), Bag.SetValueEnum(EnumName, EPropertyBagTest2::Bongo) == EPropertyBagResult::Success);
		
		TValueOrError<EPropertyBagTest1, EPropertyBagResult> Res1 = Bag.GetValueEnum<EPropertyBagTest1>(EnumName);
		TValueOrError<EPropertyBagTest2, EPropertyBagResult> Res2 = Bag.GetValueEnum<EPropertyBagTest2>(EnumName);
		
		AITEST_TRUE(TEXT("Bag get Enum should succeed"), Res1.IsValid());
		AITEST_TRUE(TEXT("Bag Enum result should be Byte"), Res1.GetValue() == EPropertyBagTest1::Foo);
		AITEST_FALSE(TEXT("Bag get Enum with different type should fail"), Res2.IsValid());

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FTest_Enum, "System.StructUtils.PropertyBag.Enum");

struct FTest_ContainerTypes : FAITestBase
{
	virtual bool InstantTest() override
	{
		FPropertyBagContainerTypes Container = { EPropertyBagContainerType::None, EPropertyBagContainerType::None };
		AITEST_TRUE(TEXT("Invalid Num Containers after creation."), Container.Num() == 0);
		AITEST_TRUE(TEXT("Invalid First Container type after creation."), Container.GetFirstContainerType() == EPropertyBagContainerType::None);

		Container.Add(EPropertyBagContainerType::Array);
		AITEST_TRUE(TEXT("Invalid num containers"), Container.Num() == 1);
		AITEST_TRUE(TEXT("Invalid First Container type."), Container.GetFirstContainerType() == EPropertyBagContainerType::Array);

		Container.Add(EPropertyBagContainerType::Array);
		AITEST_TRUE(TEXT("Invalid num containers"), Container.Num() == 2);

		const EPropertyBagContainerType HeadContainerType1 = Container.PopHead();
		AITEST_TRUE(TEXT("Invalid num containers"), Container.Num() == 1);
		AITEST_TRUE(TEXT("Invalid extracted head container 1"), HeadContainerType1 == EPropertyBagContainerType::Array);
		AITEST_TRUE(TEXT("Invalid first container type after removing Head 1"), Container.GetFirstContainerType() == EPropertyBagContainerType::Array);

		const EPropertyBagContainerType HeadContainerType2 = Container.PopHead();
		AITEST_TRUE(TEXT("Invalid num containers"), Container.Num() == 0);
		AITEST_TRUE(TEXT("Invalid extracted head container 2"), HeadContainerType2 == EPropertyBagContainerType::Array);
		AITEST_TRUE(TEXT("Invalid first container type after removing Head 2"), Container.GetFirstContainerType() == EPropertyBagContainerType::None);

		Container.Add(EPropertyBagContainerType::None);
		AITEST_TRUE(TEXT("Adding None sould not change Num containers"), Container.Num() == 0);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FTest_ContainerTypes, "System.StructUtils.PropertyBag.ContainerTypes");

struct FTest_NestedArray : FAITestBase
{
	virtual bool InstantTest() override
	{
		static const FName NestedInt32ArrayPropName(TEXT("NestedInt32ArrayProp"));
		static const TArray<TArray<int32>> NestedInt32ArrayTestValue = { { 1, 2, 3} };

		FInstancedPropertyBag Bag;

		// Set properties
		{
			Bag.AddContainerProperty(NestedInt32ArrayPropName, { EPropertyBagContainerType::Array, EPropertyBagContainerType::Array }, EPropertyBagPropertyType::Int32, nullptr);

			AITEST_TRUE(TEXT("Missing Float Array property in the Bag."), Bag.FindPropertyDescByName(NestedInt32ArrayPropName) != nullptr);
		}

		// Check Default Value
		{
			TValueOrError<const FPropertyBagArrayRef, EPropertyBagResult> NestedInt32ArrayDefaultResult = Bag.GetArrayRef(NestedInt32ArrayPropName);

			AITEST_TRUE(TEXT("Bag getting Nested Int32 Array default value should succeed."), NestedInt32ArrayDefaultResult.IsValid());

			if (NestedInt32ArrayDefaultResult.IsValid())
			{
				AITEST_TRUE(TEXT("Bag Nested Int32 Array default value incorrect size"), NestedInt32ArrayDefaultResult.GetValue().Num() == 0);
			}
		}

		// Set Nested Array values using FPropertyBagArrayRef interface
		{
			TValueOrError<FPropertyBagArrayRef, EPropertyBagResult> NestedInt32ArrayMutable = Bag.GetMutableArrayRef(NestedInt32ArrayPropName);

			AITEST_TRUE(TEXT("Getting PropertyBag Nested Int32 Array should succeed."), NestedInt32ArrayMutable.IsValid());

			FPropertyBagArrayRef& PropertyBagArrayRef = NestedInt32ArrayMutable.GetValue();

			const int32 NumArrays = NestedInt32ArrayTestValue.Num();
			PropertyBagArrayRef.AddValues(NumArrays);

			for (int32 n = 0; n < NumArrays; n++)
			{
				TValueOrError<FPropertyBagArrayRef, EPropertyBagResult> InnerArrayTestResult = PropertyBagArrayRef.GetMutableNestedArrayRef(n);
					
				AITEST_TRUE(TEXT("Getting PropertyBag Nested Inner Int32 Array should succeed."), InnerArrayTestResult.IsValid());
					
				FPropertyBagArrayRef& InnerPropertyBagArrayRef = InnerArrayTestResult.GetValue();

				const int32 NumElem = NestedInt32ArrayTestValue[n].Num();
				InnerPropertyBagArrayRef.AddUninitializedValues(NumElem);

				for (int32 i = 0; i < NumElem; i++)
				{
					const int32& value = NestedInt32ArrayTestValue[n][i];

					AITEST_TRUE(TEXT("Setting value to Nested Inner Array property failed."), InnerPropertyBagArrayRef.SetValueInt32(i, value) == EPropertyBagResult::Success);
				}
			}
		}

		// Test Nested Array Values using FPropertyBagArrayRef interface
		{
			TValueOrError<const FPropertyBagArrayRef, EPropertyBagResult> NestedInt32ArrayTestResult = Bag.GetArrayRef(NestedInt32ArrayPropName);

			AITEST_TRUE(TEXT("Getting PropertyBag Nested Int32 Array should succeed."), NestedInt32ArrayTestResult.IsValid());

			if (NestedInt32ArrayTestResult.IsValid())
			{
				const FPropertyBagArrayRef& NestedPropertyBagArrayRef = NestedInt32ArrayTestResult.GetValue();

				const int32 NumArrays = NestedPropertyBagArrayRef.Num();
				const int32 NumTestArrays = NestedInt32ArrayTestValue.Num();

				AITEST_TRUE(TEXT("Bag [%s] Nested Int32 Array Num value mismatch."), NumArrays == NumTestArrays);

				for (int32 n = 0; n < NumArrays; ++n)
				{
					TValueOrError<const FPropertyBagArrayRef, EPropertyBagResult> InnerArrayTestResult = NestedPropertyBagArrayRef.GetNestedArrayRef();

					AITEST_TRUE(TEXT("Getting PropertyBag Nested Inner Int32 Array should succeed."), InnerArrayTestResult.IsValid());

					const FPropertyBagArrayRef& PropertyBagArrayRef = InnerArrayTestResult.GetValue();

					const int32 NumElem = InnerArrayTestResult.GetValue().Num();
					for (int32 i = 0; i < NumElem; i++)
					{
						const TValueOrError<int32, EPropertyBagResult> Int32Res = PropertyBagArrayRef.GetValueInt32(i);

						AITEST_TRUE(TEXT("Getting Nested Array Element should succeed."), Int32Res.IsValid());

						if (Int32Res.IsValid())
						{
							AITEST_TRUE(TEXT("Nested Arrauy test value mismatch."), Int32Res.GetValue() == NestedInt32ArrayTestValue[n][i]);
						}
					}
				}
			}
		}

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FTest_NestedArray, "System.StructUtils.PropertyBag.NestedArray");

struct FTest_GC : FAITestBase
{
	virtual bool InstantTest() override
	{
		static const FName EnumName(TEXT("Enum"));

		UTestObjectWithPropertyBag* Obj = NewObject<UTestObjectWithPropertyBag>();
		Obj->Bag.AddProperty(EnumName, EPropertyBagPropertyType::Enum, StaticEnum<EPropertyBagTest1>());

		const UPropertyBag* BagStruct = Obj->Bag.GetPropertyBagStruct();
		check(BagStruct);
		
		const FString BagStructName = BagStruct->GetName();
		const FString ObjName = Obj->GetName();

		// Obj is unreachable, it should be collected by the GC.
		Obj = nullptr;
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

		// The used property bag struct should exists after the GC.
		const UPropertyBag* ExistingObj = FindObject<UPropertyBag>(GetTransientPackage(), *ObjName);
		UPropertyBag* ExistingBagStruct1 = FindObject<UPropertyBag>(GetTransientPackage(), *BagStructName);

		AITEST_NULL(TEXT("Obj should have been released"), ExistingObj);
		AITEST_NOT_NULL(TEXT("Bag struct should exists after Obj released"), ExistingBagStruct1);

		// The next GC should collect the bag struct
		ExistingBagStruct1->ClearFlags(RF_Standalone);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

		const UPropertyBag* ExistingBagStruct2 = FindObject<UPropertyBag>(GetTransientPackage(), *BagStructName);
		AITEST_NULL(TEXT("Bag struct should not exists after second GC"), ExistingBagStruct2);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FTest_GC, "System.StructUtils.PropertyBag.GC");

struct FTest_Arrays : FAITestBase
{
	virtual bool InstantTest() override
	{
		static const FName FloatArrayName(TEXT("FloatArray"));

		FInstancedPropertyBag Bag;
		Bag.AddProperties({
			{ FloatArrayName, EPropertyBagContainerType::Array, EPropertyBagPropertyType::Float },
		});

		TValueOrError<const FPropertyBagArrayRef, EPropertyBagResult> FloatArrayRes = Bag.GetArrayRef(FloatArrayName);
		AITEST_TRUE(TEXT("Get float array should succeed"), FloatArrayRes.IsValid());

		FPropertyBagArrayRef FloatArray = FloatArrayRes.GetValue();
		const int32 FloatIndex = FloatArray.AddValue();
		AITEST_TRUE(TEXT("Float array should have 1 item"), FloatArray.Num() == 1);

		const TValueOrError<float, EPropertyBagResult> GetDefaultFloatRes = FloatArray.GetValueFloat(FloatIndex);
		AITEST_TRUE(TEXT("Get float should succeed immediately after add"), GetDefaultFloatRes.IsValid());
		AITEST_TRUE(TEXT("Default value for Float should be 0.0f"), FMath::IsNearlyEqual(GetDefaultFloatRes.GetValue(), 0.0f));

		const EPropertyBagResult SetFloatRes = FloatArray.SetValueFloat(FloatIndex, 123.0f);
		AITEST_TRUE(TEXT("Set float should succeed"), SetFloatRes == EPropertyBagResult::Success);

		const TValueOrError<float, EPropertyBagResult> GetFloatRes = FloatArray.GetValueFloat(FloatIndex);
		AITEST_TRUE(TEXT("Get float should succeed"), GetFloatRes.IsValid());
		AITEST_TRUE(TEXT("Float value should be 123.0f"), FMath::IsNearlyEqual(GetFloatRes.GetValue(), 123.0f));

		const TValueOrError<float, EPropertyBagResult> GetFloatOOBRes = FloatArray.GetValueFloat(42);
		AITEST_FALSE(TEXT("Get float out of bounds should not succeed"), GetFloatOOBRes.IsValid());
		AITEST_TRUE(TEXT("Error should be our of bounds"), GetFloatOOBRes.GetError() == EPropertyBagResult::OutOfBounds);

		const EPropertyBagResult SetFloatOOBRes = FloatArray.SetValueFloat(-1, 0.0);
		AITEST_TRUE(TEXT("Set float out of bounds should return out of bounds"), SetFloatOOBRes == EPropertyBagResult::OutOfBounds);
		
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FTest_Arrays, "System.StructUtils.PropertyBag.Arrays");

struct FTest_Sets : FAITestBase
{
	virtual bool InstantTest() override
	{
		static const FName FloatSetName(TEXT("FloatSet"));
		static const FName EnumSetName(TEXT("EnumSet"));
		static const FName StructSetName(TEXT("StructSet"));
		static const FName ObjectSetName(TEXT("ObjectSet"));
		static const FName ClassSetName(TEXT("ClassSet"));

		FInstancedPropertyBag Bag;
		Bag.AddProperties({
			{ FloatSetName, EPropertyBagContainerType::Set, EPropertyBagPropertyType::Float },
			{ EnumSetName, EPropertyBagContainerType::Set, EPropertyBagPropertyType::Enum, StaticEnum<EPropertyBagTest1>() },
			{ StructSetName, EPropertyBagContainerType::Set, EPropertyBagPropertyType::Struct, FTestStructHashable1::StaticStruct()},
			{ ObjectSetName, EPropertyBagContainerType::Set, EPropertyBagPropertyType::Object, UBagTestObject1::StaticClass()},
			{ ClassSetName, EPropertyBagContainerType::Set, EPropertyBagPropertyType::Class, UBagTestObject1::StaticClass()}
		});

		//Test Numeric Type Set
		TValueOrError<const FPropertyBagSetRef, EPropertyBagResult> FloatSetRes = Bag.GetSetRef(FloatSetName);
		AITEST_TRUE(TEXT("Bag should have float set"), FloatSetRes.IsValid());

		FPropertyBagSetRef FloatSet = FloatSetRes.GetValue();
		float FloatValue1 = 1.f;
		TValueOrError<bool, EPropertyBagResult> EmptySetContainsResult = FloatSet.Contains(FloatValue1);
		AITEST_TRUE(TEXT("Float set contain result should have value"), EmptySetContainsResult.HasValue());
		AITEST_EQUAL(TEXT("Float set contain result should be false"), EmptySetContainsResult.GetValue(), false);

		const EPropertyBagResult SetFloatRes = FloatSet.AddValueFloat(FloatValue1);
		AITEST_EQUAL(TEXT("Float set should have 1 item"), FloatSet.Num(), 1);
		AITEST_EQUAL(TEXT("Set float should succeed"), SetFloatRes, EPropertyBagResult::Success);

		TValueOrError<bool, EPropertyBagResult> FilledSetContainsResult = FloatSet.Contains(FloatValue1);
		AITEST_TRUE(TEXT("Float set contain result should have value"), FilledSetContainsResult.HasValue());
		AITEST_EQUAL(TEXT("Float set contain result should be true"), FilledSetContainsResult.GetValue(), true);

		const float FloatValue2 = 2.f;
		const EPropertyBagResult NewSetFloatRes = FloatSet.AddValueFloat(FloatValue2);
		AITEST_TRUE(TEXT("Setting a new float value should succeed"), NewSetFloatRes == EPropertyBagResult::Success);
		AITEST_EQUAL(TEXT("Float set should have 2 items"), FloatSet.Num(), 2);

		const float FloatValue3 = 3.f;
		TValueOrError<bool, EPropertyBagResult> ContainsUnknownResult = FloatSet.Contains(FloatValue3);
		AITEST_EQUAL(TEXT("Calling Contains with a value not stored in the set should return false"), ContainsUnknownResult.GetValue(), false);

		EPropertyBagResult RemoveUnknownResult = FloatSet.Remove(FloatValue3);
		AITEST_EQUAL(TEXT("Calling Remove with a value not stored in the set should return a property not found error"), RemoveUnknownResult, EPropertyBagResult::PropertyNotFound);
		AITEST_EQUAL(TEXT("Float set should still 2 items after failed removal"), FloatSet.Num(), 2);

		const EPropertyBagResult AddExistingFloatRes = FloatSet.AddValueFloat(FloatValue2);
		AITEST_EQUAL(TEXT("Setting an existing element to a value already present in the set should return a duplicated value error"), AddExistingFloatRes, EPropertyBagResult::DuplicatedValue);

		EPropertyBagResult RemoveKnownResult = FloatSet.Remove(FloatValue2);
		AITEST_EQUAL(TEXT("Removing an element in the set should result in success"), RemoveKnownResult, EPropertyBagResult::Success);
		AITEST_EQUAL(TEXT("Float set should have 1 item after successfull removal"), FloatSet.Num(), 1);

		const int32 IntValue = 3;
		const EPropertyBagResult SetIntRes = FloatSet.AddValueInt32(IntValue);
		AITEST_EQUAL(TEXT("Setting a signed integer on a float set should succeed"), SetIntRes, EPropertyBagResult::Success);
		AITEST_TRUE(TEXT("Float set should contain the new int value"), FloatSet.Contains(IntValue).GetValue());

		const uint32 UIntValue = 4;
		const EPropertyBagResult SetUIntRes = FloatSet.AddValueUInt32(UIntValue);
		AITEST_EQUAL(TEXT("Setting an unsigned integer on a float set should succeed"), SetUIntRes, EPropertyBagResult::Success);
		AITEST_TRUE(TEXT("Float set should contain the new uint value"), FloatSet.Contains(UIntValue).GetValue());

		const FString TestString = TEXT("TestString");
		const EPropertyBagResult SetStringResult = FloatSet.AddValueString(TestString);
		AITEST_NOT_EQUAL(TEXT("Setting a string on a float set should not succeed"), SetStringResult, EPropertyBagResult::Success);
		AITEST_FALSE(TEXT("Float set should not contain the new string value"), FloatSet.Contains(TestString).GetValue());

		//Test Enum Set
		TValueOrError<const FPropertyBagSetRef, EPropertyBagResult> EnumSetRes = Bag.GetSetRef(EnumSetName);
		AITEST_TRUE(TEXT("Bag should have Enum set property"), EnumSetRes.IsValid());
		FPropertyBagSetRef EnumSet = EnumSetRes.GetValue();

		AITEST_EQUAL(TEXT("Adding enum value to set should succeed"), EnumSet.AddValueEnum(EPropertyBagTest1::Foo), EPropertyBagResult::Success);
		AITEST_TRUE(TEXT("We should be able to find the enum value we just added"), EnumSet.Contains(EPropertyBagTest1::Foo).GetValue());
		AITEST_EQUAL(TEXT("Adding a different enum value to set should succeed"), EnumSet.AddValueEnum(EPropertyBagTest1::Bar), EPropertyBagResult::Success);
		AITEST_EQUAL(TEXT("Adding an already store enum value should return a duplicated value error"), EnumSet.AddValueEnum(EPropertyBagTest1::Bar), EPropertyBagResult::DuplicatedValue);
		AITEST_EQUAL(TEXT("Adding value from a different enum type to set should return a type mismatch error"), EnumSet.AddValueEnum(EPropertyBagTest2::Bongo), EPropertyBagResult::TypeMismatch);
		AITEST_EQUAL(TEXT("Adding a non enum value to an enum set should return a type mismatch error"), EnumSet.AddValueInt32(1), EPropertyBagResult::TypeMismatch);
 
		//Test Struct Set
		TValueOrError<const FPropertyBagSetRef, EPropertyBagResult> StructSetRes = Bag.GetSetRef(StructSetName);
		AITEST_TRUE(TEXT("Bag should have Struct set property"), StructSetRes.IsValid());
		FPropertyBagSetRef StructSet = StructSetRes.GetValue();

		FTestStructHashable1 TestStructInstance1;
		TestStructInstance1.Float = 1.0f;

		FTestStructHashable1 TestStructInstance2;
		TestStructInstance2.Float = 2.0f;

		FTestStructComplex ComplexStructInstance;

		AITEST_EQUAL(TEXT("Adding struct value to set should succeed"), StructSet.AddValueStruct(FConstStructView::Make(TestStructInstance1)), EPropertyBagResult::Success);
		AITEST_TRUE(TEXT("We should be able to find the struct we just added"), StructSet.Contains(FConstStructView::Make(TestStructInstance1).GetMemory()).GetValue());
		AITEST_EQUAL(TEXT("Adding a different struct value to set should succeed"), StructSet.AddValueStruct(FConstStructView::Make(TestStructInstance2)), EPropertyBagResult::Success);
		AITEST_EQUAL(TEXT("Adding the same struct value to set should return a duplicated value error"), StructSet.AddValueStruct(FConstStructView::Make(TestStructInstance2)), EPropertyBagResult::DuplicatedValue);
		AITEST_EQUAL(TEXT("Adding a different struct type should return a type mismatch error"), StructSet.AddValueStruct(FConstStructView::Make(ComplexStructInstance)), EPropertyBagResult::TypeMismatch);
		AITEST_EQUAL(TEXT("Adding a non struct type to a struct set should return a type mismatch error"), StructSet.AddValueInt32(1), EPropertyBagResult::TypeMismatch);

		//Test Object Set
		UBagTestObject1* TestObject1_Instance1 = NewObject<UBagTestObject1>();
		UBagTestObject1* TestObject1_Instance2 = NewObject<UBagTestObject1>();
		UBagTestObject2* TestObject2 = NewObject<UBagTestObject2>();
		UBagTestObject1Derived* TestObject1Derived = NewObject<UBagTestObject1Derived>();

		TValueOrError<const FPropertyBagSetRef, EPropertyBagResult> ObjectSetRes = Bag.GetSetRef(ObjectSetName);
		AITEST_TRUE(TEXT("Bag should have Object set property"), ObjectSetRes.IsValid());
		FPropertyBagSetRef ObjectSet = ObjectSetRes.GetValue();

		AITEST_EQUAL(TEXT("Adding a TestObject1 type instance to set should succeed"), ObjectSet.AddValueObject(TestObject1_Instance1), EPropertyBagResult::Success);
		AITEST_TRUE(TEXT("We should be able to find the object we just added"), ObjectSet.Contains(TestObject1_Instance1).GetValue());
		AITEST_EQUAL(TEXT("Adding a second instance of type TestObject1 to set should succeed"), ObjectSet.AddValueObject(TestObject1_Instance2), EPropertyBagResult::Success);
		AITEST_EQUAL(TEXT("Adding an object type derived from TestObject1 to set should succeed"), ObjectSet.AddValueObject(TestObject1Derived), EPropertyBagResult::Success);
		AITEST_EQUAL(TEXT("Adding a TestObject2 type instance to a TestObject1 type set should return a type mismatch error"), ObjectSet.AddValueObject(TestObject2), EPropertyBagResult::TypeMismatch);

		//Test Class Set
		TValueOrError<const FPropertyBagSetRef, EPropertyBagResult> ClassSetRes = Bag.GetSetRef(ClassSetName);
		AITEST_TRUE(TEXT("Bag should have Object set property"), ClassSetRes.IsValid());
		FPropertyBagSetRef ClassSet = ClassSetRes.GetValue();

		AITEST_EQUAL(TEXT("Adding a class to a class set should succeed"), ClassSet.AddValueClass(UBagTestObject1::StaticClass()), EPropertyBagResult::Success);
		AITEST_TRUE(TEXT("We should be able to find the class we just added"), ClassSet.Contains(UBagTestObject1::StaticClass()).GetValue());
		AITEST_EQUAL(TEXT("Adding a different type to a class set should return a type mismatch error"), ClassSet.AddValueClass(UBagTestObject2::StaticClass()), EPropertyBagResult::TypeMismatch);
		AITEST_EQUAL(TEXT("Adding a derived class type to a class set should succeed"), ClassSet.AddValueClass(UBagTestObject1Derived::StaticClass()), EPropertyBagResult::Success);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FTest_Sets, "System.StructUtils.PropertyBag.Sets");

struct FTest_SameBag : FAITestBase
{
	virtual bool InstantTest() override
	{
		static const FName TemperatureName(TEXT("Temperature"));
		static const FName CountName(TEXT("Count"));

		FInstancedPropertyBag BagA;
		BagA.AddProperties({
			{ TemperatureName, EPropertyBagPropertyType::Float },
			{ CountName, EPropertyBagPropertyType::Int32 }
		});


		FInstancedPropertyBag BagB;
		BagB.AddProperties({
			{ TemperatureName, EPropertyBagPropertyType::Float },
			{ CountName, EPropertyBagPropertyType::Int32 }
		});

		// Same descriptors should result in same bag struct
		AITEST_TRUE(TEXT("Property bags should match"), BagA.GetPropertyBagStruct() == BagB.GetPropertyBagStruct());

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FTest_SameBag, "System.StructUtils.PropertyBag.SameBag");

} // FPropertyBagTest

#undef LOCTEXT_NAMESPACE
