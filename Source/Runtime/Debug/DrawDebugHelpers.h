// Source/Runtime/Debug/DrawDebugHelpers.h   (담당: B)
// 공유 계약 3: 디버그 드로우 API. 문서: Docs/Week14/SharedContract.md
#pragma once
#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "Math/Box.h"
#include "Containers/Array.h"

class UWorld;

// UE ESceneDepthPriorityGroup 축소판
enum class EDebugDepthPriority : uint8 { World = 0, Foreground = 1 };

// LifeTime < 0 이고 bPersistent == false 이면 이번 프레임만 그린다 (UE와 같은 의미)
// TODO(B): 아래 함수들 구현 필요
void DrawDebugLine  (const UWorld* World, const FVector& Start, const FVector& End, const FVector4& Color,
                     bool bPersistent = false, float LifeTime = -1.f, EDebugDepthPriority Depth = EDebugDepthPriority::World);
void DrawDebugSphere(const UWorld* World, const FVector& Center, float Radius, int32 Segments, const FVector4& Color,
                     bool bPersistent = false, float LifeTime = -1.f, EDebugDepthPriority Depth = EDebugDepthPriority::World);
void DrawDebugBox   (const UWorld* World, const FBox& Box, const FVector4& Color,
                     bool bPersistent = false, float LifeTime = -1.f, EDebugDepthPriority Depth = EDebugDepthPriority::World);
void DrawDebugPoint (const UWorld* World, const FVector& Position, float Size, const FVector4& Color,
                     bool bPersistent = false, float LifeTime = -1.f);
void DrawDebugDirectionalArrow(const UWorld* World, const FVector& Start, const FVector& End, float ArrowSize,
                     const FVector4& Color, bool bPersistent = false, float LifeTime = -1.f);
// 우리 엔진 추가: 경로 폴리라인 (FLineBatcher::AddPath 래핑)
void DrawDebugPath  (const UWorld* World, const TArray<FVector>& Points, const FVector4& Color,
                     bool bPersistent = false, float LifeTime = -1.f);
void FlushPersistentDebugLines(const UWorld* World);

// UE FEngineShowFlags의 이번 과제용 부분집합. 뷰포트(에디터·게임)마다 켜고 끈다.
enum class EDebugShowFlags : uint32
{
    None              = 0,
    TerrainWireframe  = 1 << 0,  // B: 와이어프레임
    TerrainLODColor   = 1 << 1,  // B: LOD별 색
    TerrainBounds     = 1 << 2,  // A: 청크 AABB
    Navigation        = 1 << 3,  // B가 그림, D가 데이터 제공
    NavPath           = 1 << 4,  // D가 DrawDebugPath로 그림
};
bool IsDebugShowFlagEnabled(const UWorld* World, EDebugShowFlags Flag);
