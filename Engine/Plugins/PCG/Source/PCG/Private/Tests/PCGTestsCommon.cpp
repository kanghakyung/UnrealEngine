// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/PCGTestsCommon.h"

#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGCrc.h"
#include "PCGElement.h"
#include "PCGGraph.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "Data/PCGPointData.h"
#include "Data/PCGPolyLineData.h" // IWYU pragma: keep
#include "Data/PCGPrimitiveData.h" // IWYU pragma: keep
#include "Data/PCGSurfaceData.h" // IWYU pragma: keep
#include "Data/PCGVolumeData.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Components/SceneComponent.h"
#endif

namespace PCGTestsCommon
{
	FTestData::FTestData(int32 RandomSeed, UPCGSettings* DefaultSettings, TSubclassOf<AActor> ActorClass)
		: Settings(DefaultSettings)
		, Seed(RandomSeed)
		, RandomStream(Seed)
	{
#if WITH_EDITOR
		check(GEditor);
		UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
		check(EditorWorld);

		// No getting the level dirty
		FActorSpawnParameters TransientActorParameters;
		TransientActorParameters.bHideFromSceneOutliner = true;
		TransientActorParameters.bTemporaryEditorActor = true;
		TransientActorParameters.ObjectFlags = RF_Transient;
		TestActor = EditorWorld->SpawnActor<AActor>(ActorClass, TransientActorParameters);
		check(TestActor);

		if (UPCGComponent* PCGComponent = TestActor->GetComponentByClass<UPCGComponent>())
		{
			TestPCGComponent = PCGComponent;
		}
		else
		{
			TestPCGComponent = NewObject<UPCGComponent>(TestActor, FName(TEXT("Test PCG Component")), RF_Transient);
			check(TestPCGComponent);
			TestActor->AddInstanceComponent(TestPCGComponent);
			TestPCGComponent->RegisterComponent();
		}

		// By default PCG components for tests will be non-partitioned
		TestPCGComponent->SetIsPartitioned(false);

		UPCGGraph* TestGraph = NewObject<UPCGGraph>(TestPCGComponent, FName(TEXT("Test PCG Graph")), RF_Transient);
		check(TestGraph);
		TestPCGComponent->SetGraphLocal(TestGraph);

		// Add Root Component to actor if none exists
		if (!TestActor->GetRootComponent())
		{
			USceneComponent* NewRootComponent = NewObject<USceneComponent>(TestActor, FName(TEXT("DefaultSceneRoot")), RF_Transient);
			TestActor->SetRootComponent(NewRootComponent);
			TestActor->AddInstanceComponent(NewRootComponent);
			NewRootComponent->RegisterComponent();
		}

		// Initialize CRC to avoid asserts.
		InputData.ComputeCrcs(/*bFullDataCrc=*/false);
#else
		TestActor = nullptr;
		TestPCGComponent = nullptr;
		Settings = nullptr;
#endif
	}

	FTestData::~FTestData()
	{
#if WITH_EDITOR
		if (GEditor)
		{
			if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
			{
				if (TestActor)
				{
					EditorWorld->DestroyActor(TestActor);
				}
			}
		}
#endif // WITH_EDITOR
	}

	void FTestData::Reset(UPCGSettings* InSettings)
	{
		// Clear all the data
		RandomStream.Reset();
		InputData.TaggedData.Empty();
		OutputData.TaggedData.Empty();
		Settings = InSettings;

		if (Settings)
		{
			InputData.TaggedData.Emplace_GetRef().Data = Settings;
			InputData.TaggedData.Last().Pin = FName(TEXT("Settings"));
		}
	}

	TUniquePtr<FPCGContext> InitializeTestContext(IPCGElement* InElement, const FPCGDataCollection& InputData, UPCGComponent* InSourceComponent, const UPCGNode* InNode)
	{
		check(InElement);
		TUniquePtr<FPCGContext> Context{ InElement->Initialize(FPCGInitializeElementParams(&InputData, InSourceComponent, InNode)) };
		Context->InitializeSettings();
		Context->AsyncState.NumAvailableTasks = 1;
		return Context;
	}

