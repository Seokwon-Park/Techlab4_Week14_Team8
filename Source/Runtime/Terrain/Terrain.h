// Source/Runtime/Terrain/Terrain.h   (담당: A, C·D는 읽기만)
// 공유 계약 1의 ATerrain. 문서: Docs/Week14/SharedContract.md
// UE ALandscape 자리. 회전·스케일은 쓰지 않고 위치만 Origin으로 쓴다 (계약 1 규칙 2).
#pragma once

#include "GameFramework/Actor.h"
#include "Terrain/TerrainTypes.h"
#include "Render/Buffer.h"
#include "Render/Vertex.h"
#include "Render/VertexFactory.h"
#include "Terrain.generated.h"

class UTerrainComponent;


// 청크 하나의 GPU 자원. UObject가 아니고 저장하지 않는다 (로드할 때 하이트맵에서 다시 만든다)
struct FTerrainChunk
{
	int32 ChunkX = 0;
	int32 ChunkY = 0;
	TUniquePtr<FVertexBuffer> VertexBuffer;
	FLocalVertexFactory VertexFactory;   // 이 청크 VB + 공유 IB 포인터 쌍. 소유하지 않는다
	FBox Bounds{};
};

UCLASS()
class ATerrain : public AActor
{
	GENERATED_BODY()
public:
	ATerrain();
	~ATerrain() override;
	// 월드 ↔ 그리드 변환. C·D는 이 값만 쓰고 따로 계산하지 않는다.
	FTerrainCoordinates GetCoordinates() const 
	{
		FTerrainCoordinates C = Coordinates;
		C.Origin = GetActorLocation();
		return C;
	}

	// 원본 16bit 높이. Heights[Coords.SampleIndex(X, Y)]
	const TArray<uint16>& GetHeights() const { return Heights; }
	UTerrainComponent* GetTerrainComponent() const { return TerrainComponent; }

	FVector GetNormal(int32 X, int32 Y) const { return Coordinates.ComputeNormal(Heights, X, Y); }

	// 렌더 컬링용 지형 AABB (액터 기준 로컬). LoadHeightmap에서 계산한다.
	const FBox& GetLocalBounds() const { return LocalBounds; }


	// D(NavMesh) 입력. LOD0 전체 그리드를 월드 좌표 메시로 돌려준다. 높이 데이터가 없으면 빈 배열.
	// 감김은 위(+Z)가 앞면. Recast로 넘길 때는 삼각형마다 2·3번째 인덱스를 바꾼다 (좌표계 det −1).
	void GetLOD0Geometry(TArray<FVector>& OutVertices, TArray<uint32>& OutIndices) const;

	// FTerrainSubsystem::OnLevelUnload에서 부른다. 청크 VB/IB/SRV 해제.
	// TODO(A): 구현 필요
	void Release();

	bool LoadHeightmap();
	bool HasHeightData() const;
	void PostEditChangeProperty(const FProperty& Property) override;

	bool BuildRenderResources();
	const TArray<FTerrainChunk>& GetChunks() const { return Chunks; }
	int32 GetChunkLOD(int32 ChunkIndex) const;

protected:
	UPROPERTY(EditAnywhere) FString HeightmapPath;            // 역직렬화는 경로와 Coordinates만 읽는다 (GPU 자원 생성 금지)
	UPROPERTY(EditAnywhere) FTerrainCoordinates Coordinates;
	TArray<uint16> Heights;
	UTerrainComponent* TerrainComponent = nullptr;
	FBox LocalBounds{};

	TUniquePtr<FIndexBuffer> SharedIndexBuffer;
	TArray<FTerrainChunk> Chunks;

	void BuildChunk(int32 ChunkX, int32 ChunkY, FTerrainChunk& OutChunk);
};
