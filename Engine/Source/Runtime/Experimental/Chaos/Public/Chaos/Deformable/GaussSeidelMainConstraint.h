// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDSpringConstraintsBase.h"
#include "Chaos/XPBDCorotatedConstraints.h"
#include "ChaosStats.h"
#include "Chaos/ImplicitQRSVD.h"
#include "Chaos/GraphColoring.h"
#include "Chaos/NewtonCorotatedCache.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/Deformable/GaussSeidelWeakConstraints.h"
#include "Chaos/Deformable/GaussSeidelCorotatedConstraints.h"


#define PERF_SCOPE(X) SCOPE_CYCLE_COUNTER(X); TRACE_CPUPROFILER_EVENT_SCOPE(X);
DECLARE_CYCLE_STAT(TEXT("Chaos.Deformable.GSMainConstraint.Apply"), STAT_ChaosGSMainConstraint_Apply, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos.Deformable.GSMainConstraint.Acceleration"), STAT_ChaosGSMainConstraint_Acceleration, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos.Deformable.GSMainConstraint.Init"), STAT_ChaosGSMainConstraint_Init, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos.Deformable.GSMainConstraint.InitTransientColor"), STAT_ChaosGSMainConstraint_InitTransientColor, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos.Deformable.GSMainConstraint.InitDynamicColor"), STAT_ChaosGSMainConstraint_InitDynamicColor, STATGROUP_Chaos);

DEFINE_LOG_CATEGORY_STATIC(LogDeformableGaussSeidelMainConstraint, Log, All);

namespace Chaos::Softs
{
	template <typename T, typename ParticleType>
	class FGaussSeidelMainConstraint
	{

	public:
		FGaussSeidelMainConstraint(
			const ParticleType& InParticles, 
			const bool bDoQuasistaticsIn = false,
			const bool bDoSORIn = true,
			const T InOmegaSOR = (T)1.6,
			const int32 ParallelMaxIn = 1000, 
			const T MaxDxRatioIn = T(1),
			const FDeformableXPBDCorotatedParams& InParams = FDeformableXPBDCorotatedParams())
			: bDoQuasistatics(bDoQuasistaticsIn)
			, bDoAcceleration(bDoSORIn)
			, OmegaSOR(InOmegaSOR)
			, ParallelMax(ParallelMaxIn)
			, CorotatedParams(InParams)
		{
			Resize((int32)InParticles.Size());

			InitializeLambdas();
			
			TVec3<T> MaxCoord((T)100.), MinCoord((T)-100.);
			for (int32 i = 0; i < (int32)InParticles.Size(); i++)
			{
				for (int32 j = 0; j < 3; j++) 
				{
					if (InParticles.X(i)[j] < MinCoord[j])
					{
						MinCoord[j] = InParticles.X(i)[j];
					}
					if (InParticles.X(i)[j] > MaxCoord[j])
					{
						MaxCoord[j] = InParticles.X(i)[j];
					}
				}
			}
			MaxDxSize = (MaxCoord - MinCoord).Size() * MaxDxRatioIn;
		}

		virtual ~FGaussSeidelMainConstraint() {}

		void Resize(const int32 NewSize)
		{
			xtilde.SetNum(NewSize);
			StaticIncidentElements.SetNum(NewSize);
			StaticIncidentElementsLocal.SetNum(NewSize);
			DynamicIncidentElements.SetNum(NewSize);
			DynamicIncidentElementsLocal.SetNum(NewSize);
			TransientIncidentElements.SetNum(NewSize);
			TransientIncidentElementsLocal.SetNum(NewSize);
			X_k_1.Init(TVector<T, 3>(T(0.)), NewSize);
			X_k.Init(TVector<T, 3>(T(0.)), NewSize);
		}

