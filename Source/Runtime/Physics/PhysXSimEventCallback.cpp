#include "EnginePCH.h"
#include "Physics/PhysXSimEventCallback.h"
#include "Components/PrimitiveComponent.h"
#include "Components/ShapeComponent.h"
#include "GameFramework/Actor.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysXConversion.h"
#include "UObject/UObjectHash.h"
#include <extensions/PxDefaultSimulationFilterShader.h>

FPhysXSimEventCallback::FPhysXSimEventCallback(const FPhysicsScene& InScene)
    : Scene(InScene)
{
}

physx::PxFilterFlags FPhysXSimEventCallback::FilterShader(
    physx::PxFilterObjectAttributes AttributesA, physx::PxFilterData FilterDataA,
    physx::PxFilterObjectAttributes AttributesB, physx::PxFilterData FilterDataB,
    physx::PxPairFlags& OutPairFlags, const void* ConstantBlock, physx::PxU32 ConstantBlockSize)
{
    using namespace physx;

    // SDK 기본 필터 정책 유지
    const PxFilterFlags Result = PxDefaultSimulationFilterShader(AttributesA, FilterDataA,
        AttributesB, FilterDataB, OutPairFlags, ConstantBlock, ConstantBlockSize);

    // 충돌 노티파이 추가
    if (Result == PxFilterFlag::eDEFAULT &&
        !PxFilterObjectIsTrigger(AttributesA) && !PxFilterObjectIsTrigger(AttributesB))
    {
        OutPairFlags |= PxPairFlag::eNOTIFY_TOUCH_FOUND;
        OutPairFlags |= PxPairFlag::eDETECT_CCD_CONTACT;
        if ((FilterDataA.word3 | FilterDataB.word3) & FPhysicsScene::ContactReportBit)
            OutPairFlags |= PxPairFlag::eNOTIFY_CONTACT_POINTS;
    }
    return Result;
}

bool FPhysXSimEventCallback::FComponentPair::Matches(const FComponentPair& Other) const
{
    // 수신 순서와 무관한 컴포넌트 쌍 비교
    return (A == Other.A && B == Other.B && UuidA == Other.UuidA && UuidB == Other.UuidB)
        || (A == Other.B && B == Other.A && UuidA == Other.UuidB && UuidB == Other.UuidA);
}

bool FPhysXSimEventCallback::FComponentPair::IsLive() const
{
    // 삭제와 주소 재사용 확인 후 등록 상태 검사
    uint32 CurrentA = 0;
    uint32 CurrentB = 0;
    return TryGetLiveObjectUUID(A, nullptr, CurrentA) && CurrentA == UuidA
        && TryGetLiveObjectUUID(B, nullptr, CurrentB) && CurrentB == UuidB
        && A->IsRegistered() && B->IsRegistered();
}

FPhysXSimEventCallback::FComponentPair FPhysXSimEventCallback::FindComponentPair(
    const physx::PxRigidActor* ActorA, const physx::PxRigidActor* ActorB) const
{
    // 기존 userData 역참조와 수신 컴포넌트 확인
    UPrimitiveComponent* A = Scene.FindHitComponent(ActorA);
    UPrimitiveComponent* B = Scene.FindHitComponent(ActorB);
    if (!A || !B || !A->IsRegistered() || !B->IsRegistered()
        || !A->GetOwner() || !B->GetOwner() || A->GetOwner() == B->GetOwner())
    {
        return {};
    }
    return { A, B, A->GetUUID(), B->GetUUID() };
}

