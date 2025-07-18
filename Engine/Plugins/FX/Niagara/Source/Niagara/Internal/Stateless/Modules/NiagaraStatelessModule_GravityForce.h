// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Stateless/NiagaraStatelessModule.h"
#include "Stateless/NiagaraStatelessEmitterDataBuildContext.h"
#include "Stateless/NiagaraStatelessModuleShaderParameters.h"
#include "Stateless/Modules/NiagaraStatelessModuleCommon.h"

#include "NiagaraStatelessModule_GravityForce.generated.h"

// Applies a gravitational force (in cm/s). This acceleration is the same regardless of mass, so particles with high and low mass will reach the same velocity.
UCLASS(MinimalAPI, EditInlineNew, meta = (DisplayName = "Gravity Force"))
class UNiagaraStatelessModule_GravityForce : public UNiagaraStatelessModule
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisplayName = "Gravity", DisableUniformDistribution, DisableBindingDistribution))
	FNiagaraDistributionRangeVector3 GravityDistribution = FNiagaraDistributionRangeVector3(GetDefaultValue());

	virtual void BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& BuildContext) const override
	{
		if (!IsModuleEnabled())
		{
			return;
		}
		const FNiagaraStatelessRangeVector3 GravityRange = GravityDistribution.CalculateRange(GetDefaultValue());

		NiagaraStateless::FPhysicsBuildData& PhysicsBuildData = BuildContext.GetTransientBuildData<NiagaraStateless::FPhysicsBuildData>();
		PhysicsBuildData.GravityRange.Min = GravityRange.Min;
		PhysicsBuildData.GravityRange.Max = GravityRange.Max;
	}

	static FVector3f GetDefaultValue() { return FVector3f(0.0f, 0.0f, -980.0f); }

#if WITH_EDITOR
	virtual bool CanDisableModule() const override { return true; }
#endif
};
