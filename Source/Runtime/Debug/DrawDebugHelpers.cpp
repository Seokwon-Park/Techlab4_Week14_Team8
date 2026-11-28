#include "EnginePCH.h"
#include "Debug/DrawDebugHelpers.h"
#include "Debug/DebugDrawSubsystem.h"
#include "AI/Navigation/NavigationSubsystem.h"
#include "AI/Navigation/NavMeshDebugGeometry.h"
#include "AI/Navigation/NavigationTypes.h"
#include "Engine/World.h"
#include "Render/LineBatcher.h"
#include "Render/ShowFlags.h"
#include <cmath>

namespace
{
	// 모든 DrawDebug* 함수가 거치는 입구. 월드가 없거나(에디터 밖에서 호출) 서브시스템이 없으면 조용히 무시한다.
	// UE의 DrawDebugLine도 World가 null이면 아무것도 하지 않는다.
	FDebugDrawSubsystem* GetQueue(const UWorld* World)
	{
		return World ? World->GetSubsystem<FDebugDrawSubsystem>() : nullptr;
	}

	// 서로 직교하는 두 축 위에 원 하나를 선분 Segments개로 쌓는다. 구와 Acceptance Radius 원에 쓴다.
	void AddCircle(FDebugDrawSubsystem& Queue, const FVector& Center, const FVector& AxisA, const FVector& AxisB,
		float Radius, int32 Segments, const FVector4& Color, bool bPersistent, float LifeTime, EDebugDepthPriority Depth)
	{
		Segments = std::max(Segments, 4);
		const float Step = 2.0f * PI / static_cast<float>(Segments);
		FVector Prev = Center + AxisA * Radius;
		for (int32 i = 1; i <= Segments; ++i)
		{
			const float Angle = Step * static_cast<float>(i);
			const FVector Next = Center + AxisA * (std::cos(Angle) * Radius) + AxisB * (std::sin(Angle) * Radius);
			Queue.AddLine(Prev, Next, Color, bPersistent, LifeTime, Depth);
			Prev = Next;
		}
	}

	// 목적지에 그리는 X 표시. 경로가 없을 때도 "어디를 요청했는지"는 보여야 하기 때문이다.
	void AddCross(FDebugDrawSubsystem& Queue, const FVector& At, float Size, const FVector4 Color, float LifeTime)
	{
		Queue.AddLine(At + FVector(-Size, -Size, 0.0f), At + FVector(Size, Size, 0.0f), Color, false, LifeTime, EDebugDepthPriority::Foreground);
		Queue.AddLine(At + FVector(-Size, Size, 0.0f), At + FVector(Size, -Size, 0.0f), Color, false, LifeTime, EDebugDepthPriority::Foreground);
	}

	uint32 ToMask(EDebugShowFlags Flag) { return static_cast<uint32>(Flag); }

	// 뷰포트 하나의 FEngineShowFlags를 계약 비트마스크로 바꾼다. RecordViewFlags에 넘기는 값이다.
	uint32 MakeViewMask(const FEngineShowFlags& ViewFlags)
	{
		uint32 Mask = 0;
		if (ViewFlags.TerrainWireframe) Mask |= ToMask(EDebugShowFlags::TerrainWireframe);
		if (ViewFlags.TerrainLODColor) Mask |= ToMask(EDebugShowFlags::TerrainLODColor);
		if (ViewFlags.TerrainBounds) Mask |= ToMask(EDebugShowFlags::TerrainBounds);
		if (ViewFlags.Navigation) Mask |= ToMask(EDebugShowFlags::Navigation);
		if (ViewFlags.NavPath) Mask |= ToMask(EDebugShowFlags::NavPath);
		return Mask;
	}

