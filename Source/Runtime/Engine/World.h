#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/MovementComponent.h"
#include "Components/TextRenderComponent.h"
#include "Math/Transform.h"
#include "Render/Renderer.h"
#include "Math/Frustum.h"
#include "Math/BVH.h"
#include "Engine/Scene.h"
#include "Camera/CameraActor.h"
#include "Asset/LOD/StaticMeshLODSelector.h"
#include "Engine/EngineTypes.h"
#include "Render/SceneRenderData.h"
#include "Script/CoroutineScheduler.h"
#include "World.generated.h"

class ULevel;
class FLineBatcher;
class UBillboardComponent;
class APawn;
class APlayerController;
class AGameModeBase;
class AGameStateBase;
class APlayerState;
class AHUD;
class AParticleEventManager;
class FPhysicsScene;
class UWindDirectionalSourceComponent;
struct FViewContext;

UCLASS()
class UWorld : public UObject
{
	GENERATED_BODY()

public:
	UWorld();
	virtual ~UWorld();

	bool Init();

	// 이 월드의 디버그 선(충돌 도형, 라이트 범위, 본 등)을 모아 그리는 도구. Init에서 만든다.
	// UE의 UWorld::LineBatcher와 같은 자리이고 UE에서도 public 멤버다.
	// UE는 선을 쌓아 두면 씬과 함께 그려지지만, 이 엔진은 쓰는 쪽이 BeginFrame, Add..., OnRender 순서로 직접 그린다.
	TUniquePtr<FLineBatcher> LineBatcher;

	// 파티클 이벤트를 받아 게임 코드에 전달하는 액터
	AParticleEventManager* MyParticleEventManager = nullptr;

	/*UPrimitiveComponent* SpawnPrimitive(FClass* Class);*/
	AActor* SpawnActor(UClass* Class, FName InName = NAME_None, const FTransform* Transform = nullptr);
	
	static bool AreAABBsOverlapping(const FBox& A, const FBox& B)
	{
		return
			A.Min.X <= B.Max.X && A.Max.X >= B.Min.X &&
			A.Min.Y <= B.Max.Y && A.Max.Y >= B.Min.Y &&
			A.Min.Z <= B.Max.Z && A.Max.Z >= B.Min.Z;
	}
	
	template <class T>
	T* SpawnActor(FName InName = NAME_None, const FTransform* Transform = nullptr)
	{
		return CastChecked<T>(SpawnActor(T::StaticClass(), InName, Transform));
	}

	void Tick(float RealDeltaTime);

	// 모든 액터 Tick이 끝난 뒤 1회. 지난 프레임 겹침 쌍과 비교해 End → Begin 순으로 보낸다. 담당: C
	void UpdateOverlaps();

	void ClearWorld();

	void GatherRenderPackets(FRenderQueue& RenderArray, const FViewContext* LODView = nullptr, const FFrustumPlanes* Frustum = nullptr, FRenderer* Renderer = nullptr);

	void CreateMainCamera();

	// 카메라 Get/Set
	void SetMainCamera(ACameraActor* Camera);
	ACameraActor* GetMainCamera() const { return MainCamera; }

	// Level
	ULevel* GetPersistentLevel() const { return PersistentLevel; }
	const TArray<ULevel*>& GetLevels() const { return Levels; }
	void SetPersistentLevel(ULevel* InLevel) { PersistentLevel = InLevel; }

	ULevel* GetCurrentLevel() const { return CurrentLevel; }
	void SetCurrentLevel(ULevel* InLevel) { CurrentLevel = InLevel; }

	int32 GetActorNum();

	bool DestroyActor(AActor* Actor);

	// View별 Billboard 행렬 공급자는 이 동기 호출 동안만 사용하며 저장하지 않는다.
	using FBillboardTraceTransform = FMatrix(*)(const UBillboardComponent&, const void*);
	// 현재 World의 Component에 Ray를 전달하고 가장 가까운 유효 교차를 반환한다.
	bool LineTraceSingle(const FRay& WorldRay, FHitResult& OutHit,
		FBillboardTraceTransform ResolveBillboard = nullptr, const void* ViewContext = nullptr);
	// bBlockingOnly: bBlockComponent인 도형/메시만 검사한다. 기본값은 기존 투사체 동작을 유지한다.
	// PhysX 바디가 있는 컴포넌트는 PhysX로, 없는 컴포넌트는 기존 방식으로 검사해 가까운 쪽을 쓴다.
	bool SweepSingle(const FRay& WorldRay, float MaxDistance, float Radius, FHitResult& OutHit, AActor* IgnoreActor = nullptr,
		bool bBlockingOnly = false);

