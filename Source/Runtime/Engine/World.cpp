#include "EnginePCH.h"
#include "Render/LineBatcher.h"
#include "World.h"
#include "Level.h"

#include "Core/EngineStatics.h"
#include "GameFramework/Actor/StaticMeshActor.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/MyPlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGraph.h"
#include "GameFramework/GameplayInfoActors.h"
#include "GameFramework/SpringArmComponent.h"
#include "Input/InputSystem.h"

#include "UObject/UObjectIterator.h"
#include "UObject/UObjectHash.h"

#include "Collision/Ray.h"
#include "Components/BillboardComponent.h"
#include "Components/ParticleSubUVComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/WindDirectionalSourceComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Asset/LOD/StaticMeshLODSelector.h"

#include "Math/Frustum.h"
#include "Math/BVH.h"

#include <algorithm>
#include <span>
#include <unordered_map>
#include <vector>

#include "Core/Stats/LightweightStats.h"
#include "Core/Stats/EditorStats.h"
#include "Core/Async/TaskPool.h"
#include "Render/ObjectConstants.h"

#include "UObject/UObjectGlobals.h"

#include "Script/LuaBindings.h"
#include "Physics/Collision.h"
#include "Core/ConfigFile.h"
#include "Physics/PhysicsScene.h"
#include "Components/ShapeComponent.h"
#include "Particles/ParticleEventManager.h"
#include "Physics/PhysicsSettings.h"

namespace
{
	inline void WritePerObjectConstants(uint8* Dest, const FMatrix& LocalToWorld, const FMatrix& WorldToLocal, bool bReceiveShadow)
	{
		FPerObjectConstants Constants{};
		Constants.World = LocalToWorld;
		Constants.NormalMatrix = WorldToLocal.GetTransposed();
		Constants.ShadowFlags.X = bReceiveShadow ? 1.0f : 0.0f;   // GBuffer가 NormalRoughness.w 부호로 넘긴다
		std::memcpy(Dest, &Constants, sizeof(Constants));
	}
}

DECLARE_CYCLE_STAT("Actor Tick", STAT_ActorTick); // Actor 틱 측정
DECLARE_CYCLE_STAT("Update All Transforms", STAT_UpdateAllTransforms); // 각 Transform의 Update 시간 측정
DECLARE_CYCLE_STAT("Gather Render Packets", STAT_GatherRenderPackets);
DECLARE_CYCLE_STAT("Frustum Cull", STAT_FrustumCull);
DECLARE_CYCLE_STAT("Gather Elements", STAT_GatherElements);
DECLARE_CYCLE_STAT("Gather - LOD", STAT_GatherLOD);
DECLARE_CYCLE_STAT("Gather - Submit", STAT_GatherSubmit);

UWorld::UWorld()
	: CoroutineScheduler(*this)
{
}

UWorld::~UWorld()
{

}

void UWorld::RegisterWindSource(UWindDirectionalSourceComponent& Source)
{
	// 같은 바람의 중복 기여 방지
	WindSources.AddUnique(&Source);
}

void UWorld::UnregisterWindSource(UWindDirectionalSourceComponent& Source)
{
	// 바람 컴포넌트 해제 전 비소유 참조 제거
	const int32 Index = WindSources.Find(&Source);
	if (Index != INDEX_NONE)
	{
		WindSources.RemoveAtSwap(Index);
	}
}

FVector UWorld::GetWindVelocity() const
{
	// 월드 좌표계에서 방향성 풍속의 벡터 합산
	FVector Velocity = FVector::ZeroVector;
	for (const UWindDirectionalSourceComponent* Source : WindSources)
	{
		Velocity += Source->GetWindVelocity();
	}
	return Velocity;
}

bool UWorld::Init()
{
	// 레벨 생성 전 월드 전용 물리 씬 준비
	PhysicsScene = MakeUnique<FPhysicsScene>();
	if (!PhysicsScene->GetPxScene())
	{
		HTR_LOG(Error, "Failed to create world PhysX scene");
		PhysicsScene.reset();
		return false;
	}

	// Spawn Actor로 카메라 생성하고 세팅하기
	PersistentLevel = NewObject<ULevel>(this);

	if (!PersistentLevel)
	{
		HTR_LOG(Error, "Failed to create PersistentLevel");
		return false;
	}

	//레벨 연결
	PersistentLevel->SetWorld(this);
	Levels.Add(PersistentLevel);
	CurrentLevel = PersistentLevel;

	//카메라 생성
	CreateMainCamera();

	// 디버그 선 도구를 만든다. FLineBatcher::Init의 렌더러 인자는 쓰이지 않는다.
	LineBatcher = MakeUnique<FLineBatcher>();
	LineBatcher->Init(nullptr, this);

	return true;
}

AActor* UWorld::SpawnActor(UClass* Class, FName InName, const FTransform* Transform)
{
	if (!Class) return nullptr;
	if (!Class->IsChildOf(AActor::StaticClass())) return nullptr;

	// 1. ObjectFactory로 Actor 생성
	AActor* NewActor = NewObject<AActor>(PersistentLevel, Class, InName);

	if (!NewActor)
	{
		HTR_LOG(Error, "SpawnActor : Failed to create Actor");
		return nullptr;
	}

	// 2. Actor에 World/Level 연결
	NewActor->World = this;
	NewActor->Level = PersistentLevel;
	NewActor->bSpawnedDuringPlay = IsGameWorld() && (bBegunPlay || bStartingPlay);

	// 3. Transform 적용
	const FTransform SpawnTransform = Transform ? *Transform : FTransform::Identity;

	if (NewActor->GetRootComponent())
	{
		NewActor->GetRootComponent()->SetTransform(SpawnTransform);
	}
	
	// 4. Level->Actors에 등록한 뒤 컴포넌트를 월드에 연결한다.
	PersistentLevel->AddActor(NewActor);
	NewActor->RegisterAllComponents();

	// 데이터 Blueprint면 BP 기본값(프로퍼티, 컴포넌트)을 덮어쓴다. 기본값의 루트 Transform은 쓰지 않고 스폰 위치를 지킨다.
	if (FBlueprint::IsBlueprintClass(Class))
	{
		FBlueprint::ApplyActorDefaults(NewActor);
		if (NewActor->GetRootComponent())
			NewActor->GetRootComponent()->SetTransform(SpawnTransform);
		// Construction Script는 기본값 다음, 레벨에 저장된 인스턴스 차이를 얹기 전에 돈다.
		// 그래서 저장 -> 불러오기를 반복해도 결과가 쌓이지 않는다 (차이에는 실행 뒤 값이 들어 있다).
		FBlueprintGraphRuntime::RunConstructionScript(NewActor);
	}

	// 월드가 재생 중일 때 생성된 액터만 별도 BeginPlay 대기열에 넣는다.
	if (bBegunPlay) BeginPlayList.Enqueue(NewActor);

	return NewActor;
}

