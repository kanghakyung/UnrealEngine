// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraScriptExecutionContext.h"
#include "NiagaraScript.h"
#include "NiagaraSettings.h"
#include "NiagaraStats.h"
#include "NiagaraDataInterface.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraFunctionLibrary.h"
#include "VectorVMRuntime.h"

DECLARE_CYCLE_STAT(TEXT("Register Setup"), STAT_NiagaraSimRegisterSetup, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Context Ticking"), STAT_NiagaraScriptExecContextTick, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Rebind DInterface Func Table"), STAT_NiagaraRebindDataInterfaceFunctionTable, STATGROUP_Niagara);
	//Add previous frame values if we're interpolated spawn.
	
	//Internal constants - only needed for non-GPU sim

static int32 GbExecVMScripts = 1;
static FAutoConsoleVariableRef CVarNiagaraExecVMScripts(
	TEXT("fx.ExecVMScripts"),
	GbExecVMScripts,
	TEXT("If > 0 VM scripts will be executed, otherwise they won't, useful for looking at the bytecode for a crashing compiled script. \n"),
	ECVF_Default
);

static int32 GbForceExecVMPath = 0;
static FAutoConsoleVariableRef CVarNiagaraForceExecVMPath(
	TEXT("fx.ForceExecVMPath"),
	GbForceExecVMPath,
	TEXT("If < 0, the legacy VM path will be used, if > 0 the experimental version will be used, and the default if 0. \n"),
	ECVF_Default
);

FNiagaraScriptExecutionContextBase::FNiagaraScriptExecutionContextBase()
	: Script(nullptr)
	, VectorVMState(nullptr)
	, ScriptType(ENiagaraSystemSimulationScript::Update)
	, bAllowParallel(true)
	, bHasDIsWithPreStageTick(false)
	, bHasDIsWithPostStageTick(false)
{

}

FNiagaraScriptExecutionContextBase::~FNiagaraScriptExecutionContextBase()
{
	VectorVM::Runtime::FreeVectorVMState(VectorVMState);
}

bool FNiagaraScriptExecutionContextBase::Init(FNiagaraSystemInstance* Instance, UNiagaraScript* InScript, ENiagaraSimTarget InTarget)
{
	Script = InScript;
	if (!ensure(Script != nullptr))
	{
		return false;
	}

	Parameters.InitFromOwningContext(Script, InTarget, true);

	HasInterpolationParameters = Script && Script->GetComputedVMCompilationId().HasInterpolatedParameters();

	VectorVMState = VectorVM::Runtime::AllocVectorVMState(Script->GetVMExecutableData().ExperimentalContextData);

	//If the instance is null, aka a system script, we need to pre calculate whether we'll have pre/post ticks or not.
	if(Instance == nullptr)
	{
		ENiagaraScriptUsage Usage = Script->GetUsage();
		for (auto& ResolvedDIInfo : Script->GetResolvedDataInterfaces())
		{
			if (ResolvedDIInfo.ResolvedDataInterface)
			{
				bHasDIsWithPreStageTick |= (ResolvedDIInfo.ResolvedDataInterface->HasPreStageTick(Usage));
				bHasDIsWithPostStageTick |= (ResolvedDIInfo.ResolvedDataInterface->HasPostStageTick(Usage));
			}
			if (bHasDIsWithPreStageTick && bHasDIsWithPostStageTick)
			{
				break;
			}
		}
	}

	return true;
}

void FNiagaraScriptExecutionContextBase::InitDITickLists(class FNiagaraSystemInstance* Instance)
{
	ENiagaraScriptUsage Usage = Script->GetUsage();
	for (auto& ResolvedDIInfo : Script->GetResolvedDataInterfaces())
	{
		if (ResolvedDIInfo.ResolvedDataInterface)
		{
			bHasDIsWithPreStageTick |= (ResolvedDIInfo.ResolvedDataInterface->HasPreStageTick(Usage));
			bHasDIsWithPostStageTick |= (ResolvedDIInfo.ResolvedDataInterface->HasPostStageTick(Usage));
		}
		if (bHasDIsWithPreStageTick && bHasDIsWithPostStageTick)
		{
			break;
		}
	}

	if (Instance && (bHasDIsWithPreStageTick || bHasDIsWithPostStageTick))
	{
		DIStageTickHandler.Init(Script, Instance);
	}
}

