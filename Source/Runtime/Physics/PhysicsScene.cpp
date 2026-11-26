#include "EnginePCH.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysicsSettings.h"
#include "Core/EngineLog.h"
#include "Physics/PhysXSDK.h"
#include "Collision/HitResult.h"
#include "Collision/Ray.h"
#include "Physics/CollisionQueryFilterCallback.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysXConversion.h"
#include "Core/StatOverlay.h"
#include "Core/Windows/WindowsPlatformTime.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include <cmath>
#include <limits>
#include <geometry/PxGeometryQuery.h>
#include <extensions/PxShapeExt.h>
#include <algorithm>
#include <PxPhysics.h>
#include <PxScene.h>
#include <PxSceneDesc.h>
#include <PxSceneLock.h>
#include "Physics/PhysXSimEventCallback.h"

// 고정 크기 버퍼로 나뉘어 전달되는 전체 겹침 결과 수집
struct FSimulationOverlapBuffer final : physx::PxOverlapCallback
{
	using physx::PxOverlapCallback::PxOverlapCallback;
	TArray<physx::PxOverlapHit> Hits;

	physx::PxAgain processTouches(const physx::PxOverlapHit* Buffer, physx::PxU32 Count) override
	{
		for (physx::PxU32 Index = 0; Index < Count; ++Index)
		{
			Hits.Add(Buffer[Index]);
		}
		return true;
	}
};

FPhysicsScene::FPhysicsScene()
{
	physx::PxPhysics* Physics = PhysXSDK::GetPhysics();
	physx::PxCpuDispatcher* Dispatcher = PhysXSDK::GetCpuDispatcher();
	if (!Physics || !Dispatcher)
	{
		return;
	}

	// 프로젝트 기본 중력과 PCM 설정의 씬 적용
	const FPhysicsSettings& Settings = FPhysicsSettings::Get();
	physx::PxSceneDesc Desc(Physics->getTolerancesScale());
	Desc.gravity = physx::PxVec3(0.0f, 0.0f, Settings.DefaultGravityZ);
	Desc.cpuDispatcher = Dispatcher;

	// 네이티브 씬 생성 전 이벤트 필터와 콜백 등록
	SimulationEvents = std::make_unique<FPhysXSimEventCallback>(*this);
	Desc.filterShader = &FPhysXSimEventCallback::FilterShader;
	Desc.simulationEventCallback = SimulationEvents.get();
	Desc.flags |= physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS;   // SyncComponentsToBodies가 움직인 액터만 받는다

	if (Settings.bEnablePCM)
	{
		Desc.flags |= physx::PxSceneFlag::eENABLE_PCM;
	}
	else
	{
		Desc.flags.clear(physx::PxSceneFlag::eENABLE_PCM);
	}

	// 프로젝트 설정에 따른 씬 CCD 허용
	if (!Settings.bDisableCCD)
	{
		Desc.flags |= physx::PxSceneFlag::eENABLE_CCD;
	}

	// 씬 접근 잠금 검사의 활성화
	Desc.flags |= physx::PxSceneFlag::eREQUIRE_RW_LOCK;

	Scene = Physics->createScene(Desc);
}

FPhysicsScene::~FPhysicsScene()
{
	// 월드가 소유한 네이티브 씬 해제
	if (Scene)
	{
		Scene->release();
	}
}

void FPhysicsScene::SetGravityZ(float GravityZ)
{
	if (!Scene) return;

	// 중력 조회와 갱신의 쓰기 잠금
	const physx::PxVec3 Gravity(0.0f, 0.0f, GravityZ);
	const physx::PxSceneWriteLock WriteLock(*Scene);
	//유효 중력이 바뀐 경우의 네이티브 씬 갱신
	if (Scene->getGravity() != Gravity)
	{
		Scene->setGravity(Gravity);
	}
}

