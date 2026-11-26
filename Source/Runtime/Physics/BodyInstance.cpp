#include "EnginePCH.h"
#include "Physics/BodyInstance.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicalMaterial.h"
#include "Physics/PhysicsSettings.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysXConversion.h"
#include "Physics/PhysXSDK.h"

#include <cmath>
#include <memory>
#include <PxSceneLock.h>

#include <extensions/PxRigidActorExt.h>
#include <extensions/PxRigidBodyExt.h>

namespace
{
    // 스윕 CCD(eENABLE_CCD)는 PhysX 4.1.2 Release DLL에서 CCD 접촉이 생기면 바디 자세가 NaN이 된다(Debug DLL은 정상)
    constexpr physx::PxRigidBodyFlag::Enum BodyCCDFlag = physx::PxRigidBodyFlag::eENABLE_SPECULATIVE_CCD;
}

FBodyInstance::~FBodyInstance()
{
    // 액터 수명의 최종 정리
    TermBody();
}

bool FBodyInstance::InitBody(const UBodySetup& InBodySetup, const FTransform& InWorldTransform,
    UPrimitiveComponent* InOwnerComponent, FPhysicsScene& InScene, EPhysicsBodyType InBodyType)
{
    // 중복 초기화와 필수 PhysX 객체 확인
    physx::PxPhysics* Physics = PhysXSDK::GetPhysics();
    physx::PxScene* NativeScene = InScene.GetPxScene();
    if (Actor || !Physics || !NativeScene)
    {
        return false;
    }

    // 강체 자세와 양의 스케일 확인. PhysX에는 미러가 없어 음수 스케일은 받지 않는다
    const physx::PxTransform WorldPose = U2PTransform(InWorldTransform);
    const FVector& Scale = InWorldTransform.Scale;
    if (!WorldPose.isValid() || !U2PVector(Scale).isFinite() || U2PVector(Scale).minElement() <= 0.0f)
    {
        return false;
    }

    // 재질 계수 선택과 지원 형상 확인
    const FKAggregateGeom& Geom = InBodySetup.AggGeom;
    const UPhysicalMaterial* PhysMaterial = InBodySetup.PhysMaterial;
    const float StaticFriction = PhysMaterial ? PhysMaterial->StaticFriction : InBodySetup.Friction;
    const float DynamicFriction = PhysMaterial ? PhysMaterial->Friction : InBodySetup.Friction;
    const float Restitution = PhysMaterial ? PhysMaterial->Restitution : InBodySetup.Restitution;
    if (Geom.GetElementCount() == 0 || Geom.ConvexElems.Num() > 0 ||
        !std::isfinite(StaticFriction) || StaticFriction < 0.0f ||
        !std::isfinite(DynamicFriction) || DynamicFriction < 0.0f ||
        !(Restitution >= 0.0f && Restitution <= 1.0f))
    {
        return false;
    }

    // 실패 경로에서 PhysX 생성 참조의 자동 해제
    const auto Release = [](auto* Object) { Object->release(); };

    std::unique_ptr<physx::PxMaterial, decltype(Release)> Material(
        Physics->createMaterial(StaticFriction, DynamicFriction, Restitution), Release);
    if (!Material)
    {
        return false;
    }

    // 항목별 프로젝트 기본값과 재질 오버라이드 적용
    const FPhysicsSettings& Settings = FPhysicsSettings::Get();
    const EPhysicalMaterialCombineMode FrictionMode =
        PhysMaterial && PhysMaterial->bOverrideFrictionCombineMode
        ? PhysMaterial->FrictionCombineMode : Settings.DefaultFrictionCombineMode;
    const EPhysicalMaterialCombineMode RestitutionMode =
        PhysMaterial && PhysMaterial->bOverrideRestitutionCombineMode
        ? PhysMaterial->RestitutionCombineMode : Settings.DefaultRestitutionCombineMode;

    Material->setFrictionCombineMode(U2PCombineMode(FrictionMode));
    Material->setRestitutionCombineMode(U2PCombineMode(RestitutionMode));

    const bool bStatic = InBodyType == EPhysicsBodyType::Static;
    std::unique_ptr<physx::PxRigidActor, decltype(Release)> NewActor(
        bStatic ? static_cast<physx::PxRigidActor*>(Physics->createRigidStatic(WorldPose))
        : Physics->createRigidDynamic(WorldPose),
        Release);
    if (!NewActor)
    {
        return false;
    }

    // 미등록 액터에 형상과 로컬 자세 설정
    TArray<physx::PxShape*> ShapesExcludedFromMass;
    const auto AttachShape = [&](const auto& Geometry, const physx::PxTransform& LocalPose,
        const FKShapeElem& Elem)
        {
            if (!Geometry.isValid() || !LocalPose.isValid())
            {
                return false;
            }

            physx::PxShape* Shape = physx::PxRigidActorExt::createExclusiveShape(
                *NewActor, Geometry, *Material);
            if (!Shape)
            {
                return false;
            }

            Shape->setLocalPose(LocalPose);
            if (!bStatic && !Elem.bContributeToMass)
            {
                ShapesExcludedFromMass.Add(Shape);
            }
            return true;
        };

    // 비균일 스케일은 회전 없는 Box만 정확하다. 나머지는 PhysicsDebugDraw와 같은 규칙으로 근사해
    // 그려지는 와이어프레임과 충돌 크기를 맞춘다
    const float SphereScale = std::max({ Scale.X, Scale.Y, Scale.Z });
    for (const FKSphereElem& Elem : Geom.SphereElems)
    {
        const physx::PxSphereGeometry Geometry(Elem.Radius * SphereScale);
        const physx::PxTransform LocalPose(U2PVector(Elem.Center * Scale));
        if (!AttachShape(Geometry, LocalPose, Elem))
        {
            return false;
        }
    }

    // 요소가 회전해 있으면 shear를 버리고 박스 로컬 축에 그대로 곱한다
    for (const FKBoxElem& Elem : Geom.BoxElems)
    {
        const physx::PxBoxGeometry Geometry(0.5f * Elem.X * Scale.X, 0.5f * Elem.Y * Scale.Y, 0.5f * Elem.Z * Scale.Z);
        const physx::PxTransform LocalPose(U2PVector(Elem.Center * Scale), U2PQuat(Elem.Rotation.Quaternion()));
        if (!AttachShape(Geometry, LocalPose, Elem))
        {
            return false;
        }
    }

    const float SphylRadiusScale = std::max(Scale.X, Scale.Y);
    for (const FKSphylElem& Elem : Geom.SphylElems)
    {
        const physx::PxTransform LocalPose = U2PSphylLocalPose(Elem.Center * Scale, Elem.Rotation);
        const float HalfHeight = 0.5f * Elem.Length * Scale.Z;
        // PhysX 캡슐은 반높이가 0이면 무효라 바디 전체가 만들어지지 않는다. 길이 0 캡슐은 같은 반지름의 구로 만든다
        const bool bAttached = HalfHeight > 0.0f
            ? AttachShape(physx::PxCapsuleGeometry(Elem.Radius * SphylRadiusScale, HalfHeight), LocalPose, Elem)
            : AttachShape(physx::PxSphereGeometry(Elem.Radius * SphylRadiusScale), LocalPose, Elem);
        if (!bAttached)
        {
            return false;
        }
    }

    if (physx::PxRigidDynamic* DynamicActor = NewActor->is<physx::PxRigidDynamic>())
    {
        // 재질 밀도(g/cm³)를 미터 단위 PhysX 밀도(kg/m³)로 변환
        constexpr float KgPerM3PerGramPerCm3 = 1000.0f;
        const float MassOrDensity = InBodySetup.bOverrideMass ? InBodySetup.MassInKgOverride
            : (PhysMaterial ? PhysMaterial->Density : 1.0f) * KgPerM3PerGramPerCm3;
        if (NewActor->getNbShapes() == static_cast<physx::PxU32>(ShapesExcludedFromMass.Num()) ||
            !std::isfinite(MassOrDensity) || MassOrDensity <= 0.0f)
        {
            return false;
        }

        // 비질량 형상을 관성 계산 동안만 제외
        for (physx::PxShape* Shape : ShapesExcludedFromMass)
        {
            Shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
        }

        const bool bMassReady = InBodySetup.bOverrideMass
            ? physx::PxRigidBodyExt::setMassAndUpdateInertia(*DynamicActor, MassOrDensity)
            : physx::PxRigidBodyExt::updateMassAndInertia(*DynamicActor, MassOrDensity);
        if (!bMassReady)
        {
            return false;
        }

        for (physx::PxShape* Shape : ShapesExcludedFromMass)
        {
            Shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, true);
        }
        const bool bKinematic = InBodyType == EPhysicsBodyType::Kinematic;
        DynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, bKinematic);
        DynamicActor->setRigidBodyFlag(BodyCCDFlag, !bKinematic && bUseCCD);
    }

    // 질량·관성 계산이 끝난 형상에 실제 충돌 정책 적용
    ApplyCollisionSettings(*NewActor, InOwnerComponent);

    // 씬 등록과 바디 역참조 설정의 쓰기 잠금
    const physx::PxSceneWriteLock WriteLock(*NativeScene);
    NativeScene->addActor(*NewActor);
    if (NewActor->getScene() != NativeScene)
    {
        return false;
    }

    Actor = NewActor.release();
    Scene = &InScene;
    OwnerComponent = InOwnerComponent;
    Scale3D = InWorldTransform.Scale;
    Actor->userData = this;
    return true;
}

