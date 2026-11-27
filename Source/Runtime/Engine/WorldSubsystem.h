// Source/Runtime/Engine/WorldSubsystem.h   (담당: B)
// 공유 계약 4: 레벨 수명 인터페이스. 문서: Docs/Week14/SharedContract.md
#pragma once
#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Math/Vector.h"
#include "Math/Box.h"

class UWorld;
class ATerrain;
class APlayerStart;

// 질문의 LevelData& 자리. LoadWorld가 끝난 뒤 B가 채워 모든 서브시스템에 같은 것을 넘긴다.
struct FLevelLoadContext
{
    UWorld*  World   = nullptr;
    FString  MapPath;
    ATerrain* Terrain = nullptr;            // ★ nullptr일 수 있다 (던전 레벨)
    TArray<APlayerStart*> PlayerStarts;     // 비어 있으면 원점에 스폰
    FBox     NavBounds;                     // 지형 AABB ∪ 스태틱 메시 AABB. 비면 내비 빌드 생략
};

// 시스템별 자원 수. 5일차 누수 검사용 Stat에 그대로 나온다.
struct FWorldSubsystemStats
{
    uint64 GPUBytes = 0;
    int32  PhysXActors = 0;
    uint64 NavMeshBytes = 0;
};

// UE UWorldSubsystem과 같은 수명. UWorld가 소유하고 UWorld와 함께 사라진다.
class FWorldSubsystem
{
public:
    virtual ~FWorldSubsystem() = default;

    // UE ShouldCreateSubsystem. 예: 프리뷰 월드에는 내비를 만들지 않는다.
    virtual bool ShouldCreateSubsystem(const UWorld& World) const { return true; }

    // UE Initialize. 월드 생성 직후. 포인터·설정만 잡는다. GPU/PhysX/Nav 자원 생성 금지.
    virtual void Initialize(UWorld& World) { OwningWorld = &World; }

    // = OnLevelLoad. UE OnWorldComponentsUpdated 자리.
    // 옛 월드가 완전히 지워진 뒤 호출된다. 무거운 자원은 여기서 만든다.
    virtual void OnLevelLoad(const FLevelLoadContext& Context) {}

    // UE OnWorldBeginPlay. 게임/PIE에서만. 텔레포트, 경로 요청 등.
    virtual void OnWorldBeginPlay(UWorld& World) {}

    // = OnLevelUnload. UE Deinitialize. 역순으로 호출되며, 이때 액터와 PxScene은 아직 살아 있다.
    virtual void OnLevelUnload() {}

    // 작은 값이 먼저 로드되고, 언로드는 반대 순서.
    virtual int32 GetLoadOrder() const = 0;

    virtual void GetStats(FWorldSubsystemStats& Out) const {}

    UWorld* GetWorld() const { return OwningWorld; }

private:
    UWorld* OwningWorld = nullptr;
};

// ── 서브시스템 등록 (계약 4 추가분) ─────────────────────────────
// 각자 자기 .cpp 맨 아래에 REGISTER_WORLD_SUBSYSTEM(FMySubsystem); 한 줄만 쓴다.
// 공용 등록 목록 파일이 없으므로 서로 같은 파일을 고칠 일이 없다.
// 엔진은 정적 라이브러리지만 exe가 /WHOLEARCHIVE로 링크하므로 정적 등록 객체가 버려지지 않는다 (UCLASS와 같은 방식).
using FWorldSubsystemFactory = FWorldSubsystem* (*)();

// TODO(B): 구현 필요. 받은 팩토리를 전역 목록에 쌓고, FWorldSubsystemCollection::Initialize가 그 목록으로 생성한다.
void RegisterWorldSubsystemClass(const char* ClassName, FWorldSubsystemFactory Factory);

#define REGISTER_WORLD_SUBSYSTEM(ClassName) \
    static const bool ClassName##_bWorldSubsystemRegistered = \
        (RegisterWorldSubsystemClass(#ClassName, []() -> FWorldSubsystem* { return new ClassName(); }), true)

// UWorld 안에 하나. UE FSubsystemCollection 축소판.
// TODO(B): 아래 멤버 함수 구현 필요 (지금은 WorldSubsystem.cpp의 빈 스텁)
class FWorldSubsystemCollection
{
public:
    // World->GetSubsystem<FNavigationSubsystem>()
    // TODO(B): 생성된 서브시스템 목록에서 찾도록 구현 (지금은 항상 nullptr → 호출하는 쪽은 nullptr을 처리해야 한다)
    template<class T> T* GetSubsystem() const { return nullptr; }
    void Initialize(UWorld& World);              // 등록된 클래스 생성 + Initialize
    void LoadLevel(const FLevelLoadContext& C);  // GetLoadOrder 오름차순으로 OnLevelLoad
    void BeginPlay(UWorld& World);
    void UnloadLevel();                          // 내림차순으로 OnLevelUnload
    FWorldSubsystemStats GatherStats() const;
};
