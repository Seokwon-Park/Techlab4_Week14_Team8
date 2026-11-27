// Source/Runtime/Terrain/TerrainSubsystem.h   (담당: A)
// 공유 계약 4, 순서 100. UE 랜드스케이프 서브시스템 자리.
#pragma once

#include "Engine/WorldSubsystem.h"

class ATerrain;

class FTerrainSubsystem : public FWorldSubsystem
{
public:
	// Terrain이 있으면 청크 VB/IB·SRV 생성
	void OnLevelLoad(const FLevelLoadContext& Context) override;
	// Terrain::Release() → VB/IB/SRV 해제, 개수 0 확인
	void OnLevelUnload() override;
	int32 GetLoadOrder() const override { return 100; }
	void GetStats(FWorldSubsystemStats& Out) const override;

	// 모든 지형 접근은 이 한 함수를 통한다. 결과를 캐시하지 말고 매번 받는다.
	// ★ nullptr 가능 (던전 레벨)
	ATerrain* GetTerrain() const;
};
