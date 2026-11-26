#pragma once

#include "UObject/Class.h"
#include "DepthOfFieldSettings.generated.h"

USTRUCT()
struct FDepthOfFieldSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	float FocusDistance = 0.0f;

	UPROPERTY(EditAnywhere)
	float FStop = 2.8f;

	UPROPERTY(EditAnywhere)
	float SensorWidthMm = 36.0f;
};
