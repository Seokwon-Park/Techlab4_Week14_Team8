#pragma once

#include <PxQueryFiltering.h>

class AActor;
class FPhysicsScene;

// UE 4.27 CollisionQueryFilterCallback.h의 경량화
// 무시 목록과 채널별 응답 판정의 확장 지점
class FCollisionQueryFilterCallback final : public physx::PxQueryFilterCallback
{
public:
	// bBlockingOnly는 UWorld::SweepSingle과 같은 뜻이다.
	// false: 보이는 메시만 (bBlockComponent 무시, Shape 컴포넌트 제외)
	// true: bBlockComponent인 메시와 Shape 컴포넌트 (표시 여부 무시, 스켈레탈 본 바디 제외)
	FCollisionQueryFilterCallback(const FPhysicsScene& InScene, const AActor* InIgnoreActor, bool bInBlockingOnly);

	physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData&, const physx::PxShape* Shape,
		const physx::PxRigidActor* Actor, physx::PxHitFlags&) override;
	physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData&, const physx::PxQueryHit&) override;

private:
	const FPhysicsScene& PhysicsScene;
	const AActor* IgnoreActor;
	bool bBlockingOnly;
};