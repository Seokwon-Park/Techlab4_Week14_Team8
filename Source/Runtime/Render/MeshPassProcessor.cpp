#include "EnginePCH.h"
#include "Render/MeshPassProcessor.h"
#include "Render/SceneRenderData.h"
#include "Render/SceneView.h"
#include "Render/RenderResourceManager.h"
#include "Render/Mesh.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Core/EngineTimer.h"
#include <algorithm>
#include <cstring>

static const FAABB ConservativeBounds = FAABB(FVector(0.0f, 0.0f, 0.0f), FVector(FLT_MAX / 4, FLT_MAX / 4, FLT_MAX / 4));

namespace
{
	float GetNormalEncoding(UTexture2D* Texture)
	{
		if (!Texture || !Texture->GetResource() || !Texture->GetResource()->GetSRV()) return 0.0f;
		switch (Texture->GetResource()->GetFormat())
		{
		case DXGI_FORMAT_BC5_UNORM: return 2.0f;
		case DXGI_FORMAT_BC5_SNORM: return 3.0f;
		default: return 1.0f;
		}
	}

	bool NeedsSRGBDecode(UTexture2D* Texture)
	{
		if (!Texture || !Texture->GetResource()) return false;
		switch (Texture->GetResource()->GetFormat())
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC7_UNORM:
			return true;
		default:
			return false;
		}
	}

	const FMatrix& PacketWorld(const FRenderPacket& Packet)
	{
		if (Packet.Proxy) return Packet.Proxy->GetLocalToWorld();
		return Packet.Model ? *Packet.Model : FMatrix::Identity;
	}

	uint64 GroupKey(const FStaticDrawGroup& Group)
	{
		return (static_cast<uint64>(Group.Material->SortID) << 47)
			| (static_cast<uint64>(Group.Mesh->SortID) << 31)
			| (static_cast<uint64>(Group.LODIndex & 0x3) << 29);
	}

	bool BuildBatch(FMeshDrawBatch& Batch, const FVertexFactory* VertexFactory,
		UMaterial* Material, EMeshPassType Pass, const FSceneView& View, ELightingModel LightingModel,
		const void* OverrideParams = nullptr, uint32 OverrideSize = 0)
	{
		if (!VertexFactory || !VertexFactory->IsValid() || !Material) return false;
		Batch.VertexFactory = VertexFactory;
		const bool bUsesUberShader = Pass == EMeshPassType::Forward || Pass == EMeshPassType::WorldNormal;
		if (bUsesUberShader)
		{
			const bool bWorldNormal = Pass == EMeshPassType::WorldNormal;
			FPipelineState* Base = FRenderResourceManager::GetPSO(bWorldNormal
				? EPSOType::StaticMesh_WorldNormal : EPSOType::StaticMesh_Forward);
			// Unlit 머티리얼은 뷰의 조명 모델과 상관없이 Unlit 변형으로 그린다
			FShaderProgram* Shader = bWorldNormal ? (Base ? Base->Shader : nullptr)
				: FRenderResourceManager::GetForwardShaderProgram(Material->IsUnlit() ? ELightingModel::Unlit : LightingModel);
			if (!Base || !Shader || !Shader->VertexShader || !Shader->PixelShader) return false;
			Batch.Pipeline = *Base;
			Batch.Pipeline.Shader = Shader;
			Batch.bUseObjectSlots = false;
			Batch.bBindViewConstants = false;
			Batch.ObjectBindingSlot = 0;
			Batch.ObjectStages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
			Batch.MaterialSlot = 2;
			Batch.MaterialStages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
			Batch.SamplerStages = Batch.MaterialStages;
			Batch.bForceWireframe = !bWorldNormal && View.bIsWireframe;

			FForwardMaterialConstants Params{};
			Params.BaseColor = Material->BaseColor;
			Params.SpecularColorShininess = FVector4(Material->SpecularColor.X,
				Material->SpecularColor.Y, Material->SpecularColor.Z, std::max(1.0f, Material->Shininess));
			UTexture2D* Color = Material->Textures.IsEmpty() ? nullptr : Material->Textures[0];
			UTexture2D* Normal = Material->Textures.Num() > 1 ? Material->Textures[1] : nullptr;
			const bool bHasColor = Color && Color->GetResource() && Color->GetResource()->GetSRV();
			// y: Masked 알파 기준값(0이면 끔). UberLit.hlsl의 SurfaceParams와 같은 순서
			Params.SurfaceParams = FVector4(Material->bFlipNormalY ? 1.0f : 0.0f, Material->GetMaskClipValue(),
				bHasColor ? 1.0f : 0.0f, GetNormalEncoding(Normal));
			const FVector2 UV = Material->UVScrollSpeed * EngineTimer::GetTotalTime();
			Params.UVTransform = FVector4(UV.X, UV.Y, 1.0f, 1.0f);
			std::memcpy(Batch.MaterialData.data(), &Params, sizeof(Params));
			Batch.MaterialDataSize = sizeof(Params);
		}
		else
		{
			EPSOType Type = Material->PSOType;
			if (Pass == EMeshPassType::GBuffer && Type == EPSOType::StaticMesh_Opaque)
				Type = EPSOType::StaticMesh_GBuffer;
			else if (View.bIsWireframe && Type == EPSOType::StaticMesh_Opaque)
				Type = EPSOType::StaticMesh_Wireframe;
			FPipelineState* Pipeline = FRenderResourceManager::GetPSO(Type);
			if (!Pipeline || !Pipeline->Shader) return false;
			Batch.Pipeline = *Pipeline;
			Batch.bForceWireframe = Pass == EMeshPassType::GBuffer && View.bIsWireframe;
			if (Material->PSOType == EPSOType::StaticMesh_Opaque ||
				Material->PSOType == EPSOType::StaticMesh_Translucent ||
				Material->PSOType == EPSOType::StaticMesh_Additive ||
				Material->PSOType == EPSOType::StaticMesh_Wireframe)
			{
				FStaticMeshMaterialParams Params{};
				Params.BaseColor = Material->BaseColor;
				Params.UVOffset = Material->UVScrollSpeed * EngineTimer::GetTotalTime();
				Params.MaskParams.X = Material->GetMaskClipValue();
				// y: Unlit이면 1. GBuffer 셰이더가 노멀을 0으로 써서 Deferred 조명이 알베도를 그대로 내보내게 한다
				Params.MaskParams.Y = Material->IsUnlit() ? 1.0f : 0.0f;
				if (Material->PSOType == EPSOType::StaticMesh_Translucent || Material->PSOType == EPSOType::StaticMesh_Additive)
				{
					UTexture2D* Color = Material->Textures.IsEmpty() ? nullptr : Material->Textures[0];
					Params.TextureFlags.X = Pass == EMeshPassType::TranslucentBeforeDOF && NeedsSRGBDecode(Color) ? 1.0f : 0.0f;
				}
				else
				{
					Params.TextureFlags.X = GetNormalEncoding(Material->Textures.Num() > 1 ? Material->Textures[1] : nullptr);
					Params.TextureFlags.Y = Material->bFlipNormalY ? 1.0f : 0.0f;
				}
				std::memcpy(Batch.MaterialData.data(), &Params, sizeof(Params));
				Batch.MaterialDataSize = sizeof(Params);
			}
			else if (OverrideParams && OverrideSize <= Batch.MaterialData.size())
			{
				std::memcpy(Batch.MaterialData.data(), OverrideParams, OverrideSize);
				Batch.MaterialDataSize = OverrideSize;
			}
			// 새로 추가: 스프라이트 VS가 b1의 카메라 축을 읽는다
			if (Material->PSOType == EPSOType::ParticleSprite_AlphaBlend ||
				Material->PSOType == EPSOType::ParticleSprite_Additive)
			{
				Batch.MaterialStages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
			}
		}
		// 그림자 깊이 패스는 전용 래스터라이저 상태(깊이 클립 설정)를 그대로 쓴다
		Batch.bTwoSided = Material->bTwoSided && Pass != EMeshPassType::ShadowDepth;
		Batch.bIsMasked = Material->IsMasked();
		Batch.Sampler = Material->SamplerState;
		Batch.Textures.Reserve(Material->Textures.Num());
		for (int32 Index = 0; Index < Material->Textures.Num(); ++Index)
		{
			// Forward 조명은 t8 이상을 전역 리소스에 사용한다.
			if (bUsesUberShader && Index >= 8) break;
			UTexture2D* Texture = Material->Textures[Index];
			EShaderBindFlagBits Stages = EShaderBindFlagBits::Pixel;
			if (bUsesUberShader && Index == 1)
				Stages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
			if (bUsesUberShader && Index == 0 &&
				(!Texture || !Texture->GetResource() || !Texture->GetResource()->GetSRV()))
				Texture = nullptr;
			if (bUsesUberShader && Index == 1 && GetNormalEncoding(Texture) <= 0.5f)
				Texture = nullptr;
			Batch.Textures.Add({ Texture, Stages });
		}
		return true;
	}

	// 배치의 셰이더를 정점 타입(GPU 스킨 등)에 맞는 변형으로 바꾼다. 메시 파티클은 호출하는 쪽이 따로 처리한다
	bool ApplyVertexFactoryShader(FMeshDrawBatch& Batch)
	{
		const EVertexFactoryType Type = Batch.GetVertexFactoryType();
		if (Type == EVertexFactoryType::MeshParticle) return true;
		FShaderProgram* Resolved = FRenderResourceManager::ResolveShader(Batch.Pipeline.Shader, Type);
		if (!Resolved) return false;
		Batch.Pipeline.Shader = Resolved;
		return true;
	}

	FMeshDrawBatch::FItem BuildItem(const FMatrix& World, const FSceneView& View,
		EMeshPassType Pass, uint32 Slot, uint32 StartIndex, uint32 IndexCount,
		const void* ObjectKey, bool bOccludedByGpu, bool bReceiveShadow, bool bReceivesDecals, const FAABB &Bounds,
		const FPrimitiveSceneProxy* Proxy)
	{
		FMeshDrawBatch::FItem Item{};
		Item.ObjectSlot = Slot;
		Item.StartIndex = StartIndex;
		Item.IndexCount = IndexCount;
		Item.ObjectKey = ObjectKey;
		Item.bOccludedByGpu = bOccludedByGpu;
		Item.bReceivesDecals = bReceivesDecals;
		Item.Bounds = Bounds;
		Item.Proxy = Proxy;
		if (Pass == EMeshPassType::Forward || Pass == EMeshPassType::WorldNormal)
		{
			FForwardObjectConstants Constants{};
			Constants.World = World;
			Constants.View = View.ViewMatrix;
			Constants.Projection = View.ProjectionMatrix;
			Constants.NormalMatrix = World.Inverse().GetTransposed();
			Constants.ShadowFlags.X = static_cast<float>(bReceiveShadow);
			std::memcpy(Item.ObjectData.data(), &Constants, sizeof(Constants));
			Item.ObjectDataSize = sizeof(Constants);
		}
		else
		{
			const FPerObjectConstants Constants = MakePerObjectConstants(World, bReceiveShadow);
			std::memcpy(Item.ObjectData.data(), &Constants, sizeof(Constants));
			Item.ObjectDataSize = sizeof(Constants);
		}
		return Item;
	}

	FMeshDrawBatch::FItem BuildBatchItem(const FMatrix& World, const FMatrix& View, const FMatrix& Projection,
		uint32 Slot, uint32 StartIndex, uint32 IndexCount,
		const void* ObjectKey, bool bOccludedByGpu)
	{
		FMeshDrawBatch::FItem Item{};
		Item.ObjectSlot = Slot;
		Item.StartIndex = StartIndex;
		Item.IndexCount = IndexCount;
		Item.ObjectKey = ObjectKey;
		Item.bOccludedByGpu = bOccludedByGpu;

		FForwardObjectConstants Constants{};
		Constants.World = World;
		Constants.View = View;
		Constants.Projection = Projection;
		Constants.NormalMatrix = World.Inverse().GetTransposed();
		std::memcpy(Item.ObjectData.data(), &Constants, sizeof(Constants));
		Item.ObjectDataSize = sizeof(Constants);

		return Item;
	}

	FMeshDrawBatch::FItem MakeShadowItem(const FPrimitiveSceneProxy* Proxy, uint32 StartIndex, uint32 IndexCount,
		const FVector& PreTranslation)
	{
		FMeshDrawBatch::FItem Item{};
		Item.StartIndex = StartIndex;
		Item.IndexCount = IndexCount;
		Item.ObjectKey = Proxy;

		// ShadowDepth.hlsl은 법선(NormalMatrix)으로 기울기 bias를 구하므로 오브젝트 상수 전체를 올린다
		FPerObjectConstants Constants = MakePerObjectConstants(Proxy->GetLocalToWorld());
		Constants.World.M[3][0] += PreTranslation.X;
		Constants.World.M[3][1] += PreTranslation.Y;
		Constants.World.M[3][2] += PreTranslation.Z;
		std::memcpy(Item.ObjectData.data(), &Constants, sizeof(Constants));
		Item.ObjectDataSize = sizeof(Constants);
		return Item;
	}
}