void FPhysicsScene::Tick(float DeltaSeconds)
{
	// 스텝이 없는 틱의 표시값 초기화
	LastTickStepCount = 0;
	LastTickStepTimeMs = 0.0;
	LastTickEventTimeMs = 0.0;

	if (!Scene || DeltaSeconds <= 0.0f)
	{
		return;
	}

	// 프로젝트 고정 스텝 크기와 최대 횟수 조회
	const FPhysicsSettings& Settings = FPhysicsSettings::Get();
	const double FixedStepSeconds = Settings.FixedStepSeconds;
	const int32 MaxStepsPerTick = Settings.MaxStepsPerTick;
	const float StepDelta = static_cast<float>(FixedStepSeconds);
	const bool bMeasurePhysicsTime = FStatOverlay::IsEnabled(EStatFlags::Physics);
	AccumulatedSeconds = std::min(
		AccumulatedSeconds + static_cast<double>(DeltaSeconds),
		FixedStepSeconds * MaxStepsPerTick);

	while (AccumulatedSeconds >= FixedStepSeconds)
	{
		// 스텝 입력 적용과 PhysX 결과 수집
		OnPreStep.Broadcast(StepDelta);
		bool bFetchedResults = false;
		{
			// 고정 스텝의 네이티브 씬 쓰기 잠금
			const physx::PxSceneWriteLock WriteLock(*Scene);
			const uint64 StartCycles = bMeasurePhysicsTime ? FPlatformTime::GetCycles64() : 0;
			Scene->simulate(StepDelta);
			bFetchedResults = Scene->fetchResults(true);
			if (bMeasurePhysicsTime && bFetchedResults)
			{
				const uint64 ElapsedCycles = FPlatformTime::GetCycles64() - StartCycles;
				LastTickStepTimeMs += FPlatformTime::ToMilliseconds64(ElapsedCycles);
			}
		}

		if (!bFetchedResults)
		{
			HTR_LOG(Error, "PxScene::fetchResults failed");
			AccumulatedSeconds = 0.0;
			return;
		}
		SyncComponentsToBodies();
		// 완료된 스텝의 시간 차감과 결과 전달
		++LastTickStepCount;
		AccumulatedSeconds -= FixedStepSeconds;
		OnPostStep.Broadcast(StepDelta);
	}
	// 마지막 스텝 이후의 게임 이벤트 전달 비용 별도 측정
	if (SimulationEvents->HasPendingEvents())
	{
		const uint64 StartCycles = bMeasurePhysicsTime ? FPlatformTime::GetCycles64() : 0;
		SimulationEvents->DispatchPending();
		if (bMeasurePhysicsTime)
		{
			LastTickEventTimeMs = FPlatformTime::ToMilliseconds64(FPlatformTime::GetCycles64() - StartCycles);
		}
	}
}

void FPhysicsScene::RefreshOverlapEvents(const UPrimitiveComponent* Component)
{
	SimulationEvents->RefreshOverlapEvents(Component);
}

float FPhysicsScene::GetInterpolationAlpha() const
{
	const double StepSeconds = FPhysicsSettings::Get().FixedStepSeconds;
	return static_cast<float>(std::clamp(AccumulatedSeconds / StepSeconds, 0.0, 1.0));
}

UPrimitiveComponent* FPhysicsScene::FindHitComponent(const physx::PxRigidActor* Actor) const
{
	// userData의 바디 역참조
	const auto* Body = Actor ? static_cast<const FBodyInstance*>(Actor->userData) : nullptr;
	return Body ? Body->OwnerComponent : nullptr;
}

void FPhysicsScene::GatherSimulationShapes(const physx::PxBoxGeometry& Box,
	const physx::PxTransform& Pose, const AActor* IgnoreActor, TArray<FPhysicsOverlapShape>& OutShapes) const
{
	OutShapes.Reset();
	if (!Scene || !Box.isValid() || !Pose.isValid())
	{
		return;
	}

	{
		// 겹침 검색과 네이티브 형상 복사까지의 읽기 잠금
		const physx::PxSceneReadLock ReadLock(*Scene);
		physx::PxOverlapHit Buffer[32];
		FSimulationOverlapBuffer Result(Buffer, 32);
		const physx::PxQueryFilterData Filter(physx::PxQueryFlag::eSTATIC
			| physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::eNO_BLOCK);
		Scene->overlap(Box, Pose, Result, Filter);

		for (const physx::PxOverlapHit& Hit : Result.Hits)
		{
			const auto Flags = Hit.shape->getFlags();
			if (!Flags.isSet(physx::PxShapeFlag::eSIMULATION_SHAPE)
				|| Flags.isSet(physx::PxShapeFlag::eTRIGGER_SHAPE)
				|| Hit.actor->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION))
			{
				continue;
			}

			const UPrimitiveComponent* Component = FindHitComponent(Hit.actor);
			const AActor* Owner = Component ? Component->GetOwner() : nullptr;
			if (!Owner || Owner == IgnoreActor || !Owner->GetActorEnableCollision())
			{
				continue;
			}

			const auto Type = Hit.shape->getGeometryType();
			if (Type != physx::PxGeometryType::eSPHERE && Type != physx::PxGeometryType::eCAPSULE
				&& Type != physx::PxGeometryType::eBOX)
			{
				continue;
			}

			// 강체의 최신 자세와 형상 오프셋을 합친 월드 자세
			const auto Geometry = Hit.shape->getGeometry();
			const auto WorldPose = physx::PxShapeExt::getGlobalPose(*Hit.shape, *Hit.actor);
			const float DistanceSquared = physx::PxGeometryQuery::pointDistance(Pose.p, Geometry.any(), WorldPose);
			OutShapes.Add({Geometry, WorldPose, DistanceSquared, reinterpret_cast<std::uintptr_t>(Hit.shape)});
		}
	}

	// 검색 중심에서 가장 가까운 형상 표면을 우선하는 순서
	std::sort(OutShapes.begin(), OutShapes.end(), [](const FPhysicsOverlapShape& A, const FPhysicsOverlapShape& B)
	{
		if (A.DistanceSquared == B.DistanceSquared)
		{
			return A.ShapeOrder < B.ShapeOrder;
		}
		return A.DistanceSquared < B.DistanceSquared;
	});
}

