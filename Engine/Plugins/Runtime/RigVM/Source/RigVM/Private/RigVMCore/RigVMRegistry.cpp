// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMRegistry.h"
#include "AssetRegistry/AssetData.h"
#include "RigVMCore/RigVMStruct.h"
#include <RigVMCore/RigVMTrait.h>
#include "RigVMTypeUtils.h"
#include "RigVMStringUtils.h"
#include "RigVMModule.h"
#include "Animation/AttributeTypes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/UObjectIterator.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DelayedAutoRegister.h"
#include "RigVMFunctions/RigVMDispatch_Core.h"
#include "Interfaces/IPluginManager.h"

// When the object system has been completely loaded, load in all the engine types that we haven't registered already in InitializeIfNeeded 
static FDelayedAutoRegisterHelper GRigVMRegistrySingletonHelper(EDelayedRegisterRunPhase::EndOfEngineInit, &FRigVMRegistry_NoLock::OnEngineInit);

static TAutoConsoleVariable<bool> CVarRigVMUpdateDispatchFactoriesGreedily(TEXT("RigVM.UpdateDispatchFactoriesGreedily"), true, TEXT("Set this to false to avoid loading dispatch factories during engine init / plugin mount."));

FRigVMRegistry_NoLock::FRigVMRegistry_NoLock() :
	bAvoidTypePropagation(false),
	bEverRefreshedEngineTypes(false),
	bEverRefreshedDispatchFactoriesAfterEngineInit(false)
{
}

FRigVMRegistry_NoLock& FRigVMRegistry_NoLock::Get(ELockType InLockType)
{
#if WITH_EDITOR
	FRigVMRegistry_RWLock::EnsureLocked(InLockType);
#endif
	return FRigVMRegistry_RWLock::Get();
}

FRigVMRegistry_NoLock::~FRigVMRegistry_NoLock()
{
	FRigVMRegistry_NoLock::Reset_NoLock();
}

void FRigVMRegistry_NoLock::AddReferencedObjects(FReferenceCollector& Collector)
{
	// registry should hold strong references to these type objects
	// otherwise GC may remove them without the registry known it
	// which can happen during cook time.
	for (FTypeInfo& Type : Types)
	{
		// the Object needs to be checked for validity since it may be a user defined type (struct or enum)
		// which is about to get removed. 
		if (Type.Type.CPPTypeObject)
		{
#if !UE_BUILD_SHIPPING
			// in non shipping builds, immediately run IsValidLowLevelFast such that
			// we can catch invalid types earlier via a direct crash more often
			if (Type.Type.CPPTypeObject->IsValidLowLevelFast())
			{
				// By design, hold strong references only to non-native types
				if (!Type.Type.CPPTypeObject->IsNative())
				{
					Collector.AddReferencedObject(Type.Type.CPPTypeObject);	
				}
			}
#else
			// in shipping builds, try to be as safe as possible
			if(IsValid(Type.Type.CPPTypeObject))
			{
				if(Type.Type.CPPTypeObject->GetClass())
				{
					if(Type.Type.CPPTypeObject->IsValidLowLevelFast() &&
						!Type.Type.CPPTypeObject->IsNative() &&
						!Type.Type.CPPTypeObject->IsUnreachable())
					{
						// make sure the object is part of the GUObjectArray and can be retrieved
						// so that GC doesn't crash after receiving the referenced object
						const int32 ObjectIndex = GUObjectArray.ObjectToIndex(Type.Type.CPPTypeObject);
						if(ObjectIndex != INDEX_NONE)
						{
							if(const FUObjectItem* Item = GUObjectArray.IndexToObject(ObjectIndex))
							{
								if(Item->GetObject() == Type.Type.CPPTypeObject)
								{
									Collector.AddReferencedObject(Type.Type.CPPTypeObject);
								}
							}
						}
					}
				}
			}
#endif
		}
	}
}

FString FRigVMRegistry_NoLock::GetReferencerName() const
{
	return TEXT("FRigVMRegistry");
}

const TArray<UScriptStruct*>& FRigVMRegistry_NoLock::GetMathTypes()
{
	// The list of base math types to automatically register 
	static const TArray<UScriptStruct*> MathTypes = { 
		TBaseStructure<FRotator>::Get(),
		TBaseStructure<FQuat>::Get(),
		TBaseStructure<FTransform>::Get(),
		TBaseStructure<FLinearColor>::Get(),
		TBaseStructure<FColor>::Get(),
		TBaseStructure<FPlane>::Get(),
		TBaseStructure<FVector>::Get(),
		TBaseStructure<FVector2D>::Get(),
		TBaseStructure<FVector4>::Get(),
		TBaseStructure<FBox2D>::Get()
	};

	return MathTypes;
}

uint32 FRigVMRegistry_NoLock::GetHashForType_NoLock(TRigVMTypeIndex InTypeIndex) const
{
	if(!Types.IsValidIndex(InTypeIndex))
	{
		return UINT32_MAX;
	}

	FRigVMRegistry_NoLock* MutableThis = (FRigVMRegistry_NoLock*)this; 
	FTypeInfo& TypeInfo = MutableThis->Types[InTypeIndex];
	
	if(TypeInfo.Hash != UINT32_MAX)
	{
		return TypeInfo.Hash;
	}

	uint32 Hash;
	if(const UScriptStruct* ScriptStruct = Cast<UScriptStruct>(TypeInfo.Type.CPPTypeObject))
	{
		Hash = GetHashForScriptStruct_NoLock(ScriptStruct, false);
	}
	else if(const UStruct* Struct = Cast<UStruct>(TypeInfo.Type.CPPTypeObject))
	{
		Hash = GetHashForStruct_NoLock(Struct);
	}
	else if(const UEnum* Enum = Cast<UEnum>(TypeInfo.Type.CPPTypeObject))
    {
    	Hash = GetHashForEnum_NoLock(Enum, false);
    }
    else
    {
    	Hash = GetTypeHash(TypeInfo.Type.CPPType.ToString());
    }

	// for used defined structs - always recompute it
	if(Cast<UUserDefinedStruct>(TypeInfo.Type.CPPTypeObject))
	{
		return Hash;
	}

	TypeInfo.Hash = Hash;
	return Hash;
}

uint32 FRigVMRegistry_NoLock::GetHashForScriptStruct_NoLock(const UScriptStruct* InScriptStruct, bool bCheckTypeIndex) const
{
	if(bCheckTypeIndex)
	{
		const TRigVMTypeIndex TypeIndex = GetTypeIndex_NoLock(*InScriptStruct->GetStructCPPName(), (UObject*)InScriptStruct);
		if(TypeIndex != INDEX_NONE)
		{
			return GetHashForType_NoLock(TypeIndex);
		}
	}
	
	const uint32 NameHash = GetTypeHash(InScriptStruct->GetStructCPPName());
	return HashCombine(NameHash, GetHashForStruct_NoLock(InScriptStruct));
}

uint32 FRigVMRegistry_NoLock::GetHashForStruct_NoLock(const UStruct* InStruct) const
{
	uint32 Hash = GetTypeHash(InStruct->GetPathName());
	for (TFieldIterator<FProperty> It(InStruct); It; ++It)
	{
		const FProperty* Property = *It;
		if(IsAllowedType_NoLock(Property))
		{
			Hash = HashCombine(Hash, GetHashForProperty_NoLock(Property));
		}
	}
	return Hash;
}

uint32 FRigVMRegistry_NoLock::GetHashForEnum_NoLock(const UEnum* InEnum, bool bCheckTypeIndex) const
{
	if(bCheckTypeIndex)
	{
		const TRigVMTypeIndex TypeIndex = GetTypeIndex_NoLock(*InEnum->CppType, (UObject*)InEnum);
		if(TypeIndex != INDEX_NONE)
		{
			return GetHashForType_NoLock(TypeIndex);
		}
	}
	
	uint32 Hash = GetTypeHash(InEnum->GetName());
	for(int32 Index = 0; Index < InEnum->NumEnums(); Index++)
	{
		Hash = HashCombine(Hash, GetTypeHash(InEnum->GetValueByIndex(Index)));
		Hash = HashCombine(Hash, GetTypeHash(InEnum->GetDisplayNameTextByIndex(Index).ToString()));
	}
	return Hash;
}

uint32 FRigVMRegistry_NoLock::GetHashForProperty_NoLock(const FProperty* InProperty) const
{
	uint32 Hash = GetTypeHash(InProperty->GetName());

	FString ExtendedCPPType;
	const FString CPPType = InProperty->GetCPPType(&ExtendedCPPType);
	Hash = HashCombine(Hash, GetTypeHash(CPPType + ExtendedCPPType));
	
	if(const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InProperty))
	{
		InProperty = ArrayProperty->Inner;
	}
	
	if(const FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
	{
		Hash = HashCombine(Hash, GetHashForStruct_NoLock(StructProperty->Struct));
	}
	else if(const FByteProperty* ByteProperty = CastField<FByteProperty>(InProperty))
	{
		if(ByteProperty->Enum)
		{
			Hash = HashCombine(Hash, GetHashForEnum_NoLock(ByteProperty->Enum));
		}
	}
	else if(const FEnumProperty* EnumProperty = CastField<FEnumProperty>(InProperty))
	{
		Hash = HashCombine(Hash, GetHashForEnum_NoLock(EnumProperty->GetEnum()));
	}
	
	return Hash;
}

void FRigVMRegistry_NoLock::RebuildRegistry_NoLock()
{
	Reset_NoLock();
	
	Types.Reset();
	TypeToIndex.Reset();
	Functions.Empty();
	Templates.Empty();
	DeprecatedTemplates.Empty();
	Factories.Reset();
	FunctionNameToIndex.Reset();
	StructNameToPredicates.Reset();
	TemplateNotationToIndex.Reset();
	DeprecatedTemplateNotationToIndex.Reset();
	TypesPerCategory.Reset();
	TemplatesPerCategory.Reset();
	UserDefinedTypeToIndex.Reset();
	AllowedClasses.Reset();

	Initialize(false);
}

