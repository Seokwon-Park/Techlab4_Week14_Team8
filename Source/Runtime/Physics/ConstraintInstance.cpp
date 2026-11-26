#include "EnginePCH.h"
#include "ConstraintInstance.h"
#include "Physics/PhysicsScene.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysXConversion.h"
#include "Physics/PhysXSDK.h"

#include <cmath>
#include <PxSceneLock.h>
#include <extensions/PxD6Joint.h>

FTransform FConstraintInstance::GetRefFrame(int32 Frame) const
{
    const bool bFrame1 = Frame == 0;
    const FVector Pri = (bFrame1 ? PriAxis1 : PriAxis2).GetSafeNormal();
    const FVector Sec = (bFrame1 ? SecAxis1 : SecAxis2).GetSafeNormal();
    const FVector& Pos = bFrame1 ? Pos1 : Pos2;

    const FVector AxisZ = Pri.Cross(Sec).GetSafeNormal();
    FMatrix M = FMatrix::Identity;
    M.SetAxis(0, Pri);
    M.SetAxis(1, AxisZ.Cross(Pri));
    M.SetAxis(2, AxisZ);
    M[3][0] = Pos.X;
    M[3][1] = Pos.Y;
    M[3][2] = Pos.Z;
    return FTransform::FromMatrix(M);
}


bool FConstraintInstance::InitConstraint(FBodyInstance* Body1, FBodyInstance* Body2, float Scale)
{
    // 중복 생성과 필수 입력 확인
    physx::PxPhysics* Physics = PhysXSDK::GetPhysics();
    if (ConstraintHandle || !Physics || !Body1 || !Body2 ||
        !std::isfinite(Scale) || Scale <= 0.0f)
    {
        return false;
    }

    physx::PxRigidActor* Actor1 = Body1->Actor;
    physx::PxRigidActor* Actor2 = Body2->Actor;
    FPhysicsScene* BodyScene = Body1->Scene;
    if (!Actor1 || !Actor2 || Actor1 == Actor2 || !BodyScene || BodyScene != Body2->Scene)
    {
        return false;
    }

    // 유효한 로컬 프레임 확인과 앵커 위치 스케일 적용
    FTransform Frame1 = GetRefFrame(0);
    FTransform Frame2 = GetRefFrame(1);
    if (!(Frame1.Scale.Z > 0.0f && Frame2.Scale.Z > 0.0f))
    {
        return false;
    }

    Frame1.Location = Frame1.Location * Scale;
    Frame2.Location = Frame2.Location * Scale;
    const physx::PxTransform LocalFrame1 = U2PTransform(Frame1);
    const physx::PxTransform LocalFrame2 = U2PTransform(Frame2);
    if (!LocalFrame1.isValid() || !LocalFrame2.isValid())
    {
        return false;
    }

    // 씬 등록 확인과 D6 조인트 설정의 쓰기 잠금
    physx::PxScene* NativeScene = BodyScene->GetPxScene();
    const physx::PxSceneWriteLock WriteLock(*NativeScene);
    if (Actor1->getScene() != NativeScene || Actor2->getScene() != NativeScene ||
        (!Actor1->is<physx::PxRigidDynamic>() && !Actor2->is<physx::PxRigidDynamic>()))
    {
        return false;
    }

    // 부모와 자식 순서의 D6 조인트 생성
    ConstraintHandle = physx::PxD6JointCreate(*Physics, Actor2, LocalFrame2, Actor1, LocalFrame1);

    if (!ConstraintHandle)
    {
        return false;
    }

    // 해제 시 잠글 씬 기록과 회전 제한 적용
    ConstraintScene = NativeScene;
    ApplyAngularLimits(*ConstraintHandle);
    return true;
}

