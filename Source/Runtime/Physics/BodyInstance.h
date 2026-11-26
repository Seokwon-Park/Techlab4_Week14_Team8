#pragma once

#include "Math/Transform.h"
#include "Math/Box.h"

class FPhysicsScene;
class UPrimitiveComponent;
class UBodySetup;
struct FConstraintInstance;

namespace physx
{
    class PxRigidActor;
    class PxRigidDynamic;
}

// 바디 초기 운동 방식
enum class EPhysicsBodyType
{
    Static,
    Dynamic,
    Kinematic
};

enum class ECollisionEnabled : uint8
{
    QueryOnly,
    QueryAndPhysics
};

// QueryOnly 바디의 Shape는 query filter data word3에 이 비트를 켠다.
// PxBatchQuery 사전 필터(차량 서스펜션 레이캐스트)는 Shape 플래그를 못 보고 filter data만 받기 때문이다
inline constexpr uint32 QueryOnlyShapeQueryBit = 1u << 30;

// 단일 리지드 액터의 수명과 강체 자세 관리
struct FBodyInstance
{
    FBodyInstance() = default;
    ~FBodyInstance();

    // 물리 콜백의 역참조를 위한 주소 고정
    FBodyInstance(const FBodyInstance&) = delete;
    FBodyInstance& operator=(const FBodyInstance&) = delete;
    FBodyInstance(FBodyInstance&&) = delete;
    FBodyInstance& operator=(FBodyInstance&&) = delete;

    // 바디 형상 생성, 초기 물성 및 씬 등록
    bool InitBody(const UBodySetup& InBodySetup, const FTransform& InWorldTransform,
        UPrimitiveComponent* InOwnerComponent, FPhysicsScene& InScene, EPhysicsBodyType InBodyType);

    // 씬 등록 해제와 액터 정리
    void TermBody();
    // 등록된 형상의 충돌·쿼리 플래그 동기화
    void UpdateCollisionSettings();

    // 월드 위치·회전 조회
    FTransform GetWorldTransform() const;
    // 월드 자세 즉시 변경
    void SetBodyTransform(const FTransform& InWorldTransform);

    // 동적·키네마틱 모드 전환
    void SetInstanceSimulatePhysics(bool bSimulate);
    void SetKinematicTarget(const FTransform& InWorldTransform);

    // 바디별 CCD 사용 설정
    void SetUseCCD(bool bInUseCCD);

    // 차량 설정용 빌린 액터 포인터, 미초기화·정적 바디는 nullptr
    physx::PxRigidDynamic* GetRigidDynamic();

    // InitBody 전에 정한다. 바꾸려면 바디를 다시 만든다
    ECollisionEnabled CollisionEnabled = ECollisionEnabled::QueryAndPhysics;

    int32 InstanceBodyIndex = INDEX_NONE;
    int32 InstanceBoneIndex = INDEX_NONE;
    bool IsValidBodyInstance() const { return Actor != nullptr; }
    // Shape 크기에 구워 넣은 스케일. PhysX 자세에는 스케일이 없어서 따로 보관한다
    const FVector& GetScale3D() const { return Scale3D; }
    void SetLinearVelocity(const FVector& NewVel, bool bAddToCurrent);

    // 시뮬레이션 중인 동적 바디에만 적용된다
    void AddImpulseAtPosition(const FVector& Impulse, const FVector& Position);
    void AddForceAtPosition(const FVector& Force, const FVector& Position);
    bool IsInstanceSimulatingPhysics() const;
    FVector GetUnrealWorldVelocity() const;
    float GetBodyMass() const;
    // 모든 형상을 감싸는 월드 AABB. 바디가 없으면 Min > Max인 빈 박스
    FBox GetBodyBounds() const;

private:
    friend class FPhysicsScene;
    friend struct FConstraintInstance;

    void ApplyCollisionSettings(physx::PxRigidActor& RigidActor, const UPrimitiveComponent* Component) const;

    // 비소유 씬·컴포넌트 참조와 소유 액터
    FPhysicsScene* Scene = nullptr;
    UPrimitiveComponent* OwnerComponent = nullptr;
    physx::PxRigidActor* Actor = nullptr;
    FVector Scale3D = FVector(1.0f, 1.0f, 1.0f);

    // 동적 모드에서 적용할 CCD 사용 여부
    bool bUseCCD = false;
};
