#pragma once
#include "Core/Stats/LightweightStats.h"

// 물리/래그돌 CPU 구간 스탯. FStatRegistry에 모이므로 뷰포트 Profile 오버레이에 자동으로 나온다.
namespace PhysicsStats
{
	DECLARE_CYCLE_STAT("Phys: Simulate", STAT_PhysSimulate);
	DECLARE_CYCLE_STAT("Phys: Sync Actors", STAT_PhysSyncActors);
	DECLARE_CYCLE_STAT("Phys: Dispatch Notifies", STAT_PhysNotifies);
	DECLARE_CYCLE_STAT("Ragdoll: Init Bodies", STAT_RagdollInit);
	DECLARE_CYCLE_STAT("Ragdoll: Kinematic Update", STAT_RagdollKinematic);
	DECLARE_CYCLE_STAT("Ragdoll: Physics To Bones", STAT_RagdollBonePose);
}
