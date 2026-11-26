#pragma once

#include "SkeletalMeshEditorPanel.h"

// 메시 상세 정보 UI를 확장하기 위한 패널 골격입니다.
class FSkeletalMeshDetailsPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FSkeletalMeshDetailsPanel(FSkeletalMeshEditor& InEditor);

protected:
	void DrawContent() override;

private:
	// 물리 단계별 확인용 (임시). C의 Physics Asset 에디터가 생기면 지운다
	void DrawPhysicsDebug();
	void DrawClothBinding();

	FString ClothBindingMessage;
	class UPhysicsAsset* DebugPhysicsAsset = nullptr;
	float DebugMinBoneSize = 20.0f;
	FString DebugMessage;
};
