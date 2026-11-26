#pragma once

#include "GameFramework/Actor.h"
#include "GameFramework/DefaultPawn.h"
#include "GameplayInfoActors.generated.h"

// GameMode가 플레이를 시작할 때 만드는 정보 액터들. UE의 같은 이름 클래스 자리다.
// 지금은 비어 있는 틀이다. 게임에서 상속해 점수, 팀, 화면 표시 같은 것을 넣는다.

// 모든 플레이어가 함께 보는 게임 상태 (점수판, 남은 시간 등). UE의 AGameStateBase.
UCLASS()
class AGameStateBase : public AActor
{
	GENERATED_BODY()
};

// 플레이어 한 명의 상태 (이름, 점수 등). UE의 APlayerState.
UCLASS()
class APlayerState : public AActor
{
	GENERATED_BODY()
};

// 플레이어 화면에 그리는 정보. UE의 AHUD.
UCLASS()
class AHUD : public AActor
{
	GENERATED_BODY()
};

// 관전할 때 쓰는 Pawn. UE처럼 ADefaultPawn을 잇는다.
UCLASS()
class ASpectatorPawn : public ADefaultPawn
{
	GENERATED_BODY()
};
