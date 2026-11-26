#include "EnginePCH.h"
#include "Core/Misc/Paths.h"
#include "Render/PostProcess/ExposureMeterRenderer.h"
#include "Render/RenderCommand.h"
#include "Render/RenderDevice.h"
#include "Render/RenderResourceManager.h"
#include "Render/Texture2D.h"
#include "Engine/PostProcessSettings.h"
#include "Core/EngineTimer.h"

#include <DirectXPackedVector.h>
#include <algorithm>
#include <cmath>

void FExposureMeterRenderer::Init(FRenderer* InRenderer)
{
	Renderer = InRenderer;
	Shader = FRenderResourceManager::GetShaderProgram(HITORI_ENGINE_SHADER("ExposureMeter.hlsl"));

	PipelineState.Shader = Shader;
	PipelineState.Topology = EPrimitiveTopology::TriangleList;
	PipelineState.RasterizerState = ERasterizerState::SolidNone;
	PipelineState.BlendState = EBlendState::Opaque;
	PipelineState.DepthStencilState = EDepthStencilState::Disabled;

	FTextureDesc Desc{};
	Desc.Width = TargetSize;
	Desc.Height = TargetSize;
	Desc.Format = ETextureFormat::R16G16_Float;
	Desc.BindFlags = ETextureBindFlags::RenderTarget | ETextureBindFlags::ShaderResource;
	Target = RenderCommand::CreateTexture2D(Desc);

	ID3D11Device* Device = FRenderDevice::Get()->GetDevice();
	D3D11_TEXTURE2D_DESC MipDesc{};
	MipDesc.Width = TargetSize;
	MipDesc.Height = TargetSize;
	MipDesc.MipLevels = 0;
	MipDesc.ArraySize = 1;
	MipDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
	MipDesc.SampleDesc.Count = 1;
	MipDesc.Usage = D3D11_USAGE_DEFAULT;
	MipDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	MipDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
	Device->CreateTexture2D(&MipDesc, nullptr, MipChain.GetAddressOf());
	if (MipChain)
		Device->CreateShaderResourceView(MipChain.Get(), nullptr, MipChainSRV.GetAddressOf());
	LastMip = static_cast<uint32>(std::log2(static_cast<float>(TargetSize)));

	D3D11_TEXTURE2D_DESC StagingDesc{};
	StagingDesc.Width = 1;
	StagingDesc.Height = 1;
	StagingDesc.MipLevels = 1;
	StagingDesc.ArraySize = 1;
	StagingDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
	StagingDesc.SampleDesc.Count = 1;
	StagingDesc.Usage = D3D11_USAGE_STAGING;
	StagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	for (auto& Texture : Staging)
		Device->CreateTexture2D(&StagingDesc, nullptr, Texture.GetAddressOf());

	assert(Shader && Target && MipChain && MipChainSRV);
}

bool FExposureMeterRenderer::Render(FTexture2D* SceneHDR)
{
	if (!Shader || !SceneHDR || !Target || !MipChain || !Staging[WriteIndex]) return false;

	RenderCommand::BindPipelineState(PipelineState);
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::BindSamplerState(0, ESamplerState::LinearClamp, EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(0, SceneHDR, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(3);
	RenderCommand::UnbindShaderResources(0, 1, EShaderBindFlagBits::Pixel);

	ID3D11DeviceContext* Context = FRenderDevice::Get()->GetContext();
	Context->CopySubresourceRegion(MipChain.Get(), 0, 0, 0, 0, Target->GetRawPtr(), 0, nullptr);
	Context->GenerateMips(MipChainSRV.Get());
	Context->CopySubresourceRegion(Staging[WriteIndex].Get(), 0, 0, 0, 0, MipChain.Get(), LastMip, nullptr);
	bStagingPending[WriteIndex] = true;
	WriteIndex = (WriteIndex + 1) % NumStaging;

	ReadBack();
	return true;
}

void FExposureMeterRenderer::ReadBack()
{
	ID3D11DeviceContext* Context = FRenderDevice::Get()->GetContext();
	const uint32 ReadIndex = WriteIndex;
	if (!bStagingPending[ReadIndex]) return;

	D3D11_MAPPED_SUBRESOURCE Mapped{};
	if (FAILED(Context->Map(Staging[ReadIndex].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &Mapped)))
		return;

	const uint16* Half = static_cast<const uint16*>(Mapped.pData);
	const float WeightedLogL = DirectX::PackedVector::XMConvertHalfToFloat(Half[0]);
	const float Weight = DirectX::PackedVector::XMConvertHalfToFloat(Half[1]);
	Context->Unmap(Staging[ReadIndex].Get(), 0);
	bStagingPending[ReadIndex] = false;

	if (Weight > 0.0f)
	{
		AverageLuminance = std::exp2(WeightedLogL / Weight);
		bHasReading = true;
	}
}

bool FExposureMeterRenderer::GetAverageLuminance(float& OutLuminance)
{
	OutLuminance = AverageLuminance;
	return bHasReading;
}

float FExposureMeterRenderer::UpdateAdaptedEV100(const FPostProcessSettings& Settings)
{
	const float MinEV = std::min(Settings.AutoExposureMinEV100, Settings.AutoExposureMaxEV100);
	const float MaxEV = std::max(Settings.AutoExposureMinEV100, Settings.AutoExposureMaxEV100);
	const float TargetEV = std::clamp(bHasReading ? FPostProcessSettings::GetMeteredEV100(AverageLuminance, DisplayGamma)
		: Settings.GetCameraEV100(), MinEV, MaxEV);

	const float Now = EngineTimer::GetTotalTime();
	if (!bHasAdapted)
	{
		AdaptedEV100 = TargetEV;
		bHasAdapted = true;
	}
	else if (Now != LastAdaptTime)
	{
		const float Dt = std::clamp(Now - LastAdaptTime, 0.0f, 0.1f);
		const float Speed = TargetEV < AdaptedEV100 ? Settings.AutoExposureSpeedUp : Settings.AutoExposureSpeedDown;
		AdaptedEV100 += (TargetEV - AdaptedEV100) * (1.0f - std::exp(-Dt * Speed));
	}
	LastAdaptTime = Now;
	return AdaptedEV100;
}
