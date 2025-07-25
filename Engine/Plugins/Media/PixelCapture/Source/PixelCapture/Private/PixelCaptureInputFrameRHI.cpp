// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelCaptureInputFrameRHI.h"
#include "PixelCaptureBufferFormat.h"

FPixelCaptureInputFrameRHI::FPixelCaptureInputFrameRHI(FTextureRHIRef InFrameTexture, TSharedPtr<FPixelCaptureUserData> UserData)
	: IPixelCaptureInputFrame(UserData)
	, FrameTexture(InFrameTexture)
{
	Metadata.SourceTime = rtc::TimeMillis();
}

int32 FPixelCaptureInputFrameRHI::GetType() const
{
	return static_cast<int32>(PixelCaptureBufferFormat::FORMAT_RHI);
}

int32 FPixelCaptureInputFrameRHI::GetWidth() const
{
	return FrameTexture->GetDesc().Extent.X;
}

int32 FPixelCaptureInputFrameRHI::GetHeight() const
{
	return FrameTexture->GetDesc().Extent.Y;
}