// Source/Runtime/Terrain/TerrainTypes.h   (담당: A, C·D가 include)
// 공유 계약 1: 하이트맵 좌표 규약. 문서: Docs/Week14/SharedContract.md
#pragma once
#include "Math/Vector.h"
#include "Math/Vector2.h"

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

    // 16bit 원본 값 → 월드 Z
    float DecodeHeight(uint16 Raw) const
    {
        return Origin.Z + (float(Raw) - TERRAIN_HEIGHT_MID) * ZScale * TERRAIN_ZSCALE;
    }

    // 월드 XY → 연속 그리드 좌표 (정수부 = 셀, 소수부 = 셀 안 위치)
    FVector2 WorldToGrid(const FVector& World) const
    {
        return FVector2((World.X - Origin.X) / CellSize, (World.Y - Origin.Y) / CellSize);
    }

    // 그리드 샘플 → 월드 위치 (Z는 높이 배열에서)
    FVector GridToWorld(int32 X, int32 Y, uint16 Raw) const
    {
        return FVector(Origin.X + X * CellSize, Origin.Y + Y * CellSize, DecodeHeight(Raw));
    }

    bool IsInside(const FVector2& Grid) const
    {
        return Grid.X >= 0.f && Grid.Y >= 0.f && Grid.X <= SizeX - 1 && Grid.Y <= SizeY - 1;
    }

    // 1차원 인덱스: 행 우선(row-major), 행 = Y, 열 = X
    int32 SampleIndex(int32 X, int32 Y) const { return Y * SizeX + X; }
};
