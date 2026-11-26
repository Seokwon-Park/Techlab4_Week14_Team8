#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Clothing/ClothingTypes.h"
#include "ClothingAsset.generated.h"

// UE UClothingAssetCommon 대응: 시뮬레이션 메시 데이터와 Cloth 물성 파라미터를 담는 에셋
UCLASS()
class UClothingAssetCommon : public UObject
{
	GENERATED_BODY()
public:
	void SetAssetName(const FString& InName) { AssetName = InName; }
	const FString& GetAssetName() const { return AssetName; }

	const FClothPhysicalMeshData& GetPhysicalMeshData() const { return PhysicalMeshData; }
	FClothPhysicalMeshData& GetMutablePhysicalMeshData() { return PhysicalMeshData; }

	const FClothConfigData& GetConfig() const { return Config; }
	FClothConfigData& GetMutableConfig() { return Config; }

	bool BuildFromSectionData(
		const TArray<FVertex>& BindVertices,
		const TArray<uint32>& Indices,
		const TArray<FSkinWeight>& SkinWeights,
		const FClothSectionBinding& Binding);

	bool IsValid() const { return !PhysicalMeshData.Vertices.IsEmpty() && !PhysicalMeshData.Indices.IsEmpty(); }

private:
	FString AssetName;
	FClothPhysicalMeshData PhysicalMeshData;
	FClothConfigData Config;
};
