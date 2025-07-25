// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Containers/UnrealString.h"

struct FText3DFontFamily;

namespace UE::Text3D::Private::Fonts
{
	void GetSystemFontInfo(TMap<FString, FText3DFontFamily>& OutFontsInfo);
	void ListAvailableFontFiles();
}