void UWorld::Tick(float RealDeltaTime)
{
	// Hit Stop 시간 흐름
	if (HitStopTimeRemaining > 0.0f)
	{
		HitStopTimeRemaining = std::max(HitStopTimeRemaining - RealDeltaTime, 0.0f);
	}

	const float DeltaTime = RealDeltaTime * GetEffectiveTimeDilation();
	RealTimeSeconds += RealDeltaTime;

	// World 시간 흐름
	if (!bIsPaused)
	{
		TimeSeconds += DeltaTime;
	}

	// 모든 SceneComponent의 Transform 갱신
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateAllTransforms);
		Scene.UpdateAllTransforms();
	}

	if (IsGameWorld())
	{
		GameWorldTick(DeltaTime);
	}

	if (WorldType == EWorldType::Editor)
	{
		EditorWorldTick(DeltaTime);
	}
	
	if (WorldType == EWorldType::EditorPreview)
	{
		PreviewWorldTick(DeltaTime);
	}

	// 월드 Tick에서 변경된 변환과 바운드를 렌더링 전에 반영합니다.
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateAllTransforms);
		Scene.UpdateAllTransforms();
	}
}

void UWorld::GameWorldTick(float DeltaTime)
{
	// 새로운 Actor들의 BeginPlay 실행
	while (!BeginPlayList.IsEmpty())
	{
		AActor* Actor = BeginPlayList.Peek();
		BeginPlayList.Dequeue();

		if (Actor)
		{
			Actor->BeginPlay();
		}
	}

	// 일시 정지 상태라면, 스킵
	if (bIsPaused)
	{
		return;
	}

	// 모든 Actor를 도는 대신 등록된 Tick 함수만 실행한다
	{
		SCOPE_CYCLE_COUNTER(STAT_ActorTick);
		TickTaskManager.RunTickGroup(ETickingGroup::PrePhysics, DeltaTime);
		// 프레임의 물리 결과 확정
		if (PhysicsScene)
		{
			// 레벨 오버라이드 또는 프로젝트 기본 중력 선택
			const float GravityZ = IsWorldGravityOverridden()
				? GetWorldGravityZ() : FPhysicsSettings::Get().DefaultGravityZ;
			PhysicsScene->SetGravityZ(GravityZ);
			PhysicsScene->Tick(DeltaTime);
		}
		TickTaskManager.RunTickGroup(ETickingGroup::DuringPhysics, DeltaTime);
		TickTaskManager.RunTickGroup(ETickingGroup::PostPhysics, DeltaTime);
		TickTaskManager.RunTickGroup(ETickingGroup::PostUpdateWork, DeltaTime);
	}

	// 코루틴 Tick
	CoroutineScheduler.Tick(DeltaTime);

	// ShapeComponent 충돌 처리
	UpdateOverlaps();

	// Lua Script 제거 처리
	FLuaBindings::FlushPendingDestroy(this);

	// 플레이어 카메라 업데이트
	if (PlayerController)
	{
		PlayerController->UpdateCameraManager(DeltaTime);
	}
}

void UWorld::EditorWorldTick(float DeltaTime)
{
	// Editor World에서 실행

	// 에디터 월드에서도 파티클 시뮬레이션은 틱이 돌아야 한다
	for (TObjectIterator<UParticleSubUVComponent> Comp; Comp; ++Comp)
	{
		if (!Comp || !Comp->IsRegistered() || Comp->GetWorld() != this)
		{
			continue;
		}

		if (Comp->GetParticlesForView().IsEmpty())
		{
			Comp->SetParticles(10);
		}

		Comp->TickComponent(DeltaTime);
	}

	for (TObjectIterator<UParticleSystemComponent> Comp; Comp; ++Comp)
	{
		if (!Comp || !Comp->IsRegistered() || Comp->GetWorld() != this)
		{
			continue;
		}
		Comp->TickComponent(DeltaTime);
	}

	// 스프링 암 컴포넌트도 에디터에서 틱이 돌아야 한다
	for (TObjectIterator<USpringArmComponent> Comp; Comp; ++Comp)
	{
		if (!Comp || !Comp->IsRegistered() || Comp->GetWorld() != this)
		{
			continue;
		}

		Comp->UpdateInEditor();
	}


	for (TObjectIterator<UDecalComponent> Comp; Comp; ++Comp)
	{
		if (!Comp || !Comp->IsRegistered() || Comp->GetWorld() != this)
		{
			continue;
		}
		Comp->UpdateInEditor();
	}

	// 애니메이션은 돌리지 않고 본 트랜스폼만 채운다. 비어 있으면 Physics Body·소켓 위치를 구할 수 없다
	for (TObjectIterator<USkinnedMeshComponent> Comp; Comp; ++Comp)
	{
		if (!Comp || !Comp->IsRegistered() || Comp->GetWorld() != this || !Comp->AreBoneTransformsDirty())
		{
			continue;
		}
		Comp->UpdateBoneTransformsNow();
	}
}

void UWorld::PreviewWorldTick(float DeltaTime)
{
	// 스킨드 메시 컴포넌트의 본이 수정되면 갱신되어야 함
	for (TObjectIterator<USkinnedMeshComponent> Comp; Comp; ++Comp)
	{
		if (!Comp || !Comp->IsRegistered() || Comp->GetWorld() != this)
		{
			continue;
		}

		Comp->TickComponent(DeltaTime);
	}

	// 파티클 에디터 미리보기: 미리보기 월드는 이 함수만 돌므로 PSC도 여기서 Tick한다
	for (TObjectIterator<UParticleSystemComponent> Comp; Comp; ++Comp)
	{
		if (!Comp || !Comp->IsRegistered() || Comp->GetWorld() != this)
		{
			continue;
		}

		Comp->TickComponent(DeltaTime);
	}

	// 컴포넌트 Tick 뒤에 스텝을 돌려야 PostPhysics의 바디 -> 본 반영이 같은 프레임에 끝난다
	if (bShouldSimulatePhysics && PhysicsScene)
	{
		const float GravityZ = IsWorldGravityOverridden()
			? GetWorldGravityZ() : FPhysicsSettings::Get().DefaultGravityZ;
		PhysicsScene->SetGravityZ(GravityZ);
		PhysicsScene->Tick(DeltaTime);
		TickTaskManager.RunTickGroup(ETickingGroup::PostPhysics, DeltaTime);
	}
}

