// Source/Runtime/AI/Navigation/NavMeshDebugGeometry.h   (D가 채우고 B가 그림)
// 공유 계약 3: 디버그 드로우. 문서: Docs/Week14/SharedContract.md
#pragma once
#include "Math/Vector.h"
#include "Containers/Array.h"

// UE FRecastDebugGeometry 축소판. 이미 월드 좌표(Z-up)로 변환된 값만 담는다.
struct FNavMeshDebugGeometry
{
    TArray<FVector> Vertices;
    TArray<uint32>  TriangleIndices;    // 걸을 수 있는 폴리곤을 삼각형으로 푼 것
    TArray<FVector> PolyEdges;          // 폴리곤 사이 경계 (2개씩 = 선 하나)
    TArray<FVector> BoundaryEdges;      // NavMesh 바깥 경계
    uint32 Revision = 0;                // NavMesh를 다시 빌드할 때마다 +1 → B는 바뀔 때만 다시 굽는다
};
