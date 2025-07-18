// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Core/Types.h"
#include "Math/Point.h"

namespace UE::CADKernel
{
struct FNurbsCurveData
{
	bool bIsRational;
	int32 Dimension = 0;

	int32 Degree = 0;
	TArray<double> NodalVector;

	TArray<double> Weights;
	TArray<FVector> Poles;
};
} // namespace UE::CADKernel

