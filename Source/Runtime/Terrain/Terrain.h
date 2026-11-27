// Source/Runtime/Terrain/Terrain.h   (담당: A, C·D는 읽기만)
// 공유 계약 1의 ATerrain. 문서: Docs/Week14/SharedContract.md
// UE ALandscape 자리. 회전·스케일은 쓰지 않고 위치만 Origin으로 쓴다 (계약 1 규칙 2).
#pragma once

#include "GameFramework/Actor.h"
#include "Terrain/TerrainTypes.h"
#include "Terrain.generated.h"

UCLASS()
class ATerrain : public AActor
{
	GENERATED_BODY()
public:
	// 월드 ↔ 그리드 변환. C·D는 이 값만 쓰고 따로 계산하지 않는다.
	FTerrainCoordinates GetCoordinates() const 
	{
		FTerrainCoordinates C = Coordinates;
		C.Origin = GetActorLocation();
		return C;
	}

	// 원본 16bit 높이. Heights[Coords.SampleIndex(X, Y)]
	const TArray<uint16>& GetHeights() const { return Heights; }

	// D(NavMesh) 입력. LOD 0 정점(월드 좌표)과 삼각형 인덱스. 대각선 방향은 계약 1 규칙 6.
	// TODO(A): 구현 필요 (지금은 빈 배열을 돌려준다)
	void GetLOD0Geometry(TArray<FVector>& OutVertices, TArray<uint32>& OutIndices) const;

	// FTerrainSubsystem::OnLevelUnload에서 부른다. 청크 VB/IB/SRV 해제.
	// TODO(A): 구현 필요
	void Release();

	bool LoadHeightmap();
	bool HasHeightData() const;
	void PostEditChangeProperty(const FProperty& Property) override;

protected:
	UPROPERTY(EditAnywhere) FString HeightmapPath;            // 역직렬화는 경로와 Coordinates만 읽는다 (GPU 자원 생성 금지)
	UPROPERTY(EditAnywhere) FTerrainCoordinates Coordinates;
	TArray<uint16> Heights;
};
