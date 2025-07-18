// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Device/DisplayClusterDeviceBase.h"

#include "IPDisplayCluster.h"
#include "IDisplayClusterCallbacks.h"
#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Cluster/Controller/IDisplayClusterClusterNodeController.h"
#include "Config/IPDisplayClusterConfigManager.h"
#include "Game/IPDisplayClusterGameManager.h"
#include "Render/IPDisplayClusterRenderManager.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterStrings.h"
#include "Misc/DisplayClusterLog.h"

#include "DisplayClusterConfigurationTypes.h"

#include "DisplayClusterRootActor.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "Components/DisplayClusterScreenComponent.h"

#include "HAL/IConsoleManager.h"

#include "RHIStaticStates.h"
#include "Slate/SceneViewport.h"

#include "Render/PostProcess/IDisplayClusterPostProcess.h"
#include "Render/Presentation/DisplayClusterPresentationBase.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"
#include "Render/Synchronization/IDisplayClusterRenderSyncPolicy.h"


#include "Render/Viewport/DisplayClusterViewportManager.h"
#include "Render/Viewport/DisplayClusterViewportManagerProxy.h"
#include "Render/Viewport/IDisplayClusterViewport.h"
#include "Render/Viewport/IDisplayClusterViewportProxy.h"

#include "Render/Viewport/Configuration/DisplayClusterViewportConfigurationHelpers_Postprocess.h"

#include "RenderGraphUtils.h"
#include "RenderGraphBuilder.h"

#include <utility>

namespace UE::DisplayCluster::DeviceBaseHelpers
{
	static inline IDisplayCluster& GetDisplayClusterAPI()
	{
		static IDisplayCluster& DisplayClusterAPISingleton = IDisplayCluster::Get();

		return DisplayClusterAPISingleton;
	}
};
using namespace UE::DisplayCluster;

// Enable/Disable ClearTexture for RTT after resolving to the backbuffer
static TAutoConsoleVariable<int32> CVarClearTextureEnabled(
	TEXT("nDisplay.render.ClearTextureEnabled"),
	1,
	TEXT("Enables RTT cleaning for left / mono eye at end of frame.\n")
	TEXT("0 : disabled\n")
	TEXT("1 : enabled\n")
	,
	ECVF_RenderThreadSafe
);

FDisplayClusterDeviceBase::FDisplayClusterDeviceBase(EDisplayClusterRenderFrameMode InRenderFrameMode)
	: RenderFrameMode(InRenderFrameMode)
{
	UE_LOG(LogDisplayClusterRender, Log, TEXT("Created DCRenderDevice"));
}

FDisplayClusterDeviceBase::~FDisplayClusterDeviceBase()
{
	//@todo: delete singleton object IDisplayClusterViewportManager
}

IDisplayClusterViewportManager* FDisplayClusterDeviceBase::GetViewportManager() const
{
	return ViewportManagerWeakPtr.IsValid() ? ViewportManagerWeakPtr.Pin().Get() : nullptr;
}