bool FPhysicsScene::Raycast(const FRay& WorldRay, float MaxDistance, FHitResult& OutHit,
	const AActor* IgnoreActor, bool bBlockingOnly) const
{
	return QuerySingle(WorldRay, MaxDistance, nullptr, physx::PxQuat(physx::PxIdentity),
		OutHit, IgnoreActor, bBlockingOnly);
}

bool FPhysicsScene::SweepSphere(const FRay& WorldRay, float MaxDistance, float Radius,
	FHitResult& OutHit, const AActor* IgnoreActor, bool bBlockingOnly) const
{
	// 반경 0의 광선 처리
	if (Radius == 0.0f)
	{
		return Raycast(WorldRay, MaxDistance, OutHit, IgnoreActor, bBlockingOnly);
	}

	const physx::PxSphereGeometry Geometry(Radius);
	return QuerySingle(WorldRay, MaxDistance, &Geometry, physx::PxQuat(physx::PxIdentity),
		OutHit, IgnoreActor, bBlockingOnly);
}

bool FPhysicsScene::SweepBox(const FRay& WorldRay, float MaxDistance, const FVector& HalfExtent,
	const FQuat& Rotation, FHitResult& OutHit, const AActor* IgnoreActor, bool bBlockingOnly) const
{
	// 월드 크기의 반크기와 시작 회전으로 박스 구성
	const physx::PxBoxGeometry Geometry(U2PVector(HalfExtent));
	return QuerySingle(WorldRay, MaxDistance, &Geometry, U2PQuat(Rotation), OutHit, IgnoreActor, bBlockingOnly);
}

bool FPhysicsScene::SweepCapsule(const FRay& WorldRay, float MaxDistance, float Radius,
	float HalfHeight, const FQuat& Rotation, FHitResult& OutHit, const AActor* IgnoreActor, bool bBlockingOnly) const
{
	// 반구를 포함한 반높이 검증과 구체 경계 처리
	if (!std::isfinite(HalfHeight) || !std::isfinite(Radius) ||
		Radius <= 0.0f || HalfHeight < Radius)
	{
		OutHit = FHitResult();
		return false;
	}
	if (HalfHeight == Radius)
	{
		return SweepSphere(WorldRay, MaxDistance, Radius, OutHit, IgnoreActor, bBlockingOnly);
	}

	// 원통 반길이 산출과 PhysX X축의 Hitori Z축 보정
	const physx::PxCapsuleGeometry Geometry(Radius, HalfHeight - Radius);
	const physx::PxQuat NativeRotation = U2PQuat(Rotation) * U2PSphylBasis;
	return QuerySingle(WorldRay, MaxDistance, &Geometry, NativeRotation, OutHit, IgnoreActor, bBlockingOnly);
}


