#include "EnginePCH.h"
#include "Terrain/TerrainSubsystem.h"
#include "Terrain/Terrain.h"

// 1일차 링크용 빈 스텁. TODO(A): 구현

void FTerrainSubsystem::OnLevelLoad(const FLevelLoadContext& Context)
{
	Terrain = Context.Terrain;
	if (Terrain)
	{
		Terrain->LoadHeightmap();
		// TODO(A, 2단계): 청크 VB/IB 생성
	}
}

void FTerrainSubsystem::OnLevelUnload()
{
	if (Terrain)
		Terrain->Release();
	Terrain = nullptr;
}

void FTerrainSubsystem::GetStats(FWorldSubsystemStats& Out) const
{
}

ATerrain* FTerrainSubsystem::GetTerrain() const
{
	return Terrain;
}

REGISTER_WORLD_SUBSYSTEM(FTerrainSubsystem);