void FPhysXSimEventCallback::onContact(const physx::PxContactPairHeader& Header,
    const physx::PxContactPair* Pairs, physx::PxU32 Count)
{
    using namespace physx;

    // 제거된 액터의 역참조 방지
    if (Header.flags.isSet(PxContactPairHeaderFlag::eREMOVED_ACTOR_0)
        || Header.flags.isSet(PxContactPairHeaderFlag::eREMOVED_ACTOR_1))
    {
        return;
    }

    // 네이티브 액터 쌍의 최초 접촉만 수집
    for (PxU32 Index = 0; Index < Count; ++Index)
    {
        const PxContactPair& Pair = Pairs[Index];
        // 액터간 첫 접촉 시작 여부 확인
        if (Pair.events.isSet(PxPairFlag::eNOTIFY_TOUCH_FOUND)
            && Pair.flags.isSet(PxContactPairFlag::eACTOR_PAIR_HAS_FIRST_TOUCH))
        {
            const FComponentPair Components = FindComponentPair(Header.actors[0], Header.actors[1]);
            if (Components.A)
            {
                FPendingEvent Event{ Components, EEvent::Hit };
                PxContactPairPoint Contacts[16];
                const PxU32 Extracted = Pair.extractContacts(Contacts, 16);
                for (PxU32 ContactIndex = 0; ContactIndex < Extracted; ++ContactIndex)
                {
                    const PxContactPairPoint& Contact = Contacts[ContactIndex];
                    const float Impulse = Contact.impulse.magnitude();
                    if (Impulse <= Event.ContactImpulse)
                        continue;

                    Event.ContactImpulse = Impulse;
                    for (int32 Side = 0; Side < 2; ++Side)
                    {
                        const PxTransform Pose = Header.actors[Side]->getGlobalPose();
                        Event.LocalPoints[Side] = P2UVector(Pose.transformInv(Contact.position));
                        Event.LocalNormals[Side] = P2UVector(Pose.q.rotateInv(Side == 0 ? Contact.normal : -Contact.normal));
                    }
                }
                PendingEvents.Add(Event);
            }
            return;
        }
    }
}

