#include "EnginePCH.h"
#include "Terrain/Terrain.h"
#include "Terrain/HeightmapImporter.h"

void ATerrain::GetLOD0Geometry(TArray<FVector>& OutVertices, TArray<uint32>& OutIndices) const
{
	OutVertices.Empty();
	OutIndices.Empty();
	if (!HasHeightData())
		return;

	// 멤버 Coordinates가 아니라 GetCoordinates()를 쓴다. Origin이 현재 액터 위치로 채워진 사본이다.
	const FTerrainCoordinates Coords = GetCoordinates();
	const int32 SizeX = Coords.SizeX;
	const int32 SizeY = Coords.SizeY;

	// 정점: 샘플마다 하나. 정점 번호 = SampleIndex(X, Y)라서 인덱스를 만들 때 그대로 쓴다.
	OutVertices.SetNum(SizeX * SizeY);
	for (int32 Y = 0; Y < SizeY; ++Y)
	{
		for (int32 X = 0; X < SizeX; ++X)
		{
			const int32 Index = Coords.SampleIndex(X, Y);
			OutVertices[Index] = Coords.GridToWorld(X, Y, Heights[Index]);
		}
	}

	const int32 QuadsX = SizeX - 1;
	const int32 QuadsY = SizeY - 1;
	OutIndices.Reserve(static_cast<uint32>(QuadsX * QuadsY * 6));
	for (int32 Y = 0; Y < QuadsY; ++Y)
	{
		for (int32 X = 0; X < QuadsX; ++X)
		{
			AppendTerrainQuadIndices(OutIndices,
				Coords.SampleIndex(X, Y),       // LeftDown
				Coords.SampleIndex(X + 1, Y),       // RightDown
				Coords.SampleIndex(X, Y + 1),   // LeftUp
				Coords.SampleIndex(X + 1, Y + 1));  // RightUp
		}
	}
}

void ATerrain::Release()
{
}

bool ATerrain::HasHeightData() const
{
	return Heights.Num() > 0 && Heights.Num() == Coordinates.SizeX * Coordinates.SizeY;
}

bool ATerrain::LoadHeightmap()
{
	Heights.Empty();

	FHeightmapImportData Data;
	FString Error;
	if (!FHeightmapImporter::Import(HeightmapPath, Coordinates.SizeX, Coordinates.SizeY, Data, Error))
	{
		HTR_LOG(Error, "[Terrain] {}: failed to load '{}': {}", GetName(), HeightmapPath, Error);
		return false;
	}

	if (!FHeightmapImporter::ValidateSize(Data.SizeX, Data.SizeY, Coordinates.ChunkQuads, Error))
	{
		HTR_LOG(Error, "[Terrain] {}: invalid heightmap '{}': {}", GetName(), HeightmapPath, Error);
		return false;
	}

	if (Data.SizeX != Coordinates.SizeX || Data.SizeY != Coordinates.SizeY)
	{
		HTR_LOG(Warning, "[Terrain] {}: size {}x{} -> {}x{} (taken from file)",
			GetName(), Coordinates.SizeX, Coordinates.SizeY, Data.SizeX, Data.SizeY);
		Coordinates.SizeX = Data.SizeX;
		Coordinates.SizeY = Data.SizeY;
	}

	Heights = std::move(Data.Heights);
	HTR_LOG(Info, "[Terrain] {}: loaded '{}' ({}x{})", GetName(), HeightmapPath, Coordinates.SizeX, Coordinates.SizeY);
	return true;
}

void ATerrain::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);

	if (Property.Name == "HeightmapPath" || Property.Name == "Coordinates")
	{
		LoadHeightmap();
		// TODO(A, 2단계): 성공하면 청크 메시를 다시 만든다
	}
}