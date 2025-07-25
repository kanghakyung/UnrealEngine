// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"


/**
 * Rendering synchronization protocol strings
 */
namespace DisplayClusterRenderSyncStrings
{
	constexpr static const TCHAR* ProtocolName = TEXT("RenderSync");

	constexpr static const TCHAR* TypeRequest  = TEXT("Request");
	constexpr static const TCHAR* TypeResponse = TEXT("Response");

	constexpr static const TCHAR* ArgumentsDefaultCategory = TEXT("RS");

	namespace SynchronizeOnBarrier
	{
		constexpr static const TCHAR* Name = TEXT("SyncOnBarrier");
	};
};
