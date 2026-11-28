#pragma once

#include "Engine/WorldSubsystem.h"

class ATerrain;

enum class ETerrainHeightInterp : uint8
{
	Triangle,
	Bilinear,
};

class FTerrainCollisionSubsystem : public FWorldSubsystem
{
public:
	// PxHeightField 생성 + 씬 등록
	void OnLevelLoad(const FLevelLoadContext& Context) override;
	// 캐릭터를 Player Start로 텔레포트
	void OnWorldBeginPlay(UWorld& World) override;
	// scene->removeActor → actor->release → heightField->release
	void OnLevelUnload() override;
	int32 GetLoadOrder() const override { return 200; }
	void GetStats(FWorldSubsystemStats& Out) const override;

	// 지형이 없거나 범위 밖이면 false
	bool GetHeightAt(const FVector& WorldLocation, float& OutZ,
		ETerrainHeightInterp Mode = ETerrainHeightInterp::Triangle) const;

	// Triangle이면 GPU가 그리는 노멀과 같은 값이다
	bool GetNormalAt(const FVector& WorldLocation, FVector& OutNormal,
		ETerrainHeightInterp Mode = ETerrainHeightInterp::Triangle) const;

private:
	const ATerrain* FindTerrain() const;
};