void FRigVMRegistry_NoLock::Initialize_NoLock()
{
	Types.Reserve(512);
	TypeToIndex.Reserve(512);
	TypesPerCategory.Reserve(19);
	TemplatesPerCategory.Reserve(19);
	
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_Execute, TArray<TRigVMTypeIndex>()).Reserve(8);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue, TArray<TRigVMTypeIndex>()).Reserve(256);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue, TArray<TRigVMTypeIndex>()).Reserve(256);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayAnyValue, TArray<TRigVMTypeIndex>()).Reserve(256);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleSimpleValue, TArray<TRigVMTypeIndex>()).Reserve(8);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArraySimpleValue, TArray<TRigVMTypeIndex>()).Reserve(8);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArraySimpleValue, TArray<TRigVMTypeIndex>()).Reserve(8);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleMathStructValue, TArray<TRigVMTypeIndex>()).Reserve(GetMathTypes().Num());
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayMathStructValue, TArray<TRigVMTypeIndex>()).Reserve(GetMathTypes().Num());
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayMathStructValue, TArray<TRigVMTypeIndex>()).Reserve(GetMathTypes().Num());
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleScriptStructValue, TArray<TRigVMTypeIndex>()).Reserve(128);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayScriptStructValue, TArray<TRigVMTypeIndex>()).Reserve(128);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayScriptStructValue, TArray<TRigVMTypeIndex>()).Reserve(128);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleEnumValue, TArray<TRigVMTypeIndex>()).Reserve(128);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayEnumValue, TArray<TRigVMTypeIndex>()).Reserve(128);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayEnumValue, TArray<TRigVMTypeIndex>()).Reserve(128);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleObjectValue, TArray<TRigVMTypeIndex>()).Reserve(128);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayObjectValue, TArray<TRigVMTypeIndex>()).Reserve(128);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayObjectValue, TArray<TRigVMTypeIndex>()).Reserve(128);

	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_Execute, TArray<int32>()).Reserve(8);
	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue, TArray<int32>()).Reserve(64);
	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue, TArray<int32>()).Reserve(64);
	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayAnyValue, TArray<int32>()).Reserve(64);
	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleSimpleValue, TArray<int32>()).Reserve(64);
	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArraySimpleValue, TArray<int32>()).Reserve(64);
	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArraySimpleValue, TArray<int32>()).Reserve(64);
	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleMathStructValue, TArray<int32>()).Reserve(64);
	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayMathStructValue, TArray<int32>()).Reserve(64);
	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayMathStructValue, TArray<int32>()).Reserve(64);
	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleScriptStructValue, TArray<int32>()).Reserve(64);
	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayScriptStructValue, TArray<int32>()).Reserve(64);
	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayScriptStructValue, TArray<int32>()).Reserve(64);
	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleEnumValue, TArray<int32>()).Reserve(64);
	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayEnumValue, TArray<int32>()).Reserve(64);
	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayEnumValue, TArray<int32>()).Reserve(64);
	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleObjectValue, TArray<int32>()).Reserve(64);
	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayObjectValue, TArray<int32>()).Reserve(64);
	TemplatesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayObjectValue, TArray<int32>()).Reserve(64);

	RigVMTypeUtils::TypeIndex::Execute = FindOrAddType_NoLock(FRigVMTemplateArgumentType(FRigVMExecuteContext::StaticStruct()), false);
	RigVMTypeUtils::TypeIndex::ExecuteArray = FindOrAddType_NoLock(FRigVMTemplateArgumentType(FRigVMExecuteContext::StaticStruct()).ConvertToArray(), false);
	RigVMTypeUtils::TypeIndex::Bool = FindOrAddType_NoLock(FRigVMTemplateArgumentType(RigVMTypeUtils::BoolTypeName, nullptr), false);
	RigVMTypeUtils::TypeIndex::Float = FindOrAddType_NoLock(FRigVMTemplateArgumentType(RigVMTypeUtils::FloatTypeName, nullptr), false);
	RigVMTypeUtils::TypeIndex::Double = FindOrAddType_NoLock(FRigVMTemplateArgumentType(RigVMTypeUtils::DoubleTypeName, nullptr), false);
	RigVMTypeUtils::TypeIndex::Int32 = FindOrAddType_NoLock(FRigVMTemplateArgumentType(RigVMTypeUtils::Int32TypeName, nullptr), false);
	RigVMTypeUtils::TypeIndex::UInt32 = FindOrAddType_NoLock(FRigVMTemplateArgumentType(RigVMTypeUtils::UInt32TypeName, nullptr), false);
	RigVMTypeUtils::TypeIndex::UInt8 = FindOrAddType_NoLock(FRigVMTemplateArgumentType(RigVMTypeUtils::UInt8TypeName, nullptr), false);
	RigVMTypeUtils::TypeIndex::FName = FindOrAddType_NoLock(FRigVMTemplateArgumentType(RigVMTypeUtils::FNameTypeName, nullptr), false);
	RigVMTypeUtils::TypeIndex::FString = FindOrAddType_NoLock(FRigVMTemplateArgumentType(RigVMTypeUtils::FStringTypeName, nullptr), false);
	RigVMTypeUtils::TypeIndex::WildCard = FindOrAddType_NoLock(FRigVMTemplateArgumentType(RigVMTypeUtils::GetWildCardCPPTypeName(), RigVMTypeUtils::GetWildCardCPPTypeObject()), false);
	RigVMTypeUtils::TypeIndex::BoolArray = FindOrAddType_NoLock(FRigVMTemplateArgumentType(RigVMTypeUtils::BoolArrayTypeName, nullptr), false);
	RigVMTypeUtils::TypeIndex::FloatArray = FindOrAddType_NoLock(FRigVMTemplateArgumentType(RigVMTypeUtils::FloatArrayTypeName, nullptr), false);
	RigVMTypeUtils::TypeIndex::DoubleArray = FindOrAddType_NoLock(FRigVMTemplateArgumentType(RigVMTypeUtils::DoubleArrayTypeName, nullptr), false);
	RigVMTypeUtils::TypeIndex::Int32Array = FindOrAddType_NoLock(FRigVMTemplateArgumentType(RigVMTypeUtils::Int32ArrayTypeName, nullptr), false);
	RigVMTypeUtils::TypeIndex::UInt32Array = FindOrAddType_NoLock(FRigVMTemplateArgumentType(RigVMTypeUtils::UInt32ArrayTypeName, nullptr), false);
	RigVMTypeUtils::TypeIndex::UInt8Array = FindOrAddType_NoLock(FRigVMTemplateArgumentType(RigVMTypeUtils::UInt8ArrayTypeName, nullptr), false);
	RigVMTypeUtils::TypeIndex::FNameArray = FindOrAddType_NoLock(FRigVMTemplateArgumentType(RigVMTypeUtils::FNameArrayTypeName, nullptr), false);
	RigVMTypeUtils::TypeIndex::FStringArray = FindOrAddType_NoLock(FRigVMTemplateArgumentType(RigVMTypeUtils::FStringArrayTypeName, nullptr), false);
	RigVMTypeUtils::TypeIndex::WildCardArray = FindOrAddType_NoLock(FRigVMTemplateArgumentType(RigVMTypeUtils::GetWildCardArrayCPPTypeName(), RigVMTypeUtils::GetWildCardCPPTypeObject()), false);

	// register the default math types
	for(UScriptStruct* MathType : GetMathTypes())
	{
		FindOrAddType_NoLock(FRigVMTemplateArgumentType(MathType), false);
	}

	// hook the registry to prepare for engine shutdown
	FCoreDelegates::OnExit.AddLambda([&]()
	{
		Reset_NoLock();

		if (FAssetRegistryModule* AssetRegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
		{
			if (AssetRegistryModule->TryGet())
			{
				AssetRegistryModule->Get().OnAssetRemoved().RemoveAll(this);
				AssetRegistryModule->Get().OnAssetRenamed().RemoveAll(this);
			}
		}

		IPluginManager::Get().OnNewPluginMounted().RemoveAll(this);
		IPluginManager::Get().OnPluginUnmounted().RemoveAll(this);

		UE::Anim::AttributeTypes::GetOnAttributeTypesChanged().RemoveAll(this);
	});
}

void FRigVMRegistry_NoLock::RefreshEngineTypesIfRequired_NoLock()
{
	if(bEverRefreshedEngineTypes)
	{
		return;
	}
	RefreshEngineTypes_NoLock();
}

void FRigVMRegistry_NoLock::RefreshEngineTypes_NoLock()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FRigVMRegistry::RefreshEngineTypes);

	TGuardValue<bool> AvoidTypePropagationGuard(bAvoidTypePropagation, true);

	const int32 NumTypesBefore = Types.Num(); 
	
	// Register all user-defined types that the engine knows about. Enumerating over the entire object hierarchy is
	// slow, so we do it for structs, enums and dispatch factories in one shot.
	TArray<UScriptStruct*> DispatchFactoriesToRegister;
	DispatchFactoriesToRegister.Reserve(32);

	for (TObjectIterator<UScriptStruct> ScriptStructIt; ScriptStructIt; ++ScriptStructIt)
	{
		UScriptStruct* ScriptStruct = *ScriptStructIt;
		
		// if this is a C++ type - skip it
		if(ScriptStruct->IsA<UUserDefinedStruct>() || ScriptStruct->IsChildOf(FRigVMExecutePin::StaticStruct()))
		{
			// this check for example makes sure we don't add structs defined in verse
			if(IsAllowedType_NoLock(ScriptStruct))
			{
				FindOrAddType_NoLock(FRigVMTemplateArgumentType(ScriptStruct), false);
			}
		}
		else if (ScriptStruct != FRigVMDispatchFactory::StaticStruct() &&
				 ScriptStruct->IsChildOf(FRigVMDispatchFactory::StaticStruct()))
		{
			DispatchFactoriesToRegister.Add(ScriptStruct);
		}
		else if(AllowedStructs.Contains(ScriptStruct))
		{
			FindOrAddType_NoLock(FRigVMTemplateArgumentType(ScriptStruct));
		}
	}

	for (TObjectIterator<UEnum> EnumIt; EnumIt; ++EnumIt)
	{
		UEnum* Enum = *EnumIt;
		if(IsAllowedType_NoLock(Enum))
		{
			const FString CPPType = Enum->CppType.IsEmpty() ? Enum->GetName() : Enum->CppType;
			FindOrAddType_NoLock(FRigVMTemplateArgumentType(*CPPType, Enum), false);
		}
	}
	
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;
		if (IsAllowedType_NoLock(Class))
		{
			// Register both the class and the object type for use
			FindOrAddType_NoLock(FRigVMTemplateArgumentType(Class, RigVMTypeUtils::EClassArgType::AsClass), false);
			FindOrAddType_NoLock(FRigVMTemplateArgumentType(Class, RigVMTypeUtils::EClassArgType::AsObject), false);
		}
	}

	// Register all dispatch factories only after all other types have been registered.
	for (UScriptStruct* DispatchFactoryStruct: DispatchFactoriesToRegister)
	{
		RegisterFactory_NoLock(DispatchFactoryStruct);
	}

	const int32 NumTypesNow = Types.Num();
	if(NumTypesBefore != NumTypesNow)
	{
		// update all of the templates once
		TArray<bool> TemplateProcessed;
		TemplateProcessed.AddZeroed(Templates.Num());
		for(const TPair<FRigVMTemplateArgument::ETypeCategory, TArray<int32>>& Pair : TemplatesPerCategory)
		{
			for(const int32 TemplateIndex : Pair.Value)
			{
				if(!TemplateProcessed[TemplateIndex])
				{
					FRigVMTemplate& Template = Templates[TemplateIndex];
					(void)Template.UpdateAllArgumentTypesSlow();
					TemplateProcessed[TemplateIndex] = true;
				}
			}
		}
	}

	// also refresh the functions and dispatches
	(void)RefreshFunctionsAndDispatches_NoLock();
	
	bEverRefreshedEngineTypes = true;
}

