#pragma once

#include "NavigationTypes.h"
#include "NavMeshDebugGeometry.h"

#include "Engine/WorldSubsystem.h"

class dtNavMesh;
class dtNavMeshQuery;

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

private:
	static void FreeNavMesh(dtNavMesh* Mesh);
	static void FreeNavQuery(dtNavMeshQuery* Query);
	void ClearNavigation();

private:
	std::unique_ptr<dtNavMesh, void (*)(dtNavMesh*)> NavMesh{ nullptr, &FNavigationSubsystem::FreeNavMesh };
	std::unique_ptr<dtNavMeshQuery, void (*)(dtNavMeshQuery*)> NavQuery{ nullptr, &FNavigationSubsystem::FreeNavQuery };
	FNavMeshDebugGeometry DebugGeometry;
};
