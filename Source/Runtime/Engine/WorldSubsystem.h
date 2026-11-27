// Source/Runtime/Engine/WorldSubsystem.h   (담당: B)
// 공유 계약 4: 레벨 수명 인터페이스. 문서: Docs/Week14/SharedContract.md
#pragma once
#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Math/Vector.h"
#include "Math/Box.h"
#include <memory>

class UWorld;
class ATerrain;
class APlayerStart;

// 질문의 LevelData& 자리. LoadWorld가 끝난 뒤 B가 채워 모든 서브시스템에 같은 것을 넘긴다.
struct FLevelLoadContext
{
    UWorld*  World   = nullptr;
    FString  MapPath;
    ATerrain* Terrain = nullptr;            // nullptr일 수 있다 (던전 레벨)
    TArray<APlayerStart*> PlayerStarts;     // 비어 있으면 원점에 스폰
    FBox     NavBounds;                     // 지형 AABB ∪ 스태틱 메시 AABB. 비면 내비 빌드 생략
    bool bHasNavBounds = false;
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

    // 매 프레임 월드 Tick 맨 앞에서 호출, 에디터 월드에서도 호출
    // 디버그 라인 수명 관리, 비동기 NavMesh 빌드완료 확인과 같은 프레임마다 할 일을 둔다.
    virtual void Tick(float DeltaTime) {};

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

// 클래스마다 하나씩 생기는 주소를 "타입 이름표"로 쓴다.
// 함수 템플릿 안의 static 변수는 실행 파일 전체에서 타입마다 정확히 하나이므로, 그 주소를 비교하면
// RTTI(dynamic_cast) 없이도 "이 서브시스템이 T인가"를 알 수 있다. GetSubsystem<T>()가 이걸로 찾는다.
template <class T>
const void* GetWorldSubsystemTypeKey()
{
    static const char Key = 0;
    return &Key;
}

// TODO(B): 구현 필요. 받은 팩토리를 전역 목록에 쌓고, FWorldSubsystemCollection::Initialize가 그 목록으로 생성한다.
void RegisterWorldSubsystemClass(const char* ClassName, const void* TypeKey, FWorldSubsystemFactory Factory);

#define REGISTER_WORLD_SUBSYSTEM(ClassName) \
    static const bool ClassName##_bWorldSubsystemRegistered = \
        (RegisterWorldSubsystemClass(#ClassName, GetWorldSubsystemTypeKey<ClassName>(), \
            []() -> FWorldSubsystem* { return new ClassName(); }), true)

// UWorld 안에 하나. UE FSubsystemCollection 축소판.
// TODO(B): 아래 멤버 함수 구현 필요 (지금은 WorldSubsystem.cpp의 빈 스텁)
class FWorldSubsystemCollection
{
public:
    FWorldSubsystemCollection() = default;
    ~FWorldSubsystemCollection();

    FWorldSubsystemCollection(const FWorldSubsystemCollection&) = delete;
    FWorldSubsystemCollection& operator=(const FWorldSubsystemCollection&) = delete;

    // World->GetSubsystem<FNavigationSubsystem>(). 없거나(ShouldCreateSubsystem=false) 아직 Initialize 전이면 nullptr
    template<class T>
    T* GetSubsystem() const
    {
        const void* Key = GetWorldSubsystemTypeKey<T>();
        for (const FEntry& Entry : Entries)
        { 
            if (Entry.TypeKey == Key)
            {
                return static_cast<T*>(Entry.Instance.get());
            }
        }
        return nullptr;
    }

    void Initialize(UWorld& World);              // 등록된 클래스 생성 + Initialize
    void LoadLevel(const FLevelLoadContext& C);  // GetLoadOrder 오름차순으로 OnLevelLoad
    void BeginPlay(UWorld& World);
    void Tick(float DeltaTime);
    void UnloadLevel();                          // 내림차순으로 OnLevelUnload
    FWorldSubsystemStats GatherStats() const;

    bool IsLevelLoaded() const { return bLevelLoaded; }
    int32 Num() const { return Entries.Num(); }

private:
    struct FEntry
    {
        const void* TypeKey = nullptr;
        const char* ClassName = nullptr;
        TUniquePtr<FWorldSubsystem> Instance;
    };

    // Initialize에서 GetLoadOrder 오름차순으로 정렬
    TArray<FEntry> Entries;
    bool bInitialized = false;
    bool bLevelLoaded = false;
};