void FConstraintInstance::ApplyAngularLimits(physx::PxD6Joint& Joint) const
{
    using PxAxis = physx::PxD6Axis;
    using PxMotion = physx::PxD6Motion;

    // 엔진의 회전 모드를 PhysX 모드로 변환
    const auto ResolveMotion = [](EAngularConstraintMotion Mode, float LimitDegrees)
        {
            if (Mode == ACM_Free)
            {
                return PxMotion::eFREE;
            }
            if (Mode == ACM_Limited && std::isfinite(LimitDegrees) && LimitDegrees > 0.0f)
            {
                return PxMotion::eLIMITED;
            }
            return PxMotion::eLOCKED;
        };

    const FTwistConstraint& Twist = ProfileInstance.TwistLimit;
    const FConeConstraint& Swing = ProfileInstance.ConeLimit;
    const auto TwistMotion = ResolveMotion(Twist.TwistMotion, Twist.TwistLimitDegrees);
    const auto SwingYMotion = ResolveMotion(Swing.Swing2Motion, Swing.Swing2LimitDegrees);
    const auto SwingZMotion = ResolveMotion(Swing.Swing1Motion, Swing.Swing1LimitDegrees);

    // 관절 로컬 X·Y·Z 회전 모드 설정
    Joint.setMotion(PxAxis::eTWIST, TwistMotion);
    Joint.setMotion(PxAxis::eSWING1, SwingYMotion);
    Joint.setMotion(PxAxis::eSWING2, SwingZMotion);

    // X축 비틀기의 대칭 각도 범위 설정
    if (TwistMotion == PxMotion::eLIMITED)
    {
        const float TwistRadians = FMath::DegreesToRadians(FMath::Min(Twist.TwistLimitDegrees, 180.0f));
        Joint.setTwistLimit(physx::PxJointAngularLimitPair(-TwistRadians, TwistRadians));
    }

    // 반대 축의 자유 여부에 따른 유효 Swing 반각 산출
    const auto ResolveSwingRadians = [](PxMotion::Enum Mode, PxMotion::Enum OtherMode, float LimitDegrees)
        {
            if (Mode != PxMotion::eLIMITED)
            {
                // PxJointLimitCone에 유효한 값을 넣어주기 위한 값. 제한이 없음은 모드로 확인함.
                return physx::PxHalfPi;
            }

            const float MaxDegrees = OtherMode == PxMotion::eFREE ? 89.9f : 179.9f;
            return FMath::DegreesToRadians(FMath::Clamp(LimitDegrees, 0.01f, MaxDegrees));
        };

    // PhysX 인자 순서에 맞춘 Y·Z 각도 범위 설정
    if (SwingYMotion == PxMotion::eLIMITED || SwingZMotion == PxMotion::eLIMITED)
    {
        const float SwingY = ResolveSwingRadians(SwingYMotion, SwingZMotion, Swing.Swing2LimitDegrees);
        const float SwingZ = ResolveSwingRadians(SwingZMotion, SwingYMotion, Swing.Swing1LimitDegrees);
        Joint.setSwingLimit(physx::PxJointLimitCone(SwingY, SwingZ));
    }
}

void FConstraintInstance::TermConstraint()
{
    // 네이티브 조인트 반환과 핸들 초기화
    if (ConstraintHandle)
    {
        const physx::PxSceneWriteLock WriteLock(*ConstraintScene);
        // 조인트 해제 전 연결 액터 조회
        physx::PxRigidActor* Actor0 = nullptr;
        physx::PxRigidActor* Actor1 = nullptr;
        ConstraintHandle->getActors(Actor0, Actor1);
        ConstraintHandle->release();
        ConstraintHandle = nullptr;

        // 연결 해제 이후 잠든 동적 바디의 시뮬레이션 재개
        for (physx::PxRigidActor* Actor : { Actor0, Actor1 })
        {
            auto* Dynamic = Actor ? Actor->is<physx::PxRigidDynamic>() : nullptr;
            if (Dynamic && Dynamic->getScene() == ConstraintScene &&
                !Dynamic->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC) &&
                !Dynamic->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION) &&
                Dynamic->isSleeping())
            {
                Dynamic->wakeUp();
            }
        }
        ConstraintScene = nullptr;
    }
}