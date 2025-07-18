// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteComposition.h"
#include "Rendering/NaniteResources.h"
#include "Rendering/NaniteStreamingManager.h"
#include "ScenePrivate.h"
#include "SystemTextures.h"
#include "PixelShaderUtils.h"
#include "PostProcess/SceneRenderTargets.h"

BEGIN_SHADER_PARAMETER_STRUCT(FDummyDepthDecompressParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepth)
END_SHADER_PARAMETER_STRUCT()

int32 GNaniteResummarizeHTile = 1;
static FAutoConsoleVariableRef CVarNaniteResummarizeHTile(
	TEXT("r.Nanite.ResummarizeHTile"),
	GNaniteResummarizeHTile,
	TEXT("")
);

int32 GNaniteDecompressDepth = 0;
static FAutoConsoleVariableRef CVarNaniteDecompressDepth(
	TEXT("r.Nanite.DecompressDepth"),
	GNaniteDecompressDepth,
	TEXT("")
);

int32 GNaniteCustomDepthExportMethod = 1;
static FAutoConsoleVariableRef CVarNaniteCustomDepthExportMethod(
	TEXT("r.Nanite.CustomDepth.ExportMethod"),
	GNaniteCustomDepthExportMethod,
	TEXT("0 - Export depth/stencil into separate targets via PS\n")
	TEXT("1 - Export depth/stencil direct to target via CS (requires HTILE support)\n")
);

#if WITH_EDITORONLY_DATA
extern int32 GNaniteIsolateInvalidCoarseMesh;
#endif

class FNaniteMarkStencilPS : public FNaniteGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNaniteMarkStencilPS);
	SHADER_USE_PARAMETER_STRUCT(FNaniteMarkStencilPS, FNaniteGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FNaniteMarkStencilPS, "/Engine/Private/Nanite/NaniteExportGBuffer.usf", "MarkStencilPS", SF_Pixel);

class FEmitSceneDepthPS : public FNaniteGlobalShader
{
	DECLARE_GLOBAL_SHADER(FEmitSceneDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitSceneDepthPS, FNaniteGlobalShader);

	class FVelocityExportDim : SHADER_PERMUTATION_BOOL("VELOCITY_EXPORT");
	class FShadingMaskExportDim : SHADER_PERMUTATION_BOOL("SHADING_MASK_EXPORT");
	class FSkinningDim : SHADER_PERMUTATION_BOOL("USE_SKINNING");
	using FPermutationDomain = TShaderPermutationDomain<FVelocityExportDim, FShadingMaskExportDim, FSkinningDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FSkinningDim>())
		{
			if (!NaniteSkinnedMeshesSupported())
			{
				return false;
			}

			if (!PermutationVector.Get<FVelocityExportDim>())
			{
				// Skinning is only needed when velocity will be exported
				return false;
			}
		}

		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_R32_UINT);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedView>, InViews)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FNaniteRasterBinMeta>, RasterBinMeta)
		SHADER_PARAMETER(FIntVector4, PageConstants)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, HierarchyBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		SHADER_PARAMETER(uint32, MeshPassIndex)
		SHADER_PARAMETER(uint32, RegularMaterialRasterBinCount)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitSceneDepthPS, "/Engine/Private/Nanite/NaniteExportGBuffer.usf", "EmitSceneDepthPS", SF_Pixel);

class FEmitSceneStencilPS : public FNaniteGlobalShader
{
	DECLARE_GLOBAL_SHADER(FEmitSceneStencilPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitSceneStencilPS, FNaniteGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADING_MASK_LOAD"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)

		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, PageConstants)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, HierarchyBuffer)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, ShadingMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitSceneStencilPS, "/Engine/Private/Nanite/NaniteExportGBuffer.usf", "EmitSceneStencilPS", SF_Pixel);

class FEmitCustomDepthStencilPS : public FNaniteGlobalShader
{
	DECLARE_GLOBAL_SHADER(FEmitCustomDepthStencilPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitCustomDepthStencilPS, FNaniteGlobalShader);

	class FWriteCustomStencilDim : SHADER_PERMUTATION_BOOL("WRITE_CUSTOM_STENCIL");
	using FPermutationDomain = TShaderPermutationDomain<FWriteCustomStencilDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FWriteCustomStencilDim>())
		{
			OutEnvironment.SetRenderTargetOutputFormat(0, PF_R16G16_UINT);
		}
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedView>, InViews)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, PageConstants)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, HierarchyBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CustomDepth)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint2>, CustomStencil)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitCustomDepthStencilPS, "/Engine/Private/Nanite/NaniteExportGBuffer.usf", "EmitCustomDepthStencilPS", SF_Pixel);

