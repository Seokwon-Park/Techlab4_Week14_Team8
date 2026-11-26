#pragma once

#include "GameFramework/PlayerController.h"
#include "MyPlayerController.generated.h"

UCLASS()
class AMyPlayerController : public APlayerController
{
	GENERATED_BODY()

	// 키 처리는 Enhanced Input으로 옮겼다. Jump / Attack은 AMyCharacter::SetupPlayerInputComponent에서 바인딩한다.
	// 게임 전용 Controller 입력이 필요하면 SetupInputComponent를 override한다.
};
