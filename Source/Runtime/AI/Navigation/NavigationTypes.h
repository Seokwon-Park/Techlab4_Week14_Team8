// Source/Runtime/AI/Navigation/NavigationTypes.h   (담당: D, C가 include)
// 공유 계약 2: 에이전트 파라미터. 문서: Docs/Week14/SharedContract.md
#pragma once
#include "Core/Types.h"
#include "Math/Vector.h"
#include "Containers/Array.h"

// UE FNavAgentProperties + ARecastNavMesh의 Agent* 값. 단위 cm, 각도 degree.
struct FNavAgentProperties
{
    float AgentRadius     = 42.0f;   // 캡슐 반지름
    float AgentHeight     = 192.0f;  // 캡슐 전체 높이 (반구 포함)
    float AgentMaxSlope   = 44.765f; // UE CMC WalkableFloorAngle 기본값
    float AgentStepHeight = 45.0f;   // UE CMC MaxStepHeight 기본값
};

// UE ARecastNavMesh의 복셀 설정
struct FNavMeshResolution
{
    float CellSize   = 19.0f;  // UE 기본값
    float CellHeight = 10.0f;
};

// C와 D는 이 함수 하나로만 값을 얻는다. 각자 상수를 두지 않는다.
const FNavAgentProperties& GetDefaultNavAgent();
const FNavMeshResolution&  GetDefaultNavResolution();

// 경로 질의 결과 (계약 추가분). "경로가 없거나 이동할 수 없을 때의 동작"은 Status로 나눠서 처리한다.
enum class ENavPathStatus : uint8
{
    Success,            // 목적지까지 경로 있음
    Partial,            // 목적지에 못 닿음. Points는 가장 가까운 곳까지 (Detour DT_PARTIAL_RESULT)
    NoNavMesh,          // NavMesh가 없음 (던전에 빌드할 지오메트리가 없거나 아직 빌드 전)
    StartNotOnNavMesh,  // findNearestPoly 실패 (시작점)
    GoalNotOnNavMesh,   // findNearestPoly 실패 (목적지)
    QueryFailed,        // 버퍼 부족이나 Detour 오류 등
};

struct FNavPathResult
{
    ENavPathStatus  Status = ENavPathStatus::NoNavMesh;
    TArray<FVector> Points;   // findStraightPath 결과, 월드 좌표(Z-up)

    bool IsValid() const { return Status == ENavPathStatus::Success || Status == ENavPathStatus::Partial; }
};

// UE RecastHelpers.h와 같은 축 변환 (Z-up 왼손 → Recast Y-up 오른손)
inline FVector Unreal2RecastPoint(const FVector& V) { return FVector(-V.X, V.Z, -V.Y); }
inline FVector Recast2UnrealPoint(const float* R)   { return FVector(-R[0], -R[2], R[1]); }