	// D가 만든 FNavMeshDebugGeometry를 LineBatcher에 넣는다.
	//   - 걸을 수 있는 면: 반투명 초록 삼각형 (AddTriangle은 양면 + 깊이 읽기 전용이라 지형에 가려지는 부분은 안 보인다)
	//   - 폴리곤 사이 경계: 옅은 초록 선, NavMesh 바깥 경계: 진한 청록 선
	//   지형과 같은 높이에 그리면 깊이 값이 같아 깜빡인다(Z-fighting). UE ARecastNavMesh::DrawOffset처럼 10cm 띄운다.
	void AddNavMeshGeometry(FLineBatcher& LineBatcher, const FNavMeshDebugGeometry& Geometry)
	{
		const FVector Offset(0.0f, 0.0f, 10.0f * CmToWorld);
		const FVector4 FillColor(0.1f, 0.8f, 0.2f, 0.35f);
		const FVector4 PolyEdgeColor(0.3f, 1.0f, 0.4f, 0.6f);
		const FVector4 BoundaryColor(0.0f, 0.9f, 1.0f, 1.0f);

		const TArray<FVector>& V = Geometry.Vertices;
		const TArray<uint32>& I = Geometry.TriangleIndices;
		const uint32 VertexCount = static_cast<uint32>(V.Num());
		for (int32 t = 0; t + 2 < I.Num(); t += 3)
		{
			// 인덱스가 범위를 벗어나면(D 쪽 버그) 그 삼각형만 건너뛴다. 오버레이 때문에 엔진이 죽으면 안 된다.
			if (I[t] >= VertexCount || I[t + 1] >= VertexCount || I[t + 2] >= VertexCount)continue;
			LineBatcher.AddTriangle(V[I[t]] + Offset, V[I[t + 1]] + Offset, V[I[t + 2]] + Offset, FillColor);

		}
		for (int32 e = 0; e + 1 < Geometry.PolyEdges.Num(); e += 2)
		{
			LineBatcher.AddLine(Geometry.PolyEdges[e] + Offset, Geometry.PolyEdges[e + 1] + Offset, PolyEdgeColor);
		}
		for (int32 e = 0; e + 1 < Geometry.BoundaryEdges.Num(); e += 2)
		{
			LineBatcher.AddLine(Geometry.BoundaryEdges[e] + Offset, Geometry.BoundaryEdges[e + 1] + Offset, BoundaryColor);
		}
	}
}

// ── 게임 코드가 부르는 쪽 ───────────────────────────────────────────
void DrawDebugLine(const UWorld* World, const FVector& Start, const FVector& End, const FVector4& Color,
	bool bPersistent, float LifeTime, EDebugDepthPriority Depth)
{
	if (FDebugDrawSubsystem* Queue = GetQueue(World))
		Queue->AddLine(Start, End, Color, bPersistent, LifeTime, Depth);
}

void DrawDebugSphere(const UWorld* World, const FVector& Center, float Radius, int32 Segments, const FVector4& Color,
	bool bPersistent, float LifeTime, EDebugDepthPriority Depth)
{
	FDebugDrawSubsystem* Queue = GetQueue(World);
	if (!Queue || Radius <= 0.0f) return;
	// LineBatcher::AddSphere와 같은 모양(서로 직교하는 원 3개)을 선으로 풀어 대기열에 넣는다.
	// 선으로 풀어 두어야 LifeTime·Persistent 규칙을 선과 똑같이 적용할 수 있다.
	const FVector X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);
	AddCircle(*Queue, Center, X, Y, Radius, Segments, Color, bPersistent, LifeTime, Depth);
	AddCircle(*Queue, Center, X, Z, Radius, Segments, Color, bPersistent, LifeTime, Depth);
	AddCircle(*Queue, Center, Y, Z, Radius, Segments, Color, bPersistent, LifeTime, Depth);
}

