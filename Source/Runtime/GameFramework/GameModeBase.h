#pragma once

#include "GameFramework/Actor.h"
#include "GameModeBase.generated.h"

// 플레이 규칙을 정하는 액터. UE의 AGameModeBase와 같은 자리다.
// 플레이를 시작할 때 월드가 하나 만들고, 아래 클래스들로 Pawn, PlayerController, GameState, PlayerState, HUD를 만든다.
// 다른 규칙을 쓰려면 이 클래스를 상속해 생성자에서 클래스 이름을 바꾸고, World Settings의 GameMode Override로 고른다.
// 클래스는 이름("ADefaultPawn")으로 적는다. 찾을 수 없으면 기본 클래스를 쓴다.
// 에디터는 이 칸들을 클래스 고르기 콤보로 그린다 (FPropertyEditor::RegisterClassPicker).
UCLASS()
class AGameModeBase : public AActor
{
	GENERATED_BODY()
public:
	AGameModeBase();

	// 레벨에 Pawn이 놓여 있지 않으면 이 Pawn을 만들어 조종한다.
	UPROPERTY(EditAnywhere)
	FString DefaultPawnClass;
	UPROPERTY(EditAnywhere)
	FString HUDClass;
	UPROPERTY(EditAnywhere)
	FString PlayerControllerClass;
	UPROPERTY(EditAnywhere)
	FString GameStateClass;
	UPROPERTY(EditAnywhere)
	FString PlayerStateClass;
	// 관전할 때 쓰는 Pawn. 아직 관전 기능이 없어 플레이 중에 만들지는 않는다.
	UPROPERTY(EditAnywhere)
	FString SpectatorClass;

	// 이름으로 찾은 클래스. 비었거나 BaseClass의 자식이 아니면 Fallback을 돌려준다.
	static UClass* ResolveClass(const FString& ClassName, UClass* BaseClass, UClass* Fallback);
};