void UWorld::ClearWorld()
{
	EndPlay();

	// BeginPlay 대기 중인 Actor 제거
	while (!BeginPlayList.IsEmpty())
	{
		BeginPlayList.Dequeue();
	}
	
	for (ULevel* Level : Levels)
	{
		for (AActor* Actor : Level->Actors)
		{
			if (!Actor) continue;
			if (Actor->HasBegunPlay())
				Actor->EndPlay();
			Actor->UnregisterAllComponents();
			Actor->RegisterAllActorTickFunctions(false);
		}
		Level->ClearActors();
		// 폴더 목록도 씬의 일부다. 비우지 않으면 새 씬이나 예전 씬 파일에 이전 씬의 폴더가 남는다.
		Level->EditorFolders.clear();
	}
	// World Settings도 씬의 일부다. 비우지 않으면 새 씬이나 이 값이 없는 예전 씬 파일에 이전 값이 남는다.
	ResetWorldSettings();
	// 액터 EndPlay에서 새로 등록한 작업까지 최종 정리한다.
	CoroutineScheduler.Reset();
	// 액터를 지우기 전에 렌더 프록시와 틱 등록부터 푼다. ClearActors는 액터를 delete만 하므로,
	// 그대로 두면 지워진 컴포넌트를 가리키는 프록시가 FScene에 남아 다음 프레임에 터진다.
	Scene.RemoveAllLights();
	Scene.RemoveAllDecals();
	Scene.RemoveAllPrimitives();
	HTR_LOG(Info, "{} : ", PersistentLevel->GetActorNum());
}

