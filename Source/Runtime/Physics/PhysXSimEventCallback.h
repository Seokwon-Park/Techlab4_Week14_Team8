#pragma once

#include "Containers/Array.h"
#include "Math/Vector.h"
#include <PxSimulationEventCallback.h>

class FPhysicsScene;
class UPrimitiveComponent;

class FPhysXSimEventCallback final : public physx::PxSimulationEventCallback
{
public:
    explicit FPhysXSimEventCallback(const FPhysicsScene& InScene);

    // 기본 필터 판정과 최초 접촉 보고 요청
    static physx::PxFilterFlags FilterShader(
        physx::PxFilterObjectAttributes AttributesA, physx::PxFilterData FilterDataA,
        physx::PxFilterObjectAttributes AttributesB, physx::PxFilterData FilterDataB,
        physx::PxPairFlags& OutPairFlags, const void* ConstantBlock, physx::PxU32 ConstantBlockSize);

    // 물리 스텝 종료 후 대기 이벤트 전달
    bool HasPendingEvents() const { return bOverlapSettingsChanged || !PendingEvents.IsEmpty(); }
    void DispatchPending();
    void RefreshOverlapEvents(const UPrimitiveComponent* Component);

private:
    enum class EEvent { Hit, BeginOverlap, EndOverlap };

    struct FComponentPair
    {
        UPrimitiveComponent* A = nullptr;
        UPrimitiveComponent* B = nullptr;
        uint32 UuidA = 0;
        uint32 UuidB = 0;

        bool Matches(const FComponentPair& Other) const;
        bool IsLive() const;
    };

    // 제거 보고에서도 비교 가능한 Shape 주소
    struct FShapePair
    {
        const physx::PxShape* Trigger;
        const physx::PxShape* Other;
    };

    struct FOverlapState
    {
        FComponentPair Components;
        TArray<FShapePair> ShapePairs;
        bool bWasOverlapping = false;
        bool bRefreshEvents = false;
    };

    struct FPendingEvent
    {
        FComponentPair Components;
        EEvent Type;
        FVector LocalPoints[2];
        FVector LocalNormals[2];
        float ContactImpulse = 0.0f;
    };

    void onContact(const physx::PxContactPairHeader& Header,
        const physx::PxContactPair* Pairs, physx::PxU32 Count) override;
    void onTrigger(physx::PxTriggerPair* Pairs, physx::PxU32 Count) override;

    // 이번 단계에서 수집하지 않는 SDK 이벤트
    void onConstraintBreak(physx::PxConstraintInfo*, physx::PxU32) override {}
    void onWake(physx::PxActor**, physx::PxU32) override {}
    void onSleep(physx::PxActor**, physx::PxU32) override {}
    void onAdvance(const physx::PxRigidBody* const*, const physx::PxTransform*, physx::PxU32) override {}

    FComponentPair FindComponentPair(const physx::PxRigidActor* ActorA,
        const physx::PxRigidActor* ActorB) const;
    void AddTriggerPair(const physx::PxTriggerPair& Pair);
    void RemoveTriggerPair(const physx::PxTriggerPair& Pair);
    void QueueChangedOverlapEvents();
    void DispatchTo(const FPendingEvent& Event, int32 Side, UPrimitiveComponent& Self, UPrimitiveComponent& Other) const;

    const FPhysicsScene& Scene;
    TArray<FOverlapState> ActiveOverlaps;
    bool bOverlapSettingsChanged = false;
    TArray<FPendingEvent> PendingEvents;
};
