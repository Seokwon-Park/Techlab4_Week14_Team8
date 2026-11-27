// Source/Runtime/Engine/LevelFlowSubsystem.h   (담당: B)
// 공유 계약 4, 순서 400. UE GameMode + 트리거 처리 자리.
#pragma once

#include "Engine/WorldSubsystem.h"

class FLevelFlowSubsystem : public FWorldSubsystem
{
public:
	// 트리거 볼륨 바인딩, FlushPersistentDebugLines
	void OnLevelLoad(const FLevelLoadContext& Context) override;
	// 바인딩 해제
	void OnLevelUnload() override;
	int32 GetLoadOrder() const override { return 400; }
};