void UWorld::GatherRenderPackets(FRenderQueue& RenderQueue, const FViewContext* LODView, const FFrustumPlanes* Frustum, FRenderer* Renderer)
{
	// 멤버로 두어 매 프레임 용량을 재사용한다.
	RenderStats.Reset();
	VisibleProxies.Reset();

	{
		SCOPE_CYCLE_COUNTER(STAT_FrustumCull);
		// 컬링 단계에서는 프록시 포인터만 모으고, 컴포넌트 역참조(가시성 확인)는 어차피 컴포넌트를 읽는 Gather로 미룬다.
		const auto Visit = [&](FPrimitiveSceneProxy* Proxy) { VisibleProxies.Add(Proxy); };

		if (Frustum)
		{
			Scene.BVH.QueryCull(
				FrustumAllPlanesMask,
				[Frustum](const FBox& Bounds, uint32& Mask)
				{
					return static_cast<EBVHCullResult>(ClassifyBoxInFrustum(Bounds, *Frustum, Mask));
				},
				Visit);
		}
		else
		{
			Scene.BVH.QueryCull(0, [](const FBox&, uint32&) { return EBVHCullResult::Inside; }, Visit);
		}
	}

	RenderStats.TotalPrimitives = Scene.PrimitiveSceneProxies.Num();

	// GPU 오클루전: 프러스텀을 통과한 물체를 GPU에서 가림 판정하고 결과를 이번 프레임에 받아 온다.
	// Cull이 켜져 있으면 가려진 물체를 목록에서 빼서 이후 Gather·정렬·드로우를 모두 건너뛴다.
	// 꺼져 있으면(검증 모드) 목록은 그대로 두고 패킷에 판정만 표시한다.
	const uint8* OccludedMask = nullptr;
	if (Renderer && LODView && Renderer->GetGPUOcclusion().GetSettings().bEnabled)
	{
		FGPUOcclusion& Occlusion = Renderer->GetGPUOcclusion();
		if (Occlusion.Run(VisibleProxies.GetData(), VisibleProxies.Num(), *LODView))
		{
			const std::vector<uint8>& Occluded = Occlusion.GetOccluded();
			if (Occlusion.GetSettings().bCull)
			{
				uint32 Kept = 0;
				for (uint32 i = 0; i < VisibleProxies.Num(); ++i)
					if (!Occluded[i])
						VisibleProxies[Kept++] = VisibleProxies[i];
				VisibleProxies.SetNum(Kept);
			}
			else
			{
				OccludedMask = Occluded.data();
			}
		}
	}

	RenderStats.VisiblePrimitives = VisibleProxies.Num();

	constexpr uint32 ExtraSlots = 4096;
	const uint32 VisibleCount = VisibleProxies.Num();
	const uint32 MaxSlots = VisibleCount + ExtraSlots;
	uint8* SlotDest = Renderer ? Renderer->BeginObjectConstants(MaxSlots) : nullptr;
	uint32 NextExtraSlot = VisibleCount;

	// 조각 수 = 스레드 수 × 4. 잘게 나눠야 먼저 끝난 스레드가 남은 조각을 가져가서 부하가 고르게 된다.
	FTaskPool& Pool = FTaskPool::Get();
	const uint32 ChunkCount = FMath::Clamp(VisibleCount, 1u, Pool.GetNumThreads() * 4);

	if (GatherChunks.Num() < ChunkCount)
		GatherChunks.SetNum(ChunkCount);     // 늘릴 때만. 줄이지 않아야 배열 용량이 계속 재사용된다.

	for (uint32 c = 0; c < GatherChunks.Num(); ++c)   // 이번에 안 쓰는 조각의 묶음도 비워 둔다 (Renderer에 넘기지 않도록)
	{
		FGatherChunk& Chunk = GatherChunks[c];
		Chunk.Packets.Reset();               // 용량은 유지, 개수만 0
		Chunk.SlowPathIndices.Reset();
		std::fill(std::begin(Chunk.LODCounts), std::end(Chunk.LODCounts), 0u);
		std::fill(std::begin(Chunk.LODTriangles), std::end(Chunk.LODTriangles), 0ull);
		for (FStaticDrawGroup& Group : Chunk.Groups)
			Group.Items.clear();             // 용량은 유지
		Chunk.StaticDrawCount = 0;
	}

	const bool bStaticGroups = false;

	{
		SCOPE_CYCLE_COUNTER(STAT_GatherElements);

		LODInputs.Reset();
		if (LODView)
		{
			LODInputs.Reserve(VisibleProxies.Num());
			for (const FPrimitiveSceneProxy* Proxy : VisibleProxies)
				LODInputs.Add({ Proxy->GetLODSphere(), Proxy->GetRenderState() });
			SelectLODs(LODInputs, *LODView, SelectedLODs);
		}
		Pool.ParallelFor(VisibleCount, ChunkCount, [&](uint32 Begin, uint32 End, uint32 ChunkIndex)
			{
				FGatherChunk& Out = GatherChunks[ChunkIndex];      // 이 조각 전용. 다른 스레드는 절대 안 건드림
				// (머티리얼, 메시, LOD) 묶음 찾기. 조합이 몇 개뿐이라 선형 탐색이면 충분하고, 바로 전 묶음을 먼저 본다.
				const auto FindGroup = [&Out](UMaterial* Material, UStaticMesh* Mesh, uint8 LOD) -> FStaticDrawGroup&
					{
						if (Out.LastGroup < Out.Groups.size())
						{
							FStaticDrawGroup& Last = Out.Groups[Out.LastGroup];
							if (Last.Material == Material && Last.Mesh == Mesh && Last.LODIndex == LOD)
								return Last;
						}
						for (uint32 g = 0; g < Out.Groups.size(); ++g)
						{
							FStaticDrawGroup& Group = Out.Groups[g];
							if (Group.Material == Material && Group.Mesh == Mesh && Group.LODIndex == LOD)
							{
								Out.LastGroup = g;
								return Group;
							}
						}
						Out.LastGroup = static_cast<uint32>(Out.Groups.size());
						FStaticDrawGroup& Group = Out.Groups.emplace_back();
						Group.Material = Material;
						Group.Mesh = Mesh;
						Group.LODIndex = LOD;
						return Group;
					};

				for (uint32 VisibleIndex = Begin; VisibleIndex < End; ++VisibleIndex)
				{
					FPrimitiveSceneProxy* Proxy = VisibleProxies[VisibleIndex];   // 읽기만
					if (!Proxy->IsVisible()) continue;

					UStaticMesh* Mesh = Proxy->GetMesh();
					if (!Mesh)
					{
						Out.SlowPathIndices.Add(VisibleIndex);     // 컴포넌트를 건드리는 경로는 메인이 나중에
						continue;
					}

					const uint32 LOD = LODView ? SelectedLODs[VisibleIndex] : 0;
					const FCachedMeshLOD& CachedLOD = Proxy->GetLOD(LOD);
					++Out.LODCounts[LOD];                          // RenderStats 대신 조각 전용 통계

					uint32 Slot = InvalidObjectSlot;
					if (SlotDest)
					{
						// 칸 VisibleIndex는 이 반복만 쓴다 → 스레드끼리 겹치지 않음
						WritePerObjectConstants(SlotDest + size_t(VisibleIndex) * ObjectSlotBytes,
							Proxy->GetLocalToWorld(), Proxy->GetWorldToLocal(), Proxy->bReceiveShadow);
						Slot = VisibleIndex;
					}

					const bool bOccludedByGpu = OccludedMask && OccludedMask[VisibleIndex];
					for (uint32 i = 0; i < CachedLOD.NumSections; ++i)
					{
						const FCachedMeshSection& Section = Proxy->GetSection(CachedLOD.FirstSection + i);
						Out.LODTriangles[LOD] += Section.IndexCount / 3;

						if (bStaticGroups && Section.Material && Section.Material->BlendState == EBlendState::Opaque)
						{
							FStaticDrawGroup& Group = FindGroup(Section.Material, Mesh, static_cast<uint8>(LOD));
							Group.Items.push_back({ Proxy, Slot, Section.StartIndex, Section.IndexCount, bOccludedByGpu ? 1u : 0u });
							++Out.StaticDrawCount;
							continue;
						}

						FRenderPacket& Packet = Out.Packets.AddDefaulted_GetRef();   // 조각 전용 배열에 추가
						Packet.Proxy = Proxy;
						Packet.Mesh = Mesh;
						Packet.Material = Section.Material;
						Packet.StartIndex = Section.StartIndex;
						Packet.IndexCount = Section.IndexCount;
						Packet.LODIndex = static_cast<uint8>(LOD);
						Packet.Slot = Slot;
						Packet.bOccludedByGpu = bOccludedByGpu;
					}
				}
			});

		uint32 StaticDraws = 0;

		uint32 TotalPackets = 0;
		for (uint32 c = 0; c < ChunkCount; ++c)
			TotalPackets += GatherChunks[c].Packets.Num();

		RenderQueue.Reserve(TotalPackets + 256);             // 느린 경로 몫 약간 여유
		for (uint32 c = 0; c < ChunkCount; ++c)
			if (GatherChunks[c].Packets.Num() > 0)
				RenderQueue.Append(GatherChunks[c].Packets);  // 조각 순서대로 이어 붙이기

		for (uint32 c = 0; c < ChunkCount; ++c)
		{
			for (uint32 VisibleIndex : GatherChunks[c].SlowPathIndices)
			{
				FPrimitiveSceneProxy* Proxy = VisibleProxies[VisibleIndex];
				UPrimitiveComponent* Primitive = Proxy->GetComponent();
				if (!Primitive || !Primitive->IsVisible())
					continue;

				const uint32 FirstNew = RenderQueue.Num();

				// 프록시 캐시가 없는 스태틱 메시는 기존처럼 LOD를 골라 제출하고, 그 외는 컴포넌트에 맡긴다.
				UStaticMeshComponent* StaticMeshComponent = LODView ? Cast<UStaticMeshComponent>(Primitive) : nullptr;
				if (StaticMeshComponent && StaticMeshComponent->GetStaticMesh())
				{
					const uint32 LOD = SelectedLODs[VisibleIndex];
					StaticMeshComponent->SubmitToRenderQueue(RenderQueue, LOD);
				}
				else
				{
					Proxy->GetDynamicMeshElements(RenderQueue, LODView ? *LODView : FViewContext{});
				}

				// continue 없이 항상 여기까지 와서 새 패킷에 여유 칸을 배정한다.
				for (uint32 p = FirstNew; p < RenderQueue.Num(); ++p)
				{
					if (!SlotDest || NextExtraSlot >= MaxSlots) break;
					FRenderPacket& Packet = RenderQueue[p];
					const FMatrix& Model = Packet.Proxy ? Packet.Proxy->GetLocalToWorld() : Packet.Model ? *Packet.Model : FMatrix::Identity;
					const FMatrix WorldToLocal = Packet.Proxy ? Packet.Proxy->GetWorldToLocal() : Model.Inverse();
					WritePerObjectConstants(SlotDest + size_t(NextExtraSlot) * ObjectSlotBytes, Model, WorldToLocal, !Packet.Proxy || Packet.Proxy->bReceiveShadow);
					Packet.Slot = NextExtraSlot++;
				}
			}
		}

		for (uint32 c = 0; c < ChunkCount; ++c)
			for (uint32 L = 0; L < 4; ++L)
			{
				RenderStats.LODCounts[L] += GatherChunks[c].LODCounts[L];
				RenderStats.LODTriangles[L] += GatherChunks[c].LODTriangles[L];
			}

		if (SlotDest) Renderer->EndObjectConstants();
		RenderStats.DrawCalls = RenderQueue.Num() + StaticDraws;
		for (uint64 T : RenderStats.LODTriangles) RenderStats.Triangles += T;
	}
}