		const TArray<TFunction<void(const ParticleType&, const int32, const int32, const T, TVec3<T>&, Chaos::PMatrix<T, 3, 3>&)>>& StaticConstraintResidualAndHessian() const { return AddStaticConstraintResidualAndHessian; }
		TArray<TFunction<void(const ParticleType&, const int32, const int32, const T, TVec3<T>&, Chaos::PMatrix<T, 3, 3>&)>>& StaticConstraintResidualAndHessian() { return AddStaticConstraintResidualAndHessian; }
		const TArray<TFunction<void(const ParticleType&, const int32, const int32, const T, TVec3<T>&, Chaos::PMatrix<T, 3, 3>&)>>& TransientConstraintResidualAndHessian() const { return AddTransientConstraintResidualAndHessian; }
		TArray<TFunction<void(const ParticleType&, const int32, const int32, const T, TVec3<T>&, Chaos::PMatrix<T, 3, 3>&)>>& TransientConstraintResidualAndHessian() { return AddTransientConstraintResidualAndHessian; }
		const TArray<TFunction<void(const ParticleType&, const int32, const int32, const T, TVec3<T>&, Chaos::PMatrix<T, 3, 3>&)>>& DynamicConstraintResidualAndHessian() const { return AddDynamicConstraintResidualAndHessian; }
		TArray<TFunction<void(const ParticleType&, const int32, const int32, const T, TVec3<T>&, Chaos::PMatrix<T, 3, 3>&)>>& DynamicConstraintResidualAndHessian() { return AddDynamicConstraintResidualAndHessian; }
		const TArray<TFunction<void(const int32, const T, Chaos::PMatrix<T, 3, 3>&)>>& PerNodeHessian() const { return AddPerNodeHessian; }
		TArray<TFunction<void(const int32, const T, Chaos::PMatrix<T, 3, 3>&)>>& PerNodeHessian() { return AddPerNodeHessian; }
		const TArray<TFunction<void(const ParticleType&, const TArray<TVec3<T>>&, TArray<TVec3<T>>&)>>& InternalForceDifferentials() const { return AddInternalForceDifferentials; }
		TArray<TFunction<void(const ParticleType&, const TArray<TVec3<T>>&, TArray<TVec3<T>>&)>>& InternalForceDifferentials() { return AddInternalForceDifferentials; }

		int32 AddStaticConstraintResidualAndHessianRange(int32 NumConstraints)
		{
			int32 CurrentSize = AddStaticConstraintResidualAndHessian.Num();
			AddStaticConstraintResidualAndHessian.AddDefaulted(NumConstraints);
			return CurrentSize;
		}

		int32 AddTransientConstraintResidualAndHessianRange(int32 NumConstraints)
		{
			int32 CurrentSize = AddTransientConstraintResidualAndHessian.Num();
			AddTransientConstraintResidualAndHessian.AddDefaulted(NumConstraints);
			return CurrentSize;
		}

		int32 AddDynamicConstraintResidualAndHessianRange(int32 NumConstraints)
		{
			int32 CurrentSize = AddDynamicConstraintResidualAndHessian.Num();
			AddDynamicConstraintResidualAndHessian.AddDefaulted(NumConstraints);
			return CurrentSize;
		}

		int32 AddPerNodeHessianRange(int32 NumConstraints)
		{
			int32 CurrentSize = AddPerNodeHessian.Num();
			AddPerNodeHessian.AddDefaulted(NumConstraints);
			return CurrentSize;
		}

		int32 AddAddInternalForceDifferentialsRange(int32 NumConstraints)
		{
			int32 CurrentSize = AddInternalForceDifferentials.Num();
			AddInternalForceDifferentials.AddDefaulted(NumConstraints);
			return CurrentSize;
		}

		CHAOS_API void AddStaticConstraints(const TArray<TArray<int32>>& ExtraConstraints, TArray<TArray<int32>>& ExtraIncidentElements, TArray<TArray<int32>>& ExtraIncidentElementsLocal);

		CHAOS_API void AddTransientConstraints(const TArray<TArray<int32>>& ExtraConstraints, TArray<TArray<int32>>& ExtraIncidentElements, TArray<TArray<int32>>& ExtraIncidentElementsLocal, bool CheckIncidentElements = false);

		CHAOS_API void AddDynamicConstraints(const TArray<TArray<int32>>& ExtraConstraints, TArray<TArray<int32>>& ExtraIncidentElements, TArray<TArray<int32>>& ExtraIncidentElementsLocal, bool CheckIncidentElements = false);

