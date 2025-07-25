// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanRenderTarget.cpp: Vulkan render target implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "ScreenRendering.h"
#include "VulkanPendingState.h"
#include "VulkanContext.h"
#include "VulkanSwapChain.h"
#include "SceneUtils.h"
#include "RHISurfaceDataConversion.h"
#include "VulkanRenderpass.h"

// Debug mode used as workaround when a DEVICE LOST occurs on alt+tab on some platforms
// This is a workaround and may end up causing some hitches on the rendering thread
static int32 GVulkanFlushOnMapStaging = 0;
static FAutoConsoleVariableRef CVarGVulkanFlushOnMapStaging(
	TEXT("r.Vulkan.FlushOnMapStaging"),
	GVulkanFlushOnMapStaging,
	TEXT("Flush GPU on MapStagingSurface calls without any fence.\n")
	TEXT(" 0: Do not Flush (default)\n")
	TEXT(" 1: Flush"),
	ECVF_Default
);

static int32 GIgnoreCPUReads = 0;
static FAutoConsoleVariableRef CVarVulkanIgnoreCPUReads(
	TEXT("r.Vulkan.IgnoreCPUReads"),
	GIgnoreCPUReads,
	TEXT("Debugging utility for GPU->CPU reads.\n")
	TEXT(" 0 will read from the GPU (default).\n")
	TEXT(" 1 will NOT read from the GPU and fill with zeros.\n"),
	ECVF_Default
	);

static FCriticalSection GStagingMapLock;
static TMap<FVulkanTexture*, VulkanRHI::FStagingBuffer*> GPendingLockedStagingBuffers;

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
TAutoConsoleVariable<int32> CVarVulkanDebugBarrier(
	TEXT("r.Vulkan.DebugBarrier"),
	0,
	TEXT("Forces a full barrier for debugging. This is a mask/bitfield (so add up the values)!\n")
	TEXT(" 0: Don't (default)\n")
	TEXT(" 1: Enable heavy barriers after EndRenderPass()\n")
	TEXT(" 2: Enable heavy barriers after every dispatch\n")
	TEXT(" 4: Enable heavy barriers after upload cmd buffers\n")
	TEXT(" 8: Enable heavy barriers after active cmd buffers\n")
	TEXT(" 16: Enable heavy buffer barrier after uploads\n")
	TEXT(" 32: Enable heavy buffer barrier between acquiring back buffer and blitting into swapchain\n"),
	ECVF_Default
);
#endif

FVulkanRenderPass* FVulkanCommandListContext::PrepareRenderPassForPSOCreation(const FGraphicsPipelineStateInitializer& Initializer)
{
	FVulkanRenderTargetLayout RTLayout(Initializer);
	return PrepareRenderPassForPSOCreation(RTLayout);
}

FVulkanRenderPass* FVulkanCommandListContext::PrepareRenderPassForPSOCreation(const FVulkanRenderTargetLayout& RTLayout)
{
	FVulkanRenderPass* RenderPass = nullptr;
	RenderPass = Device.GetRenderPassManager().GetOrCreateRenderPass(RTLayout);
	return RenderPass;
}

static void ConvertRawDataToFColor(VkFormat VulkanFormat, uint32 DestWidth, uint32 DestHeight, uint8* In, uint32 SrcPitch, FColor* Dest, const FReadSurfaceDataFlags& InFlags)
{
	const bool bLinearToGamma = InFlags.GetLinearToGamma();
	switch (VulkanFormat)
	{
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		ConvertRawR32G32B32A32DataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest, bLinearToGamma);
		break;

	case VK_FORMAT_R16G16B16A16_SFLOAT:
		ConvertRawR16G16B16A16FDataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest, bLinearToGamma);
		break;

	case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
		ConvertRawR11G11B10DataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest, bLinearToGamma);
		break;

	case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		ConvertRawR10G10B10A2DataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest);
		break;

	case VK_FORMAT_R8G8B8A8_UNORM:
		ConvertRawR8G8B8A8DataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest);
		break;

	case VK_FORMAT_R16G16B16A16_UNORM:
		ConvertRawR16G16B16A16DataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest, bLinearToGamma);
		break;

	case VK_FORMAT_B8G8R8A8_UNORM:
		ConvertRawB8G8R8A8DataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest);
		break;

	case VK_FORMAT_R8_UNORM:
		ConvertRawR8DataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest);
		break;

	case VK_FORMAT_R8G8_UNORM:
		ConvertRawR8G8DataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest);
		break;

	case VK_FORMAT_R16_UNORM:
		ConvertRawR16DataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest);
		break;

	case VK_FORMAT_R16G16_UNORM:
		ConvertRawR16G16DataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest);
		break;

	default:
		checkf(false, TEXT("Unsupported format [%d] for conversion to FColor!"), (uint32)VulkanFormat);
		break;
	}

}