void FBodyInstance::TermBody()
{
    // 콜백 역참조 해제와 네이티브 액터 반환
    if (Actor)
    {
        const physx::PxSceneWriteLock WriteLock(*Scene->GetPxScene());
        Actor->userData = nullptr;
        Actor->release();
        Actor = nullptr;
    }

    Scene = nullptr;
    OwnerComponent = nullptr;
}

void FBodyInstance::ApplyCollisionSettings(physx::PxRigidActor& RigidActor, const UPrimitiveComponent* Component) const
{
    using namespace physx;
    const AActor* Owner = Component ? Component->GetOwner() : nullptr;
    const bool bEnabled = !Owner || Owner->GetActorEnableCollision();
    const bool bQueryOnly = CollisionEnabled == ECollisionEnabled::QueryOnly;
    const bool bTrigger = bEnabled && bQueryOnly && Component && !Component->bBlockComponent
        && Component->bGenerateOverlapEvents;
    const PxU32 ShapeCount = RigidActor.getNbShapes();
    for (PxU32 Index = 0; Index < ShapeCount; ++Index)
    {
        PxShape* Shape = nullptr;
        RigidActor.getShapes(&Shape, 1, Index);
        // Trigger와 Simulation의 상호 배타적 전환 및 디버그 표시 유지
        PxShapeFlags Flags = Shape->getFlags();
        Flags.clear(PxShapeFlag::eSIMULATION_SHAPE);
        Flags.clear(PxShapeFlag::eTRIGGER_SHAPE);
        Flags.clear(PxShapeFlag::eSCENE_QUERY_SHAPE);
        if (bEnabled)
        {
            Flags |= PxShapeFlag::eSCENE_QUERY_SHAPE;
            if (!bQueryOnly)
            {
                Flags |= PxShapeFlag::eSIMULATION_SHAPE;
            }
            else if (bTrigger)
            {
                Flags |= PxShapeFlag::eTRIGGER_SHAPE;
            }
        }
        if (Flags != Shape->getFlags())
        {
            Shape->setFlags(Flags);
        }

        // 차량 쿼리에서 센서 제외 및 차체 전용 비트 보존
        PxFilterData Filter = Shape->getQueryFilterData();
        if (((Filter.word3 & QueryOnlyShapeQueryBit) != 0) != bQueryOnly)
        {
            Filter.word3 = (Filter.word3 & ~QueryOnlyShapeQueryBit) | (bQueryOnly ? QueryOnlyShapeQueryBit : 0u);
            Shape->setQueryFilterData(Filter);
        }
    }
}

