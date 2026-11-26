#pragma once

#include "Math/Transform.h"

struct FBodyInstance;
class FPhysicsScene;

namespace physx
{
	class PxRigidDynamic;
	class PxD6Joint;
}

// 형상 없는 키네마틱 액터(KinActorData)를 목표 위치로 옮기고, 잡은 바디와 D6 조인트(HandleData)의 선형 드라이브로 끌어당긴다
class FPhysicsHandle
{
public:
	FPhysicsHandle() = default;
	FPhysicsHandle(const FPhysicsHandle&) = delete;
	FPhysicsHandle& operator=(const FPhysicsHandle&) = delete;
	~FPhysicsHandle() { ReleaseComponent(); }

	// 키네마틱 바디는 잡지 않는다
	bool GrabComponentAtLocation(FPhysicsScene& InScene, FBodyInstance& InBody, const FVector& GrabLocation);
	void SetTargetLocation(const FVector& NewLocation);
	// 잡은 바디를 지우기 전에 불러야 한다
	void ReleaseComponent();

	bool IsGrabbing() const { return HandleData != nullptr; }
	FBodyInstance* GetGrabbedBody() const { return GrabbedBody; }
	FVector GetGrabbedLocation() const;
	const FVector& GetTargetLocation() const { return TargetLocation; }

	float LinearStiffness = 750.0f;
	float LinearDamping = 200.0f;

private:
	physx::PxRigidDynamic* KinActorData = nullptr;
	physx::PxD6Joint* HandleData = nullptr;
	FPhysicsScene* Scene = nullptr;
	FBodyInstance* GrabbedBody = nullptr;
	FVector GrabbedLocalOffset;
	FVector TargetLocation;
};