		inline void ResetDynamicConstraints()
		{
			DynamicConstraints = {};
			for (int32 p = 0; p < DynamicIncidentElements.Num(); p++)
			{
				DynamicIncidentElements[p].SetNum(0);
				DynamicIncidentElementsLocal[p].SetNum(0);
			}
			DynamicIncidentElementsOffsets = {};	
		}


		void Apply(ParticleType& Particles, const T Dt, const int32 MaxWriteIters = 10, const bool Write2File = false, const TPBDActiveView<FSolverParticles>* InParticleActiveView = nullptr)
		{
 			PERF_SCOPE(STAT_ChaosGSMainConstraint_Apply);
			
			if (DebugResidual && PassedIters < MaxWriteIters)
			{
				ComputeNewtonResiduals(Particles, Dt, Write2File);
			}
			
			std::atomic<int32> ParticleFailureCounter(0);

			for (int32 i = 0; i < ParticlesPerColor.Num(); i++)
			{
				int32 NumBatch = ParticlesPerColor[i].Num() / CorotatedParams.XPBDCorotatedBatchSize;
				if (ParticlesPerColor[i].Num() % CorotatedParams.XPBDCorotatedBatchSize != 0)
				{
					NumBatch ++;
				}
			
				PhysicsParallelFor(NumBatch, [&](const int32 BatchIndex)
					{
						for (int32 BatchSubIndex = 0; BatchSubIndex < CorotatedParams.XPBDCorotatedBatchSize; BatchSubIndex++) {
							int32 TaskIndex = CorotatedParams.XPBDCorotatedBatchSize * BatchIndex + BatchSubIndex;
							if (TaskIndex < ParticlesPerColor[i].Num())
							{
								if (Particles.InvM(ParticlesPerColor[i][TaskIndex]) != T(0))
								{
									int32 ParticleIndex = ParticlesPerColor[i][TaskIndex];
									if (!ApplySingleParticle(ParticleIndex, Dt, Particles))
									{
										ParticleFailureCounter.fetch_add(1, std::memory_order_relaxed);
									}
								}
							}
						}
					}, NumBatch < CorotatedParams.XPBDCorotatedBatchThreshold);
			
			}
			
						
			if (bDoAcceleration)
			{
				PERF_SCOPE(STAT_ChaosGSMainConstraint_Acceleration);
				if (InParticleActiveView)
				{
					InParticleActiveView->ParallelFor(AccelerationTechniquePerParticle, CorotatedParams.XPBDCorotatedBatchSize);
				}
				else
				{
					PhysicsParallelFor(Particles.Size(), [this, &Particles](const int32 ParticleIndex)
						{
							this->AccelerationTechniquePerParticle(Particles, ParticleIndex);
						}, Particles.Size() < 1000);
				}
			}
			
			CurrentIt ++;

			if (ParticleFailureCounter.load(std::memory_order_relaxed) > 0)
			{
				UE_LOG(LogDeformableGaussSeidelMainConstraint, Warning, TEXT("%d Particle(s) are skipped because of too large dx size"), ParticleFailureCounter.load(std::memory_order_relaxed));
			}
		}

		void InitStaticColor(const ParticleType& Particles, const TPBDActiveView<FSolverParticles>* InParticleActiveView = nullptr)
		{
			StaticParticlesPerColor = ComputeNodalColoring(StaticConstraints, Particles, 0, Particles.Size(), StaticIncidentElements, StaticIncidentElementsLocal, InParticleActiveView,  &StaticParticleColors);
			ParticleColors = StaticParticleColors;
			ParticlesPerColor = StaticParticlesPerColor;
		}

		void InitTransientColor(const ParticleType& Particles)
		{
			PERF_SCOPE(STAT_ChaosGSMainConstraint_InitTransientColor);
			ParticleColors = StaticParticleColors;
			ParticlesPerColor = StaticParticlesPerColor;
			Chaos::ComputeExtraNodalColoring(StaticConstraints, DynamicConstraints, TransientConstraints, Particles, StaticIncidentElements, DynamicIncidentElements, TransientIncidentElements, ParticleColors, ParticlesPerColor);
		}

