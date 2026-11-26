#pragma once

#include "UObject/ObjectMacros.h"
#include "PhysicsSettingsEnums.generated.h"

// 마찰, 반발 결합 방식
UENUM()
enum class EPhysicalMaterialCombineMode : uint8
{
    Average = 0,
    Min = 1,
    Multiply = 2,
    Max = 3
};