	void BeginPlay();
	void EndPlay();
	bool HasBegunPlay() const { return bBegunPlay; }
	APawn* GetPlayerPawn() const { return PlayerPawn; }
	APlayerController* GetPlayerController() const { return PlayerController; }
	bool IsPaused() const { return bIsPaused; }
	void SetPaused(bool bPaused) { bIsPaused = bPaused; }

	FScene& GetScene() { return Scene; }
	FPhysicsScene* GetPhysicsScene() const { return PhysicsScene.get(); }
	// 월드에 등록된 방향성 바람의 관리와 풍속 조회(m/s)
	void RegisterWindSource(UWindDirectionalSourceComponent& Source);
	void UnregisterWindSource(UWindDirectionalSourceComponent& Source);
	FVector GetWindVelocity() const;

	FTickTaskManager& GetTickTaskManager() { return TickTaskManager; }

	bool bShouldSimulatePhysics = false;
	FCoroutineScheduler& GetCoroutineScheduler() { return CoroutineScheduler; }

	const FRenderStats& GetRenderStats() const { return RenderStats; }

	void SetWorldType(EWorldType InType) { WorldType = InType; }
	EWorldType GetWorldType() const { return WorldType; }

	static UWorld* CreateWorld(const EWorldType InWorldType, bool bInformEngineOfWorld, FName WorldName = NAME_None); /*, UPackage* InWorldPackage = NULL, bool bAddToRoot = true, ERHIFeatureLevel::Type InFeatureLevel = ERHIFeatureLevel::Num, const InitializationValues* InIVS = nullptr, bool bInSkipInitWorld = false);*/

	bool IsPlayInEditor() const { return WorldType == EWorldType::PIE; }
	bool IsGameWorld() const { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }

	static UWorld* GetDuplicatedWorldForPIE(UWorld* InWorld);

	virtual void PostDuplicate(bool bDuplicateForPIE) override;


	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	// Slomo-hitstop
	float GetEffectiveTimeDilation() const;
	// 게임 시간 (시간 배율이 적용된 누적 초). UE의 UWorld::GetTimeSeconds.
	float GetTimeSeconds() const { return TimeSeconds; }
	void StartHitStop(float Duration, float Dilation);
	// UE AWorldSettings::CinematicTimeDilation. 시퀀스 Play Rate(Slomo) 트랙이 쓴다
	float GetCinematicTimeDilation() const { return CinematicTimeDilation; }
	void SetCinematicTimeDilation(float InDilation) { CinematicTimeDilation = InDilation; }

	// ---------------- World Settings ----------------
	// UE의 AWorldSettings 자리다. UE는 레벨에 숨은 액터로 두지만, 이 엔진은 월드 프로퍼티로 두어
	// 씬 파일, PIE 복제, 실행 취소 기록에 그대로 들어간다. 에디터의 World Settings 패널이 고친다.

	// Game Mode: 이 월드에서 쓸 GameMode 클래스. UE의 AWorldSettings::DefaultGameMode (GameMode Override).
	// 비면 기본 AGameModeBase를 쓴다. 플레이를 시작할 때 이 GameMode가 Pawn, PlayerController 등을 정한다.
	const FString& GetGameModeOverride() const { return GameModeOverride; }
	void SetGameModeOverride(const FString& InClassName) { GameModeOverride = InClassName; }
	// GameMode Override로 찾은 클래스. 비었거나 찾을 수 없으면 AGameModeBase다.
	UClass* ResolveGameModeClass() const;

	// 플레이 중에 GameMode가 만든 액터들. 에디터 월드에서는 nullptr이다.
	AGameModeBase* GetGameMode() const { return GameMode; }
	AGameStateBase* GetGameState() const { return GameState; }
	APlayerState* GetPlayerState() const { return PlayerState; }
	AHUD* GetHUD() const { return HUD; }

	// Physics: 켜면 이동 컴포넌트가 각자의 Gravity 대신 이 값을 쓴다. UE의 bGlobalGravitySet / GlobalGravityZ.
	// 아래 방향이 음수다 (월드 단위 m/s²).
	bool IsWorldGravityOverridden() const { return bOverrideWorldGravity; }
	void SetWorldGravityOverridden(bool bOverride) { bOverrideWorldGravity = bOverride; }
	float GetWorldGravityZ() const { return WorldGravityZ; }
	void SetWorldGravityZ(float InGravityZ) { WorldGravityZ = InGravityZ; }

