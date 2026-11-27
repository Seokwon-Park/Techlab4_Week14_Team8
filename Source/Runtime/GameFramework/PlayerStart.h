// Source/Runtime/GameFramework/PlayerStart.h   (담당: B)
// UE APlayerStart 자리. 레벨 전환 후 C가 캐릭터 컨트롤러를 이 위치로 텔레포트한다 (계약 4).
#pragma once

#include "GameFramework/Actor.h"
#include "PlayerStart.generated.h"

UCLASS()
class APlayerStart : public AActor
{
	GENERATED_BODY()
public:
	// 여러 개일 때 고르는 이름. UE APlayerStart::PlayerStartTag 자리. 비어 있으면 첫 번째를 쓴다.
	FName PlayerStartTag = NAME_None;
};
