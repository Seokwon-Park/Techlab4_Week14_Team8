#pragma once
#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "Math/Box.h"
#include "Containers/Array.h"

class UWorld;
class FLineBatcher;
struct FEngineShowFlags;
struct FNavPathResult;

// UE ESceneDepthPriorityGroup 축소판
// Foreground는 다른 물체에 가려지지 않고 항상 위에 그린다.
enum class EDebugDepthPriority : uint8 { World = 0, Foreground = 1 };

// LifeTime < 0 이고 bPersistent == false 이면 이번 프레임만 그린다 (UE와 같은 의미)
// TODO(B): 아래 함수들 구현 필요
void DrawDebugLine(const UWorld* World, const FVector& Start, const FVector& End, const FVector4& Color,
	bool bPersistent = false, float LifeTime = -1.f, EDebugDepthPriority Depth = EDebugDepthPriority::World);
void DrawDebugSphere(const UWorld* World, const FVector& Center, float Radius, int32 Segments, const FVector4& Color,
	bool bPersistent = false, float LifeTime = -1.f, EDebugDepthPriority Depth = EDebugDepthPriority::World);
void DrawDebugBox(const UWorld* World, const FBox& Box, const FVector4& Color,
	bool bPersistent = false, float LifeTime = -1.f, EDebugDepthPriority Depth = EDebugDepthPriority::World);
void DrawDebugPoint(const UWorld* World, const FVector& Position, float Size, const FVector4& Color,
	bool bPersistent = false, float LifeTime = -1.f);
void DrawDebugDirectionalArrow(const UWorld* World, const FVector& Start, const FVector& End, float ArrowSize,
	const FVector4& Color, bool bPersistent = false, float LifeTime = -1.f);
// 우리 엔진 추가: 경로 폴리라인 (FLineBatcher::AddPath 래핑)
void DrawDebugPath(const UWorld* World, const TArray<FVector>& Points, const FVector4& Color,
	bool bPersistent = false, float LifeTime = -1.f);
// 반투명 삼각현 한 장. NavMesh 미리보기, 영역 표시용
void DrawDebugSolidTriangle(const UWorld* World, const FVector& A, const FVector& B, const FVector& C, const FVector4& Color,
	bool bPersistent = false, float LifeTime = -1.f);

// D의 경로 질의 결과를 "요청한 경로와 목적지" 규칙대로 그린다. NavPath 플래그가 꺼져 있으면 아무것도 안 한다.
//   Success: 초록 경로 + 꺾이는 점, Partial: 노란 경로 + 빨간 목적지 X, 실패: 빨간 목적지 X만.
//   목적지에는 Acceptance Radius 크기의 원을 그린다.
void DrawDebugNavPath(const UWorld* World, const FVector& RequestedGoal, const FNavPathResult& Result,
	float AcceptanceRadius, float LifeTime = -1.f);

void FlushPersistentDebugLines(const UWorld* World);

// UE FEngineShowFlags의 이번 과제용 부분집합. 뷰포트(에디터·게임)마다 켜고 끈다.
enum class EDebugShowFlags : uint32
{
	None = 0,
	TerrainWireframe = 1 << 0,  // B: 와이어프레임
	TerrainLODColor = 1 << 1,  // B: LOD별 색
	TerrainBounds = 1 << 2,  // A: 청크 AABB
	Navigation = 1 << 3,  // B가 그림, D가 데이터 제공
	NavPath = 1 << 4,  // D가 DrawDebugPath로 그림
};
bool IsDebugShowFlagEnabled(const UWorld* World, EDebugShowFlags Flag);

bool IsDebugShowFlagSetInView(const FEngineShowFlags& ViewFlags, EDebugShowFlags Flag);

FVector4 GetTerrainLODDebugColor(int32 LODLevel);

void DrawWorldDebugToLineBatcher(const UWorld* World, FLineBatcher& LineBatcher, const FEngineShowFlags& ViewFlags,
					EDebugDepthPriority Depth);
