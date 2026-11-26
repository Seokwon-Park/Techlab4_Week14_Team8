#pragma once

#include "GameFramework/Actor.h"
#include "WindDirectionalSource.generated.h"

// 레벨에 배치해 회전으로 풍향을 편집하는 액터
UCLASS()
class AWindDirectionalSource : public AActor
{
	GENERATED_BODY()

public:
	AWindDirectionalSource();
};
