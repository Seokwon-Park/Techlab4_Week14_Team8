#include "EnginePCH.h"
#include "Terrain/Terrain.h"

// 1일차 링크용 빈 스텁. TODO(A): 구현

void ATerrain::GetLOD0Geometry(TArray<FVector>& OutVertices, TArray<uint32>& OutIndices) const
{
	OutVertices.Empty();
	OutIndices.Empty();
}

void ATerrain::Release()
{
}