bool FRigVMRegistry_NoLock::RefreshFunctionsAndDispatches_NoLock()
{
	if(!CVarRigVMUpdateDispatchFactoriesGreedily->GetBool())
	{
		return false;
	}

	// nothing to do for functions for now - they are registered by their static initialize

	bool bRegistryChanged = false;

	// factories are also registered by RegisterFactory_NoLock, so we don't need to visit all
	// currently known UScriptStructs. By the time we get here the factories are registered.
	for(const FRigVMDispatchFactory* Factory : Factories)
	{
		// pulling on the template will cause the template to be initialized.
		// that may introduce a certain cost - which we don't want to experience during
		// the game.
		if(Factory->CachedTemplate == nullptr)
		{
			(void)Factory->GetTemplate_NoLock();
			bRegistryChanged = true;
		}
	}
	return bRegistryChanged; 
}

void FRigVMRegistry_NoLock::OnAssetRenamed_NoLock(const FAssetData& InAssetData, const FString& InOldObjectPath)
{
	const FSoftObjectPath OldPath(InOldObjectPath);
	
	if (const TRigVMTypeIndex* TypeIndexPtr = UserDefinedTypeToIndex.Find(OldPath))
	{
		const TRigVMTypeIndex TypeIndex = *TypeIndexPtr;
		UserDefinedTypeToIndex.Remove(OldPath);
		UserDefinedTypeToIndex.Add(InAssetData.ToSoftObjectPath()) = TypeIndex;
	}
}

bool FRigVMRegistry_NoLock::OnAssetRemoved_NoLock(const FAssetData& InAssetData)
{
	return RemoveType_NoLock(InAssetData.ToSoftObjectPath(), InAssetData.GetClass());
}

bool FRigVMRegistry_NoLock::OnPluginLoaded_NoLock(IPlugin& InPlugin)
{
	// only update the functions / dispatches once the engine has initialized.
	if(!bEverRefreshedDispatchFactoriesAfterEngineInit)
	{
		return false;
	}
	return RefreshFunctionsAndDispatches_NoLock();
}

bool FRigVMRegistry_NoLock::OnPluginUnloaded_NoLock(IPlugin& InPlugin)
{
	const FString PluginContentPath = InPlugin.GetMountedAssetPath();

	TSet<FSoftObjectPath> PathsToRemove;
	for (const TPair<FSoftObjectPath, TRigVMTypeIndex>& Item: UserDefinedTypeToIndex)
	{
		const FSoftObjectPath ObjectPath = Item.Key;
		const FString PackageName = ObjectPath.GetLongPackageName();
		
		if (PackageName.StartsWith(PluginContentPath))
		{
			PathsToRemove.Add(ObjectPath);
		}
	}

	bool bRegistryChanged = false;
	for (FSoftObjectPath ObjectPath: PathsToRemove)
	{
		const UClass* ObjectClass = nullptr;
		if (const UObject* TypeObject = ObjectPath.ResolveObject())
		{
			ObjectClass = TypeObject->GetClass();
		}
		
		if (RemoveType_NoLock(ObjectPath, ObjectClass))
		{
			bRegistryChanged = true;
		}
	}

	return bRegistryChanged;
}

void FRigVMRegistry_NoLock::OnAnimationAttributeTypesChanged_NoLock(const UScriptStruct* InStruct, bool bIsAdded)
{
	if (!ensure(InStruct))
	{
		return;
	}

	if (bIsAdded)
	{
		FindOrAddType_NoLock(FRigVMTemplateArgumentType(const_cast<UScriptStruct*>(InStruct)));
	}
}


void FRigVMRegistry_NoLock::Reset_NoLock()
{
	for(FRigVMDispatchFactory* Factory : Factories)
	{
		if(const UScriptStruct* ScriptStruct = Factory->GetScriptStruct())
		{
			ScriptStruct->DestroyStruct(Factory, 1);
		}
		FMemory::Free(Factory);
	}
	Factories.Reset();
}

TRigVMTypeIndex FRigVMRegistry_NoLock::FindOrAddType_NoLock(const FRigVMTemplateArgumentType& InType, bool bForce)
{
	// we don't use a mutex here since by the time the engine relies on worker
	// thread for execution or async loading all types will have been registered.
	
	TRigVMTypeIndex Index = GetTypeIndex_NoLock(InType);
	if(Index == INDEX_NONE)
	{
		FRigVMTemplateArgumentType ElementType = InType;
		while(ElementType.IsArray())
		{
			ElementType.ConvertToBaseElement();
		}
		
		const UObject* CPPTypeObject = ElementType.CPPTypeObject;
		if(!bForce && (CPPTypeObject != nullptr))
		{
			if(const UClass* Class = Cast<UClass>(CPPTypeObject))
			{
				if(!IsAllowedType_NoLock(Class))
				{
					return Index;
				}	
			}
			else if(const UEnum* Enum = Cast<UEnum>(CPPTypeObject))
			{
				if(!IsAllowedType_NoLock(Enum))
				{
					return Index;
				}
			}
			else if(const UStruct* Struct = Cast<UStruct>(CPPTypeObject))
			{
				if(!IsAllowedType_NoLock(Struct))
				{					
					return Index;
				}
			}
		}

		bool bIsExecute = false;
		if(const UScriptStruct* ScriptStruct = Cast<UScriptStruct>(CPPTypeObject))
		{
			bIsExecute = ScriptStruct->IsChildOf(FRigVMExecutePin::StaticStruct());
		}

		TArray<TRigVMTypeIndex> Indices;
		Indices.Reserve(3);
		for (int32 ArrayDimension=0; ArrayDimension<3; ++ArrayDimension)
		{
			if (bIsExecute && ArrayDimension > 1)
			{
				break;
			}
			
			FRigVMTemplateArgumentType CurType = ElementType;
			for (int32 j=0; j<ArrayDimension; ++j)
			{
				CurType.ConvertToArray();
			}

			FTypeInfo Info;
			Info.Type = CurType;
			Info.bIsArray = ArrayDimension > 0;
			Info.bIsExecute = bIsExecute;
			
			Index = Types.Add(Info);
#if UE_RIGVM_DEBUG_TYPEINDEX
			Index.Name = Info.Type.CPPType;
#endif
			TypeToIndex.Add(CurType, Index);

			Indices.Add(Index);
		}

		Types[Indices[1]].BaseTypeIndex = Indices[0];
		Types[Indices[0]].ArrayTypeIndex = Indices[1];

		if (!bIsExecute)
		{
			Types[Indices[2]].BaseTypeIndex = Indices[1];
			Types[Indices[1]].ArrayTypeIndex = Indices[2];
		}

		// update the categories first then propagate to TemplatesPerCategory once all categories up to date
		TArray<TPair<FRigVMTemplateArgument::ETypeCategory, int32>> ToPropagate;
		auto RegisterNewType = [&](FRigVMTemplateArgument::ETypeCategory InCategory, TRigVMTypeIndex NewIndex)
		{
			RegisterTypeInCategory_NoLock(InCategory, NewIndex);
			ToPropagate.Emplace(InCategory, NewIndex);
		}; 

		for (int32 ArrayDimension=0; ArrayDimension<3; ++ArrayDimension)
		{
			if(bIsExecute && ArrayDimension > 1)
			{
				break;
			}
			Index = Indices[ArrayDimension];

			// Add to category
			// simple types
			if(CPPTypeObject == nullptr)
			{
				switch(ArrayDimension)
				{
					default:
					case 0:
					{
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_SingleSimpleValue, Index);
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue, Index);
						break;
					}
					case 1:
					{
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_ArraySimpleValue, Index);
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue, Index);
						break;
					}
					case 2:
					{
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_ArrayArraySimpleValue, Index);
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_ArrayArrayAnyValue, Index);
						break;
					}
				}
			}
			else if(CPPTypeObject->IsA<UClass>())
			{
				switch(ArrayDimension)
				{
					default:
					case 0:
					{
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_SingleObjectValue, Index);
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue, Index);
						break;
					}
					case 1:
					{
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_ArrayObjectValue, Index);
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue, Index);
						break;
					}
					case 2:
					{
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_ArrayArrayObjectValue, Index);
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_ArrayArrayAnyValue, Index);
						break;
					}
				}
			}
			else if(CPPTypeObject->IsA<UEnum>())
			{
				switch(ArrayDimension)
				{
					default:
					case 0:
					{
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_SingleEnumValue, Index);
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue, Index);
						break;
					}
					case 1:
					{
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_ArrayEnumValue, Index);
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue, Index);
						break;
					}
					case 2:
					{
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_ArrayArrayEnumValue, Index);
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_ArrayArrayAnyValue, Index);
						break;
					}
				}
			}
			else if(const UStruct* Struct = Cast<UStruct>(CPPTypeObject))
			{
				if(Struct->IsChildOf(FRigVMExecutePin::StaticStruct()))
				{
					if(ArrayDimension == 0)
					{
						RegisterNewType(FRigVMTemplateArgument::ETypeCategory_Execute, Index);
					}
				}
				else
				{
					if(GetMathTypes().Contains(CPPTypeObject))
					{
						switch(ArrayDimension)
						{
							default:
							case 0:
							{
								RegisterNewType(FRigVMTemplateArgument::ETypeCategory_SingleMathStructValue, Index);
								break;
							}
							case 1:
							{
								RegisterNewType(FRigVMTemplateArgument::ETypeCategory_ArrayMathStructValue, Index);
								break;
							}
							case 2:
							{
								RegisterNewType(FRigVMTemplateArgument::ETypeCategory_ArrayArrayMathStructValue, Index);
								break;
							}
						}
					}
					
					switch(ArrayDimension)
					{
						default:
						case 0:
						{
							RegisterNewType(FRigVMTemplateArgument::ETypeCategory_SingleScriptStructValue, Index);
							RegisterNewType(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue, Index);
							break;
						}
						case 1:
						{
							RegisterNewType(FRigVMTemplateArgument::ETypeCategory_ArrayScriptStructValue, Index);
							RegisterNewType(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue, Index);
							break;
						}
						case 2:
						{
							RegisterNewType(FRigVMTemplateArgument::ETypeCategory_ArrayArrayScriptStructValue, Index);
							RegisterNewType(FRigVMTemplateArgument::ETypeCategory_ArrayArrayAnyValue, Index);
							break;
						}
					}
				}
			}
		}

		// propagate new type to templates once they have all been added to the categories
		for (const auto& [Category, NewIndex]: ToPropagate)
		{
			PropagateTypeAddedToCategory_NoLock(Category, NewIndex);	
		}

		// if the type is a structure
		// then add all of its sub property types
		if(const UStruct* Struct = Cast<UStruct>(CPPTypeObject))
		{
			for (TFieldIterator<FProperty> It(Struct); It; ++It)
			{
				FProperty* Property = *It;
				if(IsAllowedType_NoLock(Property))
				{
					// by creating a template argument for the child property
					// the type will be added by calling ::FindOrAddType_Internal recursively.
					(void)FRigVMTemplateArgument::Make_NoLock(Property, *this);
				}
#if WITH_EDITOR
				else
				{
					// If the subproperty is not allowed, let's make sure it's hidden. Otherwise we end up with
					// subpins with invalid types 
					check(FRigVMStruct::GetPinDirectionFromProperty(Property) == ERigVMPinDirection::Hidden);
				}
#endif
			}			
		}
		
		Index = GetTypeIndex_NoLock(InType);
		if (IsValid(CPPTypeObject))
		{
			if (CPPTypeObject->IsA<UUserDefinedStruct>() || CPPTypeObject->IsA<UUserDefinedEnum>())
			{
				TRigVMTypeIndex ElementTypeIndex = GetTypeIndex_NoLock(ElementType);
				// used to track name changes to user defined types, stores the element type index, see RemoveType()
				UserDefinedTypeToIndex.FindOrAdd(CPPTypeObject) = ElementTypeIndex;
			}
		}
		
		return Index;
	}
	
	return Index;
}

