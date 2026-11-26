#include "EnginePCH.h"
#include "PhysicsHandle.h"
#include "Physics/PhysicsScene.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysXConversion.h"
#include "Physics/PhysXSDK.h"

#include <PxSceneLock.h>
#include <extensions/PxD6Joint.h>

bool FPhysicsHandle::GrabComponentAtLocation(FPhysicsScene& InScene, FBodyInstance& InBody, const FVector& GrabLocation)
{
	ReleaseComponent();

	physx::PxRigidDynamic* Dynamic = InBody.GetRigidDynamic();
	physx::PxScene* NativeScene = InScene.GetPxScene();
	physx::PxPhysics* Physics = PhysXSDK::GetPhysics();
	if (!Dynamic || !NativeScene || !Physics)
	{
		return false;
	}

	const physx::PxSceneWriteLock WriteLock(*NativeScene);
	if (Dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)
	{
		return false;
	}

	const physx::PxVec3 Location = U2PVector(GrabLocation);
	const physx::PxTransform LocalFrame(Dynamic->getGlobalPose().transformInv(Location));

	KinActorData = Physics->createRigidDynamic(physx::PxTransform(Location));
	if (!KinActorData)
	{
		return false;
	}
	KinActorData->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
	NativeScene->addActor(*KinActorData);

	HandleData = physx::PxD6JointCreate(*Physics, KinActorData, physx::PxTransform(physx::PxIdentity), Dynamic, LocalFrame);
	if (!HandleData)
	{
		KinActorData->release();
		KinActorData = nullptr;
		return false;
	}
	HandleData->setMotion(physx::PxD6Axis::eX, physx::PxD6Motion::eFREE);
	HandleData->setMotion(physx::PxD6Axis::eY, physx::PxD6Motion::eFREE);
	HandleData->setMotion(physx::PxD6Axis::eZ, physx::PxD6Motion::eFREE);
	HandleData->setMotion(physx::PxD6Axis::eTWIST, physx::PxD6Motion::eFREE);
	HandleData->setMotion(physx::PxD6Axis::eSWING1, physx::PxD6Motion::eFREE);
	HandleData->setMotion(physx::PxD6Axis::eSWING2, physx::PxD6Motion::eFREE);
	// 가속도 드라이브라 바디 질량과 무관하게 같은 세기로 끌린다
	const physx::PxD6JointDrive Drive(LinearStiffness, LinearDamping, PX_MAX_F32, true);
	HandleData->setDrive(physx::PxD6Drive::eX, Drive);
	HandleData->setDrive(physx::PxD6Drive::eY, Drive);
	HandleData->setDrive(physx::PxD6Drive::eZ, Drive);
	HandleData->setDrivePosition(physx::PxTransform(physx::PxIdentity));
	Dynamic->wakeUp();

	Scene = &InScene;
	GrabbedBody = &InBody;
	GrabbedLocalOffset = P2UVector(LocalFrame.p);
	TargetLocation = GrabLocation;
	return true;
}

void FPhysicsHandle::SetTargetLocation(const FVector& NewLocation)
{
	if (!IsGrabbing())
	{
		return;
	}

	TargetLocation = NewLocation;
	const physx::PxSceneWriteLock WriteLock(*Scene->GetPxScene());
	KinActorData->setKinematicTarget(physx::PxTransform(U2PVector(TargetLocation)));
	// 끌려가는 바디가 잠들면 드라이브가 멈춘다
	if (physx::PxRigidDynamic* Dynamic = GrabbedBody->GetRigidDynamic())
	{
		Dynamic->wakeUp();
	}
}

void FPhysicsHandle::ReleaseComponent()
{
	if (!KinActorData)
	{
		return;
	}

	{
		const physx::PxSceneWriteLock WriteLock(*Scene->GetPxScene());
		if (HandleData)
		{
			HandleData->release();
		}
		KinActorData->release();
	}
	HandleData = nullptr;
	KinActorData = nullptr;
	Scene = nullptr;
	GrabbedBody = nullptr;
}

FVector FPhysicsHandle::GetGrabbedLocation() const
{
	if (!GrabbedBody)
	{
		return TargetLocation;
	}
	const FTransform BodyWorld = GrabbedBody->GetWorldTransform();
	return BodyWorld.Location + BodyWorld.Rotation.RotateVector(GrabbedLocalOffset);
}