class FDepthExportCS : public FNaniteGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDepthExportCS);
	SHADER_USE_PARAMETER_STRUCT(FDepthExportCS, FNaniteGlobalShader);

	class FVelocityExportDim : SHADER_PERMUTATION_BOOL("VELOCITY_EXPORT");
	class FShadingMaskExportDim : SHADER_PERMUTATION_BOOL("SHADING_MASK_EXPORT");
	class FSkinningDim : SHADER_PERMUTATION_BOOL("USE_SKINNING");
	using FPermutationDomain = TShaderPermutationDomain<FVelocityExportDim, FShadingMaskExportDim, FSkinningDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FSkinningDim>())
		{
			if (!NaniteSkinnedMeshesSupported())
			{
				return false;
			}

			if (!PermutationVector.Get<FVelocityExportDim>())
			{
				// Skinning is only needed when velocity will be exported
				return false;
			}
		}

		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedView>, InViews)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FNaniteRasterBinMeta>, RasterBinMeta)
		SHADER_PARAMETER(FIntVector4, PageConstants)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, HierarchyBuffer)
		SHADER_PARAMETER(FIntVector4, DepthExportConfig)
		SHADER_PARAMETER(FUint32Vector4, ViewRect)
		SHADER_PARAMETER(uint32, bWriteCustomStencil)
		SHADER_PARAMETER(uint32, MeshPassIndex)
		SHADER_PARAMETER(uint32, RegularMaterialRasterBinCount)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Velocity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, ShadingMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTextureMetadata, SceneHTile)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, SceneStencil)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDepthExportCS, "/Engine/Private/Nanite/NaniteDepthExport.usf", "DepthExport", SF_Compute);

BEGIN_SHADER_PARAMETER_STRUCT(FNaniteMarkStencilRectsParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FPixelShaderUtils::FRasterizeToRectsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FNaniteMarkStencilPS::FParameters, PS)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FNaniteEmitDepthRectsParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FPixelShaderUtils::FRasterizeToRectsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FEmitSceneDepthPS::FParameters, PS)
END_SHADER_PARAMETER_STRUCT()

namespace Nanite
{

void EmitDepthTargets(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FViewInfo& View,
	bool bDrawSceneViewsInOneNanitePass,
	FRasterResults& RasterResults,
	FRDGTextureRef SceneDepth,
	FRDGTextureRef VelocityBuffer,
	FRDGTextureRef FirstStageDepthBuffer
)
{
	LLM_SCOPE_BYTAG(Nanite);
	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::EmitDepthTargets");

	FRDGTextureRef VisBuffer64 = RasterResults.VisBuffer64;
#if WITH_EDITORONLY_DATA
	// Hide all Nanite meshes when the isolate invalid coarse mesh batch debug mode is active.
	if (GNaniteIsolateInvalidCoarseMesh != 0)
	{
		const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);
		VisBuffer64 = SystemTextures.Black;
	}
#endif

	const FSceneTexturesConfig& Config = View.GetSceneTexturesConfig();
	const EShaderPlatform ShaderPlatform = View.GetShaderPlatform();
	const FIntPoint SceneTexturesExtent = Config.Extent;
	const FClearValueBinding DefaultDepthStencil = Config.DepthClearValue;

	float DefaultDepth = 0.0f;
	uint32 DefaultStencil = 0;
	DefaultDepthStencil.GetDepthStencil(DefaultDepth, DefaultStencil);