void FNiagaraScriptExecutionContextBase::BindData(int32 Index, FNiagaraDataSet& DataSet, int32 StartInstance, bool bUpdateInstanceCounts)
{
	FNiagaraDataBuffer* Input = DataSet.GetCurrentData();
	FNiagaraDataBuffer* Output = DataSet.GetDestinationData();

	DataSetInfo.SetNum(FMath::Max(DataSetInfo.Num(), Index + 1));
	DataSetInfo[Index].Init(&DataSet, Input, StartInstance, bUpdateInstanceCounts);

	//Would be nice to roll this and DataSetInfo into one but currently the VM being in it's own Engine module prevents this. Possibly should move the VM into Niagara itself.
	FDataSetMeta::FInputRegisterView InputRegisters = Input ? Input->ReadRegisterTable() : FDataSetMeta::FInputRegisterView();
	FDataSetMeta::FOutputRegisterView OutputRegisters = Output ? Output->EditRegisterTable() : FDataSetMeta::FOutputRegisterView();

	DataSetMetaTable.SetNum(FMath::Max(DataSetMetaTable.Num(), Index + 1));
	DataSetMetaTable[Index].Init(InputRegisters, OutputRegisters, StartInstance,
		Output ? &Output->GetIDTable() : nullptr, &DataSet.GetFreeIDTable(), DataSet.GetNumFreeIDsPtr(), &DataSet.NumSpawnedIDs, DataSet.GetMaxUsedIDPtr(), DataSet.GetIDAcquireTag(), &DataSet.GetSpawnedIDsTable());

	if (InputRegisters.Num() > 0)
	{
		static_assert(sizeof(DataSetMetaTable[Index].InputRegisterTypeOffsets) == sizeof(FNiagaraDataBuffer::RegisterTypeOffsetType), "ArraySizes do not match");
		memcpy(DataSetMetaTable[Index].InputRegisterTypeOffsets, Input->GetRegisterTypeOffsets(), sizeof(FNiagaraDataBuffer::RegisterTypeOffsetType));
	}

	if (OutputRegisters.Num() > 0)
	{
		static_assert(sizeof(DataSetMetaTable[Index].OutputRegisterTypeOffsets) == sizeof(FNiagaraDataBuffer::RegisterTypeOffsetType), "ArraySizes do not match");
		memcpy(DataSetMetaTable[Index].OutputRegisterTypeOffsets, Output->GetRegisterTypeOffsets(), sizeof(FNiagaraDataBuffer::RegisterTypeOffsetType));
	}
}

void FNiagaraScriptExecutionContextBase::BindData(int32 Index, FNiagaraDataBuffer* Input, int32 StartInstance, bool bUpdateInstanceCounts)
{
	check(Input && Input->GetOwner());
	DataSetInfo.SetNum(FMath::Max(DataSetInfo.Num(), Index + 1));
	FNiagaraDataSet* DataSet = Input->GetOwner();
	DataSetInfo[Index].Init(DataSet, Input, StartInstance, bUpdateInstanceCounts);

	FDataSetMeta::FInputRegisterView InputRegisters = Input->ReadRegisterTable();

	DataSetMetaTable.SetNum(FMath::Max(DataSetMetaTable.Num(), Index + 1));
	DataSetMetaTable[Index].Init(InputRegisters, FDataSetMeta::FOutputRegisterView(), StartInstance, nullptr, nullptr, DataSet->GetNumFreeIDsPtr(), &DataSet->NumSpawnedIDs, DataSet->GetMaxUsedIDPtr(), DataSet->GetIDAcquireTag(), &DataSet->GetSpawnedIDsTable());

	if (InputRegisters.Num() > 0)
	{
		static_assert(sizeof(DataSetMetaTable[Index].InputRegisterTypeOffsets) == sizeof(FNiagaraDataBuffer::RegisterTypeOffsetType), "ArraySizes do not match");
		memcpy(DataSetMetaTable[Index].InputRegisterTypeOffsets, Input->GetRegisterTypeOffsets(), sizeof(FNiagaraDataBuffer::RegisterTypeOffsetType));
	}
}