	// Time: 월드 전체의 시간 배율. UE의 AWorldSettings::TimeDilation.
	float GetGlobalTimeDilation() const { return GlobalTimeDilation; }
	void SetGlobalTimeDilation(float InDilation) { GlobalTimeDilation = FMath::Clamp(InDilation, MinTimeDilation, MaxTimeDilation); }
	static float GetMinTimeDilation() { return MinTimeDilation; }
	static float GetMaxTimeDilation() { return MaxTimeDilation; }

	// 모든 World Settings를 기본값으로 되돌린다. 씬을 비울 때(새 씬, 불러오기) 부른다.
	void ResetWorldSettings();

private:
	bool SweepSingleLegacy(const FRay& WorldRay, float MaxDistance, float Radius, FHitResult& OutHit, AActor* IgnoreActor,
		bool bBlockingOnly, bool bSkipPhysicsBodies);

	void GameWorldTick(float DeltaTime);
	void EditorWorldTick(float DeltaTime);
	void PreviewWorldTick(float DeltaTime);

	EWorldType WorldType = EWorldType::None;

	struct alignas(64) FGatherChunk
	{
		TArray<FRenderPacket> Packets;              // 스태틱 묶음에 못 들어가는 것 (반투명 섹션, Renderer 없는 호출)
		TArray<uint32> SlowPathIndices;
		std::vector<FStaticDrawGroup> Groups;       // 불투명 스태틱 메시: (머티리얼, 메시, LOD)별 묶음. 항목만 매 프레임 비운다
		uint32 LastGroup = 0;                       // 바로 전 물체가 들어간 묶음 (연속한 물체는 대개 같은 묶음)
		uint32 StaticDrawCount = 0;
		uint32 LODCounts[4] = {};
		uint64 LODTriangles[4] = {};
	};

	TArray<FGatherChunk> GatherChunks;

	// 등록된 Tick 함수만 실행한다. Actor보다 먼저 사라져도 남은 함수와의 연결을 스스로 끊는다.
	FTickTaskManager TickTaskManager;
	FCoroutineScheduler CoroutineScheduler;

	TQueue<AActor*> BeginPlayList;

	//메인 카메라 
	ACameraActor* MainCamera = nullptr;
	APawn* PlayerPawn = nullptr;
	APlayerController* PlayerController = nullptr;
	AGameModeBase* GameMode = nullptr;
	AGameStateBase* GameState = nullptr;
	APlayerState* PlayerState = nullptr;
	AHUD* HUD = nullptr;
	bool bIsPaused = false;
	bool bBegunPlay = false;
	// BeginPlay가 GameMode, Pawn 등을 만드는 동안 true. 이때 만든 액터도 "플레이 중 생성"으로 표시한다.
	bool bStartingPlay = false;

	ULevel* PersistentLevel = nullptr;
	ULevel* CurrentLevel = nullptr;
	TArray<ULevel*> Levels;

	FScene Scene;
	TUniquePtr<FPhysicsScene> PhysicsScene;
	// 등록 중인 이 월드의 바람 컴포넌트에 대한 비소유 참조
	TArray<UWindDirectionalSourceComponent*> WindSources;

	// GatherRenderPackets가 매 프레임 채우는 컬링 결과. 용량을 재사용한다.
	TArray<FPrimitiveSceneProxy*> VisibleProxies;
	TArray<FLODSelectionInput> LODInputs;
	TArray<uint8> SelectedLODs;

	FRenderStats RenderStats;

	// World Settings (씬 파일에 저장)
	UPROPERTY()
	FString GameModeOverride;
	UPROPERTY()
	bool bOverrideWorldGravity = false;
	UPROPERTY()
	float WorldGravityZ = -9.81f;
	UPROPERTY()
	float GlobalTimeDilation = 1.0f;

	// Slomo-hitstop
	float CinematicTimeDilation = 1.0f;
	float HitStopTimeRemaining = 0.0f;	// 실제 시간 (초)
	float HitStopDilation = 1.0f;
	float TimeSeconds = 0.0f;			// 게임 시간 (dilated 누적)
	float RealTimeSeconds = 0.0f;		// 실제 시간 누적

	static constexpr float MinTimeDilation = 0.0001f;
	static constexpr float MaxTimeDilation = 20.0f;
};