		void InitDynamicColor(const ParticleType& Particles)
		{
			PERF_SCOPE(STAT_ChaosGSMainConstraint_InitDynamicColor);
			ParticleColors = StaticParticleColors;
			ParticlesPerColor = StaticParticlesPerColor;
			Chaos::ComputeExtraNodalColoring(StaticConstraints, DynamicConstraints, Particles, StaticIncidentElements, TransientIncidentElements, ParticleColors, ParticlesPerColor);
		}

		void Init(const T Dt, const ParticleType& Particles)
		{
			Resize((int32)Particles.Size());

			PERF_SCOPE(STAT_ChaosGSMainConstraint_Init);
			TransientConstraints.SetNum(0);
			for (int32 p = 0; p < TransientIncidentElements.Num(); p++)
			{
				TransientIncidentElements[p].SetNum(0);
				TransientIncidentElementsLocal[p].SetNum(0);
			}
			TransientIncidentElementsOffsets.SetNum(0);
			if (!bDoQuasistatics)
			{
				for (int32 i = 0; i < xtilde.Num(); i++)
				{
					//xtilde[i] = Particles.X(i) + Dt * Particles.V(i);
					xtilde[i] = Particles.P(i);
				}
			}
			CurrentIt = 0;
		}

		static bool IsClean(const TArray<TArray<int32>>& ConstraintsIn, const TArray<TArray<int32>>& IncidentElementsIn, const TArray<TArray<int32>>& IncidentElementsLocalIn)
		{
			if (IncidentElementsIn.Num() == IncidentElementsLocalIn.Num())
			{
				int32 TotalEntries = 0;
				for (int32 i = 0; i < IncidentElementsIn.Num(); i++)
				{
					TotalEntries += IncidentElementsIn[i].Num();
					if (IncidentElementsIn[i].Num() != IncidentElementsLocalIn[i].Num())
					{
						return false;
					}
					for (int32 j = 0; j < IncidentElementsIn[i].Num(); j++)
					{
						if (IncidentElementsIn[i][j] > ConstraintsIn.Num() || IncidentElementsLocalIn[i][j] > ConstraintsIn[IncidentElementsIn[i][j]].Num())
						{
							return false;
						}
					}
				}
				if (TotalEntries > 0)
				{
					return true;
				}
			}
			return false;
		}

		CHAOS_API TArray<TVec3<T>> ComputeNewtonResiduals(const ParticleType& Particles, const T Dt, const bool Write2File = false, TArray<PMatrix<T, 3, 3>>* AllParticleHessian = nullptr);
		
		CHAOS_API void ApplyCG(ParticleType& Particles, const T Dt);

		/* Adds external acceleration, e.g. gravity (0,0,-980) cm/s^2 */
		void AddExternalAcceleration(const TVec3<T>& Acceleration) { ExternalAcceleration += Acceleration; };

		void ResetExternalAcceleration() { ExternalAcceleration = TVec3<T>((T)0.); };

	private:

		bool ApplySingleParticle(const int32 p, const T Dt, ParticleType& Particles)
		{
			int32 ConstraintIndex = 0;
			Chaos::TVector<T, 3> ParticleResidual((T)0.);
			Chaos::PMatrix<T, 3, 3> ParticleHessian((T)0., (T)0., (T)0.);

			ComputeInitialResidualAndHessian(Particles, p, Dt, ParticleResidual, ParticleHessian);

			for (int32 i = 0; i < StaticIncidentElements[p].Num(); i++)
			{
				while (StaticIncidentElements[p][i] >= StaticIncidentElementsOffsets[ConstraintIndex + 1] && ConstraintIndex < StaticIncidentElementsOffsets.Num() - 1)
				{
					ConstraintIndex ++;
				}

				AddStaticConstraintResidualAndHessian[ConstraintIndex](Particles, StaticIncidentElements[p][i] - StaticIncidentElementsOffsets[ConstraintIndex], StaticIncidentElementsLocal[p][i], Dt, ParticleResidual, ParticleHessian);
			}

			ConstraintIndex = 0;

			for (int32 i = 0; i < DynamicIncidentElements[p].Num(); i++)
			{
				while (DynamicIncidentElements[p][i] >= DynamicIncidentElementsOffsets[ConstraintIndex + 1] && ConstraintIndex < DynamicIncidentElementsOffsets.Num() - 1)
				{
					ConstraintIndex ++;
				}

				AddDynamicConstraintResidualAndHessian[ConstraintIndex](Particles, DynamicIncidentElements[p][i] - DynamicIncidentElementsOffsets[ConstraintIndex], DynamicIncidentElementsLocal[p][i], Dt, ParticleResidual, ParticleHessian);
			}

			ConstraintIndex = 0;

			for (int32 i = 0; i < TransientIncidentElements[p].Num(); i++)
			{
				while (TransientIncidentElements[p][i] >= TransientIncidentElementsOffsets[ConstraintIndex + 1] && ConstraintIndex < TransientIncidentElementsOffsets.Num() - 1)
				{
					ConstraintIndex ++;
				}

				AddTransientConstraintResidualAndHessian[ConstraintIndex](Particles, TransientIncidentElements[p][i] - TransientIncidentElementsOffsets[ConstraintIndex], TransientIncidentElementsLocal[p][i], Dt, ParticleResidual, ParticleHessian);
			}

			for (int32 i = 0; i < AddPerNodeHessian.Num(); i++)
			{
				AddPerNodeHessian[i](p, Dt, ParticleHessian);
			}

			T HessianScale = (T)1.;
			T HessianDet = ParticleHessian.Determinant();
			auto IsIllConditioned = [](T HessianDet)
			{
				return FMath::Abs(HessianDet) < TMathUtilConstants<T>::Epsilon ||
					FMath::Abs(HessianDet) > TMathUtilConstants<T>::MaxReal;
			};
			if (IsIllConditioned(HessianDet))
			{
				// scale the hessian so that the determinant (~HessianScale^3) falls into the normal range.
				HessianScale = (T)0.;
				for (int32 RowIdx = 0; RowIdx < 3; ++RowIdx)
				{
					for (int32 ColIdx = 0; ColIdx < 3; ++ColIdx)
					{
						HessianScale = FMath::Max(FMath::Abs(ParticleHessian.GetAt(RowIdx, ColIdx)), HessianScale);
					}
				}
				if (IsIllConditioned(HessianScale))
				{
					return false;
				}
				else
				{
					ParticleHessian *= 1 / HessianScale;
					HessianDet = ParticleHessian.Determinant();
					if (IsIllConditioned(HessianDet))
					{
						return false;
					}
				}
			}

			Chaos::PMatrix<T, 3, 3> HessianInv = ParticleHessian.SymmetricCofactorMatrix();
			HessianInv *= T(1) / HessianDet;
			Chaos::TVector<T, 3> Dx = HessianInv.GetTransposed() * (-ParticleResidual) / HessianScale; // add back HessianScale

			if (Dx.Size() < MaxDxSize)
			{
				Particles.P(p) += Dx;
			}
			else
			{
				return false;
			}
			return true;
		}
		
		void InitializeLambdas()
		{
			ComputeInitialResidualAndHessian = [this](const ParticleType& Particles, const int32 p, const T Dt, TVec3<T>& ParticleResidual, Chaos::PMatrix<T, 3, 3>& ParticleHessian)
			{
				if (!this->bDoQuasistatics) {
					for (int32 alpha = 0; alpha < 3; alpha++) {
						ParticleResidual[alpha] = Particles.M(p) * (Particles.P(p)[alpha] - this->xtilde[p][alpha]);
					}
					for (int32 alpha = 0; alpha < 3; alpha++) {
						ParticleHessian.SetAt(alpha, alpha, Particles.M(p));
					}
				}
				else
				{
					for (int32 alpha = 0; alpha < 3; alpha++) {
						ParticleResidual[alpha] = -Dt * Dt * ExternalAcceleration[alpha] * Particles.M(p);
					}
				}
			};

			AccelerationTechniquePerParticle = [this](ParticleType& Particles, int32 ParticleIndex)
			{
				if (Particles.InvM(ParticleIndex) != T(0) && CurrentIt > SORStart)
				{
					Particles.P(ParticleIndex) = OmegaSOR * (Particles.P(ParticleIndex) - this->X_k_1[ParticleIndex]) + this->X_k_1[ParticleIndex];
				}
				if (Particles.InvM(ParticleIndex) != T(0))
				{
					this->X_k_1[ParticleIndex] = this->X_k[ParticleIndex];
					this->X_k[ParticleIndex] = Particles.P(ParticleIndex);
				}
			};
		}