void FRigVMRegistry_NoLock::RegisterTypeInCategory_NoLock(FRigVMTemplateArgument::ETypeCategory InCategory, TRigVMTypeIndex InTypeIndex)
{
	check(InCategory != FRigVMTemplateArgument::ETypeCategory_Invalid);

	TypesPerCategory.FindChecked(InCategory).Add(InTypeIndex);
}

void FRigVMRegistry_NoLock::PropagateTypeAddedToCategory_NoLock(const FRigVMTemplateArgument::ETypeCategory InCategory, const TRigVMTypeIndex InTypeIndex)
{
	if(bAvoidTypePropagation)
	{
		return;
	}
	
	check(InCategory != FRigVMTemplateArgument::ETypeCategory_Invalid);
	if ( ensure(TypesPerCategory.FindChecked(InCategory).Contains(InTypeIndex)) )
	{
		// when adding a new type - we need to update template arguments which expect to have access to that type 
		const TArray<int32>& TemplatesToUseType = TemplatesPerCategory.FindChecked(InCategory);
		for(const int32 TemplateIndex : TemplatesToUseType)
		{
			FRigVMTemplate& Template = Templates[TemplateIndex];
			(void)Template.HandlePropagatedArgumentType(InTypeIndex);
		}
	}
}

bool FRigVMRegistry_NoLock::RemoveType_NoLock(const FSoftObjectPath& InObjectPath, const UClass* InObjectClass)
{
	if (const TRigVMTypeIndex* TypeIndexPtr = UserDefinedTypeToIndex.Find(InObjectPath))
	{
		const TRigVMTypeIndex TypeIndex = *TypeIndexPtr;
		
		UserDefinedTypeToIndex.Remove(InObjectPath);
		
		if(TypeIndex == INDEX_NONE)
		{
			return false;
		}

		check(!IsArrayType_NoLock(TypeIndex));

		TArray<TRigVMTypeIndex> Indices;
		Indices.Init(INDEX_NONE, 3);
		Indices[0] = TypeIndex;
		Indices[1] = GetArrayTypeFromBaseTypeIndex_NoLock(Indices[0]);

		// any type that can be removed should have 3 entries in the registry
		if (ensure(Indices[1] != INDEX_NONE))
		{
			Indices[2] = GetArrayTypeFromBaseTypeIndex_NoLock(Indices[1]);
		}
		
		for (int32 ArrayDimension=0; ArrayDimension<3; ++ArrayDimension)
		{
			const TRigVMTypeIndex Index = Indices[ArrayDimension];
			
			if (Index == INDEX_NONE)
			{
				break;
			}
			
			if(InObjectClass == UUserDefinedEnum::StaticClass())
			{
				switch(ArrayDimension)
				{
				default:
				case 0:
					{
						RemoveTypeInCategory_NoLock(FRigVMTemplateArgument::ETypeCategory_SingleEnumValue, Index);
						RemoveTypeInCategory_NoLock(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue, Index);
						break;
					}
				case 1:
					{
						RemoveTypeInCategory_NoLock(FRigVMTemplateArgument::ETypeCategory_ArrayEnumValue, Index);
						RemoveTypeInCategory_NoLock(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue, Index);
						break;
					}
				case 2:
					{
						RemoveTypeInCategory_NoLock(FRigVMTemplateArgument::ETypeCategory_ArrayArrayEnumValue, Index);
						RemoveTypeInCategory_NoLock(FRigVMTemplateArgument::ETypeCategory_ArrayArrayAnyValue, Index);
						break;
					}
				}
			}
			else if(InObjectClass == UUserDefinedStruct::StaticClass())
			{
				switch(ArrayDimension)
				{
				default:
				case 0:
					{
						RemoveTypeInCategory_NoLock(FRigVMTemplateArgument::ETypeCategory_SingleScriptStructValue, Index);
						RemoveTypeInCategory_NoLock(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue, Index);
						break;
					}
				case 1:
					{
						RemoveTypeInCategory_NoLock(FRigVMTemplateArgument::ETypeCategory_ArrayScriptStructValue, Index);
						RemoveTypeInCategory_NoLock(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue, Index);
						break;
					}
				case 2:
					{
						RemoveTypeInCategory_NoLock(FRigVMTemplateArgument::ETypeCategory_ArrayArrayScriptStructValue, Index);
						RemoveTypeInCategory_NoLock(FRigVMTemplateArgument::ETypeCategory_ArrayArrayAnyValue, Index);
						break;
					}
				}
			}

			// remove the type from the registry entirely
			TypeToIndex.Remove(GetType_NoLock(Index));
			Types[Index] = FTypeInfo();
		}

		return true;	
	}
	
	return false;
}

void FRigVMRegistry_NoLock::RemoveTypeInCategory_NoLock(FRigVMTemplateArgument::ETypeCategory InCategory, TRigVMTypeIndex InTypeIndex)
{
	check(InCategory != FRigVMTemplateArgument::ETypeCategory_Invalid);

	TypesPerCategory.FindChecked(InCategory).Remove(InTypeIndex);

	const TArray<int32>& TemplatesToUseType = TemplatesPerCategory.FindChecked(InCategory);
	for (const int32 TemplateIndex : TemplatesToUseType)
	{
		FRigVMTemplate& Template = Templates[TemplateIndex];
		Template.HandleTypeRemoval(InTypeIndex);
	}
}

void FRigVMRegistry_NoLock::OnEngineInit()
{
	FRigVMRegistry_RWLock& Registry = FRigVMRegistry_RWLock::Get();
	Registry.RefreshEngineTypes();
	Registry.bEverRefreshedDispatchFactoriesAfterEngineInit = true;
}

// This function needs to be in cpp file instead of header file
// to avoid confusing certain compilers into creating multiple copies of the registry
FRigVMRegistry_RWLock& FRigVMRegistry_RWLock::Get()
{
	// static in a function scope ensures that the GC system is initiated before 
	// the registry constructor is called
	static FRigVMRegistry_RWLock s_RigVMRegistry;
	return s_RigVMRegistry;
}

void FRigVMRegistry_RWLock::OnAssetRemoved(const FAssetData& InAssetData)
{
	bool bAssetRemoved = false;
	{
		FConditionalWriteScopeLock _(*this);
		bAssetRemoved = Super::OnAssetRemoved_NoLock(InAssetData);
	}

	if (bAssetRemoved)
	{
		OnRigVMRegistryChangedDelegate.Broadcast();
	}
}

void FRigVMRegistry_RWLock::OnPluginLoaded(IPlugin& InPlugin)
{
	bool bRegistryChanged = false;
	{
		FConditionalWriteScopeLock _(*this);
		bRegistryChanged = Super::OnPluginLoaded_NoLock(InPlugin);
	}
		
	if (bRegistryChanged)
	{
		OnRigVMRegistryChangedDelegate.Broadcast();
	}
}

void FRigVMRegistry_RWLock::OnPluginUnloaded(IPlugin& InPlugin)
{
	bool bRegistryChanged = false;
	{
		FConditionalWriteScopeLock _(*this);
		bRegistryChanged = Super::OnPluginUnloaded_NoLock(InPlugin);
	}
		
	if (bRegistryChanged)
	{
		OnRigVMRegistryChangedDelegate.Broadcast();
	}
}

void FRigVMRegistry_RWLock::OnAnimationAttributeTypesChanged(const UScriptStruct* InStruct, bool bIsAdded)
{
	{
		FConditionalWriteScopeLock _(*this);
		Super::OnAnimationAttributeTypesChanged_NoLock(InStruct, bIsAdded);
	}

	if (bIsAdded)
	{
		OnRigVMRegistryChangedDelegate.Broadcast();		
	}
}

TRigVMTypeIndex FRigVMRegistry_NoLock::GetTypeIndex_NoLock(const FRigVMTemplateArgumentType& InType) const
{
	if(const TRigVMTypeIndex* Index = TypeToIndex.Find(InType))
	{
		return *Index;
	}
	return INDEX_NONE;
}

const FRigVMTemplateArgumentType& FRigVMRegistry_NoLock::GetType_NoLock(TRigVMTypeIndex InTypeIndex) const
{
	if((Types.IsValidIndex(InTypeIndex)))
	{
		return Types[InTypeIndex].Type;
	}
	static FRigVMTemplateArgumentType EmptyType;
	return EmptyType;
}

const FRigVMTemplateArgumentType& FRigVMRegistry_NoLock::FindTypeFromCPPType_NoLock(const FString& InCPPType) const
{
	const int32 TypeIndex = GetTypeIndexFromCPPType_NoLock(InCPPType);
	if(ensure(Types.IsValidIndex(TypeIndex)))
	{
		return Types[TypeIndex].Type;
	}

	static FRigVMTemplateArgumentType EmptyType;
	return EmptyType;
}