#if STATS
void FNiagaraScriptExecutionContextBase::CreateStatScopeData()
{
	TArrayView<const TStatId> StatScopeIDs = Script->GetStatScopeIDs();
	StatScopeData.Empty(StatScopeIDs.Num());
	for (const TStatId& StatId : StatScopeIDs)
	{
		StatScopeData.Add(VectorVM::FStatScopeData(StatId));
	}
}

TMap<TStatIdData const*, float> FNiagaraScriptExecutionContextBase::ReportStats()
{
	// Process recorded times
	for (VectorVM::FStatScopeData& ScopeData : StatScopeData)
	{
		uint64 ExecCycles = ScopeData.ExecutionCycleCount.exchange(0);
		if (ExecCycles > 0)
		{
			ExecutionTimings.FindOrAdd(ScopeData.StatId.GetRawPointer()) = static_cast<float>(ExecCycles);
		}
	}
	return ExecutionTimings;
}
#endif

static void *VVMRealloc(void* Ptr, size_t NumBytes, const char *Filename, int LineNum)
{
	return FMemory::Realloc(Ptr, NumBytes);
}
static void VVMFree(void* Ptr, const char *Filename, int LineNum)
{
	return FMemory::Free(Ptr);
}

bool FNiagaraScriptExecutionContextBase::Execute(class FNiagaraSystemInstance* Instance, float DeltaSeconds, uint32 NumInstances, const FScriptExecutionConstantBufferTable& ConstantBufferTable)
{
	if (NumInstances == 0)
	{
		DataSetInfo.Reset();
		return true;
	}

	DIStageTickHandler.PreStageTick(Instance, DeltaSeconds);

	if (GbExecVMScripts != 0)
	{
#if STATS
		CreateStatScopeData();
#endif	

		const bool bSuccess = ExecuteInternal(NumInstances, ConstantBufferTable);

		// Tell the datasets we wrote how many instances were actually written.
		for (int Idx = 0; Idx < DataSetInfo.Num(); Idx++)
		{
			FNiagaraDataSetExecutionInfo& Info = DataSetInfo[Idx];

#if NIAGARA_NAN_CHECKING
			Info.DataSet->CheckForNaNs();
#endif
			if (Info.bUpdateInstanceCount && Info.GetOutput())
			{
				Info.GetOutput()->SetNumInstances(Info.StartInstance + DataSetMetaTable[Idx].DataSetAccessIndex + 1);
			}
		}

		//Can maybe do without resetting here. Just doing it for tidiness.
		for (int32 DataSetIdx = 0; DataSetIdx < DataSetInfo.Num(); ++DataSetIdx)
		{
			DataSetInfo[DataSetIdx].Reset();
			DataSetMetaTable[DataSetIdx].Reset();
		}
	}

	DIStageTickHandler.PostStageTick(Instance, DeltaSeconds);
	return true;//TODO: Error cases?
}

bool FNiagaraScriptExecutionContextBase::ExecuteInternal(uint32 NumInstances, const FScriptExecutionConstantBufferTable& ConstantBufferTable)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VectorVM_Experimental);
#if STATS
	const uint64 ExecutionStartCycles = FPlatformTime::Cycles64();
#endif
	VectorVM::Runtime::FVectorVMExecContext ExecCtx;
	ExecCtx.VVMState               = VectorVMState;
	ExecCtx.DataSets               = DataSetMetaTable;
	ExecCtx.ExtFunctionTable       = FunctionTable;
	ExecCtx.UserPtrTable           = UserPtrTable;
	ExecCtx.NumInstances           = NumInstances;
	ExecCtx.ConstantTableData      = ConstantBufferTable.Buffers.GetData();
	ExecCtx.ConstantTableNumBytes  = ConstantBufferTable.BufferSizes.GetData();
	ExecCtx.ConstantTableCount     = ConstantBufferTable.Buffers.Num();

	if (VectorVMState)
	{
		VectorVM::Runtime::ExecVectorVMState(&ExecCtx);
	}

