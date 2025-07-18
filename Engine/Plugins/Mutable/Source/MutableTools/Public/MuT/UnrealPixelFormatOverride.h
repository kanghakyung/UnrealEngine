// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Image.h"
#include "Interfaces/ITextureFormatManagerModule.h"
#include "Interfaces/ITextureFormatModule.h"
#include "Engine/TextureDefines.h"
#include "TextureCompressorModule.h"
#include "TextureBuildUtilities.h"
#include "ImageCore.h"

/** This function needs to be called from the game thread before any call to UnrealPixelFormatFunc can be done. */
MUTABLETOOLS_API extern void PrepareUnrealCompression();

/** */
void FillBuildSettingsFromMutableFormat(FTextureBuildSettings& Settings, bool& bOutHasAlpha, mu::EImageFormat Format);

/** */
void MutableToImageCore(const mu::FImage* InMutable, FImage& CoreImage, int32 LOD, bool bSwizzleRGBHack);

/** */
bool ImageCoreToMutable(const FCompressedImage2D& Compressed, mu::FImage* Mutable, int32 LOD);

/** Convert an Unreal platform pixel format to Mutable. */
MUTABLETOOLS_API extern mu::EImageFormat UnrealToMutablePixelFormat(EPixelFormat PlatformFormat, bool bHasAlpha);

/** Function that remaps some formats to the ones that provide more acceptable quality in Mutable. */
MUTABLETOOLS_API extern mu::EImageFormat QualityAndPerformanceFix(mu::EImageFormat Format);

/** Try to convert an image using unreal's compression. If the format is not supported, it will return false in bOutSuccess.
* Can be called from any thread.
* PrepareUnrealCompression needs to be called from the game thread once prior to using this function. 
*/
MUTABLETOOLS_API extern void UnrealPixelFormatFunc(bool& bOutSuccess, int32 Quality, mu::FImage* Target, const mu::FImage* Source, int32 OnlyLOD);

/** Debug export the given image to a temp file in the saved folder. */
MUTABLETOOLS_API void DebugImageDump(const mu::FImage*, const FString& FilePath);