bool FPhysicsScene::QuerySingle(const FRay& WorldRay, float MaxDistance,
	const physx::PxGeometry* SweepGeometry, const physx::PxQuat& Rotation, FHitResult& OutHit,
	const AActor* IgnoreActor, bool bBlockingOnly) const
{
	// 이전 결과 초기화와 PhysX 입력 확인
	OutHit = FHitResult();
	if (!Scene || !std::isfinite(MaxDistance) || MaxDistance <= 0.0f ||
		(SweepGeometry && (!physx::PxGeometryQuery::isValid(*SweepGeometry) || !Rotation.isUnit())))
	{
		return false;
	}

	const physx::PxVec3 Start = U2PVector(WorldRay.Origin);
	const physx::PxVec3 UnitDir = U2PVector(WorldRay.Direction.GetSafeNormal());
	const float TraceDistance = SweepGeometry ? std::min(MaxDistance, PX_MAX_SWEEP_DISTANCE) : MaxDistance;
	const physx::PxVec3 End = Start + UnitDir * TraceDistance;
	if (!Start.isFinite() || !UnitDir.isNormalized() || !End.isFinite())
	{
		return false;
	}
	OutHit.Init(P2UVector(Start), P2UVector(End));

	// 정적·동적 Shape의 사전 필터 구성
	FCollisionQueryFilterCallback Filter(*this, IgnoreActor, bBlockingOnly);
	const physx::PxQueryFilterData FilterData(physx::PxQueryFlag::eSTATIC |
		physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER);

	// 쿼리 실행과 히트 대상 역참조의 읽기 잠금
	const physx::PxSceneReadLock ReadLock(*Scene);

	// 최근접 블로킹 히트 하나의 수집과 복수 Touch 결과의 확장 지점
	physx::PxLocationHit Hit;

	// 셰이프 충돌 분기
	if (SweepGeometry)
	{
		physx::PxSweepBuffer Hits;
		if (!Scene->sweep(*SweepGeometry, physx::PxTransform(Start, Rotation), UnitDir,
			TraceDistance, Hits, physx::PxHitFlag::eDEFAULT | physx::PxHitFlag::eMTD, FilterData, &Filter) ||
			!Hits.hasBlock)
		{
			return false;
		}
		Hit = Hits.block;
	}
	// 광선 충돌 체크 분기
	else
	{
		physx::PxRaycastBuffer Hits;
		if (!Scene->raycast(Start, UnitDir, TraceDistance, Hits, physx::PxHitFlag::eDEFAULT,
			FilterData, &Filter) || !Hits.hasBlock)
		{
			return false;
		}
		Hit = Hits.block;
	}

	// 이동 거리와 시작 겹침 깊이의 분리
	OutHit.bBlockingHit = true;
	OutHit.bStartPentrating = Hit.hadInitialOverlap();
	OutHit.Distance = std::max(Hit.distance, 0.0f);
	OutHit.Time = OutHit.Distance / TraceDistance;
	OutHit.PenetrationDepth = std::max(-Hit.distance, 0.0f);
	OutHit.Location = P2UVector(Start + UnitDir * OutHit.Distance);

	// 유효한 접점·법선·면 인덱스와 컴포넌트 복원
	OutHit.ImpactPoint = Hit.flags.isSet(physx::PxHitFlag::ePOSITION)
		? P2UVector(Hit.position) : OutHit.Location;
	OutHit.ImpactNormal = Hit.flags.isSet(physx::PxHitFlag::eNORMAL)
		? P2UVector(Hit.normal) : P2UVector(-UnitDir);
	OutHit.Normal = OutHit.ImpactNormal;
	if (Hit.flags.isSet(physx::PxHitFlag::eFACE_INDEX) &&
		Hit.faceIndex <= static_cast<physx::PxU32>(std::numeric_limits<int32>::max()))
	{
		OutHit.FaceIndex = static_cast<int32>(Hit.faceIndex);
	}
	OutHit.HitComponent = FindHitComponent(Hit.actor);
	return true;
}

void FPhysicsScene::SyncComponentsToBodies()
{
	TArray<UPrimitiveComponent*> Components;

	{
		const physx::PxSceneReadLock ReadLock(*Scene);
		// fetchResults 직후에만 유효한 목록. 다음 simulate 전에 다 읽는다
		physx::PxU32 NumActors = 0;
		physx::PxActor** Actors = Scene->getActiveActors(NumActors);
		for (physx::PxU32 i = 0; i < NumActors; ++i)
		{
			const auto* Body = static_cast<const FBodyInstance*>(Actors[i]->userData);
			UPrimitiveComponent* Component = Body ? Body->OwnerComponent : nullptr;
			// PostPhysics에서 루트 기준으로 동기화할 래그돌 본 제외
			if (Component && Body->InstanceBodyIndex == INDEX_NONE)
			{
				Components.Add(Component);
			}
		}
	}
	// 씬 잠금 해제 후 엔진 트랜스폼 갱신
	for (UPrimitiveComponent* Component : Components)
	{
		Component->SyncComponentToRBPhysics(); // 시뮬레이션 중이 아니면 안에서 바로 return
	}
}