	const uint32 StencilDecalMask = GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1);

	const bool bEmitVelocity = VelocityBuffer != nullptr;
	const bool bClearVelocity = bEmitVelocity && !HasBeenProduced(VelocityBuffer);

	FRDGTextureDesc ShadingMaskDesc = FRDGTextureDesc::Create2D(
		SceneTexturesExtent,
		PF_R32_UINT,
		FClearValueBinding::Transparent,
		TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV);

	RasterResults.ShadingMask = GraphBuilder.CreateTexture(ShadingMaskDesc, TEXT("Nanite.ShadingMask"));

	RasterResults.ClearTileArgs = nullptr;
	RasterResults.ClearTileBuffer = nullptr;

	if (UseComputeDepthExport())
	{
		// NOTE: We intentionally skip calling AddClearRenderTargetPass on the ShadingMask here, since we will explicitly
		// write all pixels during the export depth pass below

		// Emit depth, stencil, mask and velocity

		if (GNaniteDecompressDepth != 0)
		{
			// Force depth decompression so the depth shader only processes decompressed surfaces
			FDummyDepthDecompressParameters* DecompressParams = GraphBuilder.AllocParameters<FDummyDepthDecompressParameters>();
			DecompressParams->SceneDepth = SceneDepth;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("NaniteDepthDecompress"),
				DecompressParams,
				ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
				[](FRDGAsyncTask, FRHICommandList&) {}
			);
		}

		const FIntRect ViewRect = bDrawSceneViewsInOneNanitePass ? View.GetFamilyViewRect() : View.ViewRect;
		const int32 kHTileSize = 8;
		checkf((ViewRect.Min.X % kHTileSize) == 0 && (ViewRect.Min.Y % kHTileSize) == 0, TEXT("Viewport rect must be %d-pixel aligned."), kHTileSize);

		const FIntVector DispatchDim = FComputeShaderUtils::GetGroupCount(ViewRect.Size(), kHTileSize);
		const uint32 PlatformConfig = RHIGetHTilePlatformConfig(SceneTexturesExtent.X, SceneTexturesExtent.Y);

		FRDGTextureUAVRef SceneDepthUAV			= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::CompressedSurface));
		FRDGTextureUAVRef SceneStencilUAV		= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::Stencil));
		FRDGTextureUAVRef SceneHTileUAV			= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::HTile));
		FRDGTextureUAVRef VelocityUAV			= bEmitVelocity ? GraphBuilder.CreateUAV(VelocityBuffer) : nullptr;
		FRDGTextureUAVRef ShadingMaskUAV		= GraphBuilder.CreateUAV(RasterResults.ShadingMask);

		FDepthExportCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDepthExportCS::FParameters>();

		PassParameters->View							= View.GetShaderParameters();
		PassParameters->Scene							= View.GetSceneUniforms().GetBuffer(GraphBuilder);
		PassParameters->InViews							= GraphBuilder.CreateSRV(RasterResults.ViewsBuffer);
		PassParameters->VisibleClustersSWHW				= GraphBuilder.CreateSRV(RasterResults.VisibleClustersSWHW);
		PassParameters->RasterBinMeta					= GraphBuilder.CreateSRV(RasterResults.RasterBinMeta);
		PassParameters->PageConstants					= RasterResults.PageConstants;
		PassParameters->ClusterPageData					= Nanite::GStreamingManager.GetClusterPageDataSRV(GraphBuilder);
		PassParameters->HierarchyBuffer					= Nanite::GStreamingManager.GetHierarchySRV(GraphBuilder);
		PassParameters->DepthExportConfig				= FIntVector4(PlatformConfig, SceneTexturesExtent.X, StencilDecalMask, Nanite::FGlobalResources::GetMaxVisibleClusters());
		PassParameters->ViewRect						= FUint32Vector4((uint32)ViewRect.Min.X, (uint32)ViewRect.Min.Y, (uint32)ViewRect.Max.X, (uint32)ViewRect.Max.Y);
		PassParameters->bWriteCustomStencil				= false;
		PassParameters->MeshPassIndex					= ENaniteMeshPass::BasePass;
		PassParameters->RegularMaterialRasterBinCount	= Scene.NaniteRasterPipelines[ENaniteMeshPass::BasePass].GetRegularBinCount();
		PassParameters->VisBuffer64						= VisBuffer64;
		PassParameters->Velocity						= VelocityUAV;
		PassParameters->ShadingMask						= ShadingMaskUAV;
		PassParameters->SceneHTile						= SceneHTileUAV;
		PassParameters->SceneDepth						= SceneDepthUAV;
		PassParameters->SceneStencil					= SceneStencilUAV;

		FDepthExportCS::FPermutationDomain PermutationVectorCS;
		PermutationVectorCS.Set<FDepthExportCS::FVelocityExportDim>(bEmitVelocity);
		PermutationVectorCS.Set<FDepthExportCS::FShadingMaskExportDim>(true);
		PermutationVectorCS.Set<FDepthExportCS::FSkinningDim>(bEmitVelocity && NaniteSkinnedMeshesSupported());
		auto ComputeShader = View.ShaderMap->GetShader<FDepthExportCS>(PermutationVectorCS);

		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("DepthExport"), ComputeShader, PassParameters, DispatchDim);
	}
	else
	{
		// Can't use ERenderTargetLoadAction::EClear to clear here because it needs to be the same for all render targets.
		AddClearRenderTargetPass(GraphBuilder, RasterResults.ShadingMask);
		if (bClearVelocity)
		{
			AddClearRenderTargetPass(GraphBuilder, VelocityBuffer);
		}

		// Emit scene depth buffer, mask and velocity
		{
			FEmitSceneDepthPS::FPermutationDomain PermutationVectorPS;
			PermutationVectorPS.Set<FEmitSceneDepthPS::FVelocityExportDim>(bEmitVelocity);
			PermutationVectorPS.Set<FEmitSceneDepthPS::FShadingMaskExportDim>(true);
			PermutationVectorPS.Set<FEmitSceneDepthPS::FSkinningDim>(bEmitVelocity && NaniteSkinnedMeshesSupported());
			auto  PixelShader = View.ShaderMap->GetShader<FEmitSceneDepthPS>(PermutationVectorPS);
			
			auto* PassParameters = GraphBuilder.AllocParameters<FEmitSceneDepthPS::FParameters>();

			PassParameters->View							= View.GetShaderParameters();
			PassParameters->Scene							= View.GetSceneUniforms().GetBuffer(GraphBuilder);
			PassParameters->InViews							= GraphBuilder.CreateSRV(RasterResults.ViewsBuffer);
			PassParameters->VisibleClustersSWHW				= GraphBuilder.CreateSRV(RasterResults.VisibleClustersSWHW);
			PassParameters->RasterBinMeta					= GraphBuilder.CreateSRV(RasterResults.RasterBinMeta);
			PassParameters->PageConstants					= RasterResults.PageConstants;
			PassParameters->VisBuffer64						= VisBuffer64;
			PassParameters->ClusterPageData					= Nanite::GStreamingManager.GetClusterPageDataSRV(GraphBuilder);
			PassParameters->HierarchyBuffer					= Nanite::GStreamingManager.GetHierarchySRV(GraphBuilder);
			PassParameters->MeshPassIndex					= ENaniteMeshPass::BasePass;
			PassParameters->RegularMaterialRasterBinCount	= Scene.NaniteRasterPipelines[ENaniteMeshPass::BasePass].GetRegularBinCount();
			PassParameters->RenderTargets[0]				= FRenderTargetBinding(RasterResults.ShadingMask, ERenderTargetLoadAction::ELoad);
			PassParameters->RenderTargets[1]				= bEmitVelocity ? FRenderTargetBinding(VelocityBuffer, ERenderTargetLoadAction::ELoad) : FRenderTargetBinding();
			PassParameters->RenderTargets.DepthStencil		= FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

			FPixelShaderUtils::AddFullscreenPass(
				GraphBuilder,
				View.ShaderMap,
				RDG_EVENT_NAME("Emit Scene Depth/Resolve/Velocity"),
				PixelShader,
				PassParameters,
				bDrawSceneViewsInOneNanitePass ? View.GetFamilyViewRect() : View.ViewRect,
				TStaticBlendState<>::GetRHI(),
				TStaticRasterizerState<>::GetRHI(),
				TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI()
			);
		}

		// Emit scene stencil
		{
			auto  PixelShader		= View.ShaderMap->GetShader<FEmitSceneStencilPS>();
			auto* PassParameters	= GraphBuilder.AllocParameters<FEmitSceneStencilPS::FParameters>();

			PassParameters->View						= View.ViewUniformBuffer;
			PassParameters->Scene						= View.GetSceneUniforms().GetBuffer(GraphBuilder);
			PassParameters->VisibleClustersSWHW			= GraphBuilder.CreateSRV(RasterResults.VisibleClustersSWHW);
			PassParameters->PageConstants				= RasterResults.PageConstants;
			PassParameters->ClusterPageData				= Nanite::GStreamingManager.GetClusterPageDataSRV(GraphBuilder);
			PassParameters->HierarchyBuffer				= Nanite::GStreamingManager.GetHierarchySRV(GraphBuilder);
			PassParameters->ShadingMask					= RasterResults.ShadingMask;
			PassParameters->VisBuffer64					= VisBuffer64;
			PassParameters->RenderTargets.DepthStencil	= FDepthStencilBinding
			(
				SceneDepth,
				ERenderTargetLoadAction::ELoad,
				FExclusiveDepthStencil::DepthWrite_StencilWrite
			);

			FPixelShaderUtils::AddFullscreenPass(
				GraphBuilder,
				View.ShaderMap,
				RDG_EVENT_NAME("Emit Scene Stencil"),
				PixelShader,
				PassParameters,
				View.ViewRect,
				TStaticBlendState<>::GetRHI(),
				TStaticRasterizerState<>::GetRHI(),
				TStaticDepthStencilState<false, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI(),
				StencilDecalMask | GET_STENCIL_BIT_MASK(DISTANCE_FIELD_REPRESENTATION, 1)
			);
		}

		if (GRHISupportsResummarizeHTile && GNaniteResummarizeHTile != 0)
		{
			// Resummarize HTile meta data if the RHI supports it
			AddResummarizeHTilePass(GraphBuilder, SceneDepth);
		}
	}

	// Also emit Nanite to first stage depth buffer, only if necessary for this frame.
	// It is not perfect, nanite cluster will have been HZB culled against Niagara opaque meshes that are not in the FirstStage depth buffer, so there will be holes.
	// Still an improvement for Niagara opaque particles colliding with depth buffer, and validated by artists.
	// Only using the PS path right now until we can optimise for platforms supporting write through UAV.
	if (FirstStageDepthBuffer)
	{
		FEmitSceneDepthPS::FPermutationDomain PermutationVectorPS;
		PermutationVectorPS.Set<FEmitSceneDepthPS::FVelocityExportDim>(false);
		PermutationVectorPS.Set<FEmitSceneDepthPS::FShadingMaskExportDim>(false);
		PermutationVectorPS.Set<FEmitSceneDepthPS::FSkinningDim>(false);
		auto  PixelShader = View.ShaderMap->GetShader<FEmitSceneDepthPS>(PermutationVectorPS);

		auto* PassParameters = GraphBuilder.AllocParameters<FEmitSceneDepthPS::FParameters>();

		PassParameters->View = View.GetShaderParameters();
		PassParameters->Scene = View.GetSceneUniforms().GetBuffer(GraphBuilder);
		PassParameters->InViews = GraphBuilder.CreateSRV(RasterResults.ViewsBuffer);
		PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(RasterResults.VisibleClustersSWHW);
		PassParameters->RasterBinMeta = GraphBuilder.CreateSRV(RasterResults.RasterBinMeta);
		PassParameters->PageConstants = RasterResults.PageConstants;
		PassParameters->VisBuffer64 = VisBuffer64;
		PassParameters->ClusterPageData = Nanite::GStreamingManager.GetClusterPageDataSRV(GraphBuilder);
		PassParameters->HierarchyBuffer = Nanite::GStreamingManager.GetHierarchySRV(GraphBuilder);
		PassParameters->MeshPassIndex = ENaniteMeshPass::BasePass;
		PassParameters->RegularMaterialRasterBinCount = Scene.NaniteRasterPipelines[ENaniteMeshPass::BasePass].GetRegularBinCount();
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(FirstStageDepthBuffer, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilNop);

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			View.ShaderMap,
			RDG_EVENT_NAME("Emit Scene First Stage Depth"),
			PixelShader,
			PassParameters,
			bDrawSceneViewsInOneNanitePass ? View.GetFamilyViewRect() : View.ViewRect,
			TStaticBlendState<>::GetRHI(),
			TStaticRasterizerState<>::GetRHI(),
			TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI()
		);

		if (GRHISupportsResummarizeHTile && GNaniteResummarizeHTile != 0)
		{
			// Resummarize HTile meta data if the RHI supports it
			AddResummarizeHTilePass(GraphBuilder, FirstStageDepthBuffer);
		}
	}
}