void DrawDebugBox(const UWorld* World, const FBox& Box, const FVector4& Color,
	bool bPersistent, float LifeTime, EDebugDepthPriority Depth)
{
	FDebugDrawSubsystem* Queue = GetQueue(World);
	if (!Queue) return;
	// 박스 꼭짓점 8개를 비트로 고른다: bit0 = X(Max), bit1 = Y(Max), bit2 = Z(Max)
	auto Corner = [&Box](int32 Bits)
		{
			return FVector((Bits & 1) ? Box.Max.X : Box.Min.X, (Bits & 2) ? Box.Max.Y : Box.Min.Y, (Bits & 4) ? Box.Max.Z : Box.Min.Z);
		};
	// 모서리 12개 = 한 비트만 다른 꼭짓점 쌍
	static const int32 Edges[12][2] = { {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{3,7} };
	for (const auto& Edge :Edges)
	{
		Queue->AddLine(Corner(Edge[0]), Corner(Edge[1]), Color, bPersistent, LifeTime, Depth);
	}
}

void DrawDebugPoint(const UWorld* World, const FVector& Position, float Size, const FVector4& Color,
	bool bPersistent, float LifeTime)
{
	FDebugDrawSubsystem* Queue = GetQueue(World);
	if (!Queue) return;
	// 화면 크기 고정 점은 LineBatcher가 지원하지 않으므로 세 축 십자로 대신한다. 항상 위에 보이게 Foreground로 그린다.
	const float H = Size * 0.5f;
	Queue->AddLine(Position - FVector(H, 0, 0), Position + FVector(H, 0, 0), Color, bPersistent, LifeTime, EDebugDepthPriority::Foreground);
	Queue->AddLine(Position - FVector(0, H, 0), Position + FVector(0, H, 0), Color, bPersistent, LifeTime, EDebugDepthPriority::Foreground);
	Queue->AddLine(Position - FVector(0, 0, H), Position + FVector(0, 0, H), Color, bPersistent, LifeTime, EDebugDepthPriority::Foreground);
}

void DrawDebugDirectionalArrow(const UWorld* World, const FVector& Start, const FVector& End, float ArrowSize,
	const FVector4& Color, bool bPersistent, float LifeTime)
{
	FDebugDrawSubsystem* Queue = GetQueue(World);
	if (!Queue) return;
	Queue->AddLine(Start, End, Color, bPersistent, LifeTime, EDebugDepthPriority::World);

	// 화살촉: 진행 방향의 뒤쪽으로 ArrowSize만큼 물러난 점에서 옆 방향으로 벌린 두 선
	const FVector Dir = (End - Start).GetSafeNormal();
	if (Dir.Size() < 0.5f) return; // 시작점과 끝점이 같으면 방향이 없다
	FVector Side = Dir.Cross(FVector(0, 0, 1)).GetSafeNormal();
	if (Side.Size() < 0.5f) Side = FVector(0, 1, 0); // 수직 화살표는 Y축으로 벌린다
	const FVector Back = End - Dir * ArrowSize;
	Queue->AddLine(End, Back + Side * (ArrowSize * 0.5f), Color, bPersistent, LifeTime, EDebugDepthPriority::World);
	Queue->AddLine(End, Back - Side * (ArrowSize * 0.5f), Color, bPersistent, LifeTime, EDebugDepthPriority::World);
}

void DrawDebugPath(const UWorld* World, const TArray<FVector>& Points, const FVector4& Color,
	bool bPersistent, float LifeTime)
{
	FDebugDrawSubsystem* Queue = GetQueue(World);
	if (!Queue) return;

	// 경로는 지형 위를 지나므로 지형에 묻히지 않게 Foreground로 그린다. 점 사이를 선분으로 잇는다.
	for (int32 i = 0; i + 1 < Points.Num(); ++i)
		Queue->AddLine(Points[i], Points[i + 1], Color, bPersistent, LifeTime, EDebugDepthPriority::Foreground);
}

void DrawDebugSolidTriangle(const UWorld* World, const FVector& A, const FVector& B, const FVector& C, const FVector4& Color, bool bPersistent, float LifeTime)
{
	if (FDebugDrawSubsystem* Queue = GetQueue(World))
	{
		Queue->AddTriangle(A, B, C, Color, bPersistent, LifeTime);
	}
}

void DrawDebugNavPath(const UWorld* World, const FVector& RequestedGoal, const FNavPathResult& Result, float AcceptanceRadius, float LifeTime)
{
	FDebugDrawSubsystem* Queue = GetQueue(World);
	if (!Queue || !IsDebugShowFlagEnabled(World, EDebugShowFlags::NavPath)) return;

	const FVector4 Green(0.2f, 1.0f, 0.3f, 1.0f);
	const FVector4 Yellow(1.0f, 0.85f, 0.1f, 1.0f);
	const FVector4 Red(1.0f, 0.2f, 0.2f, 1.0f);
	const FVector Lift(0.0f, 0.0f, 5.0f * CmToWorld); //지형보다 살짝 위로
	const float MarkerSize = std::max(AcceptanceRadius * 0.5f, 10.0f * CmToWorld);

	// 경로 선과 꺾이는 점(findStraightPath의 각 점)
	const bool bReached = Result.Status == ENavPathStatus::Success;
	if (Result.IsValid())
	{
		const FVector4& PathColor = bReached ? Green : Yellow;
		for (int32 i = 0; i + 1 < Result.Points.Num(); ++i)
			Queue->AddLine(Result.Points[i] + Lift, Result.Points[i + 1] + Lift, PathColor, false, LifeTime, EDebugDepthPriority::Foreground);
	}

	// 목적지: 도착 판정 반경(Acceptance Radius) 원. 성공이면 초록, 아니면 빨강 + X
	const FVector GoalAt = (bReached && Result.Points.Num() > 0) ? Result.Points.Last() : RequestedGoal;
	AddCircle(*Queue, GoalAt+ Lift, FVector(1,0,0), FVector(0,1,0), std::max(AcceptanceRadius, MarkerSize),24,
		bReached ? Green : Red, false, LifeTime, EDebugDepthPriority::Foreground);
	if (!bReached)
		AddCross(*Queue, RequestedGoal + Lift, MarkerSize, Red, LifeTime);
}

void FlushPersistentDebugLines(const UWorld* World)
{
	if (FDebugDrawSubsystem* Queue = GetQueue(World))
		Queue->FlushPersistent();
}

bool IsDebugShowFlagEnabled(const UWorld* World, EDebugShowFlags Flag)
{
	const FDebugDrawSubsystem* Queue = GetQueue(World);
	return Queue && (Queue->GetRecentViewFlags() & ToMask(Flag)) != 0;
}

bool IsDebugShowFlagSetInView(const FEngineShowFlags& ViewFlags, EDebugShowFlags Flag)
{
	return (MakeViewMask(ViewFlags) & ToMask(Flag)) != 0;
}

FVector4 GetTerrainLODDebugColor(int32 LODLevel)
{
	static const FVector4 Colors[] =
	{
		FVector4(1.0f, 0.25f, 0.25f, 1.0f), // LOD 0  빨강
		FVector4(0.25f, 1.00f, 0.25f, 1.0f), // LOD 1 초록
		FVector4(0.3f, 0.5f, 1.0f, 1.0f), // LOD 2 파랑
		FVector4(1.0f, 0.9f, 0.2f, 1.0f), // LOD 3 노랑
	};
	if (LODLevel >= 0 && LODLevel < 4) return Colors[LODLevel];
	return FVector4(0.8f, 0.3f,1.0f, 1.0f); // 그 이상 보라
}

// ── 뷰포트가 부르는 쪽 ─────────────────────────────────────────────
void DrawWorldDebugToLineBatcher(const UWorld* World, FLineBatcher& LineBatcher, const FEngineShowFlags& ViewFlags, EDebugDepthPriority Depth)
{
	FDebugDrawSubsystem* Queue = GetQueue(World);
	if (!Queue) return;

	// 이 뷰포트가 어떤 플래그로 그렸는지 남긴다 → 다음 프레임 IsDebugShowFlagEnabled가 읽는다.
	Queue->RecordViewFlags(MakeViewMask(ViewFlags));

	for (const FDebugDrawSubsystem::FLine& Line : Queue->GetLines())
	{
		if (Line.Depth == Depth)
		{
			LineBatcher.AddLine(Line.Start, Line.End, Line.Color);
		}
	}

	if (Depth != EDebugDepthPriority::World) return;

	// 반투명 면은 깊이 테스트가 있는 World 패스에서만 그린다.
	for (const FDebugDrawSubsystem::FTriangle& Tri : Queue->GetTriangles())
	{
		LineBatcher.AddTriangle(Tri.A, Tri.B, Tri.C, Tri.Color);
	}

	// NavMesh 오버레이 (언리얼의 P 키). 지오메트리가 있으면 그것, 없으면 미리보기.
	if (ViewFlags.Navigation)
	{
		const FNavMeshDebugGeometry* Geometry = nullptr;
		if (const FNavigationSubsystem* Nav = World->GetSubsystem<FNavigationSubsystem>())
		{
			Geometry = Nav->GetDebugGeometry();
		}
		if (!Geometry)
		{
			Geometry = Queue->GetNavMeshPreviewGeometry();
		}
		if (Geometry)
		{
			AddNavMeshGeometry(LineBatcher, *Geometry);
		}
	}
}