void FBodyInstance::UpdateCollisionSettings()
{
    if (Actor)
    {
        const physx::PxSceneWriteLock WriteLock(*Scene->GetPxScene());
        ApplyCollisionSettings(*Actor, OwnerComponent);
        Scene->RefreshOverlapEvents(OwnerComponent);
    }
}

FTransform FBodyInstance::GetWorldTransform() const
{
    if (!Actor)
    {
        return FTransform::Identity;
    }

    // 네이티브 바디 자세 조회의 읽기 잠금
    const physx::PxSceneReadLock ReadLock(*Scene->GetPxScene());
    return P2UTransform(Actor->getGlobalPose());
}

// 호출 즉시 이동
void FBodyInstance::SetBodyTransform(const FTransform& InWorldTransform)
{
    const physx::PxTransform WorldPose = U2PTransform(InWorldTransform);
    if (!Actor || !WorldPose.isValid())
    {
        return;
    }

    const physx::PxSceneWriteLock WriteLock(*Scene->GetPxScene());

    // 키네마틱 순간 이동 시 이전 목표 덮어쓰기
    physx::PxRigidDynamic* DynamicActor = GetRigidDynamic();
    if (DynamicActor &&
        DynamicActor->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC) &&
        !DynamicActor->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION))
    {
        DynamicActor->setKinematicTarget(WorldPose);
    }

    // 현재 강체 자세의 즉시 변경
    Actor->setGlobalPose(WorldPose);
}

