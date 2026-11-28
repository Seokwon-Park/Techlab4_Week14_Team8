#include "EnginePCH.h"
#include "Engine/WorldSubsystem.h"
#include "Engine/World.h"
#include <algorithm>

namespace
{
	// REGISTER_WORLD_SUBSYSTEM 한 줄이 남기는 정보. 클래스당 한 개
	struct FWorldSubsystemRegistration
	{
		const char* ClassName;
		const void* TypeKey;
		FWorldSubsystemFactory Factory;
	};

	// 등록 목록. 함수 안의 static으로 두는 이유:
	// 등록 매크로는 각 .cpp의 전역 변수 초기화(main 이전)에서 실행되는데, .cpp끼리의 초기화 순서는 정해져 있지 않다.
	// 전역 변수로 두면 목록보다 등록이 먼저 실행돼 빈 객체에 쓰는 사고가 날 수 있다.
	// 함수 안 static은 처음 불릴 때 만들어지므로 누가 먼저 불러도 안전하다.
	TArray<FWorldSubsystemRegistration>& GetRegistry()
	{
		static TArray<FWorldSubsystemRegistration> Registry;
		return Registry;
	}
}

void RegisterWorldSubsystemClass(const char* ClassName, const void* TypeKey, FWorldSubsystemFactory Factory)
{
	// 같은 서브 시스템 등록 방지
	for (const FWorldSubsystemRegistration& Existing : GetRegistry())
	{
		if (Existing.TypeKey == TypeKey)
		{
			return;
		}
	}
	GetRegistry().Add({ ClassName, TypeKey, Factory });
}

FWorldSubsystemCollection::~FWorldSubsystemCollection()
{
	UnloadLevel();
	while (Entries.Num() > 0)
		Entries.RemoveLast();
}

void FWorldSubsystemCollection::Initialize(UWorld& World)
{
	if (bInitialized) return;
	bInitialized = true;

	for (const FWorldSubsystemRegistration& Registration : GetRegistry())
	{
		TUniquePtr<FWorldSubsystem> Instance(Registration.Factory());

		// 에셋 에디터 프리뷰 월드에는 NavMesh가 필요 없다 -> 서브시스템이 스스로 false를 돌려 빠진다.
		if (!Instance || !Instance->ShouldCreateSubsystem(World))
			continue;
		Instance->Initialize(World);

		FEntry Entry;
		Entry.TypeKey = Registration.TypeKey;
		Entry.ClassName = Registration.ClassName;
		Entry.Instance = std::move(Instance);
		Entries.Add(std::move(Entry));
	}

	// 순서표(지형 100 → 충돌 200 → 내비 300 → 레벨 흐름 400)를 여기서 한 번 정렬해 둔다.
	// 등록 순서는 .cpp 초기화 순서라서 믿을 수 없기 때문에 별도로 정렬한다. stable_sort라 같은 값끼리는 등록 순서를 지킨다.
	std::stable_sort(Entries.begin(), Entries.end(), [](const FEntry& A, const FEntry& B)
		{
			return A.Instance->GetLoadOrder() < B.Instance->GetLoadOrder();
		});
}

void FWorldSubsystemCollection::LoadLevel(const FLevelLoadContext& Context)
{
	// 같은 월드에 레벨을 다시 읽는 경우(에디터에서 씬 열기, 실행 취소)에는 이전 레벨 자원부터 놓는다.
	// 그래야 "언로드가 먼저, 로드가 나중" 규칙이 어느 경로에서든 지켜진다.
	UnloadLevel();

	for (FEntry& Entry : Entries)
		Entry.Instance->OnLevelLoad(Context);
	bLevelLoaded = true;

	HTR_LOG(Info, "World subsystems loaded level '{}' ({} subsystems, terrain: {})",
		Context.MapPath, Entries.Num(), Context.Terrain ? "yes" : "no");
}

void FWorldSubsystemCollection::BeginPlay(UWorld& World)
{
	for (FEntry& Entry : Entries)
		Entry.Instance->OnWorldBeginPlay(World);
}

void FWorldSubsystemCollection::Tick(float DeltaTime)
{
	for (FEntry& Entry : Entries)
		Entry.Instance->Tick(DeltaTime);
}

void FWorldSubsystemCollection::UnloadLevel()
{
	// 로드된 적이 없거나 이미 언로드됐으면 아무것도 하지 않는다.
	// ClearWorld, 소멸자, LoadLevel이 모두 이 함수를 부르므로 여러 번 불려도 한 번만 동작해야 한다.
	if (!bLevelLoaded) return;
	bLevelLoaded = false;

	// 로드의 역순: 레벨 흐름(400) → 내비(300) → 충돌(200) → 지형(100).
	// 내비는 지형 지오메트리를, 충돌은 지형 높이를 참조할 수 있으므로 참조하는 쪽부터 놓는다.
	FString Order;
	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		Entries[Index].Instance->OnLevelUnload();
		Order += (Order.empty() ? "" : " -> ") + FString(Entries[Index].ClassName);
	}

	// "언로드가 정말 역순으로, 로드보다 먼저 불렸는지"를 로그로 확인할 수 있게 남긴다.
	HTR_LOG(Info, "World subsystems unloaded level: {}", Order);
}

FWorldSubsystemStats FWorldSubsystemCollection::GatherStats() const
{
	// 각 서브시스템이 자기 숫자를 더한다. 5일차 누수 검사(레벨 왕복 후 숫자가 처음과 같은지)에 쓴다.
	FWorldSubsystemStats Total;
	for (const FEntry& Entry : Entries)
		Entry.Instance->GetStats(Total);
	return Total;
}