	TUniquePtr<FPCGContext> FTestData::InitializeTestContext(const UPCGNode* InNode) const
	{
		check(Settings)
		return PCGTestsCommon::InitializeTestContext(Settings->GetElement().Get(), InputData, TestPCGComponent, InNode);
	}

	void FTestData::SetCurrentGenerationTask(FPCGTaskId InTaskId)
	{
		if (TestPCGComponent)
		{
			TestPCGComponent->CurrentGenerationTask = InTaskId;
		}
	}

	AActor* CreateTemporaryActor()
	{
		return NewObject<AActor>();
	}

	UPCGPolyLineData* CreatePolyLineData()
	{
		// TODO: spline, landscape spline
		return nullptr;
	}

	UPCGSurfaceData* CreateSurfaceData()
	{
		// TODO: either landscape, texture, render target
		return nullptr;
	}

	UPCGVolumeData* CreateVolumeData(const FBox& InBounds)
	{
		UPCGVolumeData* VolumeData = NewObject<UPCGVolumeData>();
		VolumeData->Initialize(InBounds);
		return VolumeData;
	}

	UPCGPrimitiveData* CreatePrimitiveData()
	{
		// TODO: need UPrimitiveComponent on an actor
		return nullptr;
	}

	UPCGParamData* CreateEmptyParamData()
	{
		return NewObject<UPCGParamData>();
	}

	UPCGBasePointData* CreateEmptyBasePointData()
	{
		if (CVarPCGEnablePointArrayData.GetValueOnAnyThread())
		{
			return CreateEmptyPointData<UPCGPointArrayData>();
		}
		else
		{
			return CreateEmptyPointData<UPCGPointData>();
		}
	}

	UPCGBasePointData* CreateBasePointData()
	{
		if (CVarPCGEnablePointArrayData.GetValueOnAnyThread())
		{
			return CreatePointData<UPCGPointArrayData>();
		}
		else
		{
			return CreatePointData<UPCGPointData>();
		}
	}

	UPCGBasePointData* CreateBasePointData(const FVector& InLocation)
	{
		if (CVarPCGEnablePointArrayData.GetValueOnAnyThread())
		{
			return CreatePointData<UPCGPointArrayData>(InLocation);
		}
		else
		{
			return CreatePointData<UPCGPointData>(InLocation);
		}
	}

	UPCGBasePointData* CreateRandomBasePointData(int32 PointCount, int32 Seed, bool bRandomDensity)
	{
		if (CVarPCGEnablePointArrayData.GetValueOnAnyThread())
		{
			return CreateRandomPointData<UPCGPointArrayData>(PointCount, Seed, bRandomDensity);
		}
		else
		{
			return CreateRandomPointData<UPCGPointData>(PointCount, Seed, bRandomDensity);
		}
	}

	TArray<FPCGDataCollection> GenerateAllowedData(const FPCGPinProperties& PinProperties)
	{
		TArray<FPCGDataCollection> Data;

		static const TMap<EPCGDataType, TFunction<UPCGData* (void)>> TypeToDataFn
		{
			{ EPCGDataType::Point, []() { return PCGTestsCommon::CreatePointData(); }},
			{ EPCGDataType::PolyLine, []() { return PCGTestsCommon::CreatePolyLineData(); }},
			{ EPCGDataType::Surface, []() { return PCGTestsCommon::CreateSurfaceData(); }},
			{ EPCGDataType::Volume, []() { return PCGTestsCommon::CreateVolumeData(); }},
			{ EPCGDataType::Primitive, []() { return PCGTestsCommon::CreatePrimitiveData(); }},
			{ EPCGDataType::Param, []() { return PCGTestsCommon::CreateEmptyParamData();}}
		};

		// Create empty data
		Data.Emplace();

		// Create single data & data pairs
		for (const auto& TypeToData : TypeToDataFn)
		{
			if (!(TypeToData.Key & PinProperties.AllowedTypes))
			{
				continue;
			}

			const UPCGData* SingleData = TypeToData.Value();
			if (!SingleData)
			{
				continue;
			}

			FPCGDataCollection& SingleCollection = Data.Emplace_GetRef();
			FPCGTaggedData& SingleTaggedData = SingleCollection.TaggedData.Emplace_GetRef();
			SingleTaggedData.Data = SingleData;
			SingleTaggedData.Pin = PinProperties.Label;

			if (!PinProperties.AllowsMultipleConnections())
			{
				continue;
			}

			for (const auto& SecondaryTypeToData : TypeToDataFn)
			{
				if (!(SecondaryTypeToData.Key & PinProperties.AllowedTypes))
				{
					continue;
				}

				const UPCGData* SecondaryData = SecondaryTypeToData.Value();
				if (!SecondaryData)
				{
					continue;
				}

				FPCGDataCollection& MultiCollection = Data.Emplace_GetRef();
				FPCGTaggedData& FirstTaggedData = MultiCollection.TaggedData.Emplace_GetRef();
				FirstTaggedData.Data = SingleData;
				FirstTaggedData.Pin = PinProperties.Label;

				FPCGTaggedData& SecondTaggedData = MultiCollection.TaggedData.Emplace_GetRef();
				SecondTaggedData.Data = SecondaryData;
				SecondTaggedData.Pin = PinProperties.Label;
			}
		}

		return Data;
	}

