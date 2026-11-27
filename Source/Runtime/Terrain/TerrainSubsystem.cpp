#include "EnginePCH.h"
#include "Terrain/TerrainSubsystem.h"
#include "Terrain/Terrain.h"

// 1일차 링크용 빈 스텁. TODO(A): 구현

void FTerrainSubsystem::OnLevelLoad(const FLevelLoadContext& Context)
{
}

void FTerrainSubsystem::OnLevelUnload()
{
}

void FTerrainSubsystem::GetStats(FWorldSubsystemStats& Out) const
{
}

ATerrain* FTerrainSubsystem::GetTerrain() const
{
	return nullptr;
}

REGISTER_WORLD_SUBSYSTEM(FTerrainSubsystem);