bool FMeshPassProcessor::Build(const FSceneView& View, const FRenderQueue& Queue,
	const FSceneRenderData* SceneData, EMeshPassType Pass,
	TArray<FMeshDrawBatch>& Out, ELightingModel LightingModel) const
{
	Out.Reset();
	const bool bBeforeDOF = Pass == EMeshPassType::TranslucentBeforeDOF;
	const bool bAfterDOF = Pass == EMeshPassType::TranslucentAfterDOF;
	const bool bTranslucentPass = Pass == EMeshPassType::Translucent || bBeforeDOF || bAfterDOF;
	if (!bTranslucentPass && SceneData)
	{
		TArray<const FStaticDrawGroup*> Groups;
		for (const FSceneRenderData::FGatherChunk& Chunk : SceneData->GatherChunks)
			for (const FStaticDrawGroup& Group : Chunk.Groups)
				if (!Group.Items.empty()) Groups.Add(&Group);

		std::sort(Groups.begin(), Groups.end(),
			[](const FStaticDrawGroup* A, const FStaticDrawGroup* B) { return GroupKey(*A) < GroupKey(*B); }); // 재질, 메시, LOD 순으로 정렬

		for (const FStaticDrawGroup* Group : Groups)
		{
			FMeshDrawBatch Batch; // Render에게 전달하는 최종 구조체
			if (!BuildBatch(Batch, Group->Mesh->GetVertexFactory(Group->LODIndex), Group->Material, Pass, View, LightingModel))
				return false;

			Batch.Items.Reserve(static_cast<uint32>(Group->Items.size()));

			for (const FStaticDrawItem& Item : Group->Items)
				Batch.Items.Add(BuildItem(Item.Proxy->GetLocalToWorld(), View, Pass, Item.Slot,
					Item.StartIndex, Item.IndexCount, Item.Proxy, Item.bOccludedByGpu != 0, Item.Proxy->bReceiveShadow,
					Item.Proxy->bReceivesDecals, Item.Proxy->GetBounds(), Item.Proxy));
			Out.Add(std::move(Batch));
		}
	}
	const uint32 Begin = bTranslucentPass ? Queue.GetFirstTranslucentIndex() : 0;
	const uint32 End = bTranslucentPass ? Queue.Num() : Queue.GetFirstTranslucentIndex();
	Out.Reserve(Out.Num() + End - Begin);
	for (uint32 Index = Begin; Index < End; ++Index)
	{
		const FRenderPacket& Packet = Queue[Index];
		if (bBeforeDOF || bAfterDOF)
		{
			const bool bMaterialBeforeDOF = Packet.Material && Packet.Material->ShouldRenderBeforeDOF();
			if (bMaterialBeforeDOF != bBeforeDOF) continue;
		}
		if (Pass == EMeshPassType::WorldNormal &&
			(!Packet.Material || Packet.Material->PSOType != EPSOType::StaticMesh_Opaque)) continue;
		FMeshDrawBatch Batch;
		if (!BuildBatch(Batch, Packet.GetVertexFactory(), Packet.Material, Pass, View,
			LightingModel, Packet.MaterialParamData, Packet.MaterialParamDataSize)) return false;
		if (!ApplyVertexFactoryShader(Batch)) continue;

		const uint32 IndexCount = Packet.IndexCount ? Packet.IndexCount : Batch.GetIndexBuffer()->GetIndexCount();
		const void* ObjectKey = Packet.Proxy ? static_cast<const void*>(Packet.Proxy) : static_cast<const void*>(&Packet);
		const std::span<const FMeshParticleInstanceVertex> MeshParticleInstances = Batch.GetMeshParticleInstances();
		if (Batch.GetVertexFactoryType() == EVertexFactoryType::MeshParticle)
		{
			if (MeshParticleInstances.empty()) continue;
			FShaderProgram* Variant = FRenderResourceManager::ResolveShader(Batch.Pipeline.Shader, EVertexFactoryType::MeshParticle);
			if (Pass == EMeshPassType::Forward)
			{
				// Forward 데칼이 같은 배치를 다시 그리므로 데칼 셰이더에도 인스턴싱 변형이 있어야 한다
				const FPipelineState* Decal = FRenderResourceManager::GetPSO(EPSOType::Decal);
				if (Decal && Decal->Shader && !FRenderResourceManager::ResolveShader(Decal->Shader, EVertexFactoryType::MeshParticle))
					Variant = nullptr;
			}
			if (Variant)
			{
				Batch.MeshParticleFallbackVS = Batch.Pipeline.Shader->VertexShader;
				Batch.Pipeline.Shader = Variant;
				Batch.bUseObjectSlots = false;
				Batch.Items.Add(BuildItem(FMatrix::Identity, View, Pass, InvalidObjectSlot, Packet.StartIndex,
					IndexCount, ObjectKey, false, true, true, ConservativeBounds, nullptr));
				Batch.Items.Last().InstanceCount = static_cast<uint32>(MeshParticleInstances.size());
			}
			else
			{
				// 인스턴싱 변형이 없으면 메시의 Local 스트림으로 인스턴스마다 그린다
				Batch.VertexFactory = Packet.Mesh ? Packet.Mesh->GetVertexFactory(Packet.LODIndex) : nullptr;
				if (!Batch.VertexFactory) continue;
				for (const FMeshParticleInstanceVertex& Instance : MeshParticleInstances)
				{
					Batch.Items.Add(BuildItem(Instance.GetWorldMatrix(), View, Pass, InvalidObjectSlot, Packet.StartIndex,
						IndexCount, ObjectKey, false, true, true, ConservativeBounds, nullptr));
				}
			}
			Out.Add(std::move(Batch));
			continue;
		}

		Batch.Items.Add(BuildItem(PacketWorld(Packet), View, Pass, Packet.Slot,
			Packet.StartIndex, IndexCount, ObjectKey, Packet.bOccludedByGpu, !Packet.Proxy || Packet.Proxy->bReceiveShadow,
			!Packet.Proxy || Packet.Proxy->bReceivesDecals,
			Packet.Proxy ? Packet.Proxy->GetBounds() : ConservativeBounds, Packet.Proxy));

		// 웨이트 히트맵: Forward 패스의 오브젝트 상수 빈 칸(ShadowFlags.y)에 본 번호 + 1을 싣는다
		if (Pass == EMeshPassType::Forward && Packet.DebugWeightBone >= 0)
		{
			auto* Constants = reinterpret_cast<FForwardObjectConstants*>(Batch.Items.Last().ObjectData.data());
			Constants->ShadowFlags.Y = static_cast<float>(Packet.DebugWeightBone + 1);
		}

		Out.Add(std::move(Batch));
	}
	return true;
}