	bool PointsAreIdentical(const FPCGPoint& FirstPoint, const FPCGPoint& SecondPoint)
	{
		// Trivial checks first for pruning
		if (FirstPoint.Density != SecondPoint.Density || FirstPoint.Steepness != SecondPoint.Steepness ||
			FirstPoint.BoundsMin != SecondPoint.BoundsMin || FirstPoint.BoundsMax != SecondPoint.BoundsMax ||
			FirstPoint.Color != SecondPoint.Color)
		{
			return false;
		}

		// Transform checks with epsilon
		return FirstPoint.Transform.Equals(SecondPoint.Transform);
	}
}

bool FPCGTestBaseClass::SmokeTestAnyValidInput(UPCGSettings* InSettings, TFunction<bool(const FPCGDataCollection&, const FPCGDataCollection&)> ValidationFn)
{
	TestTrue("Valid settings", InSettings != nullptr);

	if (!InSettings)
	{
		return false;
	}

	FPCGElementPtr Element = InSettings->GetElement();

	TestTrue("Valid element", Element != nullptr);

	if (!Element)
	{
		return false;
	}

	TArray<FPCGPinProperties> InputProperties = InSettings->AllInputPinProperties();
	// For each pin: take nothing, take 1 of any supported type, take 2 of any supported types (if enabled)
	TArray<TArray<FPCGDataCollection>> InputsPerProperties;
	TArray<uint32> InputIndices;

	if (!InputProperties.IsEmpty())
	{
		for (const FPCGPinProperties& InputProperty : InputProperties)
		{
			InputsPerProperties.Add(PCGTestsCommon::GenerateAllowedData(InputProperty));
			InputIndices.Add(0);
		}
	}
	else
	{
		TArray<FPCGDataCollection>& EmptyCollection = InputsPerProperties.Emplace_GetRef();
		EmptyCollection.Emplace();
		InputIndices.Add(0);
	}

	check(InputIndices.Num() == InputsPerProperties.Num());

	bool bDone = false;
	while (!bDone)
	{
		// Prepare input
		FPCGDataCollection InputData;
		InputData.TaggedData.Emplace_GetRef().Data = InSettings;

		for (int32 PinIndex = 0; PinIndex < InputIndices.Num(); ++PinIndex)
		{
			InputData.TaggedData.Append(InputsPerProperties[PinIndex][InputIndices[PinIndex]].TaggedData);
		}

		TUniquePtr<FPCGContext> Context = PCGTestsCommon::InitializeTestContext(Element.Get(), InputData, nullptr, nullptr);
		
		// Execute element until done
		while (!Element->Execute(Context.Get()))
		{
		}

		if (ValidationFn)
		{
			TestTrue("Validation", ValidationFn(Context->InputData, Context->OutputData));
		}

		// Bump indices
		int BumpIndex = 0;
		while (BumpIndex < InputIndices.Num())
		{
			if (InputIndices[BumpIndex] == InputsPerProperties[BumpIndex].Num() - 1)
			{
				InputIndices[BumpIndex] = 0;
				++BumpIndex;
			}
			else
			{
				++InputIndices[BumpIndex];
				break;
			}
		}

		if (BumpIndex == InputIndices.Num())
		{
			bDone = true;
		}
	}

	return true;
}
