#pragma once

#include "Physics/PhysicsSettingsCore.h"

struct FPhysicsSettings : public FPhysicsSettingsCore
{
    // 물리 고정 스텝 길이와 월드 틱당 실행 상한
    double FixedStepSeconds = 1.0 / 60.0;
    int32 MaxStepsPerTick = 6;

    // PhysX CPU 디스패처 워커 수
    uint32 CpuDispatcherWorkerCount = 0;

    // 프로젝트 전체 CCD 비활성화 여부
    bool bDisableCCD = false;

    // 디버그 빌드의 시작 시 PVD 연결 여부
    bool bEnablePVD = false;

    // 프로젝트 물리 설정의 읽기 전용 접근
    static const FPhysicsSettings& Get();
};