void FVulkanDynamicRHI::RHIReadSurfaceData(FRHITexture* TextureRHI, FIntRect Rect, TArray<FColor>& OutData, FReadSurfaceDataFlags InFlags)
{
	checkf((!TextureRHI->GetDesc().IsTextureCube()) || (InFlags.GetCubeFace() == CubeFace_MAX), TEXT("Cube faces not supported yet."));

	const uint32 DestWidth = Rect.Max.X - Rect.Min.X;
	const uint32 DestHeight = Rect.Max.Y - Rect.Min.Y;
	const uint32 NumRequestedPixels = DestWidth * DestHeight;
	OutData.SetNumUninitialized(NumRequestedPixels);
	if (GIgnoreCPUReads)
	{
		// Debug: Fill with CPU
		FMemory::Memzero(OutData.GetData(), NumRequestedPixels * sizeof(FColor));
		return;
	}

	const FRHITextureDesc& Desc = TextureRHI->GetDesc();
	switch (Desc.Dimension)
	{
	case ETextureDimension::Texture2D:
	case ETextureDimension::Texture2DArray:
		// In VR, the high level code calls this function on the viewport render target, without knowing that it's
		// actually a texture array created and managed by the VR runtime. In that case we'll just read the first
		// slice of the array, which corresponds to one of the eyes.
		break;

	default:
		// Just return black for texture types we don't support.
		FMemory::Memzero(OutData.GetData(), NumRequestedPixels * sizeof(FColor));
		return;
	}

	FVulkanTexture& Surface = *ResourceCast(TextureRHI);


	// Figure out the size of the buffer required to hold the requested pixels
	const uint32 PixelByteSize = VulkanRHI::GetNumBitsPerPixel(Surface.StorageFormat) / 8;
	checkf(GPixelFormats[TextureRHI->GetFormat()].Supported && (PixelByteSize > 0), TEXT("Trying to read from unsupported format."));
	const uint32 BufferSize = NumRequestedPixels * PixelByteSize;

	// Validate that the Rect is within the texture
	const uint32 MipLevel = InFlags.GetMip();
	const uint32 MipSizeX = FMath::Max(Desc.Extent.X >> MipLevel, 1);
	const uint32 MipSizeY = FMath::Max(Desc.Extent.Y >> MipLevel, 1);
	checkf((Rect.Max.X <= (int32)MipSizeX) && (Rect.Max.Y <= (int32)MipSizeY), TEXT("The specified Rect [%dx%d] extends beyond this Mip [%dx%d]."), Rect.Max.X, Rect.Max.Y, MipSizeX, MipSizeY);

	FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();
	VulkanRHI::FStagingBuffer* StagingBuffer = nullptr;
	const bool bCPUReadback = EnumHasAllFlags(Surface.GetDesc().Flags, TexCreate_CPUReadback);

	if (!bCPUReadback) //this function supports reading back arbitrary rendertargets, so if its not a cpu readback surface, we do a copy.
	{
		StagingBuffer = Device->GetStagingManager().AcquireBuffer(BufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
		ensure(StagingBuffer->GetSize() >= BufferSize);

		VkBufferImageCopy CopyRegion;
		FMemory::Memzero(CopyRegion);
		// Leave bufferRowLength/bufferImageHeight at 0 for tightly packed
		CopyRegion.imageSubresource.aspectMask = Surface.GetFullAspectMask();
		CopyRegion.imageSubresource.mipLevel = MipLevel;
		CopyRegion.imageSubresource.baseArrayLayer = InFlags.GetArrayIndex();
		CopyRegion.imageSubresource.layerCount = 1;
		CopyRegion.imageOffset.x = Rect.Min.X;
		CopyRegion.imageOffset.y = Rect.Min.Y;
		CopyRegion.imageExtent.width = DestWidth;
		CopyRegion.imageExtent.height = DestHeight;
		CopyRegion.imageExtent.depth = 1;

		RHICmdList.EnqueueLambda([CopyRegion, StagingBuffer, &Surface](FRHICommandListBase& ExecutingCmdList)
		{
			FVulkanCommandListContext& Context = FVulkanCommandListContext::Get(ExecutingCmdList);
			FVulkanCommandBuffer& CommandBuffer = Context.GetCommandBuffer();
			VulkanRHI::vkCmdCopyImageToBuffer(CommandBuffer.GetHandle(), Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, StagingBuffer->GetHandle(), 1, &CopyRegion);

			FVulkanPipelineBarrier AfterBarrier;
			AfterBarrier.AddMemoryBarrier(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
			AfterBarrier.Execute(&CommandBuffer);
		});
	}

	// We need to execute the command list so we can read the data from the map below
	RHICmdList.SubmitAndBlockUntilGPUIdle();

	uint8* In;
	uint32 SrcPitch;
	if (bCPUReadback)
	{
		// If the text was bCPUReadback, then we have to deal with our Rect potentially being a subset of the total texture
		In = (uint8*)Surface.GetMappedPointer() + ((Rect.Min.Y * MipSizeX + Rect.Min.X) * PixelByteSize);
		SrcPitch = MipSizeX * PixelByteSize;
	}
	else
	{
		// If the text was NOT bCPUReadback, the buffer contains only the (tightly packed) Rect we requested
		StagingBuffer->InvalidateMappedMemory();
		In = (uint8*)StagingBuffer->GetMappedPointer();
		SrcPitch = DestWidth * PixelByteSize;
	}

	FColor* Dest = OutData.GetData();
	ConvertRawDataToFColor(Surface.StorageFormat, DestWidth, DestHeight, In, SrcPitch, Dest, InFlags);

	if (!bCPUReadback)
	{
		Device->GetStagingManager().ReleaseBuffer(nullptr, StagingBuffer);
	}
}

void FVulkanDynamicRHI::RHIReadSurfaceData(FRHITexture* TextureRHI, FIntRect Rect, TArray<FLinearColor>& OutData, FReadSurfaceDataFlags InFlags)
{
	TArray<FColor> FromColorData;
	RHIReadSurfaceData(TextureRHI, Rect, FromColorData, InFlags);
	OutData.SetNumUninitialized(FromColorData.Num());
	for (int Index = 0, Num = FromColorData.Num(); Index < Num; Index++)
	{
		OutData[Index] = FLinearColor(FromColorData[Index]);
	}
}

void FVulkanDynamicRHI::RHIMapStagingSurface(FRHITexture* TextureRHI, FRHIGPUFence* FenceRHI, void*& OutData, int32& OutWidth, int32& OutHeight, uint32 GPUIndex)
{
	FVulkanTexture* Texture = ResourceCast(TextureRHI);

	if (FenceRHI && !FenceRHI->Poll())
	{
		FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);

		// SubmitCommandsAndFlushGPU might update fence state if it was tied to a previously submitted command buffer.
		// Its state will have been updated from Submitted to NeedReset, and would assert in WaitForCmdBuffer (which is not needed in such a case)
		FenceRHI->Wait(RHICmdList, FRHIGPUMask::All());
	}
	else
	{
		if (GVulkanFlushOnMapStaging)
		{
			FRHICommandListImmediate::Get().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
			Device->WaitUntilIdle();
		}
	}


	check(EnumHasAllFlags(Texture->GetDesc().Flags, TexCreate_CPUReadback));
	OutData = Texture->GetMappedPointer();
	Texture->InvalidateMappedMemory();
	OutWidth = Texture->GetSizeX();
	OutHeight = Texture->GetSizeY();
}

void FVulkanDynamicRHI::RHIUnmapStagingSurface(FRHITexture* TextureRHI, uint32 GPUIndex)
{
}

void FVulkanDynamicRHI::RHIReadSurfaceFloatData(FRHITexture* TextureRHI, FIntRect Rect, TArray<FFloat16Color>& OutData, ECubeFace CubeFace, int32 ArrayIndex, int32 MipIndex)
{
	auto DoCopyFloat = [](FVulkanDevice* InDevice, const FVulkanTexture& VulkanTexture, uint32 InMipIndex, uint32 SrcBaseArrayLayer, FIntRect InRect, TArray<FFloat16Color>& OutputData)
	{
		ensure(VulkanTexture.StorageFormat == VK_FORMAT_R16G16B16A16_SFLOAT);

		const FRHITextureDesc& Desc = VulkanTexture.GetDesc();

		const uint32 NumPixels = (Desc.Extent.X >> InMipIndex) * (Desc.Extent.Y >> InMipIndex);
		const uint32 Size = NumPixels * sizeof(FFloat16Color);
		VulkanRHI::FStagingBuffer* StagingBuffer = InDevice->GetStagingManager().AcquireBuffer(Size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

		// the staging buffer size may be bigger then the size due to alignment, etc. but it must not be smaller!
		ensure(StagingBuffer->GetSize() >= Size);

		VkBufferImageCopy CopyRegion;
		FMemory::Memzero(CopyRegion);
		//Region.bufferOffset = 0;
		CopyRegion.bufferRowLength = FMath::Max(1, Desc.Extent.X >> InMipIndex);
		CopyRegion.bufferImageHeight = FMath::Max(1, Desc.Extent.Y >> InMipIndex);
		CopyRegion.imageSubresource.aspectMask = VulkanTexture.GetFullAspectMask();
		CopyRegion.imageSubresource.mipLevel = InMipIndex;
		CopyRegion.imageSubresource.baseArrayLayer = SrcBaseArrayLayer;
		CopyRegion.imageSubresource.layerCount = 1;
		CopyRegion.imageExtent.width = FMath::Max(1, Desc.Extent.X >> InMipIndex);
		CopyRegion.imageExtent.height = FMath::Max(1, Desc.Extent.Y >> InMipIndex);
		CopyRegion.imageExtent.depth = 1;

		FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();
		RHICmdList.EnqueueLambda([CopyRegion, StagingBuffer, &VulkanTexture](FRHICommandListBase& ExecutingCmdList)
		{
			FVulkanCommandListContext& Context = FVulkanCommandListContext::Get(ExecutingCmdList);
			FVulkanCommandBuffer& CommandBuffer = Context.GetCommandBuffer();
			VulkanRHI::vkCmdCopyImageToBuffer(CommandBuffer.GetHandle(), VulkanTexture.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, StagingBuffer->GetHandle(), 1, &CopyRegion);

			FVulkanPipelineBarrier AfterBarrier;
			AfterBarrier.AddMemoryBarrier(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
			AfterBarrier.Execute(&CommandBuffer);
		});

		// We need to execute the command list so we can read the data from the map below
		RHICmdList.SubmitAndBlockUntilGPUIdle();

		StagingBuffer->InvalidateMappedMemory();

		uint32 OutWidth = InRect.Max.X - InRect.Min.X;
		uint32 OutHeight= InRect.Max.Y - InRect.Min.Y;
		OutputData.SetNumUninitialized(OutWidth * OutHeight);
		uint32 OutIndex = 0;
		FFloat16Color* Dest = OutputData.GetData();
		void* MappedPointer = StagingBuffer->GetMappedPointer();
		for (int32 Row = InRect.Min.Y; Row < InRect.Max.Y; ++Row)
		{
			FFloat16Color* Src = (FFloat16Color*)MappedPointer + Row * (Desc.Extent.X >> InMipIndex) + InRect.Min.X;
			for (int32 Col = InRect.Min.X; Col < InRect.Max.X; ++Col)
			{
				OutputData[OutIndex++] = *Src++;
			}
		}

		InDevice->GetStagingManager().ReleaseBuffer(nullptr, StagingBuffer);
	};

	FVulkanTexture& Surface = *ResourceCast(TextureRHI);
	const FRHITextureDesc& Desc = Surface.GetDesc();

	if (GIgnoreCPUReads)
	{
		// Debug: Fill with CPU
		uint32 NumPixels = 0;
		switch(Desc.Dimension)
		{
		case ETextureDimension::TextureCubeArray:
		case ETextureDimension::TextureCube:
			NumPixels = (Desc.Extent.X >> MipIndex) * (Desc.Extent.Y >> MipIndex);
			break;

		case ETextureDimension::Texture2DArray:
		case ETextureDimension::Texture2D:
			NumPixels = (Desc.Extent.X >> MipIndex) * (Desc.Extent.Y >> MipIndex);
			break;

		default:
			checkNoEntry();
			break;
		}

		OutData.Empty(0);
		OutData.AddZeroed(NumPixels);
	}
	else
	{
		switch (TextureRHI->GetDesc().Dimension)
		{
			case ETextureDimension::TextureCubeArray:
			case ETextureDimension::TextureCube:
				DoCopyFloat(Device, Surface, MipIndex, CubeFace + 6 * ArrayIndex, Rect, OutData);
				break;

			case ETextureDimension::Texture2DArray:
			case ETextureDimension::Texture2D:
				DoCopyFloat(Device, Surface, MipIndex, ArrayIndex, Rect, OutData);
				break;

			default:
				checkNoEntry();
				break;
		}
	}
}

void FVulkanDynamicRHI::RHIRead3DSurfaceFloatData(FRHITexture* TextureRHI, FIntRect InRect, FIntPoint ZMinMax, TArray<FFloat16Color>& OutData)
{
	FVulkanTexture& Surface = *ResourceCast(TextureRHI);
	const FRHITextureDesc& Desc = Surface.GetDesc();

	const uint32 SizeX = InRect.Width();
	const uint32 SizeY = InRect.Height();
	const uint32 SizeZ = ZMinMax.Y - ZMinMax.X;
	const uint32 NumPixels = SizeX * SizeY * SizeZ;
	const uint32 Size = NumPixels * sizeof(FFloat16Color);

	// Allocate the output buffer.
	OutData.SetNumUninitialized(Size);

	if (GIgnoreCPUReads)
	{
		// Debug: Fill with CPU
		FMemory::Memzero(OutData.GetData(), Size * sizeof(FFloat16Color));
		return;
	}

	ensure(Surface.StorageFormat == VK_FORMAT_R16G16B16A16_SFLOAT);

	VulkanRHI::FStagingBuffer* StagingBuffer = Device->GetStagingManager().AcquireBuffer(Size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
	// the staging buffer size may be bigger then the size due to alignment, etc. but it must not be smaller!
	ensure(StagingBuffer->GetSize() >= Size);

	VkBufferImageCopy CopyRegion;
	FMemory::Memzero(CopyRegion);
	//Region.bufferOffset = 0;
	CopyRegion.bufferRowLength = Desc.Extent.X;
	CopyRegion.bufferImageHeight = Desc.Extent.Y;
	CopyRegion.imageSubresource.aspectMask = Surface.GetFullAspectMask();
	//CopyRegion.imageSubresource.mipLevel = 0;
	//CopyRegion.imageSubresource.baseArrayLayer = 0;
	CopyRegion.imageSubresource.layerCount = 1;
	CopyRegion.imageOffset.x = InRect.Min.X;
	CopyRegion.imageOffset.y = InRect.Min.Y;
	CopyRegion.imageOffset.z = ZMinMax.X;
	CopyRegion.imageExtent.width = SizeX;
	CopyRegion.imageExtent.height = SizeY;
	CopyRegion.imageExtent.depth = SizeZ;

	FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();
	RHICmdList.EnqueueLambda([CopyRegion, StagingBuffer, &Surface](FRHICommandListBase& ExecutingCmdList)
	{
		FVulkanCommandListContext& Context = FVulkanCommandListContext::Get(ExecutingCmdList);
		FVulkanCommandBuffer& CommandBuffer = Context.GetCommandBuffer();
		VulkanRHI::vkCmdCopyImageToBuffer(CommandBuffer.GetHandle(), Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, StagingBuffer->GetHandle(), 1, &CopyRegion);

		FVulkanPipelineBarrier AfterBarrier;
		AfterBarrier.AddMemoryBarrier(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
		AfterBarrier.Execute(&CommandBuffer);
	});

	// We need to execute the command list so we can read the data from the map below
	RHICmdList.SubmitAndBlockUntilGPUIdle();

	StagingBuffer->InvalidateMappedMemory();

	FFloat16Color* Dest = OutData.GetData();
	for (int32 Layer = ZMinMax.X; Layer < ZMinMax.Y; ++Layer)
	{
		for (int32 Row = InRect.Min.Y; Row < InRect.Max.Y; ++Row)
		{
			FFloat16Color* Src = (FFloat16Color*)StagingBuffer->GetMappedPointer() + Layer * SizeX * SizeY + Row * Desc.Extent.X + InRect.Min.X;
			for (int32 Col = InRect.Min.X; Col < InRect.Max.X; ++Col)
			{
				*Dest++ = *Src++;
			}
		}
	}
	FFloat16Color* End = OutData.GetData() + OutData.Num();
	checkf(Dest <= End, TEXT("Memory overwrite! Calculated total size %d: SizeX %d SizeY %d SizeZ %d; InRect(%d, %d, %d, %d) InZ(%d, %d)"),
		Size, SizeX, SizeY, SizeZ, InRect.Min.X, InRect.Min.Y, InRect.Max.X, InRect.Max.Y, ZMinMax.X, ZMinMax.Y);
	Device->GetStagingManager().ReleaseBuffer(nullptr, StagingBuffer);
}

VkFormat FVulkanCommandListContext::GetSwapchainImageFormat() const
{
	TArray<FVulkanViewport*>& viewports = FVulkanDynamicRHI::Get().GetViewports();
	if (viewports.Num() == 0)
	{
		return VK_FORMAT_UNDEFINED;
	}

	return viewports[0]->GetSwapchainImageFormat();
}

FVulkanSwapChain* FVulkanCommandListContext::GetSwapChain() const
{
	TArray<FVulkanViewport*>& viewports = FVulkanDynamicRHI::Get().GetViewports();
	uint32 numViewports = viewports.Num();

	if (viewports.Num() == 0)
	{
		return nullptr;
	}

	return viewports[0]->GetSwapChain();
}

bool FVulkanCommandListContext::IsSwapchainImage(FRHITexture* InTexture) const
{
	TArray<FVulkanViewport*>& Viewports = FVulkanDynamicRHI::Get().GetViewports();
	uint32 NumViewports = Viewports.Num();

	for (uint32 i = 0; i < NumViewports; i++)
	{
		VkImage Image = ResourceCast(InTexture)->Image;
		uint32 BackBufferImageCount = Viewports[i]->GetBackBufferImageCount();

		for (uint32 SwapchainImageIdx = 0; SwapchainImageIdx < BackBufferImageCount; SwapchainImageIdx++)
		{
			if (Image == Viewports[i]->GetBackBufferImage(SwapchainImageIdx))
			{
				return true;
			}
		}
	}
	return false;
}

void FVulkanCommandListContext::RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName)
{
	RenderPassInfo = InInfo;

	if (InInfo.NumOcclusionQueries > 0)
	{
		BeginOcclusionQueryBatch(InInfo.NumOcclusionQueries);
	}

	const bool bNeedsAllPlanes = Device.NeedsAllPlanes();

	FRHITexture* DSTexture = InInfo.DepthStencilRenderTarget.DepthStencilTarget;
	VkImageLayout CurrentDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageLayout CurrentStencilLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (DSTexture)
	{
		FVulkanTexture& VulkanTexture = *ResourceCast(DSTexture);
		const VkImageAspectFlags AspectFlags = VulkanTexture.GetFullAspectMask();

		const FExclusiveDepthStencil ExclusiveDepthStencil = InInfo.DepthStencilRenderTarget.ExclusiveDepthStencil;
		if (VKHasAnyFlags(AspectFlags, VK_IMAGE_ASPECT_DEPTH_BIT))
		{
			if (ExclusiveDepthStencil.IsDepthWrite())
			{
				CurrentDepthLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
			}
			else if (ExclusiveDepthStencil.IsDepthRead())
			{
				CurrentDepthLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
			}
			else if (bNeedsAllPlanes)
			{
				CurrentDepthLayout = FVulkanPipelineBarrier::GetDepthOrStencilLayout(VulkanTexture.AllPlanesTrackedAccess[0]);
			}
		}

		if (VKHasAnyFlags(AspectFlags, VK_IMAGE_ASPECT_STENCIL_BIT))
		{
			if (ExclusiveDepthStencil.IsStencilWrite())
			{
				CurrentStencilLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
			}
			else if (ExclusiveDepthStencil.IsStencilRead())
			{
				CurrentStencilLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
			}
			else if (bNeedsAllPlanes)
			{
				CurrentStencilLayout = FVulkanPipelineBarrier::GetDepthOrStencilLayout(VulkanTexture.AllPlanesTrackedAccess[1]);
			}
		}
	}

	FVulkanRenderTargetLayout RTLayout(Device, InInfo, CurrentDepthLayout, CurrentStencilLayout);
	check(RTLayout.GetExtent2D().width != 0 && RTLayout.GetExtent2D().height != 0);

	FVulkanRenderPass* RenderPass = Device.GetRenderPassManager().GetOrCreateRenderPass(RTLayout);
	FRHISetRenderTargetsInfo RTInfo;
	InInfo.ConvertToRenderTargetsInfo(RTInfo);

	FVulkanFramebuffer* Framebuffer = Device.GetRenderPassManager().GetOrCreateFramebuffer(RTInfo, RTLayout, RenderPass);
	checkf(RenderPass != nullptr && Framebuffer != nullptr, TEXT("RenderPass not started! Bad combination of values? Depth %p #Color %d Color0 %p"), (void*)InInfo.DepthStencilRenderTarget.DepthStencilTarget, InInfo.GetNumColorRenderTargets(), (void*)InInfo.ColorRenderTargets[0].RenderTarget);

	const bool bIsParallelRenderPass = (CurrentParallelRenderPassInfo != nullptr);
	FVulkanBeginRenderPassInfo BeginRenderPassInfo{*RenderPass, *Framebuffer, bIsParallelRenderPass};
	Device.GetRenderPassManager().BeginRenderPass(*this, InInfo, RTLayout, BeginRenderPassInfo);

	check(!CurrentRenderPass);
	CurrentRenderPass = RenderPass;
	CurrentFramebuffer = Framebuffer;
}

void FVulkanCommandListContext::RHIEndRenderPass()
{
	Device.GetRenderPassManager().EndRenderPass(*this);

	const bool bHasOcclusionQueries = (RenderPassInfo.NumOcclusionQueries > 0);
	if (bHasOcclusionQueries)
	{
		// Force the syncs points to be signaled right after the render pass containing the queries
		FlushPendingSyncPoints();
	}

	check(CurrentRenderPass);
	CurrentRenderPass = nullptr;
}

void FVulkanCommandListContext::RHINextSubpass()
{
	check(CurrentRenderPass);
	FVulkanCommandBuffer& CommandBuffer = GetCommandBuffer();
	VkCommandBuffer CommandBufferHandle = CommandBuffer.GetHandle();
	VulkanRHI::vkCmdNextSubpass(CommandBufferHandle, VK_SUBPASS_CONTENTS_INLINE);
}

// Need a separate struct so we can memzero/remove dependencies on reference counts
struct FRenderPassCompatibleHashableStruct
{
	FRenderPassCompatibleHashableStruct()
	{
		FMemory::Memzero(*this);
	}

	uint8							NumAttachments;
	uint8							MultiViewCount;
	uint8							NumSamples;
	uint8							SubpassHint;
	// +1 for Depth, +1 for Stencil, +1 for Fragment Density
	VkFormat						Formats[MaxSimultaneousRenderTargets + 3];
	uint16							AttachmentsToResolve;
};

// Need a separate struct so we can memzero/remove dependencies on reference counts
struct FRenderPassFullHashableStruct
{
	FRenderPassFullHashableStruct()
	{
		FMemory::Memzero(*this);
	}

	// +1 for Depth, +1 for Stencil, +1 for Fragment Density
	TEnumAsByte<VkAttachmentLoadOp>		LoadOps[MaxSimultaneousRenderTargets + 3];
	TEnumAsByte<VkAttachmentStoreOp>	StoreOps[MaxSimultaneousRenderTargets + 3];
	// If the initial != final we need to add FinalLayout and potentially RefLayout
	VkImageLayout						InitialLayout[MaxSimultaneousRenderTargets + 3];
	//VkImageLayout						FinalLayout[MaxSimultaneousRenderTargets + 3];
	//VkImageLayout						RefLayout[MaxSimultaneousRenderTargets + 3];
};

VkImageLayout FVulkanRenderTargetLayout::GetVRSImageLayout() const
{
	if (ValidateShadingRateDataType())
	{
		if (GRHIVariableRateShadingImageDataType == VRSImage_Palette)
		{
			return VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
		}
		if (GRHIVariableRateShadingImageDataType == VRSImage_Fractional)
		{
			return VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT;
		}
	}

	return VK_IMAGE_LAYOUT_UNDEFINED;
}

FVulkanRenderTargetLayout::FVulkanRenderTargetLayout(FVulkanDevice& InDevice, const FRHISetRenderTargetsInfo& RTInfo)
	: NumAttachmentDescriptions(0)
	, NumColorAttachments(0)
	, bHasDepthStencil(false)
	, bHasResolveAttachments(false)
	, bHasDepthStencilResolve(false)
	, bHasFragmentDensityAttachment(false)
	, NumSamples(0)
	, NumUsedClearValues(0)
	, MultiViewCount(0)
{
	ResetAttachments();

	FRenderPassCompatibleHashableStruct CompatibleHashInfo;
	FRenderPassFullHashableStruct FullHashInfo;

	bool bSetExtent = false;
	bool bFoundClearOp = false;
	for (int32 Index = 0; Index < RTInfo.NumColorRenderTargets; ++Index)
	{
		const FRHIRenderTargetView& RTView = RTInfo.ColorRenderTarget[Index];
		if (RTView.Texture)
		{
			FVulkanTexture* Texture = ResourceCast(RTView.Texture);
			check(Texture);
			const FRHITextureDesc& TextureDesc = Texture->GetDesc();

			if (bSetExtent)
			{
				ensure(Extent.Extent3D.width == FMath::Max(1, TextureDesc.Extent.X >> RTView.MipIndex));
				ensure(Extent.Extent3D.height == FMath::Max(1, TextureDesc.Extent.Y >> RTView.MipIndex));
				ensure(Extent.Extent3D.depth == TextureDesc.Depth);
			}
			else
			{
				bSetExtent = true;
				Extent.Extent3D.width = FMath::Max(1, TextureDesc.Extent.X >> RTView.MipIndex);
				Extent.Extent3D.height = FMath::Max(1, TextureDesc.Extent.Y >> RTView.MipIndex);
				Extent.Extent3D.depth = TextureDesc.Depth;
			}

			ensure(!NumSamples || NumSamples == Texture->GetNumSamples());
			NumSamples = Texture->GetNumSamples();
		
			VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
			CurrDesc.samples = static_cast<VkSampleCountFlagBits>(NumSamples);
			CurrDesc.format = UEToVkTextureFormat(RTView.Texture->GetFormat(), EnumHasAllFlags(TextureDesc.Flags, TexCreate_SRGB));
			CurrDesc.loadOp = RenderTargetLoadActionToVulkan(RTView.LoadAction);
			bFoundClearOp = bFoundClearOp || (CurrDesc.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
			CurrDesc.storeOp = RenderTargetStoreActionToVulkan(RTView.StoreAction);
			CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

			// Removed this temporarily as we need a way to determine if the target is actually memoryless
			/*if (EnumHasAllFlags(Texture->UEFlags, TexCreate_Memoryless))
			{
				ensure(CurrDesc.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
			}*/

			// If the initial != final we need to change the FullHashInfo and use FinalLayout
			CurrDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			CurrDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			ColorReferences[NumColorAttachments].attachment = NumAttachmentDescriptions;
			ColorReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			const bool bHasValidResolveAttachment = RTInfo.bHasResolveAttachments && RTInfo.ColorResolveRenderTarget[Index].Texture;
			if (CurrDesc.samples > VK_SAMPLE_COUNT_1_BIT && bHasValidResolveAttachment)
			{
				Desc[NumAttachmentDescriptions + 1] = Desc[NumAttachmentDescriptions];
				Desc[NumAttachmentDescriptions + 1].samples = VK_SAMPLE_COUNT_1_BIT;
				Desc[NumAttachmentDescriptions + 1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				Desc[NumAttachmentDescriptions + 1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				ResolveReferences[NumColorAttachments].attachment = NumAttachmentDescriptions + 1;
				ResolveReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				CompatibleHashInfo.AttachmentsToResolve |= (uint16)(1 << NumColorAttachments);
				++NumAttachmentDescriptions;
				bHasResolveAttachments = true;
			}

			CompatibleHashInfo.Formats[NumColorAttachments] = CurrDesc.format;
			FullHashInfo.LoadOps[NumColorAttachments] = CurrDesc.loadOp;
			FullHashInfo.StoreOps[NumColorAttachments] = CurrDesc.storeOp;
			FullHashInfo.InitialLayout[NumColorAttachments] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			++CompatibleHashInfo.NumAttachments;

			++NumAttachmentDescriptions;
			++NumColorAttachments;
		}
	}

	if (RTInfo.DepthStencilRenderTarget.Texture)
	{
		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		FMemory::Memzero(CurrDesc);
		FVulkanTexture* Texture = ResourceCast(RTInfo.DepthStencilRenderTarget.Texture);
		check(Texture);
		const FRHITextureDesc& TextureDesc = Texture->GetDesc();

		ensure(!NumSamples || NumSamples == Texture->GetNumSamples());
		NumSamples = TextureDesc.NumSamples;

		CurrDesc.samples = static_cast<VkSampleCountFlagBits>(NumSamples);
		CurrDesc.format = UEToVkTextureFormat(RTInfo.DepthStencilRenderTarget.Texture->GetFormat(), false);
		CurrDesc.loadOp = RenderTargetLoadActionToVulkan(RTInfo.DepthStencilRenderTarget.DepthLoadAction);
		CurrDesc.stencilLoadOp = RenderTargetLoadActionToVulkan(RTInfo.DepthStencilRenderTarget.StencilLoadAction);
		bFoundClearOp = bFoundClearOp || (CurrDesc.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR || CurrDesc.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
		CurrDesc.storeOp = RenderTargetStoreActionToVulkan(RTInfo.DepthStencilRenderTarget.DepthStoreAction);
		CurrDesc.stencilStoreOp = RenderTargetStoreActionToVulkan(RTInfo.DepthStencilRenderTarget.GetStencilStoreAction());

		// Removed this temporarily as we need a way to determine if the target is actually memoryless
		/*if (EnumHasAllFlags(Texture->UEFlags, TexCreate_Memoryless))
		{
			ensure(CurrDesc.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
			ensure(CurrDesc.stencilStoreOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
		}*/

		const VkImageLayout DepthLayout = RTInfo.DepthStencilRenderTarget.GetDepthStencilAccess().IsDepthWrite() ? VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
		const VkImageLayout StencilLayout = RTInfo.DepthStencilRenderTarget.GetDepthStencilAccess().IsStencilWrite() ? VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;

		// If the initial != final we need to change the FullHashInfo and use FinalLayout
		CurrDesc.initialLayout = DepthLayout;
		CurrDesc.finalLayout = DepthLayout;
		StencilDesc.stencilInitialLayout = StencilLayout;
		StencilDesc.stencilFinalLayout = StencilLayout;

		DepthReference.attachment = NumAttachmentDescriptions;
		DepthReference.layout = DepthLayout;
		StencilReference.stencilLayout = StencilLayout;

		// Use depth/stencil resolve target only if we're MSAA
		const bool bDepthStencilResolve = (RTInfo.DepthStencilRenderTarget.DepthStoreAction == ERenderTargetStoreAction::EMultisampleResolve) || (RTInfo.DepthStencilRenderTarget.GetStencilStoreAction() == ERenderTargetStoreAction::EMultisampleResolve);
		if (GRHISupportsDepthStencilResolve && bDepthStencilResolve && CurrDesc.samples > VK_SAMPLE_COUNT_1_BIT && RTInfo.DepthStencilResolveRenderTarget.Texture)
		{
			Desc[NumAttachmentDescriptions + 1] = Desc[NumAttachmentDescriptions];
			Desc[NumAttachmentDescriptions + 1].samples = VK_SAMPLE_COUNT_1_BIT;
			Desc[NumAttachmentDescriptions + 1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			Desc[NumAttachmentDescriptions + 1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			Desc[NumAttachmentDescriptions + 1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			Desc[NumAttachmentDescriptions + 1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
			DepthStencilResolveReference.attachment = NumAttachmentDescriptions + 1;
			DepthStencilResolveReference.layout = DepthLayout;
			// NumColorAttachments was incremented after the last color attachment
			ensureMsgf(NumColorAttachments < 16, TEXT("Must have room for depth resolve bit"));
			CompatibleHashInfo.AttachmentsToResolve |= (uint16)(1 << NumColorAttachments);
			++NumAttachmentDescriptions;
			bHasDepthStencilResolve = true;
		}

		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets] = CurrDesc.loadOp;
		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets + 1] = CurrDesc.stencilLoadOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets] = CurrDesc.storeOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets + 1] = CurrDesc.stencilStoreOp;
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets] = DepthLayout;
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets + 1] = StencilLayout;
		CompatibleHashInfo.Formats[MaxSimultaneousRenderTargets] = CurrDesc.format;

		++NumAttachmentDescriptions;

		bHasDepthStencil = true;

		if (bSetExtent)
		{
			// Depth can be greater or equal to color. Clamp to the smaller size.
			Extent.Extent3D.width = FMath::Min<uint32>(Extent.Extent3D.width, TextureDesc.Extent.X);
			Extent.Extent3D.height = FMath::Min<uint32>(Extent.Extent3D.height, TextureDesc.Extent.Y);
		}
		else
		{
			bSetExtent = true;
			Extent.Extent3D.width = TextureDesc.Extent.X;
			Extent.Extent3D.height = TextureDesc.Extent.Y;
			Extent.Extent3D.depth = Texture->GetNumberOfArrayLevels();
		}
	}

	if (GRHISupportsAttachmentVariableRateShading && RTInfo.ShadingRateTexture)
	{
		FVulkanTexture* Texture = ResourceCast(RTInfo.ShadingRateTexture);
		check(Texture->GetFormat() == GRHIVariableRateShadingImageFormat);

		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		FMemory::Memzero(CurrDesc);

		const VkImageLayout VRSLayout = GetVRSImageLayout();
		
		CurrDesc.flags = 0;
		CurrDesc.format = UEToVkTextureFormat(RTInfo.ShadingRateTexture->GetFormat(), false);
		CurrDesc.samples = static_cast<VkSampleCountFlagBits>(RTInfo.ShadingRateTexture->GetNumSamples());
		CurrDesc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		CurrDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrDesc.initialLayout = VRSLayout;
		CurrDesc.finalLayout = VRSLayout;

		FragmentDensityReference.attachment = NumAttachmentDescriptions;
		FragmentDensityReference.layout = VRSLayout;

		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets + 2] = CurrDesc.stencilLoadOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets + 2] = CurrDesc.stencilStoreOp;
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets + 2] = VRSLayout;
		CompatibleHashInfo.Formats[MaxSimultaneousRenderTargets + 1] = CurrDesc.format;

		++NumAttachmentDescriptions;
		bHasFragmentDensityAttachment = true;
	}

	SubpassHint = ESubpassHint::None;
	CompatibleHashInfo.SubpassHint = 0;

	CompatibleHashInfo.NumSamples = NumSamples;
	CompatibleHashInfo.MultiViewCount = MultiViewCount;

	RenderPassCompatibleHash = FCrc::MemCrc32(&CompatibleHashInfo, sizeof(CompatibleHashInfo));
	RenderPassFullHash = FCrc::MemCrc32(&FullHashInfo, sizeof(FullHashInfo), RenderPassCompatibleHash);
	NumUsedClearValues = bFoundClearOp ? NumAttachmentDescriptions : 0;
	bCalculatedHash = true;
}


FVulkanRenderTargetLayout::FVulkanRenderTargetLayout(FVulkanDevice& InDevice, const FRHIRenderPassInfo& RPInfo, VkImageLayout CurrentDepthLayout, VkImageLayout CurrentStencilLayout)
	: NumAttachmentDescriptions(0)
	, NumColorAttachments(0)
	, bHasDepthStencil(false)
	, bHasResolveAttachments(false)
	, bHasDepthStencilResolve(false)
	, bHasFragmentDensityAttachment(false)
	, NumSamples(0)
	, NumUsedClearValues(0)
	, MultiViewCount(RPInfo.MultiViewCount)
{
	ResetAttachments();

	FRenderPassCompatibleHashableStruct CompatibleHashInfo;
	FRenderPassFullHashableStruct FullHashInfo;

	bool bSetExtent = false;
	bool bFoundClearOp = false;
	bool bMultiviewRenderTargets = false;

	int32 NumColorRenderTargets = RPInfo.GetNumColorRenderTargets();
	for (int32 Index = 0; Index < NumColorRenderTargets; ++Index)
	{
		const FRHIRenderPassInfo::FColorEntry& ColorEntry = RPInfo.ColorRenderTargets[Index];
		FVulkanTexture* Texture = ResourceCast(ColorEntry.RenderTarget);
		check(Texture);
		const FRHITextureDesc& TextureDesc = Texture->GetDesc();

		if (bSetExtent)
		{
			ensure(Extent.Extent3D.width == FMath::Max(1, TextureDesc.Extent.X >> ColorEntry.MipIndex));
			ensure(Extent.Extent3D.height == FMath::Max(1, TextureDesc.Extent.Y >> ColorEntry.MipIndex));
			ensure(Extent.Extent3D.depth == TextureDesc.Depth);
		}
		else
		{
			bSetExtent = true;
			Extent.Extent3D.width = FMath::Max(1, TextureDesc.Extent.X >> ColorEntry.MipIndex);
			Extent.Extent3D.height = FMath::Max(1, TextureDesc.Extent.Y >> ColorEntry.MipIndex);
			Extent.Extent3D.depth = TextureDesc.Depth;
		}

		// CustomResolveSubpass can have targets with a different NumSamples
		ensure(!NumSamples || NumSamples == ColorEntry.RenderTarget->GetNumSamples() || RPInfo.SubpassHint == ESubpassHint::CustomResolveSubpass);
		NumSamples = ColorEntry.RenderTarget->GetNumSamples();

		ensure(!GetIsMultiView() || !bMultiviewRenderTargets || Texture->GetNumberOfArrayLevels() > 1);
		bMultiviewRenderTargets = Texture->GetNumberOfArrayLevels() > 1;
		// With a CustomResolveSubpass last color attachment is a resolve target
		bool bCustomResolveAttachment = (Index == (NumColorRenderTargets - 1)) && RPInfo.SubpassHint == ESubpassHint::CustomResolveSubpass;

		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		CurrDesc.samples = bCustomResolveAttachment ? VK_SAMPLE_COUNT_1_BIT : static_cast<VkSampleCountFlagBits>(NumSamples);
		CurrDesc.format = UEToVkTextureFormat(ColorEntry.RenderTarget->GetFormat(), EnumHasAllFlags(Texture->GetDesc().Flags, TexCreate_SRGB));
		CurrDesc.loadOp = RenderTargetLoadActionToVulkan(GetLoadAction(ColorEntry.Action));
		bFoundClearOp = bFoundClearOp || (CurrDesc.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
		CurrDesc.storeOp = RenderTargetStoreActionToVulkan(GetStoreAction(ColorEntry.Action));
		CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		if (EnumHasAnyFlags(Texture->GetDesc().Flags, TexCreate_Memoryless))
		{
			ensure(CurrDesc.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
		}

		// If the initial != final we need to change the FullHashInfo and use FinalLayout
		CurrDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		CurrDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		ColorReferences[NumColorAttachments].attachment = NumAttachmentDescriptions;
		ColorReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		if (CurrDesc.samples > VK_SAMPLE_COUNT_1_BIT && ColorEntry.ResolveTarget)
		{
			Desc[NumAttachmentDescriptions + 1] = Desc[NumAttachmentDescriptions];
			Desc[NumAttachmentDescriptions + 1].samples = VK_SAMPLE_COUNT_1_BIT;
			Desc[NumAttachmentDescriptions + 1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			Desc[NumAttachmentDescriptions + 1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			ResolveReferences[NumColorAttachments].attachment = NumAttachmentDescriptions + 1;
			ResolveReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			CompatibleHashInfo.AttachmentsToResolve |= (uint16)(1 << NumColorAttachments);
			++NumAttachmentDescriptions;
			bHasResolveAttachments = true;
		}

		CompatibleHashInfo.Formats[NumColorAttachments] = CurrDesc.format;
		FullHashInfo.LoadOps[NumColorAttachments] = CurrDesc.loadOp;
		FullHashInfo.InitialLayout[NumColorAttachments] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		FullHashInfo.StoreOps[NumColorAttachments] = CurrDesc.storeOp;
		++CompatibleHashInfo.NumAttachments;

		++NumAttachmentDescriptions;
		++NumColorAttachments;
	}
	bool bMultiViewDepthStencil = false;
	if (RPInfo.DepthStencilRenderTarget.DepthStencilTarget)
	{
		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		FMemory::Memzero(CurrDesc);
		FVulkanTexture* Texture = ResourceCast(RPInfo.DepthStencilRenderTarget.DepthStencilTarget);
		check(Texture);
		const FRHITextureDesc& TextureDesc = Texture->GetDesc();
		bMultiViewDepthStencil = (Texture->GetNumberOfArrayLevels() > 1) && !Texture->GetDesc().IsTextureCube();
		CurrDesc.samples = static_cast<VkSampleCountFlagBits>(RPInfo.DepthStencilRenderTarget.DepthStencilTarget->GetNumSamples());
		// CustomResolveSubpass can have targets with a different NumSamples
		ensure(!NumSamples || CurrDesc.samples == NumSamples || RPInfo.SubpassHint == ESubpassHint::CustomResolveSubpass);
		NumSamples = CurrDesc.samples;
		CurrDesc.format = UEToVkTextureFormat(RPInfo.DepthStencilRenderTarget.DepthStencilTarget->GetFormat(), false);
		CurrDesc.loadOp = RenderTargetLoadActionToVulkan(GetLoadAction(GetDepthActions(RPInfo.DepthStencilRenderTarget.Action)));
		CurrDesc.stencilLoadOp = RenderTargetLoadActionToVulkan(GetLoadAction(GetStencilActions(RPInfo.DepthStencilRenderTarget.Action)));
		bFoundClearOp = bFoundClearOp || (CurrDesc.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR || CurrDesc.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);

		CurrDesc.storeOp = RenderTargetStoreActionToVulkan(GetStoreAction(GetDepthActions(RPInfo.DepthStencilRenderTarget.Action)));
		CurrDesc.stencilStoreOp = RenderTargetStoreActionToVulkan(GetStoreAction(GetStencilActions(RPInfo.DepthStencilRenderTarget.Action)));

		if (EnumHasAnyFlags(TextureDesc.Flags, TexCreate_Memoryless))
		{
			ensure(CurrDesc.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
			ensure(CurrDesc.stencilStoreOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
		}

		if (FVulkanPlatform::RequiresDepthStencilFullWrite() &&
			Texture->GetFullAspectMask() == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) &&
			(CurrDesc.storeOp == VK_ATTACHMENT_STORE_OP_STORE || CurrDesc.stencilStoreOp == VK_ATTACHMENT_STORE_OP_STORE))
		{
			// Workaround for old mali drivers: writing not all of the image aspects to compressed render-target could cause gpu-hang
			CurrDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		}

		// If the initial != final we need to change the FullHashInfo and use FinalLayout
		CurrDesc.initialLayout = CurrentDepthLayout;
		CurrDesc.finalLayout = CurrentDepthLayout;
		StencilDesc.stencilInitialLayout = CurrentStencilLayout;
		StencilDesc.stencilFinalLayout = CurrentStencilLayout;

		// We can't have the final layout be UNDEFINED, but it's possible that we get here from a transient texture
		// where the stencil was never used yet.  We can set the layout to whatever we want, the next transition will
		// happen from UNDEFINED anyhow.
		if (CurrentDepthLayout == VK_IMAGE_LAYOUT_UNDEFINED)
		{
			// Unused image aspects with a LoadOp but undefined layout should just remain untouched
			if (!RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil.IsUsingDepth() &&
				InDevice.GetOptionalExtensions().HasEXTLoadStoreOpNone &&
				(CurrDesc.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD))
			{
				CurrDesc.loadOp = VK_ATTACHMENT_LOAD_OP_NONE_KHR;
			}

			check(CurrDesc.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
			CurrDesc.finalLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		}
		if (CurrentStencilLayout == VK_IMAGE_LAYOUT_UNDEFINED)
		{
			// Unused image aspects with a LoadOp but undefined layout should just remain untouched
			if (!RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil.IsUsingStencil() &&
				InDevice.GetOptionalExtensions().HasEXTLoadStoreOpNone &&
				(CurrDesc.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD))
			{
				CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_NONE_KHR;
			}

			check(CurrDesc.stencilStoreOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
			StencilDesc.stencilFinalLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		}

		DepthReference.attachment = NumAttachmentDescriptions;
		DepthReference.layout = CurrentDepthLayout;
		StencilReference.stencilLayout = CurrentStencilLayout;

		if (GRHISupportsDepthStencilResolve && CurrDesc.samples > VK_SAMPLE_COUNT_1_BIT && RPInfo.DepthStencilRenderTarget.ResolveTarget)
		{
			Desc[NumAttachmentDescriptions + 1] = Desc[NumAttachmentDescriptions];
			Desc[NumAttachmentDescriptions + 1].samples = VK_SAMPLE_COUNT_1_BIT;
			Desc[NumAttachmentDescriptions + 1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			Desc[NumAttachmentDescriptions + 1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			Desc[NumAttachmentDescriptions + 1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			Desc[NumAttachmentDescriptions + 1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
			DepthStencilResolveReference.attachment = NumAttachmentDescriptions + 1;
			DepthStencilResolveReference.layout = CurrentDepthLayout;
			// NumColorAttachments was incremented after the last color attachment
			ensureMsgf(NumColorAttachments < 16, TEXT("Must have room for depth resolve bit"));
			CompatibleHashInfo.AttachmentsToResolve |= (uint16)(1 << NumColorAttachments);
			++NumAttachmentDescriptions;
			bHasDepthStencilResolve = true;
		}

		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets] = CurrDesc.loadOp;
		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets + 1] = CurrDesc.stencilLoadOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets] = CurrDesc.storeOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets + 1] = CurrDesc.stencilStoreOp;
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets] = CurrentDepthLayout;
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets + 1] = CurrentStencilLayout;
		CompatibleHashInfo.Formats[MaxSimultaneousRenderTargets] = CurrDesc.format;

		++NumAttachmentDescriptions;

		bHasDepthStencil = true;

		if (bSetExtent)
		{
			// Depth can be greater or equal to color. Clamp to the smaller size.
			Extent.Extent3D.width = FMath::Min<uint32>(Extent.Extent3D.width, TextureDesc.Extent.X);
			Extent.Extent3D.height = FMath::Min<uint32>(Extent.Extent3D.height, TextureDesc.Extent.Y);
		}
		else
		{
			bSetExtent = true;
			Extent.Extent3D.width = TextureDesc.Extent.X;
			Extent.Extent3D.height = TextureDesc.Extent.Y;
			Extent.Extent3D.depth = TextureDesc.Depth;
		}
	}
	else if (NumColorRenderTargets == 0)
	{
		// No Depth and no color, it's a raster-only pass so make sure the renderArea will be set up properly
		checkf(RPInfo.ResolveRect.IsValid(), TEXT("For raster-only passes without render targets, ResolveRect has to contain the render area"));
		bSetExtent = true;
		Offset.Offset3D.x = RPInfo.ResolveRect.X1;
		Offset.Offset3D.y = RPInfo.ResolveRect.Y1;
		Offset.Offset3D.z = 0;
		Extent.Extent3D.width = RPInfo.ResolveRect.X2 - RPInfo.ResolveRect.X1;
		Extent.Extent3D.height = RPInfo.ResolveRect.Y2 - RPInfo.ResolveRect.Y1;
		Extent.Extent3D.depth = 1;
	}

	if (GRHISupportsAttachmentVariableRateShading && RPInfo.ShadingRateTexture)
	{
		FVulkanTexture* Texture = ResourceCast(RPInfo.ShadingRateTexture);
		check(Texture->GetFormat() == GRHIVariableRateShadingImageFormat);

		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		FMemory::Memzero(CurrDesc);

		const VkImageLayout VRSLayout = GetVRSImageLayout();

		CurrDesc.flags = 0;
		CurrDesc.format = UEToVkTextureFormat(RPInfo.ShadingRateTexture->GetFormat(), false);
		CurrDesc.samples = static_cast<VkSampleCountFlagBits>(RPInfo.ShadingRateTexture->GetNumSamples());
		CurrDesc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		CurrDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrDesc.initialLayout = VRSLayout;
		CurrDesc.finalLayout = VRSLayout;

		FragmentDensityReference.attachment = NumAttachmentDescriptions;
		FragmentDensityReference.layout = VRSLayout;

		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets + 2] = CurrDesc.stencilLoadOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets + 2] = CurrDesc.stencilStoreOp;
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets + 2] = VRSLayout;
		CompatibleHashInfo.Formats[MaxSimultaneousRenderTargets + 1] = CurrDesc.format;

		++NumAttachmentDescriptions;
		bHasFragmentDensityAttachment = true;
	}

	SubpassHint = RPInfo.SubpassHint;
	CompatibleHashInfo.SubpassHint = (uint8)RPInfo.SubpassHint;

	CompatibleHashInfo.NumSamples = NumSamples;
	CompatibleHashInfo.MultiViewCount = MultiViewCount;
	// Depth prepass has no color RTs but has a depth attachment that must be multiview
	if (MultiViewCount > 1 && !bMultiviewRenderTargets && !(NumColorRenderTargets == 0 && bMultiViewDepthStencil))
	{
		UE_LOG(LogVulkan, Error, TEXT("Non multiview textures on a multiview layout!"));
	}

	RenderPassCompatibleHash = FCrc::MemCrc32(&CompatibleHashInfo, sizeof(CompatibleHashInfo));
	RenderPassFullHash = FCrc::MemCrc32(&FullHashInfo, sizeof(FullHashInfo), RenderPassCompatibleHash);
	NumUsedClearValues = bFoundClearOp ? NumAttachmentDescriptions : 0;
	bCalculatedHash = true;
}

FVulkanRenderTargetLayout::FVulkanRenderTargetLayout(const FGraphicsPipelineStateInitializer& Initializer)
	: NumAttachmentDescriptions(0)
	, NumColorAttachments(0)
	, bHasDepthStencil(false)
	, bHasResolveAttachments(false)
	, bHasDepthStencilResolve(false)
	, bHasFragmentDensityAttachment(false)
	, NumSamples(0)
	, NumUsedClearValues(0)
	, MultiViewCount(0)
{
	ResetAttachments();

	FRenderPassCompatibleHashableStruct CompatibleHashInfo;
	FRenderPassFullHashableStruct FullHashInfo;

	bool bFoundClearOp = false;
	MultiViewCount = Initializer.MultiViewCount;
	NumSamples = Initializer.NumSamples;
	for (uint32 Index = 0; Index < Initializer.RenderTargetsEnabled; ++Index)
	{
		EPixelFormat UEFormat = (EPixelFormat)Initializer.RenderTargetFormats[Index];
		if (UEFormat != PF_Unknown)
		{
			// With a CustomResolveSubpass last color attachment is a resolve target
			bool bCustomResolveAttachment = (Index == (Initializer.RenderTargetsEnabled - 1)) && Initializer.SubpassHint == ESubpassHint::CustomResolveSubpass;
			
			VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
			CurrDesc.samples = bCustomResolveAttachment ? VK_SAMPLE_COUNT_1_BIT : static_cast<VkSampleCountFlagBits>(NumSamples);
			CurrDesc.format = UEToVkTextureFormat(UEFormat, EnumHasAllFlags(Initializer.RenderTargetFlags[Index], TexCreate_SRGB));
			CurrDesc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			CurrDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

			// If the initial != final we need to change the FullHashInfo and use FinalLayout
			CurrDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			CurrDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			ColorReferences[NumColorAttachments].attachment = NumAttachmentDescriptions;
			ColorReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			if (CurrDesc.samples > VK_SAMPLE_COUNT_1_BIT)
			{
				Desc[NumAttachmentDescriptions + 1] = Desc[NumAttachmentDescriptions];
				Desc[NumAttachmentDescriptions + 1].samples = VK_SAMPLE_COUNT_1_BIT;
				Desc[NumAttachmentDescriptions + 1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				Desc[NumAttachmentDescriptions + 1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				ResolveReferences[NumColorAttachments].attachment = NumAttachmentDescriptions + 1;
				ResolveReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				CompatibleHashInfo.AttachmentsToResolve |= (uint16)(1 << NumColorAttachments);
				++NumAttachmentDescriptions;
				bHasResolveAttachments = true;
			}

			CompatibleHashInfo.Formats[NumColorAttachments] = CurrDesc.format;
			FullHashInfo.LoadOps[NumColorAttachments] = CurrDesc.loadOp;
			FullHashInfo.StoreOps[NumColorAttachments] = CurrDesc.storeOp;
			FullHashInfo.InitialLayout[NumColorAttachments] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			++CompatibleHashInfo.NumAttachments;

			++NumAttachmentDescriptions;
			++NumColorAttachments;
		}
	}

	if (Initializer.DepthStencilTargetFormat != PF_Unknown)
	{
		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		FMemory::Memzero(CurrDesc);

		CurrDesc.samples = static_cast<VkSampleCountFlagBits>(NumSamples);
		CurrDesc.format = UEToVkTextureFormat(Initializer.DepthStencilTargetFormat, false);
		CurrDesc.loadOp = RenderTargetLoadActionToVulkan(Initializer.DepthTargetLoadAction);
		CurrDesc.stencilLoadOp = RenderTargetLoadActionToVulkan(Initializer.StencilTargetLoadAction);
		if (CurrDesc.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR || CurrDesc.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
		{
			bFoundClearOp = true;
		}
		CurrDesc.storeOp = RenderTargetStoreActionToVulkan(Initializer.DepthTargetStoreAction);
		CurrDesc.stencilStoreOp = RenderTargetStoreActionToVulkan(Initializer.StencilTargetStoreAction);

		const VkImageLayout DepthLayout = Initializer.DepthStencilAccess.IsDepthWrite() ? VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
		const VkImageLayout StencilLayout = Initializer.DepthStencilAccess.IsStencilWrite() ? VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;

		// If the initial != final we need to change the FullHashInfo and use FinalLayout
		CurrDesc.initialLayout = DepthLayout;
		CurrDesc.finalLayout = DepthLayout;
		StencilDesc.stencilInitialLayout = StencilLayout;
		StencilDesc.stencilFinalLayout = StencilLayout;

		DepthReference.attachment = NumAttachmentDescriptions;
		DepthReference.layout = DepthLayout;
		StencilReference.stencilLayout = StencilLayout;

		const bool bDepthStencilResolve = (Initializer.DepthTargetStoreAction == ERenderTargetStoreAction::EMultisampleResolve) || (Initializer.StencilTargetStoreAction == ERenderTargetStoreAction::EMultisampleResolve);
		if (bDepthStencilResolve && GRHISupportsDepthStencilResolve && CurrDesc.samples > VK_SAMPLE_COUNT_1_BIT)
		{
			Desc[NumAttachmentDescriptions + 1] = Desc[NumAttachmentDescriptions];
			Desc[NumAttachmentDescriptions + 1].samples = VK_SAMPLE_COUNT_1_BIT;
			Desc[NumAttachmentDescriptions + 1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			Desc[NumAttachmentDescriptions + 1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			Desc[NumAttachmentDescriptions + 1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			Desc[NumAttachmentDescriptions + 1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
			DepthStencilResolveReference.attachment = NumAttachmentDescriptions + 1;
			DepthStencilResolveReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			// NumColorAttachments was incremented after the last color attachment
			ensureMsgf(NumColorAttachments < 16, TEXT("Must have room for depth resolve bit"));
			CompatibleHashInfo.AttachmentsToResolve |= (uint16)(1 << NumColorAttachments);
			++NumAttachmentDescriptions;
			bHasDepthStencilResolve = true;
		}

		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets] = CurrDesc.loadOp;
		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets + 1] = CurrDesc.stencilLoadOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets] = CurrDesc.storeOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets + 1] = CurrDesc.stencilStoreOp;
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets] = DepthLayout;
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets + 1] = StencilLayout;
		CompatibleHashInfo.Formats[MaxSimultaneousRenderTargets] = CurrDesc.format;

		++NumAttachmentDescriptions;
		bHasDepthStencil = true;
	}

	if (Initializer.bHasFragmentDensityAttachment)
	{
		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		FMemory::Memzero(CurrDesc);

		const VkImageLayout VRSLayout = GetVRSImageLayout();

		check(GRHIVariableRateShadingImageFormat != PF_Unknown);

		CurrDesc.flags = 0;
		CurrDesc.format = UEToVkTextureFormat(GRHIVariableRateShadingImageFormat, false);
		CurrDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		CurrDesc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		CurrDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrDesc.initialLayout = VRSLayout;
		CurrDesc.finalLayout = VRSLayout;

		FragmentDensityReference.attachment = NumAttachmentDescriptions;
		FragmentDensityReference.layout = VRSLayout;

		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets + 2] = CurrDesc.stencilLoadOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets + 2] = CurrDesc.stencilStoreOp;
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets + 2] = VRSLayout;
		CompatibleHashInfo.Formats[MaxSimultaneousRenderTargets + 1] = CurrDesc.format;

		++NumAttachmentDescriptions;
		bHasFragmentDensityAttachment = true;
	}

	SubpassHint = Initializer.SubpassHint;
	CompatibleHashInfo.SubpassHint = (uint8)Initializer.SubpassHint;

	CompatibleHashInfo.NumSamples = NumSamples;
	CompatibleHashInfo.MultiViewCount = MultiViewCount;

	RenderPassCompatibleHash = FCrc::MemCrc32(&CompatibleHashInfo, sizeof(CompatibleHashInfo));
	RenderPassFullHash = FCrc::MemCrc32(&FullHashInfo, sizeof(FullHashInfo), RenderPassCompatibleHash);
	NumUsedClearValues = bFoundClearOp ? NumAttachmentDescriptions : 0;
	bCalculatedHash = true;
}