TRigVMTypeIndex FRigVMRegistry_NoLock::GetTypeIndexFromCPPType_NoLock(const FString& InCPPType) const
{
	TRigVMTypeIndex Result = INDEX_NONE;
	if(!InCPPType.IsEmpty())
	{
		const FName CPPTypeName = *InCPPType;

		auto Predicate = [CPPTypeName](const FTypeInfo& Info) -> bool
		{
			return Info.Type.CPPType == CPPTypeName;
		};

		Result = Types.IndexOfByPredicate(Predicate);

		// in game / non-editor it's possible that a user defined struct or enum 
		// has not been registered. thus we'll try to find it and if not,
		// we will call RefreshEngineTypes to bring things up to date here.
		if (Result == INDEX_NONE)
		{
			const FName BaseCPPTypeName = RigVMTypeUtils::IsArrayType(InCPPType) ? *RigVMTypeUtils::BaseTypeFromArrayType(InCPPType) : *InCPPType;

			for (TObjectIterator<UUserDefinedStruct> ScriptStructIt; ScriptStructIt; ++ScriptStructIt)
			{
				UUserDefinedStruct* ScriptStruct = *ScriptStructIt;
				const FRigVMTemplateArgumentType ArgumentType(ScriptStruct);
				const FName StuctCppType = *RigVMTypeUtils::GetUniqueStructTypeName(ScriptStruct);
				if (ArgumentType.CPPType == BaseCPPTypeName)
				{
					// this check for example makes sure we don't add structs defined in verse
					if (IsAllowedType_NoLock(ScriptStruct))
					{
						const_cast<FRigVMRegistry_NoLock*>(this)->FindOrAddType_NoLock(ArgumentType, false);
						Result = Types.IndexOfByPredicate(Predicate);
						break;
					}
				}
			}
			if (Result == INDEX_NONE) // if we can not find a struct, lets try an enum
			{
				for (TObjectIterator<UUserDefinedEnum> EnumIt; EnumIt; ++EnumIt)
				{
					UUserDefinedEnum* Enum = *EnumIt;
					const FRigVMTemplateArgumentType ArgumentType(Enum);
					const FName EnumCppType = *RigVMTypeUtils::CPPTypeFromEnum(Enum);
					if (ArgumentType.CPPType == BaseCPPTypeName)
					{
						// this check for example makes sure we don't add enums defined in verse
						if (IsAllowedType_NoLock(Enum))
						{
							const_cast<FRigVMRegistry_NoLock*>(this)->FindOrAddType_NoLock(ArgumentType, false);
							Result = Types.IndexOfByPredicate(Predicate);
							break;
						}
					}
				}
			}
			if (Result == INDEX_NONE) // else a full scan
			{
				// we may need to update the types again to registry potentially
				// missing predicate types 
				const_cast<FRigVMRegistry_NoLock*>(this)->RefreshEngineTypes_NoLock();
				Result = Types.IndexOfByPredicate(Predicate);
			}
		}

		// If not found, try to find a redirect
		if (Result == INDEX_NONE)
		{
			const FString NewCPPType = RigVMTypeUtils::PostProcessCPPType(InCPPType);
			Result = Types.IndexOfByPredicate([NewCPPType](const FTypeInfo& Info) -> bool
			{
				return Info.Type.CPPType == *NewCPPType;
			});
		}
	}
	return Result;
}

bool FRigVMRegistry_NoLock::IsArrayType_NoLock(TRigVMTypeIndex InTypeIndex) const
{
	if((Types.IsValidIndex(InTypeIndex)))
	{
		return Types[InTypeIndex].bIsArray;
	}
	return false;
}

bool FRigVMRegistry_NoLock::IsExecuteType_NoLock(TRigVMTypeIndex InTypeIndex) const
{
	if(InTypeIndex == INDEX_NONE)
	{
		return false;
	}
	
	if(ensure(Types.IsValidIndex(InTypeIndex)))
	{
		return Types[InTypeIndex].bIsExecute;
	}
	return false;
}

bool FRigVMRegistry_NoLock::ConvertExecuteContextToBaseType_NoLock(TRigVMTypeIndex& InOutTypeIndex) const
{
	if(InOutTypeIndex == INDEX_NONE)
	{
		return false;
	}
		
	if(InOutTypeIndex == RigVMTypeUtils::TypeIndex::Execute) 
	{
		return true;
	}

	if(!IsExecuteType_NoLock(InOutTypeIndex))
	{
		return false;
	}

	// execute arguments can have various execute context types. but we always
	// convert them to the base execute type to make matching types easier later.
	// this means that the execute argument in every permutations shares 
	// the same type index of RigVMTypeUtils::TypeIndex::Execute
	if(IsArrayType_NoLock(InOutTypeIndex))
	{
		InOutTypeIndex = GetArrayTypeFromBaseTypeIndex_NoLock(RigVMTypeUtils::TypeIndex::Execute);
	}
	else
	{
		InOutTypeIndex = RigVMTypeUtils::TypeIndex::Execute;
	}

	return true;
}

int32 FRigVMRegistry_NoLock::GetArrayDimensionsForType_NoLock(TRigVMTypeIndex InTypeIndex) const
{
	if(ensure(Types.IsValidIndex(InTypeIndex)))
	{
		const FTypeInfo& Info = Types[InTypeIndex];
		if(Info.bIsArray)
		{
			return 1 + GetArrayDimensionsForType_NoLock(Info.BaseTypeIndex);
		}
	}
	return 0;
}

bool FRigVMRegistry_NoLock::IsWildCardType_NoLock(TRigVMTypeIndex InTypeIndex) const
{
	return RigVMTypeUtils::TypeIndex::WildCard == InTypeIndex ||
		RigVMTypeUtils::TypeIndex::WildCardArray == InTypeIndex;
}

bool FRigVMRegistry_NoLock::CanMatchTypes_NoLock(TRigVMTypeIndex InTypeIndexA, TRigVMTypeIndex InTypeIndexB, bool bAllowFloatingPointCasts) const
{
	if(!Types.IsValidIndex(InTypeIndexA) || !Types.IsValidIndex(InTypeIndexB))
	{
		return false;
	}

	if(InTypeIndexA == InTypeIndexB)
	{
		return true;
	}

	// execute types can always be connected
	if(IsExecuteType_NoLock(InTypeIndexA) && IsExecuteType_NoLock(InTypeIndexB))
	{
		return GetArrayDimensionsForType_NoLock(InTypeIndexA) == GetArrayDimensionsForType_NoLock(InTypeIndexB);
	}

	if(bAllowFloatingPointCasts)
	{
		// swap order since float is known to registered before double
		if(InTypeIndexA > InTypeIndexB)
		{
			Swap(InTypeIndexA, InTypeIndexB);
		}
		if(InTypeIndexA == RigVMTypeUtils::TypeIndex::Float && InTypeIndexB == RigVMTypeUtils::TypeIndex::Double)
		{
			return true;
		}
		if(InTypeIndexA == RigVMTypeUtils::TypeIndex::FloatArray && InTypeIndexB == RigVMTypeUtils::TypeIndex::DoubleArray)
		{
			return true;
		}
	}
	return false;
}

const TArray<TRigVMTypeIndex>& FRigVMRegistry_NoLock::GetCompatibleTypes_NoLock(TRigVMTypeIndex InTypeIndex) const
{
	if(InTypeIndex == RigVMTypeUtils::TypeIndex::Float)
	{
		static const TArray<TRigVMTypeIndex> CompatibleTypes = {RigVMTypeUtils::TypeIndex::Double};
		return CompatibleTypes;
	}
	if(InTypeIndex == RigVMTypeUtils::TypeIndex::Double)
	{
		static const TArray<TRigVMTypeIndex> CompatibleTypes = {RigVMTypeUtils::TypeIndex::Float};
		return CompatibleTypes;
	}
	if(InTypeIndex == RigVMTypeUtils::TypeIndex::FloatArray)
	{
		static const TArray<TRigVMTypeIndex> CompatibleTypes = {RigVMTypeUtils::TypeIndex::DoubleArray};
		return CompatibleTypes;
	}
	if(InTypeIndex == RigVMTypeUtils::TypeIndex::DoubleArray)
	{
		static const TArray<TRigVMTypeIndex> CompatibleTypes = {RigVMTypeUtils::TypeIndex::FloatArray};
		return CompatibleTypes;
	}

	static const TArray<TRigVMTypeIndex> EmptyTypes;
	return EmptyTypes;
}

const TArray<TRigVMTypeIndex>& FRigVMRegistry_NoLock::GetTypesForCategory_NoLock(FRigVMTemplateArgument::ETypeCategory InCategory) const
{
	check(InCategory != FRigVMTemplateArgument::ETypeCategory_Invalid);
	return TypesPerCategory.FindChecked(InCategory);
}

TRigVMTypeIndex FRigVMRegistry_NoLock::GetArrayTypeFromBaseTypeIndex_NoLock(TRigVMTypeIndex InTypeIndex) const
{
	if(ensure(Types.IsValidIndex(InTypeIndex)))
	{
#if UE_RIGVM_DEBUG_TYPEINDEX
		TRigVMTypeIndex Result = Types[InTypeIndex].ArrayTypeIndex;
		if(!InTypeIndex.Name.IsNone())
		{
			Result.Name = *RigVMTypeUtils::ArrayTypeFromBaseType(InTypeIndex.Name.ToString());
		}
		return Result;
#else
		return Types[InTypeIndex].ArrayTypeIndex;
#endif
	}
	return INDEX_NONE;
}

TRigVMTypeIndex FRigVMRegistry_NoLock::GetBaseTypeFromArrayTypeIndex_NoLock(TRigVMTypeIndex InTypeIndex) const
{
	if(ensure(Types.IsValidIndex(InTypeIndex)))
	{
#if UE_RIGVM_DEBUG_TYPEINDEX
		TRigVMTypeIndex Result = Types[InTypeIndex].BaseTypeIndex;
		if(!InTypeIndex.Name.IsNone())
		{
			Result.Name = *RigVMTypeUtils::BaseTypeFromArrayType(InTypeIndex.Name.ToString());
		}
		return Result;
#else
		return Types[InTypeIndex].BaseTypeIndex;
#endif
	}
	return INDEX_NONE;
}

