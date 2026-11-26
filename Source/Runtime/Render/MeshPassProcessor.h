#pragma once

#include "Render/RenderPacket.h"
#include "Render/PipelineState.h"
#include "Render/ObjectConstants.h"
#include "Render/RenderEnums.h"
#include "Render/Buffer.h"
#include "Render/RenderCommand.h"
#include "Containers/Array.h"
#include "Math/Rect.h"
#include "Render/LightRenderData.h"
#include <array>
#include <Engine\PrimitiveSceneProxy.h>

struct FSceneView;
struct FSceneRenderData;
class FScene;
struct FLightRenderParameters;

// 한 패스에서 함께 바인딩할 GPU 상태. UObject 메시/재질 포인터는 보관하지 않는다.
struct FMeshDrawBatch
{
	struct FTextureBinding
	{
		UTexture2D* Texture = nullptr;
		EShaderBindFlagBits Stages = EShaderBindFlagBits::Pixel;
	};

	struct FItem
	{
		std::array<uint8, sizeof(FForwardObjectConstants)> ObjectData{};
		uint32 ObjectDataSize = 0;
		uint32 ObjectSlot = InvalidObjectSlot;
		uint32 StartIndex = 0;
		uint32 IndexCount = 0;
		uint32 InstanceCount = 0; // 0이면 인스턴싱하지 않는다
		const void* ObjectKey = nullptr;
		bool bOccludedByGpu = false;
		bool bReceivesDecals = true;
		FAABB Bounds{};
		const FPrimitiveSceneProxy* Proxy = nullptr;
	};

	FPipelineState Pipeline;

	// 정점/인덱스/본/인스턴스 스트림. 소유자(메시, 프록시, FRenderQueue)가 이 배치를 그릴 때까지 살아 있다
	const FVertexFactory* VertexFactory = nullptr;
	// 메시 파티클 인스턴스 버퍼를 올리지 못했을 때 인스턴스마다 그리는 Local VS
	FVertexShader* MeshParticleFallbackVS = nullptr;

	EVertexFactoryType GetVertexFactoryType() const
	{
		return VertexFactory ? VertexFactory->GetType() : EVertexFactoryType::Local;
	}
	FIndexBuffer* GetIndexBuffer() const { return VertexFactory ? VertexFactory->GetIndexBuffer() : nullptr; }
	// 메시 파티클이면 인스턴스 배열, 아니면 빈 span
	std::span<const FMeshParticleInstanceVertex> GetMeshParticleInstances() const
	{
		if (GetVertexFactoryType() != EVertexFactoryType::MeshParticle) return {};
		return static_cast<const FMeshParticleVertexFactory*>(VertexFactory)->GetInstances();
	}

	TArray<FTextureBinding> Textures;
	ESamplerState Sampler = ESamplerState::LinearClamp;
	EShaderBindFlagBits SamplerStages = EShaderBindFlagBits::Pixel;
	std::array<uint8, sizeof(FForwardMaterialConstants)> MaterialData{};
	uint32 MaterialDataSize = 0;
	uint32 MaterialSlot = 1;
	EShaderBindFlagBits MaterialStages = EShaderBindFlagBits::Pixel;
	uint32 ObjectBindingSlot = 2;
	EShaderBindFlagBits ObjectStages = EShaderBindFlagBits::Vertex;
	bool bUseObjectSlots = true;
	bool bBindViewConstants = true;
	bool bForceWireframe = false;
	bool bTwoSided = false;
	// Masked 머티리얼: 깊이 프리패스도 알파로 픽셀을 버려야 하므로 전용 깊이 PS를 쓴다
	bool bIsMasked = false;
	TArray<FItem> Items;
};

struct FOcclusionMeasureResult
{
	bool bValid = false;
	uint32 TotalObjects = 0;
	uint32 VisibleObjects = 0;
	uint32 TotalDraws = 0;
	uint32 VisibleDraws = 0;
	uint64 TotalTriangles = 0;
	uint64 VisibleTriangles = 0;
	double ElapsedMs = 0.0;
	uint32 GPUOccludedDraws = 0;
	uint32 FalseCulls = 0;
};

enum class EMeshPassType : uint8
{
	Opaque,
	GBuffer,
	Forward,
	Translucent,
	WorldNormal,
	ShadowDepth,
	TranslucentBeforeDOF,
	TranslucentAfterDOF,
};

// CPU에서 프록시/패킷을 패스별 GPU 배치로 변환한다. GPU 명령은 호출하지 않는다.
class FMeshPassProcessor
{
public:
	bool Build(const FSceneView& View, const FRenderQueue& Queue,
		const FSceneRenderData* SceneData, EMeshPassType Pass,
		TArray<FMeshDrawBatch>& Out, ELightingModel LightingModel = ELightingModel::Gouraud) const;

	// PreTranslation: 그림자 원점 기준으로 옮길 때 World 이동에 더하는 값 (언리얼 PreShadowTranslation). 큰 좌표끼리의 뺄셈을 GPU가 아니라 CPU에서 한다.
	bool Build(const TArray<FPrimitiveSceneProxy*>& ShadowMeshes, EPSOType PSOType, TArray<FMeshDrawBatch>& OutBatches,
		const FVector& PreTranslation = FVector::ZeroVector);
};
