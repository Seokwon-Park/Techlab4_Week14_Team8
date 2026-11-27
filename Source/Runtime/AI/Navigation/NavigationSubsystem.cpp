#include "EnginePCH.h"
#include "AI/Navigation/NavigationSubsystem.h"
#include "AI/Navigation/NavMeshDebugGeometry.h"

// 1일차 링크용 빈 스텁. TODO(D): 구현

void FNavigationSubsystem::OnLevelLoad(const FLevelLoadContext& Context)
{
}

void FNavigationSubsystem::OnLevelUnload()
{
}

void FNavigationSubsystem::GetStats(FWorldSubsystemStats& Out) const
{
}

void FNavigationSubsystem::BuildNavigation()
{
}

FNavPathResult FNavigationSubsystem::FindPath(const FVector& Start, const FVector& End) const
{
	return {}; // Status = NoNavMesh
}

const FNavMeshDebugGeometry* FNavigationSubsystem::GetDebugGeometry() const
{
	return nullptr;
}

REGISTER_WORLD_SUBSYSTEM(FNavigationSubsystem);