// 메인 카메라 생성
void UWorld::CreateMainCamera()
{
	if (MainCamera)
		return;

	MainCamera = NewObject<ACameraActor>();

	if (!MainCamera)
	{
		HTR_LOG(Error, "Failed to create MainCamera");
		return;
	}

	MainCamera->World = this;
	MainCamera->Level = nullptr;
	MainCamera->GetCameraComponent()->SetRelativeLocation(FVector(-5.0f, -5.0f, 5.0f));

	// 메인 카메라는 Level에 속하지 않아 BeginPlay를 거치지 않으므로 여기서 등록한다.
	MainCamera->RegisterAllActorTickFunctions(true);
}

void UWorld::SetMainCamera(ACameraActor* Camera)
{
	if (MainCamera == Camera)
		return;

	if (MainCamera)
		MainCamera->RegisterAllActorTickFunctions(false);

	MainCamera = Camera;

	if (MainCamera)
	{
		MainCamera->World = this;
		MainCamera->RegisterAllActorTickFunctions(true);
	}
}

int32 UWorld::GetActorNum()
{
	return PersistentLevel->GetActorNum();
}

bool UWorld::DestroyActor(AActor* Actor)
{
	if (!Actor)
		return false;

	ULevel* Level = Actor->GetLevel();

	if (!Level)
		return false;

	// 1. BeginPlay 대기열에서 제거
	TQueue<AActor*> NewBeginPlayList;

	while (!BeginPlayList.IsEmpty())
	{
		AActor* PendingActor = BeginPlayList.Peek();
		BeginPlayList.Dequeue();

		if (PendingActor != Actor)
		{
			NewBeginPlayList.Enqueue(PendingActor);
		}
	}

	BeginPlayList = std::move(NewBeginPlayList);

	// 2. Level의 Actors에서 제거
	for (int32 i = Level->Actors.Num() - 1; i >= 0; --i)
	{
		if (Level->Actors[i] == Actor)
		{
			Level->Actors.RemoveAt(i, 1);
			break;
		}
	}

	FString ActorName = Actor->GetName();
	uint32 ActorUUID = Actor->GetUUID();

	if (Actor->HasBegunPlay())
		Actor->EndPlay();

	// 3. 프록시 제거
	Actor->UnregisterAllComponents();

	Actor->RegisterAllActorTickFunctions(false);

	// 4. Actor 삭제
	delete Actor;

	HTR_LOG(Info, "Destroy Actor : {} UUID {}", ActorName, ActorUUID);

	return true;
}

bool UWorld::SweepSingle(const FRay& WorldRay, float MaxDistance, float Radius, FHitResult& OutHit, AActor* IgnoreActor,
	bool bBlockingOnly)
{
	const FPhysicsScene* Physics = GetPhysicsScene();
	if (!Physics)
		return SweepSingleLegacy(WorldRay, MaxDistance, Radius, OutHit, IgnoreActor, bBlockingOnly, false);

	// 바디가 있는 컴포넌트를 기존 방식으로도 검사하면 확장 AABB가 늘 먼저 맞아 PhysX 결과가 묻힌다
	FHitResult PhysicsHit;
	const bool bPhysicsHit = Physics->SweepSphere(WorldRay, MaxDistance, Radius, PhysicsHit, IgnoreActor, bBlockingOnly);
	const bool bLegacyHit = SweepSingleLegacy(WorldRay, MaxDistance, Radius, OutHit, IgnoreActor, bBlockingOnly, true);
	if (bPhysicsHit && (!bLegacyHit || PhysicsHit.Distance <= OutHit.Distance))
		OutHit = PhysicsHit;
	return bPhysicsHit || bLegacyHit;
}