FCustomDepthContext InitCustomDepthStencilContext(
	FRDGBuilder& GraphBuilder,
	const FCustomDepthTextures& CustomDepthTextures,
	bool bWriteCustomStencil
)
{
	enum ECustomDepthExportMethod
	{
		DepthExportSeparatePS,	// Emit depth & stencil from PS (Stencil separated and written to RT0)
		DepthExportCS			// Emit depth & stencil from CS with HTILE (requires RHI support)
	};

	check(CustomDepthTextures.IsValid());

	FCustomDepthContext Output;
	Output.bComputeExport = UseComputeDepthExport() && GNaniteCustomDepthExportMethod == DepthExportCS;

	if (Output.bComputeExport)
	{
		if (!CustomDepthTextures.Depth->HasBeenProduced())
		{
			AddClearDepthStencilPass(GraphBuilder, CustomDepthTextures.Depth, CustomDepthTextures.DepthAction, CustomDepthTextures.StencilAction);
		}

		Output.InputDepth = CustomDepthTextures.Depth;
		Output.InputStencilSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateWithPixelFormat(CustomDepthTextures.Depth, PF_X24_G8));

		// We can output directly to the depth target using compute
		Output.DepthTarget = CustomDepthTextures.Depth;
		Output.StencilTarget = bWriteCustomStencil ? CustomDepthTextures.Depth : nullptr;
	}
	else
	{
		if (CustomDepthTextures.Depth->HasBeenProduced())
		{
			Output.InputDepth = CustomDepthTextures.Depth;
			Output.InputStencilSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateWithPixelFormat(CustomDepthTextures.Depth, PF_X24_G8));
		}
		else
		{
			// Custom depth buffer hasn't been produced (because no non-Nanite has been rendered to it), so just composite Nanite
			// custom depth with blank 1x1 textures
			const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);
			Output.InputDepth = SystemTextures.DepthDummy;
			Output.InputStencilSRV = SystemTextures.StencilDummySRV;
		}

		// Since we cannot output the stencil ref from the pixel shader, we'll combine Nanite and non-Nanite custom depth/stencil
		// into new, separate targets. Note that stencil test using custom stencil from this point will require tests to be
		// performed manually in the pixel shader (see PostProcess materials, for example).
		const FIntPoint CustomDepthExtent = CustomDepthTextures.Depth->Desc.Extent;
		FRDGTextureDesc OutCustomDepthDesc = FRDGTextureDesc::Create2D(
			CustomDepthExtent,
			PF_DepthStencil,
			FClearValueBinding::DepthFar,
			TexCreate_DepthStencilTargetable | TexCreate_ShaderResource);
		Output.DepthTarget = GraphBuilder.CreateTexture(OutCustomDepthDesc, TEXT("CombinedCustomDepth"));

		if (bWriteCustomStencil)
		{
			FRDGTextureDesc OutCustomStencilDesc = FRDGTextureDesc::Create2D(
				CustomDepthExtent,
				PF_R16G16_UINT, //PF_R8G8_UINT,
				FClearValueBinding::Transparent,
				TexCreate_RenderTargetable | TexCreate_ShaderResource);

			Output.StencilTarget = GraphBuilder.CreateTexture(OutCustomStencilDesc, TEXT("CombinedCustomStencil"));
		}
	}

	return Output;
}

