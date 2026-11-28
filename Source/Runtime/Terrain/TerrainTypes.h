// Source/Runtime/Terrain/TerrainTypes.h   (담당: A, C·D가 include)
// 공유 계약 1: 하이트맵 좌표 규약. 문서: Docs/Week14/SharedContract.md
#pragma once
#include <algorithm>

#include "Math/Vector.h"
#include "Math/Vector2.h"
#include "Math/EngineMath.h"
#include "Containers/Array.h"


#include "UObject/Class.h"
#include "TerrainTypes.generated.h"

// UE LANDSCAPE_ZSCALE / LANDSCAPE_INV_ZSCALE 와 같은 값
constexpr float TERRAIN_ZSCALE     = 1.0f / 128.0f;
constexpr float TERRAIN_INV_ZSCALE = 128.0f;
constexpr int32 TERRAIN_HEIGHT_MID = 32768;   // uint16 이 값이 높이 0

// UE ALandscape의 Transform + ULandscapeInfo 일부 역할.
// 지형 하나의 "월드 ↔ 그리드" 변환은 이 구조체 하나만 쓴다. 각자 따로 계산하지 않는다.
USTRUCT()
struct FTerrainCoordinates
{
    GENERATED_BODY()

    FVector Origin   = FVector::ZeroVector; // 샘플 (0,0)의 월드 위치 = ATerrain 액터 위치
    UPROPERTY(EditAnywhere) float   CellSize = 100.0f;              // 샘플 간격 (cm) = UE 스케일 X/Y
    UPROPERTY(EditAnywhere) float   ZScale   = 100.0f;              // UE 스케일 Z. 월드 높이 = (H - 32768) * ZScale / 128
    UPROPERTY(EditAnywhere) int32   SizeX    = 0;                   // 샘플 수 (쿼드 수 + 1)
    UPROPERTY(EditAnywhere) int32   SizeY    = 0;
    UPROPERTY(EditAnywhere) int32   ChunkQuads = 64;                // 청크당 쿼드 수 (2^n). SizeX = ChunksX * ChunkQuads + 1

    //CellSize, ZScale은 UE와 같은 cm 기준으로 저장하고, 월드(m)로 나갈 때 여기서만 CmToWorld로 변환한다.
    float GetWorldCellSize() const { return CellSize * CmToWorld; }
    float GetWorldHeightScale() const { return ZScale * TERRAIN_ZSCALE * CmToWorld; }

    // 16bit 원본 값 → 월드 Z
    float DecodeHeight(uint16 Raw) const
    {
        return Origin.Z + (float(Raw) - TERRAIN_HEIGHT_MID) * GetWorldHeightScale();
    }

    // 월드 XY → 연속 그리드 좌표 (정수부 = 셀, 소수부 = 셀 안 위치)
    FVector2 WorldToGrid(const FVector& World) const
    {
        const float Cell = GetWorldCellSize();
        return FVector2((World.X - Origin.X) / Cell, (World.Y - Origin.Y) / Cell);
    }

    // 그리드 샘플 → 월드 위치 (Z는 높이 배열에서)
    FVector GridToWorld(int32 X, int32 Y, uint16 Raw) const
    {
        const float Cell = GetWorldCellSize();
        return FVector(Origin.X + X * Cell, Origin.Y + Y * Cell, DecodeHeight(Raw));
    }

    bool IsInside(const FVector2& Grid) const
    {
        return Grid.X >= 0.f && Grid.Y >= 0.f && Grid.X <= SizeX - 1 && Grid.Y <= SizeY - 1;
    }

    // 1차원 인덱스: 행 우선(row-major), 행 = Y, 열 = X
    int32 SampleIndex(int32 X, int32 Y) const { return Y * SizeX + X; }


    // 계약 1 규칙 5: 샘플 (X, Y)의 노멀. 지형은 회전·스케일이 없어서(규칙 2) 그대로 월드 노멀이다.
    // 중앙 차분이고, 경계에서는 이웃 인덱스를 클램프한다.
    // 분모는 실제로 비교한 두 샘플의 거리 (안쪽 2칸, 가장자리 1칸 → 한쪽 차분).
    // A(렌더)와 C(GetNormalAt)는 이 함수 하나만 쓴다. 따로 계산하지 않는다.
    FVector ComputeNormal(const TArray<uint16>& Heights, int32 X, int32 Y) const
    {
        if (SizeX < 2 || SizeY < 2 || static_cast<int32>(Heights.Num()) != SizeX * SizeY)
            return FVector(0.0f, 0.0f, 1.0f);

        // 범위 밖은 가장 가까운 샘플로
        X = std::clamp(X, 0, SizeX - 1);
        Y = std::clamp(Y, 0, SizeY - 1);

        const int32 Left = std::max(X - 1, 0);
        const int32 Right = std::min(X + 1, SizeX - 1);
        const int32 Down = std::max(Y - 1, 0);
        const int32 Up = std::min(Y + 1, SizeY - 1);

        //월드 높이 = Origin.Z + (Raw − 32768) × ZScale / 128
        const float hR = DecodeHeight(Heights[SampleIndex(Right, Y)]);
        const float hL = DecodeHeight(Heights[SampleIndex(Left, Y)]);
        const float hU = DecodeHeight(Heights[SampleIndex(X, Up)]);
        const float hD = DecodeHeight(Heights[SampleIndex(X, Down)]);

        const float Cell = GetWorldCellSize();
        const float DhDx = (hR - hL) / ((Right - Left) * Cell);
        const float DhDy = (hU - hD) / ((Up - Down) * Cell);

        FVector Tx = FVector(1, 0, DhDx);
        FVector Ty = FVector(0, 1, DhDy);

        return Tx.Cross(Ty).GetSafeNormal();
    }
};

// 쿼드 하나를 삼각형 2개로 나누는 유일한 규칙.
// 대각선은 LeftDown(X,Y) → RightUp(X+1,Y+1). 감김은 위(+Z)에서 볼 때 앞면이다.
// Left/Right = X−/X+, Down/Up = Y−/Y+ (위에서 내려다본 그리드 기준. 높이 Z가 아님)
//
//    LeftUp ●──● RightUp
//           │  ╱ │
//           │ ╱  │
//  LeftDown ●──● RightDown
inline void AppendTerrainQuadIndices(TArray<uint32>& Out,
    uint32 LeftDown, uint32 RightDown, uint32 LeftUp, uint32 RightUp)
{
    Out.Add(LeftDown); Out.Add(RightDown); Out.Add(RightUp);   // 아래쪽 삼각형
    Out.Add(LeftDown); Out.Add(RightUp);   Out.Add(LeftUp);    // 위쪽 삼각형
}