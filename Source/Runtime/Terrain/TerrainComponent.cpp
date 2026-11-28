#include "EnginePCH.h"

#include "Asset/AssetManager.h"
#include "Render/RenderPacket.h"

#include "Terrain/TerrainComponent.h"
#include "Terrain/Terrain.h"

ATerrain* UTerrainComponent::GetTerrain() const
{
	ATerrain* Terrain = Cast<ATerrain>(GetOwner());
	if (!Terrain || Terrain->GetTerrainComponent() != this)
		return nullptr;
	return Terrain;

}

FBox UTerrainComponent::CalcLocalBounds() const
{
	if (const ATerrain* Terrain = GetTerrain())
	{
		return Terrain->GetLocalBounds();
	}
	return Super::CalcLocalBounds();
}

void UTerrainComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
	const ATerrain* Terrain = GetTerrain();
	if (!Terrain || !Terrain->HasHeightData())
	{
		return;
	}

	UMaterial* Material = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
	if (!Material)
	{
		return;
	}

	const TArray<FTerrainChunk>& Chunks = Terrain->GetChunks();
	for (int32 ChunkIndex = 0; ChunkIndex < Chunks.Num(); ++ChunkIndex)
	{
		const FTerrainChunk& Chunk = Chunks[ChunkIndex];

		FRenderPacket& Packet = RenderQueue.AddDefaulted_GetRef();
		Packet.Proxy = GetSceneProxy();

		Packet.VertexFactory = &Chunk.VertexFactory;
		Packet.Material = Material;
		Packet.IndexCount = 0;
		Packet.LODIndex = static_cast<uint8>(Terrain->GetChunkLOD(ChunkIndex));
	}
}