// 동적 시뮬레이션 API
void FBodyInstance::SetInstanceSimulatePhysics(bool bSimulate)
{
    physx::PxRigidDynamic* DynamicActor = GetRigidDynamic();
    if (!DynamicActor)
    {
        return;
    }

    // 바디 플래그 조회와 모드 전환의 쓰기 잠금
    const physx::PxSceneWriteLock WriteLock(*Scene->GetPxScene());
    if (DynamicActor->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION))
    {
        return;
    }

    // 요청 모드가 현재 모드와 같은 경우의 생략
    const bool bKinematic = !bSimulate;
    if (DynamicActor->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC) == bKinematic)
    {
        return;
    }

    if (bKinematic)
    {
        // 키네마틱 전환 전 CCD 해제
        DynamicActor->setRigidBodyFlag(BodyCCDFlag, false);
    }

    DynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, bKinematic);

    if (bSimulate)
    {
        // 동적 복귀 시 CCD 복원과 시뮬레이션 재개
        DynamicActor->setRigidBodyFlag(BodyCCDFlag, bUseCCD);
        DynamicActor->wakeUp();
    }
}

void FBodyInstance::SetKinematicTarget(const FTransform& InWorldTransform)
{
    physx::PxRigidDynamic* DynamicActor = GetRigidDynamic();
    if (!DynamicActor)
    {
        return;
    }

    // 바디 플래그 조회와 키네마틱 목표 설정의 쓰기 잠금
    const physx::PxSceneWriteLock WriteLock(*Scene->GetPxScene());
    if (!DynamicActor->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC) ||
        DynamicActor->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION))
    {
        return;
    }

    // 다음 물리 스텝에서 도달할 월드 자세 등록
    const physx::PxTransform WorldPose = U2PTransform(InWorldTransform);
    if (WorldPose.isValid())
    {
        DynamicActor->setKinematicTarget(WorldPose);
    }
}

void FBodyInstance::SetUseCCD(bool bInUseCCD)
{
    physx::PxRigidDynamic* DynamicActor = GetRigidDynamic();
    if (!DynamicActor)
    {
        // 초기화 전 또는 정적 바디의 CCD 설정 저장
        bUseCCD = bInUseCCD;
        return;
    }

    // 등록된 바디의 CCD 설정과 플래그 변경 잠금
    const physx::PxSceneWriteLock WriteLock(*Scene->GetPxScene());
    bUseCCD = bInUseCCD;

    if (!DynamicActor->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC))
    {
        // 동적 모드의 CCD 설정 즉시 반영
        DynamicActor->setRigidBodyFlag(BodyCCDFlag, bUseCCD);
    }
}

physx::PxRigidDynamic* FBodyInstance::GetRigidDynamic()
{
    // 액터가 Dynamic일시 반환
    return Actor ? Actor->is<physx::PxRigidDynamic>() : nullptr;
}