#if STATS
	// We fill them all out as this makes sure the UI can pickup the appropriate scope
	for (VectorVM::FStatScopeData& StatScope : StatScopeData)
	{
		StatScope.ExecutionCycleCount += FPlatformTime::Cycles64() - ExecutionStartCycles;
	}
#endif
	return true;
}

TArrayView<const uint8> FNiagaraScriptExecutionContextBase::GetScriptLiterals() const
{
#if WITH_EDITORONLY_DATA
	if (!Script->IsScriptCooked())
	{
		return Parameters.GetScriptLiterals();
	}
#endif
	return MakeArrayView(Script->GetVMExecutableData().ScriptLiterals);
}

//////////////////////////////////////////////////////////////////////////

void FNiagaraScriptExecutionContextBase::DirtyDataInterfaces()
{
	Parameters.MarkInterfacesDirty();
}

bool FNiagaraScriptExecutionContext::Tick(FNiagaraSystemInstance* ParentSystemInstance, ENiagaraSimTarget SimTarget)
{
	//Bind data interfaces if needed.
	if (Parameters.GetInterfacesDirty())
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraScriptExecContextTick);
		if (Script && Script->IsReadyToRun(ENiagaraSimTarget::CPUSim) && SimTarget == ENiagaraSimTarget::CPUSim)//TODO: Remove. Script can only be null for system instances that currently don't have their script exec context set up correctly.
		{
			const FNiagaraVMExecutableData& ScriptExecutableData = Script->GetVMExecutableData();
			const TArray<UNiagaraDataInterface*>& DataInterfaces = GetDataInterfaces();

			SCOPE_CYCLE_COUNTER(STAT_NiagaraRebindDataInterfaceFunctionTable);
			// UE_LOG(LogNiagara, Log, TEXT("Updating data interfaces for script %s"), *Script->GetFullName());

			// We must make sure that the data interfaces match up between the original script values and our overrides...
			if (ScriptExecutableData.DataInterfaceInfo.Num() != DataInterfaces.Num())
			{
				UE_LOG(LogNiagara, Warning, TEXT("Mismatch between Niagara Exectuion Context data interfaces and those in it's script!"));
				return false;
			}

			const FNiagaraScriptExecutionParameterStore* ScriptParameterStore = Script->GetExecutionReadyParameterStore(SimTarget);
			check(ScriptParameterStore != nullptr);

			//Fill the instance data table.
			if (ParentSystemInstance)
			{
				UserPtrTable.SetNumZeroed(ScriptExecutableData.NumUserPtrs, EAllowShrinking::No);
				for (int32 i = 0; i < DataInterfaces.Num(); i++)
				{
					UNiagaraDataInterface* Interface = DataInterfaces[i];

					int32 UserPtrIdx = ScriptExecutableData.DataInterfaceInfo[i].UserPtrIdx;
					if (UserPtrIdx != INDEX_NONE)
					{
						if (void* InstData = ParentSystemInstance->FindDataInterfaceInstanceData(Interface))
						{
							UserPtrTable[UserPtrIdx] = InstData;
						}
						else
						{
							UE_LOG(LogNiagara, Warning, TEXT("Failed to resolve User Pointer for UserPtrTable[%d] looking for DI: %s for system: %s"),
								UserPtrIdx,
								*Interface->GetName(),
								*ParentSystemInstance->GetSystem()->GetName());
							return false;
						}
					}
				}
			}
			else
			{
				check(ScriptExecutableData.NumUserPtrs == 0);//Can't have user ptrs if we have no parent instance.
			}

			const int32 FunctionCount = ScriptExecutableData.CalledVMExternalFunctions.Num();
			FunctionTable.Reset(FunctionCount);
			FunctionTable.AddZeroed(FunctionCount);
			LocalFunctionTable.Reset();
			TArray<int32> LocalFunctionTableIndices;
			LocalFunctionTableIndices.Reserve(FunctionCount);

			const auto& ScriptDataInterfaces = ScriptParameterStore->GetDataInterfaces();

			bool bSuccessfullyMapped = true;

			for (int32 FunctionIt = 0; FunctionIt < FunctionCount; ++FunctionIt)
			{
				const FVMExternalFunctionBindingInfo& BindingInfo = ScriptExecutableData.CalledVMExternalFunctions[FunctionIt];

				// First check to see if we can pull from the fast path library..
				FVMExternalFunction FuncBind;
				if (UNiagaraFunctionLibrary::GetVectorVMFastPathExternalFunction(BindingInfo, FuncBind) && FuncBind.IsBound())
				{
					LocalFunctionTable.Add(FuncBind);
					LocalFunctionTableIndices.Add(FunctionIt);
					continue;
				}

				for (int32 i = 0; i < ScriptExecutableData.DataInterfaceInfo.Num(); i++)
				{
					const FNiagaraScriptDataInterfaceCompileInfo& ScriptInfo = ScriptExecutableData.DataInterfaceInfo[i];
					UNiagaraDataInterface* ExternalInterface = DataInterfaces[i];
					if (ScriptInfo.Name == BindingInfo.OwnerName)
					{
						// first check to see if we should just use the one from the script
						if (ScriptExecutableData.CalledVMExternalFunctionBindings.IsValidIndex(FunctionIt)
							&& ScriptDataInterfaces.IsValidIndex(i)
							&& ExternalInterface == ScriptDataInterfaces[i])
						{
							const FVMExternalFunction& ScriptFuncBind = ScriptExecutableData.CalledVMExternalFunctionBindings[FunctionIt];
							if (ScriptFuncBind.IsBound())
							{
								FunctionTable[FunctionIt] = &ScriptFuncBind;

								check(ScriptInfo.UserPtrIdx == INDEX_NONE);
								break;
							}
						}

						void* InstData = ScriptInfo.UserPtrIdx == INDEX_NONE ? nullptr : UserPtrTable[ScriptInfo.UserPtrIdx];
						FVMExternalFunction& LocalFunction = LocalFunctionTable.AddDefaulted_GetRef();
						LocalFunctionTableIndices.Add(FunctionIt);

						if (ExternalInterface != nullptr)
						{
							ExternalInterface->GetVMExternalFunction(BindingInfo, InstData, LocalFunction);
						}

						if (!LocalFunction.IsBound())
						{
							UE_LOG(LogNiagara, Error, TEXT("Could not Get VMExternalFunction '%s'.. emitter will not run!"), *BindingInfo.Name.ToString());
							bSuccessfullyMapped = false;
						}
						break;
					}
				}
			}

			const int32 LocalFunctionCount = LocalFunctionTableIndices.Num();
			for (int32 LocalFunctionIt = 0; LocalFunctionIt < LocalFunctionCount; ++LocalFunctionIt)
			{
				FunctionTable[LocalFunctionTableIndices[LocalFunctionIt]] = &LocalFunctionTable[LocalFunctionIt];
			}

			for (int32 i = 0; i < FunctionTable.Num(); i++)
			{
				if (FunctionTable[i] == nullptr)
				{
					UE_LOG(LogNiagara, Warning, TEXT("Invalid Function Table Entry! %s"), *ScriptExecutableData.CalledVMExternalFunctions[i].Name.ToString());
				}
			}

#if WITH_EDITOR	
			// We may now have new errors that we need to broadcast about, so flush the asset parameters delegate..
			if (ParentSystemInstance)
			{
				ParentSystemInstance->RaiseNeedsUIResync();
			}
#endif

			if (!bSuccessfullyMapped)
			{
				UE_LOG(LogNiagara, Warning, TEXT("Error building data interface function table!"));
				FunctionTable.Empty();
				return false;
			}
		}
	}
	if (ParentSystemInstance && Parameters.GetPositionDataDirty())
	{
		Parameters.ResolvePositions(ParentSystemInstance->GetLWCConverter());
	}
	Parameters.Tick();

	return true;
}

