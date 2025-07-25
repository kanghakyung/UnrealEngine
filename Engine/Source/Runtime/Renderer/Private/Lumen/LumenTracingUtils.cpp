// Copyright Epic Games, Inc. All Rights Reserved.

#include "LumenTracingUtils.h"
#include "ScenePrivate.h"
#include "SceneRendering.h"
#include "SystemTextures.h"

static TAutoConsoleVariable<float> CVarLumenSkylightLeakingRoughness(
	TEXT("r.Lumen.SkylightLeaking.Roughness"),
	0.3f,
	TEXT("Roughness used to sample the skylight leaking cubemap.  A value of 0 gives no prefiltering of the skylight leaking, while larger values can be useful to hide sky features in the leaking."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarLumenSkylightLeakingReflectionAverageAlbedo(
	TEXT("r.Lumen.SkylightLeaking.ReflectionAverageAlbedo"),
	0.25f,
	TEXT("Average albedo when approximating Skyleaking for reflection hit points."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenSampleFog(
	TEXT("r.Lumen.SampleFog"),
	0,
	TEXT("Sample the fog contribution in Lumen tracing. Disabled by default."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

void GetLumenCardTracingParameters(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View, 
	const FLumenSceneData& LumenSceneData,
	const FLumenSceneFrameTemporaries& FrameTemporaries,
	bool bSurfaceCacheFeedback,
	FLumenCardTracingParameters& TracingParameters)
{
	LLM_SCOPE_BYTAG(Lumen);

	TracingParameters.View = View.ViewUniformBuffer;
	TracingParameters.Scene = View.GetSceneUniforms().GetBuffer(GraphBuilder);
	TracingParameters.LumenCardScene = FrameTemporaries.LumenCardSceneUniformBuffer;
	TracingParameters.ReflectionStruct = CreateReflectionUniformBuffer(GraphBuilder, View);
	
	TracingParameters.DiffuseColorBoost = 1.0f / FMath::Max(View.FinalPostProcessSettings.LumenDiffuseColorBoost, 1.0f);
	TracingParameters.SkylightLeakingColor = FMath::Max(View.FinalPostProcessSettings.LumenSkylightLeaking, 0.0f) * FVector3f(View.FinalPostProcessSettings.LumenSkylightLeakingTint);
	TracingParameters.ReflectionSkylightLeakingAverageAlbedo = FMath::Max(CVarLumenSkylightLeakingReflectionAverageAlbedo.GetValueOnRenderThread(), 0.0f);
	TracingParameters.SkylightLeakingRoughness = CVarLumenSkylightLeakingRoughness.GetValueOnRenderThread();
	TracingParameters.InvFullSkylightLeakingDistance = 1.0f / FMath::Clamp<float>(View.FinalPostProcessSettings.LumenFullSkylightLeakingDistance, .1f, Lumen::GetMaxTraceDistance(View));
	TracingParameters.CachedLightingPreExposure = Lumen::GetCachedLightingPreExposure();
	TracingParameters.OneOverCachedLightingPreExposure = 1.0f / Lumen::GetCachedLightingPreExposure();

	TracingParameters.SampleHeightFog = CVarLumenSampleFog.GetValueOnRenderThread() > 0 ? 1u : 0u;
	TracingParameters.FogUniformParameters = CreateFogUniformBuffer(GraphBuilder, View);

	const FScene* Scene = ((const FScene*)View.Family->Scene);

	if (FrameTemporaries.CardPageLastUsedBufferUAV && FrameTemporaries.CardPageHighResLastUsedBufferUAV)
	{
		TracingParameters.RWCardPageLastUsedBuffer = FrameTemporaries.CardPageLastUsedBufferUAV;
		TracingParameters.RWCardPageHighResLastUsedBuffer = FrameTemporaries.CardPageHighResLastUsedBufferUAV;
	}
	else
	{
		TracingParameters.RWCardPageLastUsedBuffer = GraphBuilder.CreateUAV(GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("Lumen.DummyCardPageLastUsedBuffer")));
		TracingParameters.RWCardPageHighResLastUsedBuffer = GraphBuilder.CreateUAV(GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("Lumen.DummyCardPageHighResLastUsedBuffer")));
	}

	// Lumen surface cache feedback
	if (FrameTemporaries.SurfaceCacheFeedbackResources.BufferUAV && bSurfaceCacheFeedback)
	{
		TracingParameters.RWSurfaceCacheFeedbackBufferAllocator = FrameTemporaries.SurfaceCacheFeedbackResources.BufferAllocatorUAV;
		TracingParameters.RWSurfaceCacheFeedbackBuffer = FrameTemporaries.SurfaceCacheFeedbackResources.BufferUAV;
		TracingParameters.SurfaceCacheFeedbackBufferSize = FrameTemporaries.SurfaceCacheFeedbackResources.BufferSize;
		TracingParameters.SurfaceCacheFeedbackBufferTileJitter = LumenSceneData.SurfaceCacheFeedback.GetFeedbackBufferTileJitter();
		TracingParameters.SurfaceCacheFeedbackBufferTileWrapMask = Lumen::GetFeedbackBufferTileWrapMask();
	}
	else
	{
		TracingParameters.RWSurfaceCacheFeedbackBufferAllocator = LumenSceneData.SurfaceCacheFeedback.GetDummyFeedbackAllocatorUAV(GraphBuilder);
		TracingParameters.RWSurfaceCacheFeedbackBuffer = LumenSceneData.SurfaceCacheFeedback.GetDummyFeedbackUAV(GraphBuilder);
		TracingParameters.SurfaceCacheFeedbackBufferSize = 0;
		TracingParameters.SurfaceCacheFeedbackBufferTileJitter = FIntPoint(0, 0);
		TracingParameters.SurfaceCacheFeedbackBufferTileWrapMask = 0;
	}

	extern float GLumenSurfaceCacheFeedbackResLevelBias;
	TracingParameters.SurfaceCacheFeedbackResLevelBias = GLumenSurfaceCacheFeedbackResLevelBias + 0.5f; // +0.5f required for uint to float rounding in shader
	TracingParameters.SurfaceCacheUpdateFrameIndex = Scene->GetLumenSceneData(View)->GetSurfaceCacheUpdateFrameIndex();

	// Lumen surface cache atlas
	TracingParameters.DirectLightingAtlas = FrameTemporaries.DirectLightingAtlas ? FrameTemporaries.DirectLightingAtlas : GSystemTextures.GetBlackDummy(GraphBuilder);
	TracingParameters.IndirectLightingAtlas = FrameTemporaries.IndirectLightingAtlas ? FrameTemporaries.IndirectLightingAtlas : GSystemTextures.GetBlackDummy(GraphBuilder);
	TracingParameters.FinalLightingAtlas = FrameTemporaries.FinalLightingAtlas ? FrameTemporaries.FinalLightingAtlas : GSystemTextures.GetBlackDummy(GraphBuilder);
	if (FrameTemporaries.TileShadowDownsampleFactorAtlas)
	{
		TracingParameters.TileShadowDownsampleFactorAtlasForResampling = GraphBuilder.CreateSRV(FrameTemporaries.TileShadowDownsampleFactorAtlas, PF_R32G32B32A32_UINT);
	}
	else
	{
		FRDGBufferRef DummyBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 4), TEXT("Lumen.DummyTileShadowDownsampleFactorAtlas"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DummyBuffer, PF_R32G32B32A32_UINT), 0);
		TracingParameters.TileShadowDownsampleFactorAtlasForResampling = GraphBuilder.CreateSRV(DummyBuffer, PF_R32G32B32A32_UINT);
	}
	TracingParameters.AlbedoAtlas = FrameTemporaries.AlbedoAtlas ? FrameTemporaries.AlbedoAtlas : GSystemTextures.GetBlackDummy(GraphBuilder);
	TracingParameters.OpacityAtlas = FrameTemporaries.OpacityAtlas ? FrameTemporaries.OpacityAtlas : GSystemTextures.GetBlackDummy(GraphBuilder);
	TracingParameters.NormalAtlas = FrameTemporaries.NormalAtlas ? FrameTemporaries.NormalAtlas : GSystemTextures.GetBlackDummy(GraphBuilder);
	TracingParameters.EmissiveAtlas = FrameTemporaries.EmissiveAtlas ? FrameTemporaries.EmissiveAtlas : GSystemTextures.GetBlackDummy(GraphBuilder);
	TracingParameters.DepthAtlas = FrameTemporaries.DepthAtlas ? FrameTemporaries.DepthAtlas : GSystemTextures.GetBlackDummy(GraphBuilder);

	if (View.GlobalDistanceFieldInfo.PageObjectGridBuffer)
	{
		TracingParameters.GlobalDistanceFieldPageObjectGridBuffer = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(View.GlobalDistanceFieldInfo.PageObjectGridBuffer));
	}
	else
	{
		TracingParameters.GlobalDistanceFieldPageObjectGridBuffer = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(FVector4f)));
	}

	TracingParameters.NumGlobalSDFClipmaps = View.GlobalDistanceFieldInfo.Clipmaps.Num();
}