// 다른 World의 객체를 제외하고 Component 교차 중 최근접 결과를 선택한다.
bool UWorld::SweepSingleLegacy(const FRay& WorldRay, float MaxDistance, float Radius, FHitResult& OutHit, AActor* IgnoreActor,
	bool bBlockingOnly, bool bSkipPhysicsBodies)
{
	OutHit = FHitResult(1.0f);
	if (MaxDistance <= 0.0f || !std::isfinite(MaxDistance) || Radius < 0.0f || !std::isfinite(Radius)) return false;
	Scene.UpdateAllTransforms();
	float NearestT = MaxDistance;
	const FVector Extent(Radius, Radius, Radius);
	if (bBlockingOnly)
	{
		// 도형은 렌더 프록시가 없으므로 직접 검사한다. 표시 여부는 충돌 여부와 별개다.
		// 기존 메시 Sweep과 같은 확장 AABB 방식이며, 바닥/트리거는 bBlockComponent=false로 둔다.
		for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
		{
			UPrimitiveComponent* Component = *It;
			AActor* Owner = Component->GetOwner();
			if (!Component->IsRegistered() || Component->GetWorld() != this || !Component->bBlockComponent ||
				!Owner || Owner == IgnoreActor || !Owner->GetActorEnableCollision()) continue;
			if (!Cast<UShapeComponent>(Component) && !Component->GetMeshData()) continue;
			if (bSkipPhysicsBodies && Component->HasValidPhysicsBody()) continue;

			const FBox Bounds = Component->CalcBounds();
			// NaN 바운드는 아래 슬랩 비교가 전부 거짓이 되어 거리 0 충돌로 판정된다
			if (!std::isfinite(Bounds.Min.X + Bounds.Min.Y + Bounds.Min.Z + Bounds.Max.X + Bounds.Max.Y + Bounds.Max.Z)) continue;
			const FVector Min = Bounds.Min - Extent, Max = Bounds.Max + Extent;
			float Enter = 0.0f, Exit = NearestT;
			FVector Normal = -WorldRay.Direction;
			bool bIntersects = true;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const float Origin = WorldRay.Origin[Axis], Direction = WorldRay.Direction[Axis];
				if (std::abs(Direction) <= SMALL_NUMBER)
				{
					if (Origin < Min[Axis] || Origin > Max[Axis]) { bIntersects = false; break; }
					continue;
				}
				float Near = (Min[Axis] - Origin) / Direction, Far = (Max[Axis] - Origin) / Direction;
				if (Near > Far) std::swap(Near, Far);
				if (Near > Enter)
				{
					Enter = Near;
					Normal = FVector::ZeroVector;
					Normal[Axis] = Direction > 0.0f ? -1.0f : 1.0f;
				}
				Exit = std::min(Exit, Far);
				if (Enter > Exit) { bIntersects = false; break; }
			}
			if (!bIntersects) continue;
			NearestT = Enter;
			OutHit.bBlockingHit = true;
			OutHit.bStartPentrating = Enter == 0.0f;
			OutHit.HitComponent = Component;
			OutHit.Distance = Enter;
			OutHit.Time = Enter / MaxDistance;
			OutHit.Location = WorldRay.PointAt(Enter);
			OutHit.ImpactPoint = OutHit.Location - Normal * Radius;
			OutHit.Normal = OutHit.ImpactNormal = Normal;
		}
		return OutHit.bBlockingHit;
	}
	const FPreparedRay PreparedRay(WorldRay);
	Scene.BVH.TraceClosest(
		[&](const FBox& Bounds, float& OutEnterT)
		{
			return RayIntersectsAABB(PreparedRay, Bounds.Min - Extent, Bounds.Max + Extent, OutEnterT);
		},
		[&](FPrimitiveSceneProxy* Proxy, float& InOutNearestT)
		{
			UPrimitiveComponent* Component = Proxy ? Proxy->GetComponent() : nullptr;
			if (!Component || !Component->IsShown(true) ||
				!Component->GetOwner() || !Component->GetOwner()->GetActorEnableCollision() ||
				Component->GetOwner() == IgnoreActor || Component->GetOwner()->GetWorld() != this ||
				(bSkipPhysicsBodies && Component->HasValidPhysicsBody()))
				return false;
			if (!Proxy->GetMesh())
			{
				const FMatrix& WorldToLocal = Proxy->GetWorldToLocal();
				const FRay LocalRay{
					.Origin = WorldToLocal.TransformPosition(WorldRay.Origin),
					.Direction = WorldToLocal.TransformVector(WorldRay.Direction)
				};
				float T = InOutNearestT;
				if (!Component->LineTraceComponentLocal(LocalRay, T) || T > InOutNearestT) return false;
				OutHit.bBlockingHit = true;
				OutHit.HitComponent = Component;
				OutHit.Distance = T;
				OutHit.Time = T / MaxDistance;
				OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
				OutHit.Location = OutHit.ImpactPoint;
				OutHit.ImpactNormal = -WorldRay.Direction;
				OutHit.Normal = OutHit.ImpactNormal;
				InOutNearestT = T;
				return true;
			}

			FAABB Bounds = Proxy->GetBounds();
			Bounds.Extent += Extent;
			float T = InOutNearestT;
			if (!RayIntersectsAABB(WorldRay, Bounds.Center - Bounds.Extent, Bounds.Center + Bounds.Extent, T) ||
				T > InOutNearestT) return false;

			const FVector Point = WorldRay.Origin + WorldRay.Direction * T;
			const FVector Offset = Point - Bounds.Center;
			const float X = Bounds.Extent.X > 0.0f ? Offset.X / Bounds.Extent.X : 0.0f;
			const float Y = Bounds.Extent.Y > 0.0f ? Offset.Y / Bounds.Extent.Y : 0.0f;
			const float Z = Bounds.Extent.Z > 0.0f ? Offset.Z / Bounds.Extent.Z : 0.0f;
			const float AX = std::abs(X), AY = std::abs(Y), AZ = std::abs(Z);
			FVector Normal = AX >= AY && AX >= AZ ? FVector(X >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f)
				: AY >= AZ ? FVector(0.0f, Y >= 0.0f ? 1.0f : -1.0f, 0.0f)
				: FVector(0.0f, 0.0f, Z >= 0.0f ? 1.0f : -1.0f);
			if (Normal.Dot(WorldRay.Direction) > 0.0f) return false;

			OutHit.bBlockingHit = true;
			OutHit.HitComponent = Component;
			OutHit.Distance = T;
			OutHit.Time = T / MaxDistance;
			OutHit.ImpactPoint = Point;
			OutHit.Location = Point;
			OutHit.ImpactNormal = Normal;
			OutHit.Normal = Normal;
			InOutNearestT = T;
			return true;
		}, NearestT);
	return OutHit.bBlockingHit;
}

bool UWorld::LineTraceSingle(const FRay& WorldRay, FHitResult& OutHit,
	FBillboardTraceTransform ResolveBillboard, const void* ViewContext)
{
	SCOPE_CYCLE_COUNTER_ALWAYS(EditorStats::STAT_PickingTime_Name);
	OutHit = FHitResult();
	float NearestT = std::numeric_limits<float>::max();

	const auto TraceComponent = [&](FPrimitiveSceneProxy* Proxy, float& InOutNearestT)
		{
			if (UStaticMesh* Mesh = Proxy ? Proxy->GetMesh() : nullptr)
			{
				if (!Proxy->IsVisible())
					return false;

				const FMatrix& WorldToLocal = Proxy->GetWorldToLocal();
				const FRay LocalRay{
					.Origin = WorldToLocal.TransformPosition(WorldRay.Origin),
					.Direction = WorldToLocal.TransformVector(WorldRay.Direction)
				};

				float T = InOutNearestT;
				if (!RayIntersectsMesh(LocalRay, Mesh->GetMeshData(), T))
					return false;

				OutHit.HitComponent = Proxy->GetComponent();
				OutHit.Distance = T;
				OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
				InOutNearestT = T;
				return true;
			}

			UPrimitiveComponent* Component = Proxy ? Proxy->GetComponent() : nullptr;

			if (!Component || !Component->IsVisible())
				return false;

			if (UBillboardComponent* Billboard = Cast<UBillboardComponent>(Component))
			{
				if (!ResolveBillboard)
				{
					FHitResult Hit;
					if (!Billboard->LineTraceComponent(WorldRay, Hit) ||
						Hit.Distance >= InOutNearestT)
					{
						return false;
					}

					OutHit = Hit;
					InOutNearestT = Hit.Distance;
					return true;
				}

				const FMatrix BillboardToWorld = ResolveBillboard(*Billboard, ViewContext);

				const FRay LocalRay = ToLocalRay(WorldRay, BillboardToWorld);

				float T = InOutNearestT;
				if (!Billboard->LineTraceComponentLocal(LocalRay, T))
				{
					return false;
				}

				OutHit.HitComponent = Billboard;
				OutHit.Distance = T;
				OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
				InOutNearestT = T;
				return true;
			}

			const FMatrix& WorldToLocal = Proxy->GetWorldToLocal();
			const FRay LocalRay{
				.Origin = WorldToLocal.TransformPosition(WorldRay.Origin),
				.Direction = WorldToLocal.TransformVector(WorldRay.Direction)
			};

			float T = InOutNearestT;
			if (!Component->LineTraceComponentLocal(LocalRay, T))
			{
				return false;
			}

			OutHit.HitComponent = Component;
			OutHit.Distance = T;
			OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
			InOutNearestT = T;
			return true;
		};

	const FPreparedRay PreparedRay(WorldRay);

	Scene.BVH.TraceClosest(
		[&](const FBox& Bounds, float& OutEnterT) { return RayIntersectsAABB(PreparedRay, Bounds.Min, Bounds.Max, OutEnterT); },
		[&](FPrimitiveSceneProxy* Proxy, float& OutNearestT) { return TraceComponent(Proxy, OutNearestT); },
		NearestT);

	return OutHit.HitComponent != nullptr;
}