void FNiagaraScriptExecutionContextBase::PostTick()
{
	//If we're for interpolated spawn, copy over the previous frame's parameters into the Prev parameters.
	if (HasInterpolationParameters)
	{
		Parameters.CopyCurrToPrev();
	}
}

//////////////////////////////////////////////////////////////////////////
static void PerInsFn(FVectorVMExternalFunctionContext& ParentContext, FVectorVMExternalFunctionContext& PerInsFnContext, TArray<FNiagaraSystemInstance*>** SystemInstances, ENiagaraSystemSimulationScript ScriptType, int32 PerInstFunctionIndex, int32 UserPtrIdx)
{
	check(PerInsFnContext.DataSets.Num() > 0);
	check(SystemInstances);
	check(*SystemInstances);

	void* SavedUserPtrData = UserPtrIdx != INDEX_NONE ? PerInsFnContext.UserPtrTable[UserPtrIdx] : NULL;
	int32 InstanceOffset = PerInsFnContext.DataSets[0].InstanceOffset; //Apparently the function table is generated based off the first data set, therefore this is safe. (I don't like this - @shawn mcgrath)	
	int NumInstances = PerInsFnContext.NumInstances;
	PerInsFnContext.NumInstances = 1;
	for (int i = 0; i < NumInstances; ++i)
	{
		PerInsFnContext.RegReadCount = 0;
		PerInsFnContext.PerInstanceFnInstanceIdx = i;

		int32 InstanceIndex = InstanceOffset + PerInsFnContext.StartInstance + i;
		FNiagaraSystemInstance* Instance = (**SystemInstances)[InstanceIndex];
		const FNiagaraPerInstanceDIFuncInfo& FuncInfo = Instance->GetPerInstanceDIFunction(ScriptType, PerInstFunctionIndex);

		if (UserPtrIdx != INDEX_NONE)
		{
			PerInsFnContext.UserPtrTable[UserPtrIdx] = FuncInfo.InstData;
		}
		FuncInfo.Function.Execute(ParentContext);
	}

	if (SavedUserPtrData)
	{
		PerInsFnContext.UserPtrTable[UserPtrIdx] = SavedUserPtrData;
	}
}