		//Constraints storage:
		TArray<TArray<int32>> StaticConstraints = {};
		TArray<TArray<int32>> StaticIncidentElements;
		TArray<TArray<int32>> StaticIncidentElementsLocal;
		TArray<TArray<int32>> TransientConstraints = {};
		TArray<TArray<int32>> TransientIncidentElements;
		TArray<TArray<int32>> TransientIncidentElementsLocal;
		TArray<TArray<int32>> DynamicConstraints = {};
		TArray<TArray<int32>> DynamicIncidentElements;
		TArray<TArray<int32>> DynamicIncidentElementsLocal;

		//Lambds for spcifying residual/hessian computations:
		TFunction<void(const ParticleType&, const int32, const T, TVec3<T>&, Chaos::PMatrix<T, 3, 3>&)> ComputeInitialResidualAndHessian;
		TArray<TFunction<void(const ParticleType&, const int32, const int32, const T, TVec3<T>&, Chaos::PMatrix<T, 3, 3>&)>> AddStaticConstraintResidualAndHessian;
		TArray<TFunction<void(const ParticleType&, const int32, const int32, const T, TVec3<T>&, Chaos::PMatrix<T, 3, 3>&)>> AddDynamicConstraintResidualAndHessian;
		TArray<TFunction<void(const ParticleType&, const int32, const int32, const T, TVec3<T>&, Chaos::PMatrix<T, 3, 3>&)>> AddTransientConstraintResidualAndHessian;
		TArray<TFunction<void(const int32, const T, Chaos::PMatrix<T, 3, 3>&)>> AddPerNodeHessian;

		//Coloring information:
		TArray<int32> StaticParticleColors;
		TArray<TArray<int32>> StaticParticlesPerColor;

		TArray<int32> ParticleColors;
		TArray<TArray<int32>> ParticlesPerColor;

		TArray<int32> StaticIncidentElementsOffsets;
		TArray<int32> TransientIncidentElementsOffsets;
		TArray<int32> DynamicIncidentElementsOffsets;

		bool bDoQuasistatics = false;
		mutable TArray<Chaos::TVector<T, 3>> xtilde;

		//SOR variables:
		TFunction<void(ParticleType&, int32)> AccelerationTechniquePerParticle;
		mutable TArray<Chaos::TVector<T, 3>> X_k_1;
		mutable TArray<Chaos::TVector<T, 3>> X_k;
		int32 CurrentIt = 0;
		bool bDoAcceleration = true;
		T OmegaSOR = T(1.6);
		int32 SORStart = 1; //1 is smallest

		int32 ParallelMax = 1000;

		FDeformableXPBDCorotatedParams CorotatedParams;

		//Newton solver variables:
		TArray<TFunction<void(const ParticleType&, const TArray<TVec3<T>>&, TArray<TVec3<T>>&)>> AddInternalForceDifferentials;
		TUniquePtr<TArray<int32>> use_list;

		int32 NumTotalParticles;
		TArray<TVec3<T>> ReorderedPs;

		T MaxDxSize = T(0);

		public:
		bool DebugResidual = false;
		bool IsFirstFrame = true;
		int32 PassedIters = 0;

		TVec3<T> ExternalAcceleration = TVec3<T>((T)0.);

	};

	//template class FGaussSeidelMainConstraint<FSolverReal, FSolverParticles>;
}