void UWorld::BeginPlay()
{
	if (!IsGameWorld() || bBegunPlay) return;
	bStartingPlay = true;

	//TODO : PlayerPawn, PlayerController Class 설정을 GameMode로 옮겨 수정 가능하게 바꿔야 한다.
	if (!PlayerPawn)
	{
		for (AActor* Actor : PersistentLevel->GetActors())
		{
			if (APawn* Pawn = Cast<APawn>(Actor)) { PlayerPawn = Pawn; break; }
		}
	}
	// World Settings의 GameMode를 만들고, 그 GameMode가 정한 클래스로 나머지를 만든다 (UE의 순서와 같다).
	if (!GameMode)
		GameMode = Cast<AGameModeBase>(SpawnActor(ResolveGameModeClass()));
	if (GameMode && !GameState)
		GameState = Cast<AGameStateBase>(SpawnActor(AGameModeBase::ResolveClass(GameMode->GameStateClass,
			AGameStateBase::StaticClass(), AGameStateBase::StaticClass())));

	if (!PlayerPawn)
	{
		FTransform Transform = MainCamera ? MainCamera->GetActorTransform() : FTransform::Identity;
		// 놓인 Pawn이 없으면 GameMode의 Default Pawn Class로 만든다.
		// Default Pawn Class가 None이면 메시 없는 관전 Pawn을 만들어 에디터처럼 자유 시점으로 날아다닌다.
		UClass* PawnClass = ADefaultPawn::StaticClass();
		if (GameMode)
			PawnClass = GameMode->DefaultPawnClass.empty()
				? ASpectatorPawn::StaticClass()
				: AGameModeBase::ResolveClass(GameMode->DefaultPawnClass, APawn::StaticClass(), ADefaultPawn::StaticClass());
		PlayerPawn = Cast<APawn>(SpawnActor(PawnClass, NAME_None, &Transform));
	}
	if (!PlayerController) 
		PlayerController = Cast<APlayerController>(SpawnActor(GameMode
			? AGameModeBase::ResolveClass(GameMode->PlayerControllerClass, APlayerController::StaticClass(), AMyPlayerController::StaticClass())
			: AMyPlayerController::StaticClass()));
	if (GameMode && !PlayerState)
		PlayerState = Cast<APlayerState>(SpawnActor(AGameModeBase::ResolveClass(GameMode->PlayerStateClass,
			APlayerState::StaticClass(), APlayerState::StaticClass())));
	if (GameMode && !HUD)
		HUD = Cast<AHUD>(SpawnActor(AGameModeBase::ResolveClass(GameMode->HUDClass, AHUD::StaticClass(), AHUD::StaticClass())));
	
	if (PlayerController)
		PlayerController->SpawnPlayerCameraManager();
	
	if (PlayerController && PlayerPawn) PlayerController->Possess(PlayerPawn);

	if (!MyParticleEventManager)
		MyParticleEventManager = SpawnActor<AParticleEventManager>();

	// 기존 액터 목록을 고정한 후 시작한다. BeginPlay 중 생성된 액터는 대기열로 간다.
	TArray<AActor*> InitialActors;
	for (ULevel* Level : Levels)
		if (Level)
			for (AActor* Actor : Level->GetActors())
				if (Actor) InitialActors.Add(Actor);
	bStartingPlay = false;
	bBegunPlay = true;
	for (AActor* Actor : InitialActors)
		Actor->BeginPlay();

	// BeginPlay 중 생성된 액터도 PIE 입력 전에 시작한다.
	while (!BeginPlayList.IsEmpty())
	{
		AActor* Actor = BeginPlayList.Peek();
		BeginPlayList.Dequeue();
		if (Actor) Actor->BeginPlay();
	}
}

void UWorld::EndPlay()
{
	CoroutineScheduler.Reset();
	if (PhysicsScene)
	{
		PhysicsScene->ResetStepTime();
	}
	bBegunPlay = false;
	if (PlayerController)
	{
		PlayerController->ClearInputState();
		PlayerController->UnPossess();
	}
	PlayerController = nullptr;
	PlayerPawn = nullptr;
	GameMode = nullptr;
	GameState = nullptr;
	PlayerState = nullptr;
	HUD = nullptr;
	bIsPaused = false;
	MyParticleEventManager = nullptr;
}

UWorld* UWorld::CreateWorld(const EWorldType InWorldType, bool bInformEngineOfWorld, FName WorldName)
{
	UWorld* NewWorld = NewObject<UWorld>(nullptr, WorldName);
	if (!NewWorld){ return nullptr; }

	NewWorld->SetFlags(EObjectFlags::RF_Transactional);
	NewWorld->WorldType = InWorldType;

	if (!NewWorld->Init())
	{
		delete NewWorld;
		return nullptr;
	}

	return NewWorld;
}

UWorld* UWorld::GetDuplicatedWorldForPIE(UWorld * InWorld)
{
	if (!InWorld)
	{
		return nullptr;
	}

	UWorld* PIEWorld = CreateWorld(EWorldType::PIE, false);
	if (!PIEWorld)
	{
		return nullptr;
	}
	FObjectDuplicationParameters Params(InWorld, nullptr);
	Params.DuplicationSeed.Add(InWorld, PIEWorld);
	Params.DuplicationSeed.Add(InWorld->GetPersistentLevel(), PIEWorld->GetPersistentLevel());
	Params.PortFlags = EPropertyPortFlags::PPF_DuplicateForPIE;

	StaticDuplicateObjectEx(Params);
	return PIEWorld;
}

void UWorld::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	if (!bDuplicateForPIE)
	{
		assert(PersistentLevel);

		// Update the persistent level's owning world. This is needed for some initialization
		if (!PersistentLevel->OwningWorld)
		{
			PersistentLevel->OwningWorld = this;
		}
	}

	for (AActor* Actor : PersistentLevel->GetActors())
	{
		if (!Actor)
			continue;

		for (UActorComponent* Component : Actor->GetComponents())
			if (USceneComponent* SceneComp = Cast<USceneComponent>(Component))
				SceneComp->MarkTransformDirty();

		Actor->RegisterAllComponents();

	}

	Scene.BuildBVH();
}

void UWorld::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);   // "Properties"

	FArchive& Ar = Record.GetUnderlyingArchive();
	PersistentLevel->Serialize(Record.EnterRecord("PersistentLevel"));
}

