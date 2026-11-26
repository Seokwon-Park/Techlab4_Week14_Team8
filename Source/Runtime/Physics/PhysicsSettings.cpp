#include "EnginePCH.h"
#include "Physics/PhysicsSettings.h"

const FPhysicsSettings& FPhysicsSettings::Get()
{
    // 모든 월드가 공유하는 프로젝트 기본값
    static constexpr FPhysicsSettings Settings{};
    return Settings;
}