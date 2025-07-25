// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"
#include "Stateless/NiagaraStatelessSimulationShader.h"

#include "RenderGraphFwd.h"
#include "ShaderParameterStruct.h"

#include "NiagaraStatelessEmitterTemplate.generated.h"

UCLASS(MinimalAPI, abstract)
class UNiagaraStatelessEmitterTemplate : public UObject
{
	GENERATED_BODY()

public:
	//~Begin: UObject Interface
	NIAGARA_API virtual void PostInitProperties() override;
	//~End: UObject Interface

	static void InitCDOPropertiesAfterModuleStartup();
	virtual void InitModulesAndAttributes() {}

	TConstArrayView<TObjectPtr<UClass>> GetModules() const { return Modules; }
#if WITH_EDITORONLY_DATA
	TConstArrayView<FNiagaraVariableBase> GetOututputComponents() const { return OutputComponents; }
#endif

	virtual const FShaderParametersMetadata* GetShaderParametersMetadata() const { checkNoEntry(); return nullptr; }
	virtual TShaderRef<NiagaraStateless::FSimulationShader> GetSimulationShader() const { checkNoEntry(); return TShaderRef<NiagaraStateless::FSimulationShader>(); }

	//-TODO: Add a way to set them directly, we should know that the final struct is a series of ints in the order of the provided variables
	virtual void SetShaderParameters(uint8* ShaderParametersBase, TConstArrayView<int32> ComponentOffsets) const { checkNoEntry(); }
	//-TODO: Add a way to set them directly, we should know that the final struct is a series of ints in the order of the provided variables

protected:
	TArray<TObjectPtr<UClass>>		Modules;
#if WITH_EDITORONLY_DATA
	TArray<FNiagaraVariableBase>	OutputComponents;
#endif
};

UCLASS(Category = "Statless", DisplayName = "Emitter Example 0")
class UNiagaraStatelessEmitterDefault : public UNiagaraStatelessEmitterTemplate
{
	GENERATED_BODY()

	virtual void InitModulesAndAttributes() override;

	virtual const FShaderParametersMetadata* GetShaderParametersMetadata() const override;
	virtual TShaderRef<NiagaraStateless::FSimulationShader> GetSimulationShader() const override;

	//-TODO: Add a way to set them directly, we should know that the final struct is a series of ints in the order of the provided variables
	virtual void SetShaderParameters(uint8* ShaderParametersBase, TConstArrayView<int32> ComponentOffsets) const override;
	//-TODO: Add a way to set them directly, we should know that the final struct is a series of ints in the order of the provided variables
};
