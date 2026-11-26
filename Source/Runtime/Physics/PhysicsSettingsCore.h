#pragma once

#include "Physics/PhysicsSettingsEnums.h"

struct FPhysicsSettingsCore
{
    // 프로젝트 기본 Z축 중력
    float DefaultGravityZ = -9.81f;

    // 프로젝트 기본 마찰·반발 결합 모드
    EPhysicalMaterialCombineMode DefaultFrictionCombineMode = EPhysicalMaterialCombineMode::Average;
    EPhysicalMaterialCombineMode DefaultRestitutionCombineMode = EPhysicalMaterialCombineMode::Average;

    // 지속 접촉 매니폴드 사용 여부
    bool bEnablePCM = true;
};