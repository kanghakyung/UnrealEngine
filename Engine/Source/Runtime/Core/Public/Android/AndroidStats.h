// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

class FAndroidStats
{
public:
	static void Init();
	static void UpdateAndroidStats();
	static void OnThermalStatusChanged(int Status);
	static void OnTrimMemory(int TrimLevel);
	static void LogGPUStats();
};
