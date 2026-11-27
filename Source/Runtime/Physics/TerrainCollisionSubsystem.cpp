#include "EnginePCH.h"
#include "Physics/TerrainCollisionSubsystem.h"

// 1일차 링크용 빈 스텁. TODO(C): 구현

void FTerrainCollisionSubsystem::OnLevelLoad(const FLevelLoadContext& Context)
{
}

void FTerrainCollisionSubsystem::OnWorldBeginPlay(UWorld& World)
{
}

void FTerrainCollisionSubsystem::OnLevelUnload()
{
}

void FTerrainCollisionSubsystem::GetStats(FWorldSubsystemStats& Out) const
{
}

bool FTerrainCollisionSubsystem::GetHeightAt(const FVector& WorldLocation, float& OutZ) const
{
	return false;
}

bool FTerrainCollisionSubsystem::GetNormalAt(const FVector& WorldLocation, FVector& OutNormal) const
{
	return false;
}

REGISTER_WORLD_SUBSYSTEM(FTerrainCollisionSubsystem);