void EmitCustomDepthStencilTargets(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FViewInfo& View,
	bool bDrawSceneViewsInOneNanitePass,
	const FIntVector4& PageConstants,
	FRDGBufferRef VisibleClustersSWHW,
	FRDGBufferRef ViewsBuffer,
	FRDGTextureRef VisBuffer64,
	const FCustomDepthContext& CustomDepthContext
)
{
	LLM_SCOPE_BYTAG(Nanite);
	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::EmitCustomDepthStencilTargets");

	FRDGTextureRef CustomDepth = CustomDepthContext.InputDepth;
	FRDGTextureSRVRef CustomStencilSRV = CustomDepthContext.InputStencilSRV;
	const bool bWriteCustomStencil = CustomDepthContext.StencilTarget != nullptr;

	if (CustomDepthContext.bComputeExport)
	{
		// Emit custom depth and stencil from a CS that can handle HTILE
		if (GNaniteDecompressDepth != 0)
		{
			// Force depth decompression so the depth shader only processes decompressed surfaces
			FDummyDepthDecompressParameters* DecompressParams = GraphBuilder.AllocParameters<FDummyDepthDecompressParameters>();
			DecompressParams->SceneDepth = CustomDepth;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("NaniteCustomDepthDecompress"),
				DecompressParams,
				ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
				[](FRDGAsyncTask, FRHICommandList&) {}
			);
		}

		const FIntRect ViewRect = bDrawSceneViewsInOneNanitePass ? View.GetFamilyViewRect() : View.ViewRect;
		const int32 kHTileSize = 8;
		checkf((ViewRect.Min.X % kHTileSize) == 0 && (ViewRect.Min.Y % kHTileSize) == 0, TEXT("Viewport rect must be %d-pixel aligned."), kHTileSize);

		// Export depth
		{
			const FIntPoint CustomDepthExtent = CustomDepth->Desc.Extent;
			const FIntVector DispatchDim = FComputeShaderUtils::GetGroupCount(ViewRect.Size(), kHTileSize);
			const uint32 PlatformConfig = RHIGetHTilePlatformConfig(CustomDepthExtent.X, CustomDepthExtent.Y);

			FRDGTextureUAVRef CustomDepthUAV		= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(CustomDepth, ERDGTextureMetaDataAccess::CompressedSurface));
			FRDGTextureUAVRef CustomStencilUAV		= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(CustomDepth, ERDGTextureMetaDataAccess::Stencil));
			FRDGTextureUAVRef CustomHTileUAV		= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(CustomDepth, ERDGTextureMetaDataAccess::HTile));

			FDepthExportCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDepthExportCS::FParameters>();

			PassParameters->View					= View.GetShaderParameters();
			PassParameters->Scene					= View.GetSceneUniforms().GetBuffer(GraphBuilder);
			PassParameters->InViews					= GraphBuilder.CreateSRV(ViewsBuffer);
			PassParameters->VisibleClustersSWHW		= GraphBuilder.CreateSRV(VisibleClustersSWHW);
			PassParameters->PageConstants			= PageConstants;
			PassParameters->ClusterPageData			= Nanite::GStreamingManager.GetClusterPageDataSRV(GraphBuilder);
			PassParameters->HierarchyBuffer			= Nanite::GStreamingManager.GetHierarchySRV(GraphBuilder);
			PassParameters->DepthExportConfig		= FIntVector4(PlatformConfig, CustomDepthExtent.X, 0, Nanite::FGlobalResources::GetMaxVisibleClusters());
			PassParameters->ViewRect				= FUint32Vector4((uint32)ViewRect.Min.X, (uint32)ViewRect.Min.Y, (uint32)ViewRect.Max.X, (uint32)ViewRect.Max.Y);
			PassParameters->bWriteCustomStencil		= bWriteCustomStencil;
			PassParameters->MeshPassIndex			= ENaniteMeshPass::BasePass;
			PassParameters->VisBuffer64				= VisBuffer64;
			PassParameters->Velocity				= nullptr;
			PassParameters->ShadingMask				= nullptr;
			PassParameters->SceneHTile				= CustomHTileUAV;
			PassParameters->SceneDepth				= CustomDepthUAV;
			PassParameters->SceneStencil			= CustomStencilUAV;

			FDepthExportCS::FPermutationDomain PermutationVectorCS;
			PermutationVectorCS.Set<FDepthExportCS::FVelocityExportDim>(false);
			PermutationVectorCS.Set<FDepthExportCS::FShadingMaskExportDim>(false);
			PermutationVectorCS.Set<FDepthExportCS::FSkinningDim>(false);
			auto ComputeShader = View.ShaderMap->GetShader<FDepthExportCS>(PermutationVectorCS);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("DepthExport"),
				ComputeShader,
				PassParameters,
				DispatchDim
			);
		}
	}
	else // DepthExportSeparatePS
	{
		FRDGTextureRef OutCustomDepth = CustomDepthContext.DepthTarget;
		FRDGTextureRef OutCustomStencil = CustomDepthContext.StencilTarget;

		FEmitCustomDepthStencilPS::FPermutationDomain PermutationVectorPS;
		PermutationVectorPS.Set<FEmitCustomDepthStencilPS::FWriteCustomStencilDim>(bWriteCustomStencil);
		auto PixelShader = View.ShaderMap->GetShader<FEmitCustomDepthStencilPS>(PermutationVectorPS);

		auto* PassParameters = GraphBuilder.AllocParameters<FEmitCustomDepthStencilPS::FParameters>();

		// If we aren't emitting stencil, clear it so it's not garbage
		ERenderTargetLoadAction StencilLoadAction = OutCustomStencil ? ERenderTargetLoadAction::ENoAction : ERenderTargetLoadAction::EClear;

		PassParameters->View						= View.ViewUniformBuffer;
		PassParameters->Scene						= View.GetSceneUniforms().GetBuffer(GraphBuilder);
		PassParameters->InViews						= GraphBuilder.CreateSRV(ViewsBuffer);
		PassParameters->VisibleClustersSWHW			= GraphBuilder.CreateSRV(VisibleClustersSWHW);
		PassParameters->PageConstants				= PageConstants;
		PassParameters->VisBuffer64					= VisBuffer64;
		PassParameters->ClusterPageData				= Nanite::GStreamingManager.GetClusterPageDataSRV(GraphBuilder);
		PassParameters->HierarchyBuffer				= Nanite::GStreamingManager.GetHierarchySRV(GraphBuilder);
		PassParameters->CustomDepth					= CustomDepth;
		PassParameters->CustomStencil				= CustomStencilSRV;
		PassParameters->RenderTargets[0]			= OutCustomStencil ? FRenderTargetBinding(OutCustomStencil, ERenderTargetLoadAction::ENoAction) : FRenderTargetBinding();
		PassParameters->RenderTargets.DepthStencil	= FDepthStencilBinding(OutCustomDepth, ERenderTargetLoadAction::ENoAction, StencilLoadAction, FExclusiveDepthStencil::DepthWrite_StencilNop);

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			View.ShaderMap,
			OutCustomStencil ? RDG_EVENT_NAME("Emit Custom Depth/Stencil") : RDG_EVENT_NAME("Emit Custom Depth"),
			PixelShader,
			PassParameters,
			bDrawSceneViewsInOneNanitePass ? View.GetFamilyViewRect() : View.ViewRect,
			TStaticBlendState<>::GetRHI(),
			TStaticRasterizerState<>::GetRHI(),
			TStaticDepthStencilState<true, CF_Always>::GetRHI()
		);
	}
}