FDisplayClusterViewportManagerProxy* FDisplayClusterDeviceBase::GetViewportManagerProxy_RenderThread() const
{
	return ViewportManagerProxyWeakPtr.IsValid() ? ViewportManagerProxyWeakPtr.Pin().Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterStereoDevice
//////////////////////////////////////////////////////////////////////////////////////////////

bool FDisplayClusterDeviceBase::Initialize()
{
	if (GDisplayCluster->GetOperationMode() == EDisplayClusterOperationMode::Disabled)
	{
		return false;
	}

	return true;
}

void FDisplayClusterDeviceBase::StartScene(UWorld* InWorld)
{
}

void FDisplayClusterDeviceBase::EndScene()
{
}

void FDisplayClusterDeviceBase::PreTick(float DeltaSeconds)
{
	if (!bIsCustomPresentSet)
	{
		// Set up our new present handler
		if (MainViewport)
		{
			// Current sync policy
			TSharedPtr<IDisplayClusterRenderSyncPolicy> SyncPolicy = GDisplayCluster->GetRenderMgr()->GetCurrentSynchronizationPolicy();
			check(SyncPolicy.IsValid());

			// Create present handler
			CustomPresentHandler = CreatePresentationObject(MainViewport, SyncPolicy);
			check(CustomPresentHandler);

			const FViewportRHIRef& MainViewportRHI = MainViewport->GetViewportRHI();

			if (MainViewportRHI)
			{
				MainViewportRHI->SetCustomPresent(CustomPresentHandler);
				bIsCustomPresentSet = true;
				GDisplayCluster->GetCallbacks().OnDisplayClusterCustomPresentSet().Broadcast();
			}
			else
			{
				UE_LOG(LogDisplayClusterRender, Error, TEXT("PreTick: MainViewport->GetViewportRHI() returned null reference"));
			}
		}
	}
}

IDisplayClusterPresentation* FDisplayClusterDeviceBase::GetPresentation() const
{
	return CustomPresentHandler;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IStereoRendering
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterDeviceBase::IsStereoEnabled() const
{
	return true;
}

bool FDisplayClusterDeviceBase::IsStereoEnabledOnNextFrame() const
{
	return true;
}

bool FDisplayClusterDeviceBase::EnableStereo(bool stereo /*= true*/)
{
	return true;
}

void FDisplayClusterDeviceBase::InitCanvasFromView(class FSceneView* InView, class UCanvas* Canvas)
{
	if (!bIsCustomPresentSet)
	{
		// Set up our new present handler
		if (MainViewport)
		{
			// Current sync policy
			TSharedPtr<IDisplayClusterRenderSyncPolicy> SyncPolicy = GDisplayCluster->GetRenderMgr()->GetCurrentSynchronizationPolicy();
			check(SyncPolicy.IsValid());

			// Create present handler
			CustomPresentHandler = CreatePresentationObject(MainViewport, SyncPolicy);
			check(CustomPresentHandler);

			MainViewport->GetViewportRHI()->SetCustomPresent(CustomPresentHandler);

			GDisplayCluster->GetCallbacks().OnDisplayClusterCustomPresentSet().Broadcast();
		}

		bIsCustomPresentSet = true;
	}
}

EStereoscopicPass FDisplayClusterDeviceBase::GetViewPassForIndex(bool bStereoRequested, int32 ViewIndex) const
{
	if (bStereoRequested)
	{
		if (IsInRenderingThread())
		{
			if (IDisplayClusterViewportManagerProxy* ViewportManagerProxy = GetViewportManagerProxy_RenderThread())
			{
				uint32 ViewportContextNum = 0;
				IDisplayClusterViewportProxy* ViewportProxy = ViewportManagerProxy->FindViewport_RenderThread(ViewIndex, &ViewportContextNum);
				if (ViewportProxy)
				{
					const FDisplayClusterViewport_Context& Context = ViewportProxy->GetContexts_RenderThread()[ViewportContextNum];
					return Context.StereoscopicPass;
				}
			}
		}
		else
		{
			if (IDisplayClusterViewportManager* ViewportManager = GetViewportManager())
			{
				uint32 ViewportContextNum = 0;
				IDisplayClusterViewport* ViewportPtr = ViewportManager->FindViewport(ViewIndex, &ViewportContextNum);
				if (ViewportPtr)
				{
					const FDisplayClusterViewport_Context& Context = ViewportPtr->GetContexts()[ViewportContextNum];
					return Context.StereoscopicPass;
				}
			}
		}
	}

	return EStereoscopicPass::eSSP_FULL;
}

void FDisplayClusterDeviceBase::AdjustViewRect(int32 ViewIndex, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const
{
	check(IsInGameThread());

	IDisplayClusterViewportManager* ViewportManager = GetViewportManager();
	if (ViewportManager == nullptr || ViewportManager->GetConfiguration().IsSceneOpened() == false)
	{
		return;
	}
	// ViewIndex == eSSE_MONOSCOPIC(-1) is a special case called for ISR culling math.
	// Since nDisplay is not ISR compatible, we ignore this request. This won't be neccessary once
	// we stop using nDisplay as a stereoscopic rendering device (IStereoRendering).
	else if (ViewIndex < 0)
	{
		return;
	}

	uint32 ViewportContextNum = 0;
	IDisplayClusterViewport* ViewportPtr = ViewportManager->FindViewport(ViewIndex, &ViewportContextNum);
	if (ViewportPtr == nullptr)
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Viewport StereoViewIndex='%i' not found"), ViewIndex);
		return;
	}

	const FIntRect& ViewRect = ViewportPtr->GetContexts()[ViewportContextNum].RenderTargetRect;

	X = ViewRect.Min.X;
	Y = ViewRect.Min.Y;

	SizeX = ViewRect.Width();
	SizeY = ViewRect.Height();

	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Adjusted view rect: Viewport='%s', ViewIndex=%d, [%d,%d - %d,%d]"), *ViewportPtr->GetId(), ViewportContextNum, ViewRect.Min.X, ViewRect.Min.Y, ViewRect.Max.X, ViewRect.Max.Y);
}

void FDisplayClusterDeviceBase::CalculateStereoViewOffset(const int32 ViewIndex, FRotator& ViewRotation, const float WorldToMeters, FVector& ViewLocation)
{
	check(IsInGameThread());
	check(WorldToMeters > 0.f);

	IDisplayClusterViewportManager* ViewportManager = GetViewportManager();

	uint32 ViewportContextNum = 0;
	IDisplayClusterViewport* ViewportPtr = ViewportManager ? ViewportManager->FindViewport(ViewIndex, &ViewportContextNum) : nullptr;
	if (ViewportPtr == nullptr)
	{
		return;
	}

	// The camera position has already been determined from the SetupViewPoint() function
	// Obtaining the offset of the stereo eye and the values of the projection clipping plane for the given viewport was moved inside CalculateView().
	// Perform view calculations on a policy side
	if (ViewportPtr->CalculateView(ViewportContextNum, ViewLocation, ViewRotation, WorldToMeters) == false)
	{
#if WITH_EDITOR
		// Hide spam in logs when configuring VP in editor [UE-114493]
		static const bool bIsEditorOperationMode = DeviceBaseHelpers::GetDisplayClusterAPI().GetOperationMode() == EDisplayClusterOperationMode::Editor;
		if (!bIsEditorOperationMode)
#endif
		{
			UE_LOG(LogDisplayClusterRender, Warning, TEXT("Couldn't compute view parameters for Viewport %s, ViewIdx: %d"), *ViewportPtr->GetId(), ViewportContextNum);
		}
	}

	UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("ViewLoc: %s, ViewRot: %s"), *ViewLocation.ToString(), *ViewRotation.ToString());
}

FMatrix FDisplayClusterDeviceBase::GetStereoProjectionMatrix(const int32 ViewIndex) const
{
	check(IsInGameThread());

	IDisplayClusterViewportManager* ViewportManager = GetViewportManager();

	FMatrix PrjMatrix = FMatrix::Identity;

	// ViewIndex == eSSE_MONOSCOPIC(-1) is a special case called for ISR culling math.
	// Since nDisplay is not ISR compatible, we ignore this request. This won't be neccessary once
	// we stop using nDisplay as a stereoscopic rendering device (IStereoRendering).
	if (ViewportManager && ViewportManager->GetConfiguration().IsSceneOpened() && ViewIndex >= 0)
	{
		uint32 ViewportContextNum = 0;
		IDisplayClusterViewport* ViewportPtr = ViewportManager->FindViewport(ViewIndex, &ViewportContextNum);
		if (ViewportPtr == nullptr)
		{
			UE_LOG(LogDisplayClusterRender, Warning, TEXT("Viewport StereoViewIndex='%i' not found"), ViewIndex);
		}
		else
		if (ViewportPtr->GetProjectionMatrix(ViewportContextNum, PrjMatrix) == false)
		{
			UE_LOG(LogDisplayClusterRender, Warning, TEXT("Got invalid projection matrix: Viewport %s, ViewIdx: %d"), *ViewportPtr->GetId(), ViewportContextNum);
		}
	}
	
	return PrjMatrix;
}

bool FDisplayClusterDeviceBase::BeginNewFrame(FViewport* InViewport, UWorld* InWorld, FDisplayClusterRenderFrame& OutRenderFrame)
{
	check(IsInGameThread());
	check(InViewport);

	if (ADisplayClusterRootActor* RootActor = DeviceBaseHelpers::GetDisplayClusterAPI().GetGameMgr()->GetRootActor())
	{
		if (IDisplayClusterViewportManager* ViewportManagerPtr = RootActor->GetOrCreateViewportManager())
		{
			const FString LocalNodeId = DeviceBaseHelpers::GetDisplayClusterAPI().GetConfigMgr()->GetLocalNodeId();

			// Get preview settings from RootActor properties
			FDisplayClusterViewport_PreviewSettings NewPreviewSettings = RootActor->GetPreviewSettings(true);
			NewPreviewSettings.bPreviewEnable = false;

			// Dont use preview setting on primary RootActor in game
			ViewportManagerPtr->GetConfiguration().SetPreviewSettings(NewPreviewSettings);

			// Update local node viewports (update\create\delete) and build new render frame
			if (ViewportManagerPtr->GetConfiguration().UpdateConfigurationForClusterNode(RenderFrameMode, InWorld, LocalNodeId))
			{
				if (ViewportManagerPtr->BeginNewFrame(InViewport, OutRenderFrame))
				{
					// update total number of views for this frame (in multiple families)
					DesiredNumberOfViews = OutRenderFrame.DesiredNumberOfViews;

					return true;
				}
			}
		}
	}

	return false;
}

void FDisplayClusterDeviceBase::InitializeNewFrame()
{
	check(IsInGameThread());

	if (ADisplayClusterRootActor* RootActor = DeviceBaseHelpers::GetDisplayClusterAPI().GetGameMgr()->GetRootActor())
	{
		if (IDisplayClusterViewportManager* ViewportManager = RootActor->GetOrCreateViewportManager())
		{
			// Begin use viewport manager for current frame
			ViewportManagerWeakPtr = ViewportManager->ToSharedPtr();

			// Initialize frame for render
			ViewportManager->InitializeNewFrame();

			FDisplayClusterViewportManagerProxy* ViewportManagerProxyPtr = static_cast<FDisplayClusterViewportManagerProxy*>(ViewportManager->GetProxy());

			// Send viewport manager proxy on render thread
			ENQUEUE_RENDER_COMMAND(DisplayClusterDevice_SetViewportManagerProxy)(
				[DCRenderDevice = SharedThis(this), NewViewportManagerProxy = ViewportManagerProxyPtr->AsShared()](FRHICommandListImmediate& RHICmdList)
				{
					DCRenderDevice->ViewportManagerProxyWeakPtr = NewViewportManagerProxy;
				});
		}
	}
}

void FDisplayClusterDeviceBase::FinalizeNewFrame()
{
	IDisplayClusterViewportManager* ViewportManager = GetViewportManager();
	if (ViewportManager)
	{
		ViewportManager->FinalizeNewFrame();
	}

	// reset viewport manager ptr on game thread
	ViewportManagerWeakPtr.Reset();
}

DECLARE_GPU_STAT_NAMED(nDisplay_Device_RenderTexture, TEXT("nDisplay RenderDevice::RenderTexture"));

void FDisplayClusterDeviceBase::RenderTexture_RenderThread(class FRDGBuilder& GraphBuilder, FRDGTextureRef BackBuffer, FRDGTextureRef SrcTexture, FVector2f WindowSize) const
{
	const FIntVector SrcSize = SrcTexture->Desc.GetSize();
	const FIntVector DstSize = BackBuffer->Desc.GetSize();

	FRHICopyTextureInfo CopyInfo;
	CopyInfo.Size.X = FMath::Min(SrcSize.X, DstSize.X);
	CopyInfo.Size.Y = FMath::Min(SrcSize.Y, DstSize.Y);

	AddCopyTexturePass(GraphBuilder, SrcTexture, BackBuffer, CopyInfo);

	if (RenderFrameMode == EDisplayClusterRenderFrameMode::Stereo)
	{
		if (FDisplayClusterViewportManagerProxy* ViewportManagerProxy = GetViewportManagerProxy_RenderThread())
		{
			// QuadBufStereo: Copy RIGHT_EYE to backbuffer
			ViewportManagerProxy->ImplResolveFrameTargetToBackBuffer_RenderThread(GraphBuilder, 1, 1, BackBuffer, WindowSize);
		}
	}

	const bool bClearTextureEnabled = CVarClearTextureEnabled.GetValueOnRenderThread() != 0;
	if (bClearTextureEnabled)
	{
		// Clear render target before out frame resolving, help to make things look better visually for console/resize, etc.
		AddClearRenderTargetPass(GraphBuilder, SrcTexture);
	}
};

//////////////////////////////////////////////////////////////////////////////////////////////
// IStereoRenderTargetManager
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterDeviceBase::UpdateViewport(bool bUseSeparateRenderTarget, const class FViewport& Viewport, class SViewport* ViewportWidget)
{
	// Store viewport
	if (!MainViewport)
	{
		// UE viewport
		MainViewport = (FViewport*)&Viewport;
	}
}

void FDisplayClusterDeviceBase::CalculateRenderTargetSize(const class FViewport& Viewport, uint32& InOutSizeX, uint32& InOutSizeY)
{
	InOutSizeX = FMath::Max(1, (int32)InOutSizeX);
	InOutSizeY = FMath::Max(1, (int32)InOutSizeY);
}

bool FDisplayClusterDeviceBase::NeedReAllocateViewportRenderTarget(const class FViewport& Viewport)
{
	check(IsInGameThread());

	// Get current RT size
	const FIntPoint rtSize = Viewport.GetRenderTargetTextureSizeXY();

	// Get desired RT size
	uint32 newSizeX = rtSize.X;
	uint32 newSizeY = rtSize.Y;

	CalculateRenderTargetSize(Viewport, newSizeX, newSizeY);

	// Here we conclude if need to re-allocate
	const bool Result = (newSizeX != rtSize.X || newSizeY != rtSize.Y);

	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Is reallocate viewport render target needed: %d"), Result ? 1 : 0);

	if (Result)
	{
		UE_LOG(LogDisplayClusterRender, Log, TEXT("Need to re-allocate render target: cur %d:%d, new %d:%d"), rtSize.X, rtSize.Y, newSizeX, newSizeY);
	}

	return Result;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterDeviceBase
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterDeviceBase::StartFinalPostprocessSettings(FPostProcessSettings* StartPostProcessingSettings, const enum EStereoscopicPass StereoPassType, const int32 StereoViewIndex)
{
	check(IsInGameThread());

	// eSSP_FULL pass reserved for UE internal render
	if (StereoPassType != EStereoscopicPass::eSSP_FULL && StartPostProcessingSettings)
	{
		IDisplayClusterViewportManager* ViewportManager = GetViewportManager();
		uint32 ContextNum = 0;
		if (IDisplayClusterViewport* Viewport = ViewportManager ? ViewportManager->FindViewport(StereoViewIndex, &ContextNum) : nullptr)
		{
			Viewport->GetViewport_CustomPostProcessSettings().ApplyCustomPostProcess(Viewport, ContextNum, IDisplayClusterViewport_CustomPostProcessSettings ::ERenderPass::Start, *StartPostProcessingSettings);
		}
	}
}

bool FDisplayClusterDeviceBase::OverrideFinalPostprocessSettings(FPostProcessSettings* OverridePostProcessingSettings, const enum EStereoscopicPass StereoPassType, const int32 StereoViewIndex, float& BlendWeight)
{
	check(IsInGameThread());

	// eSSP_FULL pass reserved for UE internal render
	if (StereoPassType != EStereoscopicPass::eSSP_FULL)
	{
		IDisplayClusterViewportManager* ViewportManager = GetViewportManager();
		uint32 ContextNum = 0;
		if (IDisplayClusterViewport* Viewport = ViewportManager ? ViewportManager->FindViewport(StereoViewIndex, &ContextNum) : nullptr)
		{
			return Viewport->GetViewport_CustomPostProcessSettings().ApplyCustomPostProcess(Viewport, ContextNum, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Override, *OverridePostProcessingSettings, &BlendWeight);
		}
	}

	return false;
}

void FDisplayClusterDeviceBase::EndFinalPostprocessSettings(FPostProcessSettings* FinalPostProcessingSettings, const enum EStereoscopicPass StereoPassType, const int32 StereoViewIndex)
{
	check(IsInGameThread());

	// eSSP_FULL pass reserved for UE internal render
	if (StereoPassType != EStereoscopicPass::eSSP_FULL)
	{
		IDisplayClusterViewportManager* ViewportManager = GetViewportManager();
		uint32 ContextNum = 0;
		if (IDisplayClusterViewport* Viewport = ViewportManager ? ViewportManager->FindViewport(StereoViewIndex, &ContextNum) : nullptr)
		{
			Viewport->GetViewport_CustomPostProcessSettings().ApplyCustomPostProcess(Viewport, ContextNum, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Final, *FinalPostProcessingSettings);
		}
	}
}
