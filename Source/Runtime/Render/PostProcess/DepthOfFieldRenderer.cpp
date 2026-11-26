#include "EnginePCH.h"
#include "Core/Misc/Paths.h"
#include "Render/PostProcess/DepthOfFieldRenderer.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/SceneView.h"
#include "Render/Texture2D.h"
#include <cmath>

namespace
{
	FPipelineState MakeFullscreenPipeline(FShaderProgram* Shader)
	{
		FPipelineState State;
		State.Shader = Shader;
		State.Topology = EPrimitiveTopology::TriangleList;
		State.RasterizerState = ERasterizerState::SolidNone;
		State.BlendState = EBlendState::Opaque;
		State.DepthStencilState = EDepthStencilState::Disabled;
		return State;
	}
}

void FDepthOfFieldRenderer::Init(FRenderer* InRenderer)
{
	Renderer = InRenderer;
	SetupShader = FRenderResourceManager::GetShaderProgram(HITORI_ENGINE_SHADER("DepthOfFieldSetup.hlsl"));
	VisualizeShader = FRenderResourceManager::GetShaderProgram(HITORI_ENGINE_SHADER("DepthOfFieldVisualize.hlsl"));
	DownsampleShader = FRenderResourceManager::GetShaderProgram(HITORI_ENGINE_SHADER("DepthOfFieldDownsample.hlsl"));
	GatherShader = FRenderResourceManager::GetShaderProgram(HITORI_ENGINE_SHADER("DepthOfFieldGather.hlsl"));
	PostFilterShader = FRenderResourceManager::GetShaderProgram(HITORI_ENGINE_SHADER("DepthOfFieldPostFilter.hlsl"));
	CompositeShader = FRenderResourceManager::GetShaderProgram(HITORI_ENGINE_SHADER("DepthOfFieldComposite.hlsl"));
	SetupPipelineState = MakeFullscreenPipeline(SetupShader);
	VisualizePipelineState = MakeFullscreenPipeline(VisualizeShader);
	DownsamplePipelineState = MakeFullscreenPipeline(DownsampleShader);
	GatherPipelineState = MakeFullscreenPipeline(GatherShader);
	PostFilterPipelineState = MakeFullscreenPipeline(PostFilterShader);
	CompositePipelineState = MakeFullscreenPipeline(CompositeShader);
	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FDepthOfFieldData));
	GatherConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FDepthOfFieldGatherData));
	assert(SetupShader && VisualizeShader && DownsampleShader && GatherShader && PostFilterShader && CompositeShader && ConstantBuffer && GatherConstantBuffer);
}

bool FDepthOfFieldRenderer::RenderSetup(const FSceneView& View, FTexture2D* DepthTarget)
{
	if (!SetupShader || !ConstantBuffer || !DepthTarget) return false;

	const FPostProcessSettings& PP = View.FinalPostProcessSettings;
	FDepthOfFieldData Data{};
	Data.ZScale = View.ProjectionMatrix.M[0][2];
	Data.ZOffset = View.ProjectionMatrix.M[3][2];
	Data.FocalDistance = PP.DepthOfFieldFocalDistance;
	Data.FocusRange = PP.DepthOfFieldFocusRange;
	Data.Fstop = std::max(PP.DepthOfFieldFstop, 0.5f);
	Data.ViewWidthPx = View.ViewRect.Width;
	// Hitori 월드 단위는 미터(m)이므로 센서 폭(mm)에 0.001을 곱해 미터로 변환한다
	constexpr float MillimetersToWorld = 0.001f;
	Data.FocalLength = PP.DepthOfFieldSensorWidth * 0.5f * std::abs(View.ProjectionMatrix.M[1][0]) * MillimetersToWorld;
	Data.SensorWidth = PP.DepthOfFieldSensorWidth * MillimetersToWorld;

	RenderCommand::BindPipelineState(SetupPipelineState);
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Data, sizeof(Data));
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(0, DepthTarget, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(6);
	RenderCommand::BindShaderResource(0, static_cast<FTexture2D*>(nullptr), EShaderBindFlagBits::Pixel);
	return true;
}

bool FDepthOfFieldRenderer::RenderVisualize(FTexture2D* CoCTexture, FTexture2D* SceneColorCopy)
{
	if (!VisualizeShader || !CoCTexture || !SceneColorCopy) return false;

	RenderCommand::BindPipelineState(VisualizePipelineState);
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::BindShaderResource(0, CoCTexture, EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(1, SceneColorCopy, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(6);
	RenderCommand::UnbindShaderResources(0, 2, EShaderBindFlagBits::Pixel);
	return true;
}

bool FDepthOfFieldRenderer::DrawFullscreen(const FPipelineState& Pipeline, std::initializer_list<FTexture2D*> Textures)
{
	if (!Pipeline.Shader) return false;
	for (FTexture2D* Texture : Textures)
		if (!Texture) return false;

	RenderCommand::BindPipelineState(Pipeline);
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::BindSamplerState(0, ESamplerState::LinearClamp, EShaderBindFlagBits::Pixel);
	uint32 Slot = 0;
	for (FTexture2D* Texture : Textures)
		RenderCommand::BindShaderResource(Slot++, Texture, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(6);
	RenderCommand::UnbindShaderResources(0, Slot, EShaderBindFlagBits::Pixel);
	return true;
}

bool FDepthOfFieldRenderer::RenderDownsample(FTexture2D* SceneColorCopy, FTexture2D* CoCTexture)
{
	return DrawFullscreen(DownsamplePipelineState, { SceneColorCopy, CoCTexture });
}

bool FDepthOfFieldRenderer::RenderGather(const FSceneView& View, FTexture2D* HalfResTexture)
{
	if (!GatherConstantBuffer) return false;

	FDepthOfFieldGatherData Data{};
	Data.BladeCount = static_cast<float>(std::clamp(View.FinalPostProcessSettings.DepthOfFieldBladeCount, 4, 16));
	RenderCommand::UpdateBufferData(GatherConstantBuffer.get(), &Data, sizeof(Data));
	RenderCommand::BindConstantBuffer(0, GatherConstantBuffer.get(), EShaderBindFlagBits::Pixel);
	return DrawFullscreen(GatherPipelineState, { HalfResTexture });
}

bool FDepthOfFieldRenderer::RenderPostFilter(FTexture2D* FarTexture, FTexture2D* NearTexture)
{
	return DrawFullscreen(PostFilterPipelineState, { FarTexture, NearTexture });
}

bool FDepthOfFieldRenderer::RenderComposite(FTexture2D* SceneColorCopy, FTexture2D* CoCTexture, FTexture2D* FarTexture, FTexture2D* NearTexture)
{
	return DrawFullscreen(CompositePipelineState, { SceneColorCopy, CoCTexture, FarTexture, NearTexture });
}
