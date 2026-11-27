#include "EnginePCH.h"
#include "Debug/DrawDebugHelpers.h"

// 1일차 링크용 빈 스텁. 아무것도 그리지 않는다. TODO(B): 구현

void DrawDebugLine(const UWorld* World, const FVector& Start, const FVector& End, const FVector4& Color,
	bool bPersistent, float LifeTime, EDebugDepthPriority Depth)
{
}

void DrawDebugSphere(const UWorld* World, const FVector& Center, float Radius, int32 Segments, const FVector4& Color,
	bool bPersistent, float LifeTime, EDebugDepthPriority Depth)
{
}

void DrawDebugBox(const UWorld* World, const FBox& Box, const FVector4& Color,
	bool bPersistent, float LifeTime, EDebugDepthPriority Depth)
{
}

void DrawDebugPoint(const UWorld* World, const FVector& Position, float Size, const FVector4& Color,
	bool bPersistent, float LifeTime)
{
}

void DrawDebugDirectionalArrow(const UWorld* World, const FVector& Start, const FVector& End, float ArrowSize,
	const FVector4& Color, bool bPersistent, float LifeTime)
{
}

void DrawDebugPath(const UWorld* World, const TArray<FVector>& Points, const FVector4& Color,
	bool bPersistent, float LifeTime)
{
}

void FlushPersistentDebugLines(const UWorld* World)
{
}

bool IsDebugShowFlagEnabled(const UWorld* World, EDebugShowFlags Flag)
{
	return false;
}