bool FNiagaraSystemScriptExecutionContext::Init(FNiagaraSystemInstance* ParentSystemInstance, UNiagaraScript* InScript, ENiagaraSimTarget InTarget)
{
	check(ParentSystemInstance == nullptr);
	//FORT - 314222 - There is a bug currently when system scripts execute in parallel.
	//This is unlikely for these scripts but we're explicitly disallowing it for safety.
	bAllowParallel = false;

	return FNiagaraScriptExecutionContextBase::Init(ParentSystemInstance, InScript, InTarget);
}

bool FNiagaraSystemScriptExecutionContext::Tick(class FNiagaraSystemInstance* Instance, ENiagaraSimTarget SimTarget)
{
	//Bind data interfaces if needed.
	if (Parameters.GetInterfacesDirty())
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraScriptExecContextTick);
		if (Script && Script->IsReadyToRun(ENiagaraSimTarget::CPUSim))//TODO: Remove. Script can only be null for system instances that currently don't have their script exec context set up correctly.
		{
			const FNiagaraVMExecutableData& ScriptExecutableData = Script->GetVMExecutableData();
			const TArray<UNiagaraDataInterface*>& DataInterfaces = GetDataInterfaces();

			const int32 FunctionCount = ScriptExecutableData.CalledVMExternalFunctions.Num();

			FunctionTable.Init(nullptr, FunctionCount);
			ExtFunctionInfo.Init(FExternalFuncInfo(), FunctionCount);

			const FNiagaraScriptExecutionParameterStore* ScriptParameterStore = Script->GetExecutionReadyParameterStore(ENiagaraSimTarget::CPUSim);
			check(ScriptParameterStore != nullptr);
			const auto& ScriptDataInterfaces = ScriptParameterStore->GetDataInterfaces();
			int32 NumPerInstanceFunctions = 0;
			for (int32 FunctionIndex = 0; FunctionIndex < FunctionCount; ++FunctionIndex)
			{
				const FVMExternalFunctionBindingInfo& BindingInfo = ScriptExecutableData.CalledVMExternalFunctions[FunctionIndex];

				FExternalFuncInfo& FuncInfo = ExtFunctionInfo[FunctionIndex];

				// First check to see if we can pull from the fast path library..		
				if (UNiagaraFunctionLibrary::GetVectorVMFastPathExternalFunction(BindingInfo, FuncInfo.Function) && FuncInfo.Function.IsBound())
				{
					continue;
				}

				//TODO: Remove use of userptr table here and just embed the instance data in the function lambda.
				UserPtrTable.SetNumZeroed(ScriptExecutableData.NumUserPtrs, EAllowShrinking::No);

				//Next check DI functions.
				for (int32 i = 0; i < ScriptExecutableData.DataInterfaceInfo.Num(); i++)
				{
					const FNiagaraScriptDataInterfaceCompileInfo& ScriptDIInfo = ScriptExecutableData.DataInterfaceInfo[i];
					UNiagaraDataInterface* ScriptInterface = ScriptDataInterfaces[i];
					UNiagaraDataInterface* ExternalInterface = GetDataInterfaces()[i];

					if (ScriptDIInfo.Name == BindingInfo.OwnerName)
					{
						//Currently we must assume that any User DI is overridden but maybe we can be less conservative with this in future.
						if (ScriptDIInfo.NeedsPerInstanceBinding())
						{
							FuncInfo.Function = FVMExternalFunction::CreateLambda(
								[SystemInstances = &this->SystemInstances, ScriptType = this->ScriptType, NumPerInstanceFunctions, UserPtrIdx = ScriptDIInfo.UserPtrIdx](FVectorVMExternalFunctionContext& ExtFnContext)
								{
									PerInsFn(ExtFnContext, ExtFnContext, SystemInstances, ScriptType, NumPerInstanceFunctions, UserPtrIdx);
								}
							);
							++NumPerInstanceFunctions;
						}
						else
						{
							// first check to see if we should just use the one from the script
							if (ScriptExecutableData.CalledVMExternalFunctionBindings.IsValidIndex(FunctionIndex)
								&& ScriptInterface
								&& ExternalInterface == ScriptDataInterfaces[i])
							{
								const FVMExternalFunction& ScriptFuncBind = ScriptExecutableData.CalledVMExternalFunctionBindings[FunctionIndex];
								if (ScriptFuncBind.IsBound())
								{
									FuncInfo.Function = ScriptFuncBind;
									check(ScriptDIInfo.UserPtrIdx == INDEX_NONE);
									break;
								}
							}

							//If we don't need a call per instance we can just bind directly to the DI function call;
							check(ExternalInterface);
							ExternalInterface->GetVMExternalFunction(BindingInfo, nullptr, FuncInfo.Function);
						}
						break;
					}
				}				

				if (FuncInfo.Function.IsBound() == false)
				{
					UE_LOG(LogNiagara, Warning, TEXT("Error building data interface function table for system script!"));
					FunctionTable.Empty();
					return false;
				}
			}

			if (FunctionTable.Num() != ExtFunctionInfo.Num())
			{
				UE_LOG(LogNiagara, Warning, TEXT("Error building data interface function table for system script!"));
				FunctionTable.Empty();
				return false;
			}

			for (int32 FunctionIt = 0; FunctionIt < FunctionTable.Num(); ++FunctionIt)
			{
				FunctionTable[FunctionIt] = &ExtFunctionInfo[FunctionIt].Function;
			}

			for (int32 i = 0; i < FunctionTable.Num(); i++)
			{
				if (FunctionTable[i] == nullptr)
				{
					UE_LOG(LogNiagara, Warning, TEXT("Invalid Function Table Entry! %s"), *ScriptExecutableData.CalledVMExternalFunctions[i].Name.ToString());
				}
			}
		}
	}
	if (Instance && Parameters.GetPositionDataDirty())
	{
		Parameters.ResolvePositions(Instance->GetLWCConverter());
	}
	Parameters.Tick();

	return true;
}

