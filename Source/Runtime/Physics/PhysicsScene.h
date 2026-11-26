#pragma once

#include "Core/Delegate/Delegate.h"
#include "Containers/Array.h"
#include <geometry/PxGeometryHelpers.h>
#include <cstdint>
#include <memory>

class AActor;
class FCollisionQueryFilterCallback;
class FPhysXSimEventCallback;
class UPrimitiveComponent;
struct FHitResult;
struct FRay;
struct FVector;
struct FQuat;

namespace physx
{
	class PxScene;
	class PxGeometry;
	class PxQuat;
	class PxRigidActor;
}

// 씬 읽기 잠금 안에서 복사한 단순 충돌 형상
struct FPhysicsOverlapShape
{
	physx::PxGeometryHolder Geometry;
	physx::PxTransform WorldPose;
	float DistanceSquared;
	std::uintptr_t ShapeOrder; // 같은 거리에서의 실행 중 정렬 기준
};

DECLARE_DELEGATE(FPhysicsStepDelegate, float);

class FPhysicsScene
{
public:
	FPhysicsScene();
	~FPhysicsScene();

	// 네이티브 씬 소유권 중복 방지
	FPhysicsScene(const FPhysicsScene&) = delete;
	FPhysicsScene& operator=(const FPhysicsScene&) = delete;

	// 월드에서 결정한 Z축 중력 적용
	void SetGravityZ(float GravityZ);

	// 월드 시간 누적과 고정 스텝 실행
	void Tick(float DeltaSeconds);

	// 물리 누적 시간과 마지막 틱 통계 초기화
	void ResetStepTime()
	{
		AccumulatedSeconds = 0.0;
		LastTickStepCount = 0;
		LastTickStepTimeMs = 0.0;
		LastTickEventTimeMs = 0.0;
	}
	int32 GetLastTickStepCount() const { return LastTickStepCount; }
	double GetLastTickStepTimeMs() const { return LastTickStepTimeMs; }
	double GetLastTickEventTimeMs() const { return LastTickEventTimeMs; }
	float GetInterpolationAlpha() const;
	// 겹쳐 있는 고체의 이벤트 허용 상태 변경 전달
	void RefreshOverlapEvents(const UPrimitiveComponent* Component);
	// 바디 생성과 차량 쿼리용 비소유 씬 접근
	physx::PxScene* GetPxScene() const { return Scene; }
	static constexpr uint32 ContactReportBit = 1u << 31; // simulation filter word3

	// 월드 공간 정점 충돌 검사
	bool Raycast(const FRay& WorldRay, float MaxDistance, FHitResult& OutHit,
		const AActor* IgnoreActor = nullptr, bool bBlockingOnly = false) const;

	// 월드공간 구체 충돌 검사
	bool SweepSphere(const FRay& WorldRay, float MaxDistance, float Radius, FHitResult& OutHit,
		const AActor* IgnoreActor = nullptr, bool bBlockingOnly = false) const;

	// 월드공간 박스 충돌 검사
	bool SweepBox(const FRay& WorldRay, float MaxDistance, const FVector& HalfExtent,
		const FQuat& Rotation, FHitResult& OutHit, const AActor* IgnoreActor = nullptr,
		bool bBlockingOnly = false) const;

	// 월드공간 캡슐 충돌 검사
	bool SweepCapsule(const FRay& WorldRay, float MaxDistance, float Radius, float HalfHeight,
		const FQuat& Rotation, FHitResult& OutHit, const AActor* IgnoreActor = nullptr,
		bool bBlockingOnly = false) const;

	// 검색 박스와 겹치는 물리 형상의 거리순 조회
	void GatherSimulationShapes(const physx::PxBoxGeometry& Box, const physx::PxTransform& Pose,
		const AActor* IgnoreActor, TArray<FPhysicsOverlapShape>& OutShapes) const;

	// 물리 계산 전후 Delegate
	FPhysicsStepDelegate OnPreStep;
	FPhysicsStepDelegate OnPostStep;

private:
	friend class FCollisionQueryFilterCallback;
	friend class FPhysXSimEventCallback;

	std::unique_ptr<FPhysXSimEventCallback> SimulationEvents; // PhysX 이벤트 콜백
	physx::PxScene* Scene = nullptr;
	double AccumulatedSeconds = 0.0;

	// 마지막 물리 틱의 완료 스텝 수와 처리 시간 합계
	int32 LastTickStepCount = 0;
	double LastTickStepTimeMs = 0.0;
	// 게임 스레드에서 Hit·Overlap을 수신자에게 전달한 시간
	double LastTickEventTimeMs = 0.0;

	// PhysX 쿼리, 엔진 히트 결과
	bool QuerySingle(const FRay& WorldRay, float MaxDistance, const physx::PxGeometry* SweepGeometry,
		const physx::PxQuat& Rotation, FHitResult& OutHit, const AActor* IgnoreActor, bool bBlockingOnly) const;

	// 액터 사용자 데이터의 소유 컴포넌트 조회
	UPrimitiveComponent* FindHitComponent(const physx::PxRigidActor* Actor) const;

	// UE FPhysScene_PhysX::SyncComponentsToBodies. 이번 스텝에 움직인 액터의 컴포넌트를 바디 위치로 옮긴다
	void SyncComponentsToBodies();
};
