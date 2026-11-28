#include "EnginePCH.h"
#include "Terrain/TerrainSubsystem.h"
#include "Terrain/Terrain.h"

void FTerrainSubsystem::OnLevelLoad(const FLevelLoadContext& Context)
{
	Terrain = Context.Terrain;
	if (Terrain)
	{
		if (Terrain->LoadHeightmap())
		{
			Terrain->BuildRenderResources();
		}
	}
}

void FTerrainSubsystem::OnLevelUnload()
{
	if (Terrain)
		Terrain->Release();
	Terrain = nullptr;
}

void FTerrainSubsystem::OnTerrainDestroyed(const ATerrain* DestroyedTerrain)
{
	// Release()는 부르지 않는다. 지워지는 중인 객체이고, 버퍼는 ATerrain 멤버(TUniquePtr)가 알아서 놓는다.
	if (Terrain == DestroyedTerrain)
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
