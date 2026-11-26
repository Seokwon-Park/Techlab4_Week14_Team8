#pragma once

#include "UObject/Object.h"
#include "Components/PrimitiveComponent.h"

#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "Containers/Set.h"
#include "Engine/EngineBaseTypes.h"
#include "Actor.generated.h"

class UWorld;
class ULevel;
class USceneComponent;
struct FLuaScriptInstance;

// 불러올 때 다른 액터의 컴포넌트에 붙어 있던 컴포넌트. 그 액터가 아직 없을 수 있어
// 레벨의 액터를 모두 만든 뒤 AActor::ResolvePendingActorAttachments가 붙인다.
struct FPendingActorAttachment
{
	USceneComponent* Child = nullptr;
	FString ParentActorName;
	FString ParentComponentName;
};
struct FMinimalViewInfo;

UCLASS()
class AActor : public UObject
{
	GENERATED_BODY()
public:
	AActor();
	virtual ~AActor();

	virtual void BeginPlay(); // xx World->AddPrimitive 책임이동 필요
	bool HasBegunPlay() const { return bHasBegunPlay; }
	// 액터 자신의 로직. 컴포넌트는 각자의 PrimaryComponentTick으로 따로 실행된다.
	virtual void Tick(float DeltaTime) {}
	virtual void EndPlay();
	// FActorTickFunction이 호출하는 진입점
	void TickActor(float DeltaTime);

	//Actor 위치의 시점 계산
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutPOV);

	UWorld* GetWorld() const { return World; }
	ULevel* GetLevel() const { return Level; }

	const TArray<UActorComponent*>& GetComponents() const { return Components; }
	USceneComponent* GetRootComponent() const { return RootComponent; }
	void SetRootComponent(USceneComponent* SceneComponent) { RootComponent = SceneComponent; }
	void SetActorEnableCollision(bool bEnabled);
	void PostEditChangeProperty(const FProperty& Property) override;
	bool GetActorEnableCollision() const { return bActorEnableCollision; }

	void RemoveOwnedComponent(UActorComponent* Component);
	
	FVector GetActorLocation() const;
	FRotator GetActorRotation() const;
	FVector GetActorScale3D() const;
	FQuat GetActorQuat() const;           //xx 타입명변경
	FTransform GetActorTransform() const;
	bool TryGetActorBounds(FBox& OutBounds) const;

	bool Destroy();
	
	//xx삭제 예정
	friend class UWorld;

	// ---------------- 액터끼리 붙이기 ----------------
	// 이 액터의 루트를 ParentActor의 루트에 붙인다. UE의 AActor::AttachToActor와 같다.
	// 순환(자기 자신이나 자기 자식에 붙이기)이면 붙이지 않고 false를 돌려준다.
	// 씬 파일에는 부모를 "액터 이름 + 컴포넌트 이름"으로 남겨 다시 열어도 붙어 있다.
	bool AttachToActor(AActor* ParentActor, EAttachmentRule Rule = EAttachmentRule::KeepWorld);
	// 다른 액터에 붙어 있으면 뗀다. UE의 AActor::DetachFromActor와 같다.
	void DetachFromActor(EAttachmentRule Rule = EAttachmentRule::KeepWorld);
	// 이 액터의 루트가 붙어 있는 다른 액터. 없으면 nullptr. UE의 AActor::GetAttachParentActor와 같다.
	AActor* GetAttachParentActor() const;
	// 루트가 이 액터(의 컴포넌트)에 붙어 있는 다른 액터들. UE의 AActor::GetAttachedActors와 같다.
	void GetAttachedActors(TArray<AActor*>& OutActors) const;
	// 불러오기가 끝난 뒤 부른다. 다른 액터에 붙어 있던 컴포넌트를 같은 레벨에서 부모를 찾아 다시 붙인다.
	void ResolvePendingActorAttachments();

	UActorComponent* AddComponent(UClass* ComponentClass, FName Name);
	void DestroyComponent(UActorComponent* Component);

	// bCanEverTick이 켜진 액터·컴포넌트의 Tick 함수만 World의 FTickTaskManager에 등록하거나 해제한다.
	void RegisterAllActorTickFunctions(bool bRegister);

	// UE와 같이 기본값은 bCanEverTick = false. 생성자에서 Target = this
	FActorTickFunction PrimaryActorTick;

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
	UActorComponent* FindComponentByName(FName Name) const;

	virtual void RegisterAllComponents();
	// Parameter : 바로 다시 Register 할껀지
	virtual void UnregisterAllComponents();

	// Lua 스크립트 (담당: A). ScriptPath가 비어 있으면 Script를 만들지 않는다.
	// 이 헤더에서 sol.hpp를 include하지 않도록 포인터로 둔다.
	UPROPERTY(EditAnywhere)
	FString ScriptPath;
	FLuaScriptInstance* Script = nullptr;

	// 에디터 Outliner에서 이 액터가 들어 있는 폴더. "Lighting/Sky"처럼 /로 나눈 경로이고 비면 맨 위다.
	// UE의 AActor::FolderPath와 같다. 게임 동작에는 쓰지 않고 씬 파일에만 남는다.
	UPROPERTY()
	FString FolderPath;
	const FString& GetFolderPath() const { return FolderPath; }
	void SetFolderPath(const FString& InFolderPath) { FolderPath = InFolderPath; }

	// 플레이를 시작하면서(GameMode, Pawn 등) 또는 플레이 중에 생성된 액터면 true. 에디터 레벨에서 복제된 액터는 false.
	// UE Outliner처럼 이 액터들을 노란 글씨로 보여 주는 데 쓴다. 저장하지 않는다.
	bool WasSpawnedDuringPlay() const { return bSpawnedDuringPlay; }

	// 컴포넌트 충돌 이벤트를 액터 단위로 받는 지점 (C가 호출, A가 Lua로 넘김)
	virtual void NotifyActorBeginOverlap(AActor* OtherActor);
	virtual void NotifyActorEndOverlap(AActor* OtherActor);
	virtual void NotifyHit(AActor* OtherActor);
	virtual void NotifyDeath(AActor* OtherActor);

	// 담당: C
	bool IsOverlappingActor(const AActor* Other) const;
	void GetOverlappingActors(TArray<AActor*>& OutActors) const;
protected:
	virtual void OnDefaultSubobjectCreated(UObject* Subobject) override;

	//TSet<TObjectPtr<UActorComponent>> OwnedComponents;
	TArray<UActorComponent*> Components;
	USceneComponent* RootComponent = nullptr;

	UWorld* World = nullptr;
	ULevel* Level = nullptr;

	// 불러오는 중에 아직 못 붙인 액터 간 부착. ResolvePendingActorAttachments가 비운다.
	TArray<FPendingActorAttachment> PendingActorAttachments;
private:
	UPROPERTY(EditAnywhere)
	bool bActorEnableCollision = true;
	bool bHasBegunPlay = false;
	bool bSpawnedDuringPlay = false;

};
