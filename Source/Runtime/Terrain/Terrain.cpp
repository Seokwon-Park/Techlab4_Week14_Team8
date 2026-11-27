#include "EnginePCH.h"
#include "Terrain/Terrain.h"
#include "Terrain/HeightmapImporter.h"

void ATerrain::GetLOD0Geometry(TArray<FVector>& OutVertices, TArray<uint32>& OutIndices) const
{
	OutVertices.Empty();
	OutIndices.Empty();
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