void FinalizeCustomDepthStencil(
	FRDGBuilder& GraphBuilder,
	const FCustomDepthContext& CustomDepthContext,
	FCustomDepthTextures& OutTextures
)
{
	OutTextures.Depth = CustomDepthContext.DepthTarget;

	if (CustomDepthContext.StencilTarget)
	{
		if (CustomDepthContext.bComputeExport)
		{
			// we wrote straight to the depth/stencil buffer
			OutTextures.Stencil = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateWithPixelFormat(CustomDepthContext.StencilTarget, PF_X24_G8));
		}
		else
		{
			// separate stencil texture
			OutTextures.Stencil = GraphBuilder.CreateSRV(CustomDepthContext.StencilTarget);
		}
	}
	else
	{
		OutTextures.Stencil = CustomDepthContext.InputStencilSRV;
	}

	OutTextures.bSeparateStencilBuffer = !CustomDepthContext.bComputeExport;
}

void MarkSceneStencilRects(
	FRDGBuilder& GraphBuilder,
	const FRasterContext& RasterContext,
	FScene& Scene,
	FViewInfo* SharedView,
	FIntPoint ViewportSize,
	uint32 NumRects,
	FRDGBufferSRVRef RectMinMaxBufferSRV,
	FRDGTextureRef DepthAtlasTexture
)
{
	FNaniteMarkStencilRectsParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteMarkStencilRectsParameters>();

	PassParameters->PS.View = SharedView->ViewUniformBuffer;
	PassParameters->PS.VisBuffer64 = RasterContext.VisBuffer64;

	PassParameters->PS.RenderTargets.DepthStencil = FDepthStencilBinding(
		DepthAtlasTexture,
		ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ELoad,
		FExclusiveDepthStencil::DepthRead_StencilWrite
	);

	auto PixelShader = SharedView->ShaderMap->GetShader<FNaniteMarkStencilPS>();

	FPixelShaderUtils::AddRasterizeToRectsPass(GraphBuilder,
		SharedView->ShaderMap,
		RDG_EVENT_NAME("Mark Stencil"),
		PixelShader,
		PassParameters,
		ViewportSize,
		RectMinMaxBufferSRV,
		NumRects,
		TStaticBlendState<>::GetRHI(),
		TStaticRasterizerState<>::GetRHI(),
		TStaticDepthStencilState<false, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI(),
		STENCIL_SANDBOX_MASK
	);
}