void UWorld::UpdateOverlaps()
{
	// 1. 이 월드에서 살아 있고 등록된 모든 충돌 도형을 수집한다.
	// bGenerateOverlapEvents가 false인 도형도 넣는다.
	// 그래야 실행 중 이벤트를 껐을 때 기존 Overlap에 End를 보낼 수 있다.
	TArray<UShapeComponent*> Shapes;

	for (TObjectIterator<UShapeComponent> It; It; ++It)
	{
		UShapeComponent* Shape = *It;

		if (!Shape ||
			!Shape->IsRegistered() ||
			Shape->GetWorld() != this ||
			!Shape->GetOwner())
		{
			continue;
		}

		Shapes.Add(Shape);
	}

	// 2. 도형은 렌더용 Scene.BVH에 없으므로 이번 프레임의 도형 전용 BVH를 만든다.
	std::vector<int32> Indices;
	std::unordered_map<UPrimitiveComponent*, int32> ShapeIndices;
	Indices.reserve(Shapes.Num());
	ShapeIndices.reserve(Shapes.Num());
	for (int32 i = 0; i < Shapes.Num(); ++i)
	{
		Indices.push_back(i);
		ShapeIndices.emplace(Shapes[i], i);
	}

	TBVH<int32> OverlapBVH([&](const int32& Index) -> FBox
		{
			return Shapes[Index]->GetWorldAABB();
		});
	OverlapBVH.Build(std::span<const int32>(Indices.data(), Indices.size()));

	std::vector<int32> Candidates;
	for (int32 i = 0; i < Shapes.Num(); ++i)
	{
		UShapeComponent* A = Shapes[i];
		AActor* OwnerA = A->GetOwner();

		Candidates.clear();
		const FBox QueryBounds = A->GetWorldAABB();
		OverlapBVH.Query(
			[&](const FBox& Bounds) { return AreAABBsOverlapping(QueryBounds, Bounds); },
			[&](int32 j)
			{
				if (j > i) Candidates.push_back(j);
			});

		// BVH 후보에서 빠진 이전 겹침도 검사해야 분리/충돌 설정 해제 시 End를 보낼 수 있다.
		for (const FOverlapInfo& Info : A->GetOverlapInfos())
		{
			const auto It = ShapeIndices.find(Info.OtherComp);
			if (It != ShapeIndices.end() && It->second > i)
				Candidates.push_back(It->second);
		}

		// 후보 중복을 없애고 기존 i < j 순서대로 이벤트를 전달한다.
		std::sort(Candidates.begin(), Candidates.end());
		Candidates.erase(std::unique(Candidates.begin(), Candidates.end()), Candidates.end());

		for (int32 j : Candidates)
		{
			UShapeComponent* B = Shapes[j];
			AActor* OwnerB = B->GetOwner();

			// 같은 Actor의 콜라이더끼리는 여기서는 무시한다.
			// 필요하면 나중에 self-collision 정책을 별도로 추가한다.
			const bool bCanGenerateOverlap =
				OwnerA &&
				OwnerB &&
				OwnerA != OwnerB &&
				OwnerA->GetActorEnableCollision() &&
				OwnerB->GetActorEnableCollision() &&
				A->bGenerateOverlapEvents &&
				B->bGenerateOverlapEvents;

			bool bIsOverlappingNow = false;

			if (bCanGenerateOverlap)
			{
				// 3. 값싼 AABB 검사에서 먼저 탈락시킨다.
				const FBox BoundsA = A->GetWorldAABB();
				const FBox BoundsB = B->GetWorldAABB();

				if (AreAABBsOverlapping(BoundsA, BoundsB))
				{
					// 4. 실제 도형 충돌: Sphere/Box/Capsule 조합
					bIsOverlappingNow = FCollision::Overlap(A, B);
				}
			}

			// 5. 지난 프레임의 상태
			const bool bWasOverlapping =
				A->IsOverlappingComponent(B);

			// false -> true: 첫 접촉
			if (!bWasOverlapping && bIsOverlappingNow)
			{
				A->BeginComponentOverlap(B);
				B->BeginComponentOverlap(A);

				// 지금은 “처음 막혔을 때 한 번” Hit를 보내는 정책
				// 고체 두 개의 Hit는 PhysX 접촉 콜백에서 전달
				const bool bNativeHit = A->HasValidPhysicsBody() && B->HasValidPhysicsBody()
					&& A->BodyInstance.CollisionEnabled == ECollisionEnabled::QueryAndPhysics
					&& B->BodyInstance.CollisionEnabled == ECollisionEnabled::QueryAndPhysics;
				if (A->bBlockComponent && B->bBlockComponent && !bNativeHit)
				{
					A->DispatchHit(B);
					B->DispatchHit(A);
				}
			}
			// true -> false: 분리
			else if (bWasOverlapping && !bIsOverlappingNow)
			{
				A->EndComponentOverlap(B);
				B->EndComponentOverlap(A);
			}
		}
	}
}

void UWorld::ResetWorldSettings()
{
	GameModeOverride.clear();
	bOverrideWorldGravity = false;
	WorldGravityZ = -9.81f;
	GlobalTimeDilation = 1.0f;
}

UClass* UWorld::ResolveGameModeClass() const
{
	// 씬의 GameMode Override가 비어 있으면 프로젝트 기본값을 쓴다.
	// UE의 Project Settings > Maps & Modes > Default GameMode(GlobalDefaultGameMode) 자리다.
	// 플레이를 시작할 때만 불리므로 매번 읽는다. 에디터에서 ini를 고치면 다음 플레이부터 반영된다.
	FString ClassName = GameModeOverride;
	if (ClassName.empty())
	{
		FConfigFile Config;
		if (Config.Load("Config/DefaultGame.ini"))
			ClassName = Config.GetString("Game", "GlobalDefaultGameMode", "");
	}
	return AGameModeBase::ResolveClass(ClassName, AGameModeBase::StaticClass(), AGameModeBase::StaticClass());
}

float UWorld::GetEffectiveTimeDilation() const
{
	const float HitStop = (HitStopTimeRemaining > 0.0f) ? HitStopDilation : 1.0f;
	// UE AWorldSettings::GetEffectiveTimeDilation: TimeDilation * CinematicTimeDilation (* DemoPlay)
	return FMath::Clamp(GlobalTimeDilation * CinematicTimeDilation * HitStop, MinTimeDilation, MaxTimeDilation);
}

void UWorld::StartHitStop(float Duration, float Dilation)
{
	HitStopTimeRemaining = std::max(HitStopTimeRemaining, Duration);
	HitStopDilation = Dilation;
}