void FPhysXSimEventCallback::AddTriggerPair(const physx::PxTriggerPair& Pair)
{
    using namespace physx;

    // TriggerPair의 컴포넌트 제거 여부 확인
    if (Pair.flags.isSet(PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER)
        || Pair.flags.isSet(PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
    {
        return;
    }

    const FComponentPair Components = FindComponentPair(Pair.triggerActor, Pair.otherActor);
    // 두 도형 사이의 겹침은 Trigger-Trigger까지 검사하는 UWorld::UpdateOverlaps가 소유
    if (!Components.A || (Cast<UShapeComponent>(Components.A) && Cast<UShapeComponent>(Components.B)))
    {
        return;
    }

    // 기존 컴포넌트 쌍에 새 Shape 쌍 편입
    for (FOverlapState& State : ActiveOverlaps)
    {
        if (State.Components.Matches(Components))
        {
            State.ShapePairs.Add({ Pair.triggerShape, Pair.otherShape });
            return;
        }
    }

    // 첫 진입의 컴포넌트 겹침 상태 생성
    FOverlapState State{ Components };
    State.ShapePairs.Add({ Pair.triggerShape, Pair.otherShape });
    ActiveOverlaps.Add(std::move(State));
}

void FPhysXSimEventCallback::RemoveTriggerPair(const physx::PxTriggerPair& Pair)
{
    // 삭제된 Shape 주소의 비교 전용 사용
    for (FOverlapState& State : ActiveOverlaps)
    {
        for (int32 Index = 0; Index < State.ShapePairs.Num(); ++Index)
        {
            // Shapes중 삭제된 Pair와 같은 값을 제거
            const FShapePair& Shapes = State.ShapePairs[Index];
            if (Shapes.Trigger == Pair.triggerShape && Shapes.Other == Pair.otherShape)
            {
                State.ShapePairs.RemoveAtSwap(Index);
                return;
            }
        }
    }
}

void FPhysXSimEventCallback::onTrigger(physx::PxTriggerPair* Pairs, physx::PxU32 Count)
{
    using namespace physx;

    // 이번 보고 묶음의 Shape 진입·이탈 반영
    for (PxU32 Index = 0; Index < Count; ++Index)
    {
        const PxTriggerPair& Pair = Pairs[Index];
        if (Pair.status == PxPairFlag::eNOTIFY_TOUCH_FOUND)
        {
            AddTriggerPair(Pair);
        }
        else if (Pair.status == PxPairFlag::eNOTIFY_TOUCH_LOST)
        {
            RemoveTriggerPair(Pair);
        }
    }

    // 보고 묶음 전후의 컴포넌트 겹침 상태 비교
    for (int32 Index = ActiveOverlaps.Num() - 1; Index >= 0; --Index)
    {
        FOverlapState& State = ActiveOverlaps[Index];
        const bool bIsOverlapping = !State.ShapePairs.IsEmpty();
        if (State.bWasOverlapping != bIsOverlapping)
        {
            PendingEvents.Add({ State.Components, bIsOverlapping ? EEvent::BeginOverlap : EEvent::EndOverlap });
        }

        if (bIsOverlapping)
        {
            State.bWasOverlapping = true;
        }
        else
        {
            ActiveOverlaps.RemoveAtSwap(Index);
        }
    }
}

void FPhysXSimEventCallback::RefreshOverlapEvents(const UPrimitiveComponent* Component)
{
    for (FOverlapState& State : ActiveOverlaps)
    {
        // 고체의 이벤트 정책 변경 기록. 트리거 자체의 변경은 PhysX가 보고
        if (State.Components.B == Component)
        {
            State.bRefreshEvents = true;
            bOverlapSettingsChanged = true;
        }
    }
}

void FPhysXSimEventCallback::QueueChangedOverlapEvents()
{
    // 같은 스텝 사이의 연속 변경은 최종 상태로 한 번만 반영
    for (FOverlapState& State : ActiveOverlaps)
    {
        if (!State.bRefreshEvents)
        {
            continue;
        }
        State.bRefreshEvents = false;
        const FComponentPair& Pair = State.Components;
        if (Pair.IsLive())
        {
            const AActor* A = Pair.A->GetOwner();
            const AActor* B = Pair.B->GetOwner();
            const bool bEnabled = A && B && A->GetActorEnableCollision() && B->GetActorEnableCollision()
                && Pair.A->bGenerateOverlapEvents && Pair.B->bGenerateOverlapEvents;
            PendingEvents.Add({ Pair, bEnabled ? EEvent::BeginOverlap : EEvent::EndOverlap });
        }
    }
    bOverlapSettingsChanged = false;
}

void FPhysXSimEventCallback::DispatchPending()
{
    if (bOverlapSettingsChanged)
    {
        QueueChangedOverlapEvents();
    }

    // 대기 큐 분리와 양쪽 수신 직전 생존 확인
    TArray<FPendingEvent> Events;
    Events.Swap(PendingEvents);
    for (const FPendingEvent& Event : Events)
    {
        const FComponentPair& Pair = Event.Components;
        if (Pair.IsLive())
        {
            DispatchTo(Event, 0, *Pair.A, *Pair.B);
        }
        if (Pair.IsLive())
        {
            DispatchTo(Event, 1, *Pair.B, *Pair.A);
        }
    }
}

void FPhysXSimEventCallback::DispatchTo(const FPendingEvent& Event, int32 Side, UPrimitiveComponent& Self, UPrimitiveComponent& Other) const
{
    // 충돌 설정에 관계없는 기존 겹침 상태 정리
    if (Event.Type == EEvent::EndOverlap)
    {
        Self.EndComponentOverlap(&Other);
        return;
    }

    // 새 이벤트의 소유 액터 충돌 설정 확인
    const AActor* Owner = Self.GetOwner();
    const AActor* OtherOwner = Other.GetOwner();
    if (!Owner || !OtherOwner || !Owner->GetActorEnableCollision() || !OtherOwner->GetActorEnableCollision())
    {
        return;
    }

    if (Event.Type == EEvent::Hit)
    {
        if (Event.ContactImpulse > 0.0f)
        {
            Self.OnComponentContact.Broadcast(Event.LocalPoints[Side], Event.LocalNormals[Side], Event.ContactImpulse);
            if (!Event.Components.IsLive()) return;
        }
        Self.DispatchHit(&Other);
    }
    else if (Self.bGenerateOverlapEvents && Other.bGenerateOverlapEvents)
    {
        Self.BeginComponentOverlap(&Other);
    }
}
