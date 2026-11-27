// Source/Runtime/Physics/TerrainCollisionSubsystem.h   (담당: C)
// 공유 계약 4, 순서 200. UE ULandscapeHeightfieldCollisionComponent 자리.
// 계약 1의 PxHeightField 매핑과 CPU 높이·노멀 질의를 맡는다.
#pragma once

#include "Engine/WorldSubsystem.h"

class FTerrainCollisionSubsystem : public FWorldSubsystem
{
public:
	// PxHeightField 생성 + 씬 등록
	void OnLevelLoad(const FLevelLoadContext& Context) override;
	// BeginPlay: 캐릭터 컨트롤러를 Player Start로 setFootPosition 텔레포트
	void OnWorldBeginPlay(UWorld& World) override;
	// scene->removeActor → actor->release → heightField->release
	void OnLevelUnload() override;
	int32 GetLoadOrder() const override { return 200; }
	void GetStats(FWorldSubsystemStats& Out) const override;

	// 지형이 없거나 범위 밖이면 false. 피킹은 그때 UWorld::LineTraceSingle(PhysX)로 대체한다.
	bool GetHeightAt(const FVector& WorldLocation, float& OutZ) const;
	// 계약 1 규칙 5의 중앙 차분 노멀. A의 렌더 노멀과 같은 값이어야 한다.
	bool GetNormalAt(const FVector& WorldLocation, FVector& OutNormal) const;
};
