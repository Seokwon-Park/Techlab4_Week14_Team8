#include "EnginePCH.h"
#include "Physics/TerrainCollisionSubsystem.h"

#include "Engine/World.h"
#include "Terrain/Terrain.h"
#include "Terrain/TerrainSubsystem.h"
#include "Terrain/TerrainTypes.h"

#include <cmath>

namespace
{
	// H 뒤 숫자 = 셀 왼쪽 아래 샘플에서의 (X, Y) 오프셋
	struct FTerrainCellSample
	{
		int32 CellX = 0;
		int32 CellY = 0;
		float Fx = 0.0f;
		float Fy = 0.0f;
		float H00 = 0.0f;
		float H10 = 0.0f;
		float H01 = 0.0f;
		float H11 = 0.0f;
	};

	struct FTerrainCellWeights
	{
		float W00 = 0.0f;
		float W10 = 0.0f;
		float W01 = 0.0f;
		float W11 = 0.0f;
	};

	bool FindCell(const FTerrainCoordinates& Coords, const TArray<uint16>& Heights,
		const FVector& WorldLocation, FTerrainCellSample& OutCell)
	{
		if (Coords.SizeX < 2 || Coords.SizeY < 2)
			return false;
		if (static_cast<int32>(Heights.Num()) != Coords.SizeX * Coords.SizeY)
			return false;

		const FVector2 Grid = Coords.WorldToGrid(WorldLocation);

		// NaN도 비교가 모두 false라 여기서 걸러진다
		if (!Coords.IsInside(Grid))
			return false;

		// 끝 경계(Grid == Size-1)에서 +1 샘플이 배열 밖으로 나가지 않게 Size-2로 자른다
		const int32 CellX = std::clamp(static_cast<int32>(std::floor(Grid.X)), 0, Coords.SizeX - 2);
		const int32 CellY = std::clamp(static_cast<int32>(std::floor(Grid.Y)), 0, Coords.SizeY - 2);

		OutCell.CellX = CellX;
		OutCell.CellY = CellY;
		// Frac이 아니라 (Grid - Cell). 끝 경계에서 1이 나와야 마지막 샘플 높이가 정확하다
		OutCell.Fx = Grid.X - static_cast<float>(CellX);
		OutCell.Fy = Grid.Y - static_cast<float>(CellY);

		OutCell.H00 = Coords.DecodeHeight(Heights[Coords.SampleIndex(CellX, CellY)]);
		OutCell.H10 = Coords.DecodeHeight(Heights[Coords.SampleIndex(CellX + 1, CellY)]);
		OutCell.H01 = Coords.DecodeHeight(Heights[Coords.SampleIndex(CellX, CellY + 1)]);
		OutCell.H11 = Coords.DecodeHeight(Heights[Coords.SampleIndex(CellX + 1, CellY + 1)]);
		return true;
	}

	FTerrainCellWeights ComputeWeights(const FTerrainCellSample& Cell, ETerrainHeightInterp Mode)
	{
		const float Fx = Cell.Fx;
		const float Fy = Cell.Fy;
		FTerrainCellWeights W;

		if (Mode == ETerrainHeightInterp::Bilinear)
		{
			W.W00 = (1.0f - Fx) * (1.0f - Fy);
			W.W10 = Fx * (1.0f - Fy);
			W.W01 = (1.0f - Fx) * Fy;
			W.W11 = Fx * Fy;
			return W;
		}

		// 대각선 (0,0)→(1,1)로 나눈 두 삼각형 중 점이 있는 쪽의 barycentric
		if (Fx >= Fy)
		{
			// 아래쪽 삼각형 (H00, H10, H11): h = H00 + Fx(H10-H00) + Fy(H11-H10)
			W.W00 = 1.0f - Fx;
			W.W10 = Fx - Fy;
			W.W11 = Fy;
		}
		else
		{
			// 위쪽 삼각형 (H00, H11, H01): h = H00 + Fy(H01-H00) + Fx(H11-H01)
			W.W00 = 1.0f - Fy;
			W.W01 = Fy - Fx;
			W.W11 = Fx;
		}
		return W;
	}
}

// TODO(C)
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

const ATerrain* FTerrainCollisionSubsystem::FindTerrain() const
{
	UWorld* World = GetWorld();
	if (!World)
		return nullptr;

	FTerrainSubsystem* TerrainSubsystem = World->GetSubsystem<FTerrainSubsystem>();
	if (!TerrainSubsystem)
		return nullptr;

	// 로드 실패로 높이 배열이 비어 있어도 지형이 없는 것으로 본다
	const ATerrain* Terrain = TerrainSubsystem->GetTerrain();
	if (!Terrain || !Terrain->HasHeightData())
		return nullptr;

	return Terrain;
}

bool FTerrainCollisionSubsystem::GetHeightAt(const FVector& WorldLocation, float& OutZ,
	ETerrainHeightInterp Mode) const
{
	const ATerrain* Terrain = FindTerrain();
	if (!Terrain)
		return false;

	const FTerrainCoordinates Coords = Terrain->GetCoordinates();

	FTerrainCellSample Cell;
	if (!FindCell(Coords, Terrain->GetHeights(), WorldLocation, Cell))
		return false;

	const FTerrainCellWeights W = ComputeWeights(Cell, Mode);
	OutZ = Cell.H00 * W.W00 + Cell.H10 * W.W10 + Cell.H01 * W.W01 + Cell.H11 * W.W11;
	return true;
}

bool FTerrainCollisionSubsystem::GetNormalAt(const FVector& WorldLocation, FVector& OutNormal,
	ETerrainHeightInterp Mode) const
{
	const ATerrain* Terrain = FindTerrain();
	if (!Terrain)
		return false;

	const FTerrainCoordinates Coords = Terrain->GetCoordinates();
	const TArray<uint16>& Heights = Terrain->GetHeights();

	FTerrainCellSample Cell;
	if (!FindCell(Coords, Heights, WorldLocation, Cell))
		return false;

	const int32 X = Cell.CellX;
	const int32 Y = Cell.CellY;
	const FVector N00 = Coords.ComputeNormal(Heights, X, Y);
	const FVector N10 = Coords.ComputeNormal(Heights, X + 1, Y);
	const FVector N01 = Coords.ComputeNormal(Heights, X, Y + 1);
	const FVector N11 = Coords.ComputeNormal(Heights, X + 1, Y + 1);

	// 높이와 같은 가중치로 섞는다. Triangle이면 GPU의 정점 노멀 보간과 같고,
	// 섞으면 길이가 1보다 짧아지므로 다시 정규화한다
	const FTerrainCellWeights W = ComputeWeights(Cell, Mode);
	const FVector Blended = N00 * W.W00 + N10 * W.W10 + N01 * W.W01 + N11 * W.W11;
	OutNormal = Blended.GetSafeNormal();
	return true;
}

REGISTER_WORLD_SUBSYSTEM(FTerrainCollisionSubsystem);
