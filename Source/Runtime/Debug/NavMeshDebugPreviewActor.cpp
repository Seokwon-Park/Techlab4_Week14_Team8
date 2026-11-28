#include "EnginePCH.h"
#include "NavMeshDebugPreviewActor.h"
#include "DebugDrawSubsystem.h"
#include "DrawDebugHelpers.h"
#include "AI/Navigation/NavigationTypes.h"
#include "Engine/World.h"

ANavDebugPreviewActor::ANavDebugPreviewActor()
{
	// Tick이 돌아야 매 프레임 경로를 다시 그린다 (한 프레임짜리 선은 다음 Tick에 지워진다).
	PrimaryActorTick.bCanEverTick = true;
}

void ANavDebugPreviewActor::BuildFakeGeometry()
{
	// 액터 위치를 한가운데로 하는 GridCells × GridCells 격자. 칸 하나 = 삼각형 2개.
	FakeGeometry = FNavMeshDebugGeometry{};
	const FVector Center = GetActorLocation();
	const float Half = GridCells * CellSize * 0.5f;
	const int32 Row = GridCells + 1;
	for (int32 y = 0; y <= GridCells; ++y)
	{
		for (int32 x = 0; x <= GridCells; ++x)
		{
			FakeGeometry.Vertices.Add(Center + FVector(x * CellSize - Half, y * CellSize - Half, 0.0f));
		}
	}

	for (int32 y = 0; y < GridCells; ++y)
	{
		for (int x = 0; x < GridCells; ++x)
		{
			const uint32 I0 = y * Row + x, I1 = I0 + 1, I2 = I0 + Row, I3 = I2 + 1;
			FakeGeometry.TriangleIndices.Add(I0);
			FakeGeometry.TriangleIndices.Add(I1);
			FakeGeometry.TriangleIndices.Add(I3);
			FakeGeometry.TriangleIndices.Add(I0);
			FakeGeometry.TriangleIndices.Add(I3);
			FakeGeometry.TriangleIndices.Add(I2);

			if (x + 1 < GridCells)
			{
				FakeGeometry.PolyEdges.Add(FakeGeometry.Vertices[I1]);
				FakeGeometry.PolyEdges.Add(FakeGeometry.Vertices[I3]);
			}
			if (y + 1 < GridCells)
			{
				FakeGeometry.PolyEdges.Add(FakeGeometry.Vertices[I2]);
				FakeGeometry.PolyEdges.Add(FakeGeometry.Vertices[I3]);
			}
		}
	}

	const FVector C0 = FakeGeometry.Vertices[0], C1 = FakeGeometry.Vertices[GridCells];
	const FVector C2 = FakeGeometry.Vertices[GridCells*Row], C3 = FakeGeometry.Vertices[GridCells*Row + GridCells];
	const FVector Corners[5] = { C0, C1,C3,C2, C0 };
	for (int32 i = 0; i < 4; ++i)
	{
		FakeGeometry.BoundaryEdges.Add(Corners[i]); 
		FakeGeometry.BoundaryEdges.Add(Corners[i + 1]);
	}
	FakeGeometry.Revision++;
}

void ANavDebugPreviewActor::BeginPlay()
{
	Super::BeginPlay();
	GridCells = std::max(GridCells, 1);
	CellSize = std::max(CellSize, 0.01f);
	BuildFakeGeometry();
	// 오버레이 미리보기로 등록. D의 진짜 NavMesh가 생기면 그쪽이 우선한다.
	// 이 액터가 레벨과 함께 사라질 때 서브시스템의 OnLevelUnload가 포인터를 지우므로 매달린 포인터가 남지 않는다.
	if (FDebugDrawSubsystem* Debug = GetWorld()->GetSubsystem<FDebugDrawSubsystem>())
		Debug->SetNavMeshPreviewGeometry(&FakeGeometry);
}

void ANavDebugPreviewActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	const FVector Center = GetActorLocation();
	const float Half = GridCells * CellSize * 0.5f;

	FNavPathResult Success;
	Success.Status = ENavPathStatus::Success;
	Success.Points.Add(Center + FVector(-Half * 0.8f, -Half * 0.8f, 0.0f));
	Success.Points.Add(Center + FVector(0.0f, -Half * 0.8f, 0.0f));
	Success.Points.Add(Center + FVector(Half * 0.8f, Half * 0.8f, 0.0f));
	DrawDebugNavPath(World, Success.Points.Last(), Success, 0.5f * CellSize);

	// 부분 경로: 격자 밖 목적지를 요청했지만 가장자리까지만 감 → 노란 선 + 빨간 X(요청한 목적지)
	FNavPathResult Partial;
	Partial.Status = ENavPathStatus::Partial;
	Partial.Points.Add(Center + FVector(-Half * 0.8f, Half * 0.8f, 0.0f));
	Partial.Points.Add(Center + FVector(Half, Half * 0.8f, 0.0f));
	DrawDebugNavPath(World, Center + FVector(Half * 1.6f, Half * 0.8f, 0.0f), Partial, 0.5f * CellSize);
}