bool FRigVMRegistry_NoLock::IsAllowedType_NoLock(const FProperty* InProperty) const
{
	if(InProperty->IsA<FBoolProperty>() ||
		InProperty->IsA<FUInt32Property>() ||
		InProperty->IsA<FInt8Property>() ||
		InProperty->IsA<FInt16Property>() ||
		InProperty->IsA<FIntProperty>() ||
		InProperty->IsA<FInt64Property>() ||
		InProperty->IsA<FFloatProperty>() ||
		InProperty->IsA<FDoubleProperty>() ||
		InProperty->IsA<FNumericProperty>() ||
		InProperty->IsA<FNameProperty>() ||
		InProperty->IsA<FStrProperty>())
	{
		return true;
	}

	if(const FArrayProperty* ArrayProperty  = CastField<FArrayProperty>(InProperty))
	{
		if (ArrayProperty->Inner)
		{
			return IsAllowedType_NoLock(ArrayProperty->Inner);
		}
	}
	if(const FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
	{
		return IsAllowedType_NoLock(StructProperty->Struct);
	}
	if(const FClassProperty* ClassProperty = CastField<FClassProperty>(InProperty))
	{
		return IsAllowedType_NoLock(ClassProperty->MetaClass);
	}
	if(const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(InProperty))
	{
		return IsAllowedType_NoLock(ObjectProperty->PropertyClass);
	}
	if(const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(InProperty))
	{
		return IsAllowedType_NoLock(SoftObjectProperty->PropertyClass);
	}
	if(const FEnumProperty* EnumProperty = CastField<FEnumProperty>(InProperty))
	{
		return IsAllowedType_NoLock(EnumProperty->GetEnum());
	}
	if(const FByteProperty* ByteProperty = CastField<FByteProperty>(InProperty))
	{
		if(const UEnum* Enum = ByteProperty->Enum)
		{
			return IsAllowedType_NoLock(Enum);
		}
		return true;
	}
	return false;
}

bool FRigVMRegistry_NoLock::IsAllowedType_NoLock(const UEnum* InEnum) const
{
	if(!InEnum)
	{
		return false;
	}
	
	// disallow verse based enums for now
	if (FPackageName::IsVersePackage(InEnum->GetPackage()->GetName()))
	{
		return false;
	}

	static const FName VerseEnumName(TEXT("VerseEnum"));
	if(IsTypeOfByName(InEnum, VerseEnumName))
	{
		return false;
	}

	return !InEnum->HasAnyFlags(DisallowedFlags()) && InEnum->HasAllFlags(NeededFlags());
}

bool FRigVMRegistry_NoLock::IsAllowedType_NoLock(const UStruct* InStruct) const
{
	if(!InStruct || InStruct->HasAnyFlags(DisallowedFlags()) || !InStruct->HasAllFlags(NeededFlags()))
	{
		return false;
	}
	if(InStruct->IsChildOf(FRigVMStruct::StaticStruct()) &&
		!InStruct->IsChildOf(FRigVMTrait::StaticStruct()))
	{
		return false;
	}
	if(InStruct->IsChildOf(FRigVMDispatchFactory::StaticStruct()))
	{
		return false;
	}
	
	// disallow verse data structures for now
	if (FPackageName::IsVersePackage(InStruct->GetPackage()->GetName()))
	{
		return false;
	}
	
	static const FName VerseStructName(TEXT("VerseStruct"));
	if(IsTypeOfByName(InStruct, VerseStructName))
	{
		return false;
	}

	// allow all user defined structs since they can always be changed to be compliant with RigVM restrictions
	if (InStruct->IsA<UUserDefinedStruct>())
	{
		return true;
	}

	// Allow structs we have explicitly opted into
	// This is on the understanding that if they have invalid sub-members that any pins representing them will need to be hidden
	if (const UScriptStruct* ScriptStruct = Cast<UScriptStruct>(InStruct))
	{
		if(AllowedStructs.Contains(ScriptStruct))
		{
			return true;
		}
	}

	for (TFieldIterator<FProperty> It(InStruct); It; ++It)
	{
		if(!IsAllowedType_NoLock(*It))
		{
			return false;
		}
	}
	return true;
}

bool FRigVMRegistry_NoLock::IsAllowedType_NoLock(const UClass* InClass) const
{
	if(!InClass || InClass->HasAnyClassFlags(CLASS_Hidden))
	{
		return false;
	}

	// Only allow native object types
	if (!InClass->HasAnyClassFlags(CLASS_Native))
	{
		return false;
	}

	// disallow verse based classes for now
	if (FPackageName::IsVersePackage(InClass->GetPackage()->GetName()))
	{
		return false;
	}

	static const FName VerseClassName(TEXT("VerseClass"));
	if(IsTypeOfByName(InClass, VerseClassName))
	{
		return false;
	}

	return AllowedClasses.Contains(InClass);
}

bool FRigVMRegistry_NoLock::IsTypeOfByName(const UObject* InObject, const FName& InName)
{
	if(!InObject || InName.IsNone())
	{
		return false;
	}
	
	const UClass* Class = InObject->GetClass();
	while(Class)
	{
		if(Class->GetFName().IsEqual(InName, ENameCase::CaseSensitive))
		{
			return true;
		}
		Class = Class->GetSuperClass();
	}
	
	return false;
}

void FRigVMRegistry_NoLock::Register_NoLock(const TCHAR* InName, FRigVMFunctionPtr InFunctionPtr, UScriptStruct* InStruct, const TArray<FRigVMFunctionArgument>& InArguments)
{
	if (FindFunction_NoLock(InName) != nullptr)
	{
		return;
	}

#if WITH_EDITOR
	FString StructureError;
	if (!FRigVMStruct::ValidateStruct(InStruct, &StructureError))
	{
		UE_LOG(LogRigVM, Error, TEXT("Failed to validate struct '%s': %s"), *InStruct->GetName(), *StructureError);
		return;
	}
#endif

	const FRigVMFunction Function(InName, InFunctionPtr, InStruct, Functions.Num(), InArguments);
	Functions.AddElement(Function);
	FunctionNameToIndex.Add(InName, Function.Index);

	// register all of the types used by the function
	for (TFieldIterator<FProperty> It(InStruct); It; ++It)
	{
		// creating the argument causes the registration
		(void)FRigVMTemplateArgument::Make_NoLock(*It, *this);
	}

#if WITH_EDITOR
	
	FString TemplateMetadata;
	if (InStruct->GetStringMetaDataHierarchical(TemplateNameMetaName, &TemplateMetadata))
	{
		bool bIsDeprecated = InStruct->HasMetaData(FRigVMStruct::DeprecatedMetaName);
		TChunkedArray<FRigVMTemplate>& TemplateArray = (bIsDeprecated) ? DeprecatedTemplates : Templates;
		TMap<FName, int32>& NotationToIndex = (bIsDeprecated) ? DeprecatedTemplateNotationToIndex : TemplateNotationToIndex;
		
		FString MethodName;
		if (FString(InName).Split(TEXT("::"), nullptr, &MethodName))
		{
			const FString TemplateName = RigVMStringUtils::JoinStrings(TemplateMetadata, MethodName, TEXT("::"));
			FRigVMTemplate Template(InStruct, TemplateName, Function.Index);
			if (Template.IsValid())
			{
				bool bWasMerged = false;

				const int32* ExistingTemplateIndexPtr = NotationToIndex.Find(Template.GetNotation());
				if(ExistingTemplateIndexPtr)
				{
					FRigVMTemplate& ExistingTemplate = TemplateArray[*ExistingTemplateIndexPtr];
					if (ExistingTemplate.Merge(Template))
					{
						if (!bIsDeprecated)
						{
							Functions[Function.Index].TemplateIndex = ExistingTemplate.Index;
						}
						bWasMerged = true;
					}
				}

				if (!bWasMerged)
				{
					Template.Index = TemplateArray.Num();
					if (!bIsDeprecated)
					{
						Functions[Function.Index].TemplateIndex = Template.Index;
					}
					TemplateArray.AddElement(Template);
					
					if(ExistingTemplateIndexPtr == nullptr)
					{
						NotationToIndex.Add(Template.GetNotation(), Template.Index);
					}
				}
			}
		}
	}

#endif
}

const FRigVMDispatchFactory* FRigVMRegistry_NoLock::RegisterFactory_NoLock(UScriptStruct* InFactoryStruct)
{
	check(InFactoryStruct);
	check(InFactoryStruct != FRigVMDispatchFactory::StaticStruct());
	check(InFactoryStruct->IsChildOf(FRigVMDispatchFactory::StaticStruct()));

	// ensure to register factories only once
	const FRigVMDispatchFactory* ExistingFactory = nullptr;

	const bool bFactoryAlreadyRegistered = Factories.ContainsByPredicate([InFactoryStruct, &ExistingFactory](const FRigVMDispatchFactory* Factory)
	{
		if(Factory->GetScriptStruct() == InFactoryStruct)
		{
			ExistingFactory = Factory;
			return true;
		}
		return false;
	});
	if(bFactoryAlreadyRegistered)
	{
		return ExistingFactory;
	}

#if WITH_EDITOR
	if(InFactoryStruct->HasMetaData(TEXT("Abstract")))
	{
		return nullptr;
	}
#endif
	
	FRigVMDispatchFactory* Factory = (FRigVMDispatchFactory*)FMemory::Malloc(InFactoryStruct->GetStructureSize());
	InFactoryStruct->InitializeStruct(Factory, 1);
	Factory->FactoryScriptStruct = InFactoryStruct;
	Factories.Add(Factory);
	Factory->RegisterDependencyTypes_NoLock(*this);
	return Factory;
}

void FRigVMRegistry_NoLock::RegisterPredicate_NoLock(UScriptStruct* InStruct, const TCHAR* InName, const TArray<FRigVMFunctionArgument>& InArguments)
{
	// Make sure the predicate does not already exist
	TArray<FRigVMFunction>& Predicates = StructNameToPredicates.FindOrAdd(InStruct->GetFName());
	if (Predicates.ContainsByPredicate([InName](const FRigVMFunction& Predicate)
	{
		return Predicate.Name == InName;
	}))
	{
		
		return;
	}

	FRigVMFunction Function(InName, nullptr, InStruct, Predicates.Num(), InArguments);
	Predicates.Add(Function);
}

void FRigVMRegistry_NoLock::RegisterObjectTypes_NoLock(TConstArrayView<TPair<UClass*, ERegisterObjectOperation>> InClasses)
{
	for (TPair<UClass*, ERegisterObjectOperation> ClassOpPair : InClasses)
	{
		UClass* Class = ClassOpPair.Key;
		ERegisterObjectOperation Operation = ClassOpPair.Value;

		// Only allow native object types
		if (Class->HasAnyClassFlags(CLASS_Native))
		{
			switch (Operation)
			{
			case ERegisterObjectOperation::Class:
				AllowedClasses.Add(Class);
				break;
			case ERegisterObjectOperation::ClassAndParents:
				{
					// Add all parent classes
					do
					{
						AllowedClasses.Add(Class);
						Class = Class->GetSuperClass();
					} while (Class);
					break;
				}
			case ERegisterObjectOperation::ClassAndChildren:
				{
					// Add all child classes
					TArray<UClass*> DerivedClasses({ Class });
					GetDerivedClasses(Class, DerivedClasses, /*bRecursive=*/true);
					for (UClass* DerivedClass : DerivedClasses)
					{
						AllowedClasses.Add(DerivedClass);
					}
					break;
				}
			}

		}
	}
}

void FRigVMRegistry_NoLock::RegisterStructTypes_NoLock(TConstArrayView<UScriptStruct*> InStructs)
{
	for (UScriptStruct* Struct : InStructs)
	{
		if(!Struct->IsA<UUserDefinedStruct>())
		{
			AllowedStructs.Add(Struct);
		}
	}
}

const FRigVMFunction* FRigVMRegistry_NoLock::FindFunction_NoLock(const TCHAR* InName, const FRigVMUserDefinedTypeResolver& InTypeResolver) const
{
	// Check first if the function is provided by internally registered rig units. 
	if(const int32* FunctionIndexPtr = FunctionNameToIndex.Find(InName))
	{
		return &Functions[*FunctionIndexPtr];
	}

	// Otherwise ask the associated dispatch factory for a function matching this signature.
	const FString NameString(InName);
	FString StructOrFactoryName, SuffixString;
	if(NameString.Split(TEXT("::"), &StructOrFactoryName, &SuffixString))
	{
		// if the factory has never been registered - FindDispatchFactory will try to look it up and register
		if(const FRigVMDispatchFactory* Factory = FindDispatchFactory_NoLock(*StructOrFactoryName))
		{
			if(const FRigVMTemplate* Template = Factory->GetTemplate_NoLock())
			{
				const FRigVMTemplateTypeMap ArgumentTypes = Template->GetArgumentTypesFromString_Impl(SuffixString, &InTypeResolver, false);
				if(ArgumentTypes.Num() == Template->NumArguments())
				{
					const int32 PermutationIndex = Template->FindPermutation(ArgumentTypes, false);
					if(PermutationIndex != INDEX_NONE)
					{
						return ((FRigVMTemplate*)Template)->GetOrCreatePermutation_NoLock(PermutationIndex);
					}
				}
			}
		}
	}

	// if we haven't been able to find the function - try to see if we can get the dispatch or rigvmstruct
	// from a core redirect
	if(!StructOrFactoryName.IsEmpty())
	{
		static const FString StructPrefix = TEXT("F");
		const bool bIsDispatchFactory = StructOrFactoryName.StartsWith(FRigVMDispatchFactory::DispatchPrefix, ESearchCase::CaseSensitive);
		if(bIsDispatchFactory)
		{
			StructOrFactoryName = StructOrFactoryName.Mid(FCString::Strlen(FRigVMDispatchFactory::DispatchPrefix));
		}
		else if(StructOrFactoryName.StartsWith(StructPrefix, ESearchCase::CaseSensitive))
		{
			StructOrFactoryName = StructOrFactoryName.Mid(StructPrefix.Len());
		}
		
		const FCoreRedirectObjectName OldObjectName(StructOrFactoryName);
		TArray<const FCoreRedirect*> Redirects;
		if(FCoreRedirects::GetMatchingRedirects(ECoreRedirectFlags::Type_Struct, OldObjectName, Redirects, ECoreRedirectMatchFlags::AllowPartialMatch))
		{
			for(const FCoreRedirect* Redirect : Redirects)
			{
				FString NewStructOrFactoryName = Redirect->NewName.ObjectName.ToString();

				// Check name differs - this could just be a struct that moved package
				if(NewStructOrFactoryName != StructOrFactoryName)
				{
					if(bIsDispatchFactory)
					{
						NewStructOrFactoryName = FRigVMDispatchFactory::DispatchPrefix + NewStructOrFactoryName;
					}
					else
					{
						NewStructOrFactoryName = StructPrefix + NewStructOrFactoryName;
					}
					const FRigVMFunction* RedirectedFunction = FindFunction_NoLock(*(NewStructOrFactoryName + TEXT("::") + SuffixString), InTypeResolver);
					if(RedirectedFunction)
					{
						FRigVMRegistry_NoLock& MutableRegistry = Get(LockType_Write);
						MutableRegistry.FunctionNameToIndex.Add(InName, RedirectedFunction->Index);
						return RedirectedFunction;
					}
				}
			}
		}
	}
	
	return nullptr;
}

const FRigVMFunction* FRigVMRegistry_NoLock::FindFunction_NoLock(UScriptStruct* InStruct, const TCHAR* InName, const FRigVMUserDefinedTypeResolver& InResolvalInfo) const
{
	check(InStruct);
	check(InName);

	const FString FunctionName = RigVMStringUtils::JoinStrings(InStruct->GetStructCPPName(), InName, TEXT("::"));
	return FindFunction_NoLock(*FunctionName, InResolvalInfo);
}

const TChunkedArray<FRigVMFunction>& FRigVMRegistry_NoLock::GetFunctions_NoLock() const
{
	return Functions;
}

const FRigVMTemplate* FRigVMRegistry_NoLock::FindTemplate_NoLock(const FName& InNotation, bool bIncludeDeprecated) const
{
	if (InNotation.IsNone())
	{
		return nullptr;
	}

	if(const int32* TemplateIndexPtr = TemplateNotationToIndex.Find(InNotation))
	{
		return &Templates[*TemplateIndexPtr];
	}

	const FString NotationString(InNotation.ToString());
	FString FactoryName, ArgumentsString;
	if(NotationString.Split(TEXT("("), &FactoryName, &ArgumentsString))
	{
		FRigVMRegistry_NoLock* MutableThis = const_cast<FRigVMRegistry_NoLock*>(this);
		
		// deal with a couple of custom cases
		static const TMap<FString, FString> CoreDispatchMap =
		{
			{
				TEXT("Equals::Execute"),
				MutableThis->FindOrAddDispatchFactory_NoLock<FRigVMDispatch_CoreEquals>()->GetFactoryName().ToString()
			},
			{
				TEXT("NotEquals::Execute"),
				MutableThis->FindOrAddDispatchFactory_NoLock<FRigVMDispatch_CoreNotEquals>()->GetFactoryName().ToString()
			},
		};

		if(const FString* RemappedDispatch = CoreDispatchMap.Find(FactoryName))
		{
			FactoryName = *RemappedDispatch;
		}
		
		if(const FRigVMDispatchFactory* Factory = FindDispatchFactory_NoLock(*FactoryName))
		{
			return Factory->GetTemplate_NoLock();
		}
	}

	if (bIncludeDeprecated)
	{
		if(const int32* TemplateIndexPtr = DeprecatedTemplateNotationToIndex.Find(InNotation))
		{
			return &DeprecatedTemplates[*TemplateIndexPtr];
		}
	}

	const FString OriginalNotation = InNotation.ToString();

	// we may have a dispatch factory which has to be redirected
#if WITH_EDITOR
	if(OriginalNotation.StartsWith(FRigVMDispatchFactory::DispatchPrefix))
	{
		static const int32 PrefixLen = FCString::Strlen(FRigVMDispatchFactory::DispatchPrefix);
		const int32 BraceIndex = OriginalNotation.Find(TEXT("("));
		const FString OriginalDispatchFactoryName = OriginalNotation.Mid(PrefixLen, BraceIndex - PrefixLen); 

		const FCoreRedirectObjectName OldObjectName(OriginalDispatchFactoryName);
		TArray<const FCoreRedirect*> Redirects;
		if(FCoreRedirects::GetMatchingRedirects(ECoreRedirectFlags::Type_Struct, OldObjectName, Redirects, ECoreRedirectMatchFlags::AllowPartialMatch))
		{
			for(const FCoreRedirect* Redirect : Redirects)
			{
				const FString NewDispatchFactoryName = FRigVMDispatchFactory::DispatchPrefix + Redirect->NewName.ObjectName.ToString();
				if(const FRigVMDispatchFactory* NewDispatchFactory = FindDispatchFactory_NoLock(*NewDispatchFactoryName))
				{
					return NewDispatchFactory->GetTemplate_NoLock();
				}
			}
		}
	}
#endif

	// if we still arrive here we may have a template that used to contain an executecontext.
	{
		FString SanitizedNotation = OriginalNotation;

		static const TArray<TPair<FString, FString>> ExecuteContextArgs = {
			{ TEXT("FRigUnit_SequenceExecution::Execute(in ExecuteContext,out A,out B,out C,out D)"), TEXT("FRigUnit_SequenceExecution::Execute()") },
			{ TEXT("FRigUnit_SequenceAggregate::Execute(in ExecuteContext,out A,out B)"), TEXT("FRigUnit_SequenceAggregate::Execute()") },
			{ TEXT(",io ExecuteContext"), TEXT("") },
			{ TEXT("io ExecuteContext,"), TEXT("") },
			{ TEXT("(io ExecuteContext)"), TEXT("()") },
			{ TEXT(",out ExecuteContext"), TEXT("") },
			{ TEXT("out ExecuteContext,"), TEXT("") },
			{ TEXT("(out ExecuteContext)"), TEXT("()") },
			{ TEXT(",out Completed"), TEXT("") },
			{ TEXT("out Completed,"), TEXT("") },
			{ TEXT("(out Completed)"), TEXT("()") },
		};

		for(int32 Index = 0; Index < ExecuteContextArgs.Num(); Index++)
		{
			const TPair<FString, FString>& Pair = ExecuteContextArgs[Index];
			if(SanitizedNotation.Contains(Pair.Key))
			{
				SanitizedNotation = SanitizedNotation.Replace(*Pair.Key, *Pair.Value);
			}
		}

		if(SanitizedNotation != OriginalNotation)
		{
			return FindTemplate_NoLock(*SanitizedNotation, bIncludeDeprecated);
		}
	}

	return nullptr;
}

const TChunkedArray<FRigVMTemplate>& FRigVMRegistry_NoLock::GetTemplates_NoLock() const
{
	return Templates;
}

const FRigVMTemplate* FRigVMRegistry_NoLock::GetOrAddTemplateFromArguments_NoLock(const FName& InName, const TArray<FRigVMTemplateArgumentInfo>& InInfos, const FRigVMTemplateDelegates& InDelegates)
{
	// avoid reentry in FindTemplate. try to find an existing
	// template only if we are not yet in ::FindTemplate.
	const FName Notation = FRigVMTemplateArgumentInfo::ComputeTemplateNotation(InName, InInfos);
	if(const FRigVMTemplate* ExistingTemplate = FindTemplate_NoLock(Notation))
	{
		return ExistingTemplate;
	}

	return AddTemplateFromArguments_NoLock(InName, InInfos, InDelegates);
}

const FRigVMTemplate* FRigVMRegistry_NoLock::AddTemplateFromArguments_NoLock(const FName& InName, const TArray<FRigVMTemplateArgumentInfo>& InInfos, const FRigVMTemplateDelegates& InDelegates)
{
	// we only support to ask for templates here which provide singleton types
	int32 NumPermutations = 0;
	FRigVMTemplate Template(InName, InInfos);
	for(const FRigVMTemplateArgument& Argument : Template.Arguments)
	{
		const int32 NumIndices = Argument.GetNumTypes_NoLock();
		if(!Argument.IsSingleton_NoLock() && NumPermutations > 1)
		{
			if(NumIndices != NumPermutations)
			{
				UE_LOG(LogRigVM, Error, TEXT("Failed to add template '%s' since the arguments' types counts don't match."), *InName.ToString());
				return nullptr;
			}
		}
		NumPermutations = FMath::Max(NumPermutations, NumIndices); 
	}

	// if any of the arguments are wildcards we'll need to update the types
	for(FRigVMTemplateArgument& Argument : Template.Arguments)
	{
		const int32 NumTypes = Argument.GetNumTypes_NoLock();
		if(NumTypes == 1)
		{
			const TRigVMTypeIndex FirstTypeIndex = Argument.GetTypeIndex_NoLock(0);
			if(IsWildCardType_NoLock(FirstTypeIndex))
			{
#if WITH_EDITOR
				Argument.InvalidatePermutations(FirstTypeIndex);
#endif
				if(IsArrayType_NoLock(FirstTypeIndex))
				{
					Argument.TypeCategories.Add(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue);
				}
				else
				{
					Argument.TypeCategories.Add(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue);
				}
				Argument.bUseCategories = true;
				Argument.TypeIndices.Reset();
		
				NumPermutations = FMath::Max(NumPermutations, Argument.GetNumTypes_NoLock());
			}
		}
	}

	// Remove duplicate permutations
	/*
	 * we'll disable this for now since it's not a valid approach.
	 * most arguments use type indices by categories, so we can't just remove
	 * single type indices.
	 */

	TArray<FRigVMTypeCacheScope_NoLock> TypeCaches;
	if(!Template.Arguments.IsEmpty())
	{
		//TArray<bool> ToRemove;
		//int32 NumToRemove = 0;
		//ToRemove.AddZeroed(NumPermutations);

		//TSet<uint32> EncounteredPermutations;
		//EncounteredPermutations.Reserve(NumPermutations);

		const int32 NumArguments = Template.Arguments.Num();
		TypeCaches.SetNum(NumArguments);
		
		bool bAnyArgumentWithZeroTypes = false;
		for(int32 ArgIndex = 0; ArgIndex < NumArguments; ArgIndex++)
		{
			(void)TypeCaches[ArgIndex].UpdateIfRequired(Template.Arguments[ArgIndex]);
			bAnyArgumentWithZeroTypes = bAnyArgumentWithZeroTypes || TypeCaches[ArgIndex].GetNumTypes_NoLock() == 0;
		}

		/*
		if(!bAnyArgumentWithZeroTypes)
		{
			for(int32 Index = 0; Index < NumPermutations; Index++)
			{
				uint32 Hash = GetTypeHash(TypeCaches[0].GetTypeIndex_NoLock(Index));
				for(int32 ArgIndex = 1; ArgIndex < NumArguments; ArgIndex++)
				{
					Hash = HashCombine(Hash, GetTypeHash(TypeCaches[ArgIndex].GetTypeIndex_NoLock(Index)));
				}

				if (EncounteredPermutations.Contains(Hash))
				{
					ToRemove[Index] = true;
					NumToRemove++;
				}
				else
				{
					EncounteredPermutations.Add(Hash);
				}
			}
		}
		
		if(NumToRemove > 0)
		{
			for(FRigVMTemplateArgument& Argument : Template.Arguments)
			{
				// this is not enough - we may have arguments where the type indices
				// array is empty...
				// if(Argument.IsSingleton_NoLock())
				//{
				//	continue;
				//}
				
				TArray<TRigVMTypeIndex> NewTypeIndices;
				NewTypeIndices.Reserve(Argument.TypeIndices.Num() - NumToRemove);

				for(int32 Index = 0; Index < Argument.TypeIndices.Num(); Index++)
				{
					if(!ToRemove[Index])
					{
						NewTypeIndices.Add(Argument.TypeIndices[Index]);
					}
				}
				Argument.TypeIndices = MoveTemp(NewTypeIndices);
			}
			NumPermutations -= NumToRemove;
		}
		*/
	}

#if WITH_EDITOR
	for(FRigVMTemplateArgument& Argument : Template.Arguments)
	{
		Argument.UpdateTypeToPermutationsSlow();
	}
#endif

	Template.Permutations.Init(INDEX_NONE, NumPermutations);
	Template.RecomputeTypesHashToPermutations(TypeCaches);

	const int32 Index = Templates.AddElement(Template);
	Templates[Index].Index = Index;
	Templates[Index].Delegates = InDelegates;
	TemplateNotationToIndex.Add(Template.GetNotation(), Index);

	for(int32 ArgumentIndex=0; ArgumentIndex < Templates[Index].Arguments.Num(); ArgumentIndex++)
	{
		for(const FRigVMTemplateArgument::ETypeCategory& ArgumentTypeCategory : Templates[Index].Arguments[ArgumentIndex].TypeCategories)
		{
			TemplatesPerCategory.FindChecked(ArgumentTypeCategory).AddUnique(Index);
		}
	}
	
	return &Templates[Index];
}

FRigVMDispatchFactory* FRigVMRegistry_NoLock::FindDispatchFactory_NoLock(const FName& InFactoryName) const
{
	FRigVMDispatchFactory* const* FactoryPtr = Factories.FindByPredicate([InFactoryName](const FRigVMDispatchFactory* Factory) -> bool
	{
		return Factory->GetFactoryName() == InFactoryName;
	});
	if(FactoryPtr)
	{
		return *FactoryPtr;
	}

	FString FactoryName = InFactoryName.ToString();
	
	// if the factory has never been registered - we should try to look it up	
	if(FactoryName.StartsWith(FRigVMDispatchFactory::DispatchPrefix))
	{
		const FString ScriptStructName = FactoryName.Mid(FCString::Strlen(FRigVMDispatchFactory::DispatchPrefix));
		if(UScriptStruct* FactoryStruct = FindFirstObject<UScriptStruct>(*ScriptStructName, EFindFirstObjectOptions::NativeFirst | EFindFirstObjectOptions::EnsureIfAmbiguous))
		{
			FRigVMRegistry_NoLock* MutableThis = const_cast<FRigVMRegistry_NoLock*>(this);
			return const_cast<FRigVMDispatchFactory*>(MutableThis->RegisterFactory_NoLock(FactoryStruct));
		}
	}	
	
	return nullptr;
}

FRigVMDispatchFactory* FRigVMRegistry_NoLock::FindOrAddDispatchFactory_NoLock(UScriptStruct* InFactoryStruct)
{
	return const_cast<FRigVMDispatchFactory*>(RegisterFactory_NoLock(InFactoryStruct));
}

FString FRigVMRegistry_NoLock::FindOrAddSingletonDispatchFunction_NoLock(UScriptStruct* InFactoryStruct)
{
	if(const FRigVMDispatchFactory* Factory = FindOrAddDispatchFactory_NoLock(InFactoryStruct))
	{
		if(Factory->IsSingleton())
		{
			if(const FRigVMTemplate* Template = Factory->GetTemplate_NoLock())
			{
				// use the types for the first permutation - since we don't care
				// for a singleton dispatch
				const FRigVMTemplateTypeMap TypesForPrimaryPermutation = Template->GetTypesForPermutation_NoLock(0);
				const FString Name = Factory->GetPermutationName(TypesForPrimaryPermutation, false);
				if(const FRigVMFunction* Function = FindFunction_NoLock(*Name))
				{
					return Function->Name;
				}
			}
		}
	}
	return FString();
}

const TArray<FRigVMDispatchFactory*>& FRigVMRegistry_NoLock::GetFactories_NoLock() const
{	return Factories;
}

const TArray<FRigVMFunction>* FRigVMRegistry_NoLock::GetPredicatesForStruct_NoLock(const FName& InStructName) const
{
	return StructNameToPredicates.Find(InStructName);
}

FRigVMRegistry_RWLock::FRigVMRegistry_RWLock()
	: FRigVMRegistry_NoLock()
{
	Initialize(true);
}

void FRigVMRegistry_RWLock::Initialize(bool bLockRegistry)
{
	LockType = LockType_Invalid;
	LockCount = 0;
	
	const FConditionalWriteScopeLock _(*this, bLockRegistry);
	
	Initialize_NoLock();
	
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().OnAssetRemoved().AddRaw(this, &FRigVMRegistry_RWLock::OnAssetRemoved);
	AssetRegistryModule.Get().OnAssetRenamed().AddRaw(this, &FRigVMRegistry_RWLock::OnAssetRenamed);

	IPluginManager::Get().OnNewPluginMounted().AddRaw(this, &FRigVMRegistry_RWLock::OnPluginLoaded);
	IPluginManager::Get().OnPluginUnmounted().AddRaw(this, &FRigVMRegistry_RWLock::OnPluginUnloaded);
	
	UE::Anim::AttributeTypes::GetOnAttributeTypesChanged().AddRaw(this, &FRigVMRegistry_RWLock::OnAnimationAttributeTypesChanged);
}

void FRigVMRegistry_RWLock::EnsureLocked(ELockType InLockType)
{
	check(InLockType != LockType_Invalid);

	const FRigVMRegistry_RWLock& Registry = Get();
	const ELockType CurrentLockType = Registry.LockType.load();

	switch(InLockType)
	{
		case LockType_Read:
		{
			ensureMsgf(
				(CurrentLockType == LockType_Read) ||
				(CurrentLockType == LockType_Write),
				TEXT("The Registry is not locked for reading yet - access to the NoLock registry is only possible after locking the RWLock registry (by using its public API calls)."));
			break;
		}
		case LockType_Write:
		{
			ensureMsgf(
				(CurrentLockType == LockType_Write),
				TEXT("The Registry is not locked for writing yet - access to the NoLock registry is only possible after locking the RWLock registry (by using its public API calls)."));
			break;
		}
		default:
		{
			break;
		}
	}
}