void EmitSceneDepthRects(
	FRDGBuilder& GraphBuilder,
	const FRasterContext& RasterContext,
	FScene& Scene,
	FViewInfo* SharedView,
	FIntPoint ViewportSize,
	uint32 NumRects,
	FRDGBufferSRVRef RectMinMaxBufferSRV,
	FRDGTextureRef DepthAtlasTexture
)
{
	FNaniteEmitDepthRectsParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteEmitDepthRectsParameters>();

	PassParameters->PS.VisBuffer64 = RasterContext.VisBuffer64;
	PassParameters->PS.RenderTargets.DepthStencil = FDepthStencilBinding(
		DepthAtlasTexture,
		ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ELoad,
		FExclusiveDepthStencil::DepthWrite_StencilRead
	);

	FEmitSceneDepthPS::FPermutationDomain PermutationVectorPS;
	PermutationVectorPS.Set<FEmitSceneDepthPS::FVelocityExportDim>(false);
	PermutationVectorPS.Set<FEmitSceneDepthPS::FShadingMaskExportDim>(false);
	PermutationVectorPS.Set<FEmitSceneDepthPS::FSkinningDim>(false);
	auto PixelShader = SharedView->ShaderMap->GetShader<FEmitSceneDepthPS>(PermutationVectorPS);

	FPixelShaderUtils::AddRasterizeToRectsPass(GraphBuilder,
		SharedView->ShaderMap,
		RDG_EVENT_NAME("Emit Depth"),
		PixelShader,
		PassParameters,
		ViewportSize,
		RectMinMaxBufferSRV,
		NumRects,
		TStaticBlendState<>::GetRHI(),
		TStaticRasterizerState<>::GetRHI(),
		TStaticDepthStencilState<true, CF_Always, true, CF_Equal>::GetRHI(),
		STENCIL_SANDBOX_MASK
	);
}

} // Nanite
