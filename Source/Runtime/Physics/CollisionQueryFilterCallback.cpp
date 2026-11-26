#include "EnginePCH.h"
#include "Physics/CollisionQueryFilterCallback.h"

#include "Components/PrimitiveComponent.h"
#include "Components/ShapeComponent.h"
#include "GameFramework/Actor.h"
#include "Physics/PhysicsScene.h"

#include <PxShape.h>

FCollisionQueryFilterCallback::FCollisionQueryFilterCallback(
	const FPhysicsScene& InScene, const AActor* InIgnoreActor, bool bInBlockingOnly)
	: PhysicsScene(InScene), IgnoreActor(InIgnoreActor), bBlockingOnly(bInBlockingOnly)
{
}

physx::PxQueryHitType::Enum FCollisionQueryFilterCallback::preFilter(const physx::PxFilterData&,
	const physx::PxShape* Shape, const physx::PxRigidActor* Actor, physx::PxHitFlags&)
{
	if (Shape->getFlags().isSet(physx::PxShapeFlag::eTRIGGER_SHAPE))
	{
		return physx::PxQueryHitType::eNONE;
	}

	const UPrimitiveComponent* Component = PhysicsScene.FindHitComponent(Actor);
	const AActor* Owner = Component ? Component->GetOwner() : nullptr;
	if (!Owner || Owner == IgnoreActor || !Owner->GetActorEnableCollision())
	{
		return physx::PxQueryHitType::eNONE;
	}

	if (bBlockingOnly)
	{
		// 스켈레탈 본 바디는 제외한다. 카메라가 캐릭터 래그돌에 걸리지 않게 한다
		const bool bShapeOrMesh = Cast<UShapeComponent>(Component) || Component->GetMeshData();
		if (!Component->bBlockComponent || !bShapeOrMesh)
		{
			return physx::PxQueryHitType::eNONE;
		}
	}
	else if (!Component->IsShown(true) || Cast<UShapeComponent>(Component))
	{
		return physx::PxQueryHitType::eNONE;
	}
	return physx::PxQueryHitType::eBLOCK;
}

// PhysX 필터 인터페이스의 후처리 반환값
physx::PxQueryHitType::Enum FCollisionQueryFilterCallback::postFilter(
	const physx::PxFilterData&, const physx::PxQueryHit&)
{
	return physx::PxQueryHitType::eBLOCK;
}