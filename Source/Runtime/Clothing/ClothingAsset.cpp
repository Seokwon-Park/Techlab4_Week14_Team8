#include "EnginePCH.h"
#include "Clothing/ClothingAsset.h"

bool UClothingAssetCommon::BuildFromSectionData(
	const TArray<FVertex>& BindVertices,
	const TArray<uint32>& Indices,
	const TArray<FSkinWeight>& SkinWeights,
	const FClothSectionBinding& Binding)
{
	PhysicalMeshData = {};
	if (BindVertices.IsEmpty() || Indices.IsEmpty())
		return false;

	const int32 NumVerts = BindVertices.Num();
	PhysicalMeshData.Vertices.Reserve(NumVerts);
	PhysicalMeshData.InverseMasses.Reserve(NumVerts);
	PhysicalMeshData.MaxDistances.Reserve(NumVerts);
	PhysicalMeshData.SkinWeights.Reserve(NumVerts);

	for (int32 i = 0; i < NumVerts; ++i)
	{
		PhysicalMeshData.Vertices.Add(BindVertices[i].Position);
		float MaxDist = 0.0f;
		if (const float* Found = Binding.MaxDistanceByRenderVertex.Find(static_cast<uint32>(i)))
		{
			MaxDist = *Found;
		}
		PhysicalMeshData.MaxDistances.Add(MaxDist);
		PhysicalMeshData.InverseMasses.Add(MaxDist > 0.0001f ? 1.0f : 0.0f);
		PhysicalMeshData.SkinWeights.Add((i < SkinWeights.Num()) ? SkinWeights[i] : FSkinWeight{});
	}

	PhysicalMeshData.Indices = Indices;
	return IsValid();
}
