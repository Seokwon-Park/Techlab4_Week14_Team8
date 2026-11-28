#include "EnginePCH.h"
#include "Terrain/Terrain.h"
#include "Terrain/TerrainComponent.h"
#include "Terrain/HeightmapImporter.h"
#include "Components/TerrainCollisionComponent.h"
#include "Render/RenderDevice.h"
#include "Terrain/TerrainSubsystem.h"
#include "Engine/World.h"

ATerrain::ATerrain()
{
	TerrainComponent = CreateDefaultSubobject<UTerrainComponent>("TerrainComponent");
	TerrainComponent->SetupAttachment(GetRootComponent());

	CollisionComponent = CreateDefaultSubobject<UTerrainCollisionComponent>("CollisionComponent");
	CollisionComponent->SetupAttachment(GetRootComponent());
}

ATerrain::~ATerrain()
{
	if (UWorld* World = GetWorld())
	{
		if (FTerrainSubsystem* Subsystem = World->GetSubsystem<FTerrainSubsystem>())
			Subsystem->OnTerrainDestroyed(this);
	}
}

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
	Chunks.Empty();
	SharedIndexBuffer.reset();
}

bool ATerrain::BuildRenderResources()
{
	Release();
	if (!HasHeightData())
	{
		return (false);
	}

	FRenderDevice* Device = FRenderDevice::Get();
	if (!Device)
	{
		return (false);
	}

	const int32 Quads = Coordinates.ChunkQuads;
	// 공유 LOD0 IB. 청크 로컬 번호(LY * VertsPerSide + LX) 기준이라 모든 청크가 같이 쓴다.
	TArray<uint32> Indices;
	Indices.Reserve(static_cast<uint32>(Quads * Quads * 6));
	for (int32 Y = 0; Y < Quads; ++Y)
	{
		for (int32 X = 0; X < Quads; ++X)
		{
			AppendTerrainQuadIndices(Indices,
				Coordinates.ChunkVertexIndex(X, Y),          // LeftDown
				Coordinates.ChunkVertexIndex(X + 1, Y),      // RightDown
				Coordinates.ChunkVertexIndex(X, Y + 1),      // LeftUp
				Coordinates.ChunkVertexIndex(X + 1, Y + 1)); // RightUp

		}
	}
	SharedIndexBuffer = Device->CreateStaticIndexBuffer(Indices.GetData(), Indices.Num());
	
	const int32 ChunkCountX = Coordinates.GetChunkCountX();
	const int32 ChunkCountY = Coordinates.GetChunkCountY();
	Chunks.Reserve(static_cast<uint32>(ChunkCountX * ChunkCountY));
	for (int32 ChunkY = 0; ChunkY < ChunkCountY; ++ChunkY)
	{
		for (int32 ChunkX = 0; ChunkX < ChunkCountX; ++ChunkX)
		{
			BuildChunk(ChunkX, ChunkY, Chunks.AddDefaulted_GetRef());
		}
	}

	HTR_LOG(Info, "[Terrain] {}: built {} chunk(s), {} indices", GetName(), Chunks.Num(), Indices.Num());
	return true;
}

void ATerrain::BuildChunk(int32 ChunkX, int32 ChunkY, FTerrainChunk& OutChunk)
{
	const FTerrainCoordinates& Coords = Coordinates;
	const int32 Quads = Coords.ChunkQuads;
	const int32 VertsPerSide = Coords.GetChunkVertsPerSide();
	const float InverseU = 1.0f / static_cast<float>(Coords.SizeX - 1);
	const float InverseV = 1.0f / static_cast<float>(Coords.SizeY - 1);

	TArray<FVertex> Vertices;
	Vertices.SetNum(static_cast<uint32>(VertsPerSide * VertsPerSide));
	float MinZ = FLT_MAX;
	float MaxZ = -FLT_MAX;

	for (int32 LocalY = 0; LocalY < VertsPerSide; ++LocalY)
	{
		for (int32 LocalX = 0; LocalX < VertsPerSide; ++LocalX)
		{
			const int32 GridX = ChunkX * Quads + LocalX;
			const int32 GridY = ChunkY * Quads + LocalY;

			FVertex& Vertex = Vertices[Coords.ChunkVertexIndex(LocalX, LocalY)];
			Vertex.Position = Coords.GridToWorld(GridX, GridY, Heights[Coords.SampleIndex(GridX, GridY)]);
			Vertex.Normal = Coords.ComputeNormal(Heights, GridX, GridY);
			Vertex.UV = FVector2(GridX * InverseU, GridY * InverseV);
			// Tangent(+X), Color는 FVertex 기본값

			MinZ = std::min(MinZ, Vertex.Position.Z);
			MaxZ = std::max(MaxZ, Vertex.Position.Z);
		}
	}

	OutChunk.ChunkX = ChunkX;
	OutChunk.ChunkY = ChunkY;
	OutChunk.VertexBuffer = FRenderDevice::Get()->CreateStaticVertexBuffer(
		Vertices.GetData(), sizeof(FVertex) * Vertices.Num(), sizeof(FVertex));
	OutChunk.VertexFactory.Init(OutChunk.VertexBuffer.get(), SharedIndexBuffer.get());

	const float Cell = Coords.GetWorldCellSize();
	OutChunk.Bounds.Min = FVector(ChunkX * Quads * Cell, ChunkY * Quads * Cell, MinZ);
	OutChunk.Bounds.Max = FVector((ChunkX + 1) * Quads * Cell, (ChunkY + 1) * Quads * Cell, MaxZ);
}

int32 ATerrain::GetChunkLOD(int32 ChunkIndex) const
{
	if (ChunkIndex < 0 || ChunkIndex >= Chunks.Num())
		return 0;
	return 0;
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

	//랜더 컬링용 Local AABB
	uint16 MinRaw = UINT16_MAX;
	uint16 MaxRaw = 0;

	for (uint16 Raw : Heights)
	{
		MinRaw = std::min(MinRaw, Raw);
		MaxRaw = std::max(MaxRaw, Raw);
	}

	const float Cell = Coordinates.GetWorldCellSize();
	LocalBounds.Min = FVector(0.0f, 0.0f, Coordinates.DecodeHeight(MinRaw));
	LocalBounds.Max = FVector((Coordinates.SizeX - 1) * Cell, (Coordinates.SizeY - 1) * Cell, Coordinates.DecodeHeight(MaxRaw));

	if (TerrainComponent)
	{
		TerrainComponent->OnTransformDirty();
	}

	HTR_LOG(Info, "[Terrain] {}: loaded '{}' ({}x{})", GetName(), HeightmapPath, Coordinates.SizeX, Coordinates.SizeY);
	return true;
}

void ATerrain::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);

	if (Property.Name == "HeightmapPath" || Property.Name == "Coordinates")
	{
		if (LoadHeightmap())
		{
			BuildRenderResources();
		}
		else
		{
			Release();
		}
		if (CollisionComponent)
			CollisionComponent->RecreatePhysicsState();
	}
}