// false는 대입, true는 가산
void FBodyInstance::SetLinearVelocity(const FVector& NewVel, bool bAddToCurrent)
{
    physx::PxRigidDynamic* DynamicActor = GetRigidDynamic();
    if (!DynamicActor)
    {
        return;
    }

    // 바디 플래그 조회와 선속도 갱신의 쓰기 잠금
    const physx::PxSceneWriteLock WriteLock(*Scene->GetPxScene());
    if (DynamicActor->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC) ||
        DynamicActor->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION))
    {
        return;
    }

    // 기존 속도 가산 또는 새 속도로 교체
    const physx::PxVec3 Velocity = U2PVector(NewVel);
    const physx::PxVec3 FinalVelocity = bAddToCurrent ? DynamicActor->getLinearVelocity() + Velocity : Velocity;
    if (FinalVelocity.isFinite())
    {
        DynamicActor->setLinearVelocity(FinalVelocity);
    }
}

namespace
{
    // 키네마틱이거나 시뮬레이션이 꺼진 바디는 nullptr
    physx::PxRigidDynamic* GetSimulatingActor(physx::PxRigidActor* Actor)
    {
        physx::PxRigidDynamic* DynamicActor = Actor ? Actor->is<physx::PxRigidDynamic>() : nullptr;
        if (!DynamicActor ||
            DynamicActor->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC) ||
            DynamicActor->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION))
        {
            return nullptr;
        }
        return DynamicActor;
    }
}

void FBodyInstance::AddImpulseAtPosition(const FVector& Impulse, const FVector& Position)
{
    if (!Actor) return;
    const physx::PxSceneWriteLock WriteLock(*Scene->GetPxScene());
    physx::PxRigidDynamic* DynamicActor = GetSimulatingActor(Actor);
    const physx::PxVec3 PxImpulse = U2PVector(Impulse);
    if (DynamicActor && PxImpulse.isFinite())
        physx::PxRigidBodyExt::addForceAtPos(*DynamicActor, PxImpulse, U2PVector(Position), physx::PxForceMode::eIMPULSE);
}

void FBodyInstance::AddForceAtPosition(const FVector& Force, const FVector& Position)
{
    if (!Actor) return;
    const physx::PxSceneWriteLock WriteLock(*Scene->GetPxScene());
    physx::PxRigidDynamic* DynamicActor = GetSimulatingActor(Actor);
    const physx::PxVec3 PxForce = U2PVector(Force);
    if (DynamicActor && PxForce.isFinite())
        physx::PxRigidBodyExt::addForceAtPos(*DynamicActor, PxForce, U2PVector(Position), physx::PxForceMode::eFORCE);
}

bool FBodyInstance::IsInstanceSimulatingPhysics() const
{
    if (!Actor) return false;
    const physx::PxSceneReadLock ReadLock(*Scene->GetPxScene());
    return GetSimulatingActor(Actor) != nullptr;
}

FVector FBodyInstance::GetUnrealWorldVelocity() const
{
    if (!Actor) return FVector::ZeroVector;
    const physx::PxSceneReadLock ReadLock(*Scene->GetPxScene());
    const physx::PxRigidDynamic* DynamicActor = Actor->is<physx::PxRigidDynamic>();
    return DynamicActor ? P2UVector(DynamicActor->getLinearVelocity()) : FVector::ZeroVector;
}

FBox FBodyInstance::GetBodyBounds() const
{
    if (!Actor) return FBox{ FVector(1.0f, 1.0f, 1.0f), FVector(-1.0f, -1.0f, -1.0f) };
    const physx::PxSceneReadLock ReadLock(*Scene->GetPxScene());
    const physx::PxBounds3 Bounds = Actor->getWorldBounds();
    return FBox{ P2UVector(Bounds.minimum), P2UVector(Bounds.maximum) };
}

float FBodyInstance::GetBodyMass() const
{
    if (!Actor) return 0.0f;
    const physx::PxSceneReadLock ReadLock(*Scene->GetPxScene());
    const physx::PxRigidDynamic* DynamicActor = Actor->is<physx::PxRigidDynamic>();
    return DynamicActor ? DynamicActor->getMass() : 0.0f;
}