bool FNiagaraSystemScriptExecutionContext::Execute(FNiagaraSystemInstance* ParentSystemInstance, float DeltaSeconds, uint32 NumInstances, const FScriptExecutionConstantBufferTable& ConstantBufferTable)
{
	check(ParentSystemInstance == nullptr);
	if(bHasDIsWithPreStageTick && SystemInstances)
	{
		for(FNiagaraSystemInstance* Inst : *SystemInstances)
		{
			if (FNDIStageTickHandler* Handler = Inst->GetSystemDIStageTickHandler(Script->GetUsage()))
			{
				Handler->PreStageTick(Inst, DeltaSeconds);
			}
		}
	}

	bool bSuccess = FNiagaraScriptExecutionContextBase::Execute(ParentSystemInstance, DeltaSeconds, NumInstances, ConstantBufferTable);

	if(bHasDIsWithPostStageTick && SystemInstances)
	{
		for (FNiagaraSystemInstance* Inst : *SystemInstances)
		{
			if(FNDIStageTickHandler* Handler = Inst->GetSystemDIStageTickHandler(Script->GetUsage()))
			{
				Handler->PostStageTick(Inst, DeltaSeconds);
			}
		}
	}

	return bSuccess;
}

bool FNiagaraSystemScriptExecutionContext::GeneratePerInstanceDIFunctionTable(FNiagaraSystemInstance* Inst, TArray<FNiagaraPerInstanceDIFuncInfo>& OutFunctions)
{
	const FNiagaraScriptExecutionParameterStore* ScriptParameterStore = Script->GetExecutionReadyParameterStore(ENiagaraSimTarget::CPUSim);
	const FNiagaraVMExecutableData& ScriptExecutableData = Script->GetVMExecutableData();

	for (int32 FunctionIndex = 0; FunctionIndex < ScriptExecutableData.CalledVMExternalFunctions.Num(); ++FunctionIndex)
	{
		const FVMExternalFunctionBindingInfo& BindingInfo = ScriptExecutableData.CalledVMExternalFunctions[FunctionIndex];

		for (int32 i = 0; i < ScriptExecutableData.DataInterfaceInfo.Num(); i++)
		{
			const FNiagaraScriptDataInterfaceCompileInfo& ScriptDIInfo = ScriptExecutableData.DataInterfaceInfo[i];
			const FNiagaraScriptResolvedDataInterfaceInfo& ResolvedDIInfo = Script->GetResolvedDataInterfaces()[i];
			//UNiagaraDataInterface* ScriptInterface = ScriptDataInterfaces[i];
			UNiagaraDataInterface* ExternalInterface = GetDataInterfaces()[i];

			if (ScriptDIInfo.Name == BindingInfo.OwnerName && (ScriptDIInfo.NeedsPerInstanceBinding() || ResolvedDIInfo.NeedsPerInstanceBinding()))
			{
				UNiagaraDataInterface* DIToBind = nullptr;
				FNiagaraPerInstanceDIFuncInfo& NewFuncInfo = OutFunctions.AddDefaulted_GetRef();
				void* InstData = nullptr;

				if (const int32* DIIndex = Inst->GetInstanceParameters().FindParameterOffset(FNiagaraVariable(ScriptDIInfo.Type, ScriptDIInfo.Name)))
				{
					//If this is a User DI we bind to the user DI and find instance data with it.
					if (UNiagaraDataInterface* UserInterface = Inst->GetInstanceParameters().GetDataInterface(*DIIndex))
					{
						DIToBind = UserInterface;
						InstData = Inst->FindDataInterfaceInstanceData(UserInterface);
					}
				}
				else if (const int32* ResolvedDIIndex = Inst->GetInstanceParameters().FindParameterOffset(ResolvedDIInfo.ResolvedVariable))
				{
					//If this is a User DI we bind to the user DI and find instance data with it.
					if (UNiagaraDataInterface* UserInterface = Inst->GetInstanceParameters().GetDataInterface(*ResolvedDIIndex))
					{
						DIToBind = UserInterface;
						InstData = Inst->FindDataInterfaceInstanceData(UserInterface);
					}
				}
				else
				{
					//Otherwise we use the script DI and search for instance data with that.
					DIToBind = ExternalInterface;
					InstData = Inst->FindDataInterfaceInstanceData(ExternalInterface);
				}

				if (DIToBind)
				{
					check(ExternalInterface->PerInstanceDataSize() == 0 || InstData);
					DIToBind->GetVMExternalFunction(BindingInfo, InstData, NewFuncInfo.Function);
					NewFuncInfo.InstData = InstData;
				}

				if (NewFuncInfo.Function.IsBound() == false)
				{
					return false;
				}
				break;
			}
		}
	}
	return true;
};
