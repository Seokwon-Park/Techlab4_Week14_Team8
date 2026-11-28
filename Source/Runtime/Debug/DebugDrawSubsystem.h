#pragma once
// DrawDebug* 함수가 쌓는 월드별 디버그 도형 대기열.
//
// 왜 서브시스템인가
//   대기열은 월드마다 따로 있어야 한다(에디터 월드와 PIE 월드가 동시에 살아 있다).
//   UWorld에 멤버를 또 붙이는 대신 계약 4의 서브시스템으로 만들면, 월드와 함께 생기고 사라지고,
//   레벨이 바뀔 때 OnLevelUnload에서 이전 레벨 선을 자동으로 비울 수 있다.
#include "Engine/WorldSubsystem.h"
#include "Debug/DrawDebugHelpers.h"
#include "Math/Vector4.h"

struct FNavMeshDebugGeometry;

class FDebugDrawSubsystem : public FWorldSubsystem
{
public:
	struct FLine
	{
		FVector Start;
		FVector End;
		FVector4 Color;
		float RemainingLife = 0.0f;
		bool bPersistent = false;
		EDebugDepthPriority Depth = EDebugDepthPriority::World;
	};
	struct FTriangle
	{
		FVector A, B, C;
		FVector4 Color;
		float RemainingLife = 0.0f;
		bool bPersistent = false;
	};

	// 다른 계약 서브시스템보다 먼저(10) 만들어 두고 가장 늦게 정리한다.
	int32 GetLoadOrder() const override { return 10; }

	// 한 프레임짜리 도형을 지우고 시간이 다 된 도형을 지운다. 플래그 기록도 한 칸 민다.
	void Tick(float DeltaTime) override;
	// 레벨이 바뀌면 이전 레벨 위치에 그린 선은 의미가 없으므로 전부 지운다 (계약 3: 레벨 전환 시 Flush).
	void OnLevelUnload() override;

	void AddLine(const FVector& Start, const FVector& End, const FVector4& Color, bool bPersistent, float LifeTime, EDebugDepthPriority Depth);
	void AddTriangle(const FVector& A, const FVector& B, const FVector& C, const FVector4& Color, bool bPersistent, float LifeTime);
	void FlushPersistent();

	const TArray<FLine>& GetLines() const { return Lines; }
	const TArray<FTriangle>& GetTriangles() const { return Triangles; }

	// 뷰포트가 그릴 때 자기 플래그를 알린다. IsDebugShowFlagEnabled가 이 기록을 읽는다.
	void RecordViewFlags(uint32 Flags) { FlagsThisFrame |= Flags; }
	uint32 GetRecentViewFlags() const { return FlagsThisFrame | FlagsLastFrame; }

	// NavMesh가 아직 없을 때 오버레이를 시험하려고 넣는 미리보기 지오메트리.
	// FNavigationSubsystem::GetDebugGeometry()가 nullptr이 아니면 그쪽이 우선한다.
	void SetNavMeshPreviewGeometry(const FNavMeshDebugGeometry* Geometry) { PreviewGeometry = Geometry; }
	const FNavMeshDebugGeometry* GetNavMeshPreviewGeometry() const { return PreviewGeometry; }

private:
	TArray<FLine> Lines;
	TArray<FTriangle> Triangles;
	uint32 FlagsThisFrame = 0;
	uint32 FlagsLastFrame = 0;
	const FNavMeshDebugGeometry* PreviewGeometry = nullptr;
};
