// Source/Runtime/AI/Navigation/NavigationSubsystem.h   (담당: D)
// 공유 계약 4, 순서 300. UE UNavigationSystemV1 자리.
#pragma once

#include "Engine/WorldSubsystem.h"
#include "AI/Navigation/NavigationTypes.h"

struct FNavMeshDebugGeometry;

class FNavigationSubsystem : public FWorldSubsystem
{
public:
	// NavBounds가 있으면 빌드 또는 캐시 로드, 디버그 지오메트리 생성
	void OnLevelLoad(const FLevelLoadContext& Context) override;
	// 진행 중 경로 취소 → dtFreeNavMeshQuery → dtFreeNavMesh
	void OnLevelUnload() override;
	int32 GetLoadOrder() const override { return 300; }
	void GetStats(FWorldSubsystemStats& Out) const override;

	// 에디터 Build Navigation 버튼이 부른다.
	void BuildNavigation();

	// findNearestPoly → findPath → findStraightPath
	FNavPathResult FindPath(const FVector& Start, const FVector& End) const;

	// B가 오버레이로 그린다. NavMesh가 없으면 nullptr
	const FNavMeshDebugGeometry* GetDebugGeometry() const;
};