bool FMeshPassProcessor::Build(const TArray<FPrimitiveSceneProxy*>& ShadowMeshes, EPSOType PSOType, TArray<FMeshDrawBatch>& OutBatches,
	const FVector& PreTranslation)
{
	OutBatches.Reset();

	const FPipelineState* Pipeline = FRenderResourceManager::GetPSO(PSOType);
	if (!Pipeline || !Pipeline->Shader || !Pipeline->Shader->VertexShader)
		return false;

	FRenderQueue DynamicPackets;
	for (FPrimitiveSceneProxy* Proxy : ShadowMeshes)
	{
		if (!Proxy || !Proxy->GetRenderState())
			continue;

		if (!Proxy->GetMesh() && Proxy->HasDynamicMeshElements())
		{
			DynamicPackets.Reset();
			Proxy->GetDynamicMeshElements(DynamicPackets, FViewContext());
			for (const FRenderPacket& Packet : DynamicPackets)
			{
				// 그림자는 메시 정점(Local, GPU 스킨)만 그린다. 파티클 스트림은 깊이 셰이더 입력과 맞지 않고,
				// 메시 파티클 VF는 지역 큐(DynamicPackets)가 들고 있어 이 함수 밖에서 쓸 수 없다
				const FVertexFactory* Factory = Packet.VertexFactory;
				if (!Factory || !Factory->IsValid() || Packet.IndexCount == 0)
					continue;
				if (Factory->GetType() != EVertexFactoryType::Local && Factory->GetType() != EVertexFactoryType::GpuSkin)
					continue;
				FMeshDrawBatch Batch{};
				Batch.Pipeline = *Pipeline;
				Batch.VertexFactory = Factory;
				if (!ApplyVertexFactoryShader(Batch)) continue;
				Batch.bUseObjectSlots = false;
				Batch.bBindViewConstants = true;
				Batch.ObjectBindingSlot = 2;
				Batch.ObjectStages = EShaderBindFlagBits::Vertex;
				Batch.Items.Add(MakeShadowItem(Proxy, Packet.StartIndex, Packet.IndexCount, PreTranslation));
				OutBatches.Add(std::move(Batch));
			}
			continue;
		}

		UStaticMesh* Mesh = Proxy->GetMesh();
		if (!Mesh || Proxy->GetLODCount() == 0)
			continue;

		constexpr uint8 LOD = 0;
		const FCachedMeshLOD& CachedLOD = Proxy->GetLOD(LOD);

		FMeshDrawBatch Batch{};
		Batch.Pipeline = *Pipeline;
		Batch.VertexFactory = Mesh->GetVertexFactory(LOD);
		Batch.bUseObjectSlots = false;
		Batch.bBindViewConstants = true;
		Batch.ObjectBindingSlot = 2;
		Batch.ObjectStages = EShaderBindFlagBits::Vertex;

		if (!Batch.VertexFactory->IsValid())
		{
			return false;
		}

		for (uint32 Index = 0; Index < CachedLOD.NumSections; ++Index)
		{
			const FCachedMeshSection& Section = Proxy->GetSection(CachedLOD.FirstSection + Index);
			if (Section.IndexCount == 0)
				continue;

			Batch.Items.Add(std::move(MakeShadowItem(Proxy, Section.StartIndex, Section.IndexCount, PreTranslation)));
		}

		if (!Batch.Items.IsEmpty())
			OutBatches.Add(std::move(Batch));
	}
	return true;
}

