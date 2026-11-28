#pragma once

#include "SkeletalMeshEditorPanel.h"

class UPhysicsAsset;

// UE5 스켈레탈 메시 에디터 왼쪽의 Editing Tools 패널.
// 왼쪽 세로 줄에서 도구 분류(Skin, Pose, Cloth, Physics)를 고르고, 오른쪽에 그 분류의 도구를 그린다.
// 아래에는 Asset Editing Settings(편집 LOD와 Undo/Redo)를 고정한다.
class FSkeletalMeshEditingToolsPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FSkeletalMeshEditingToolsPanel(FSkeletalMeshEditor& InEditor);

protected:
	void DrawContent() override;

private:
	enum class ECategory : uint8
	{
		Skin,
		Pose,
		Cloth,
		Physics,
	};

	void DrawCategoryBar(float Height);
	void DrawSkinTools();
	void DrawPoseTools();
	void DrawClothTools();
	void DrawPhysicsTools();
	void DrawAssetEditingSettings();

	ECategory Category = ECategory::Skin;
	bool bWasClothPaintMode = false;

	FString ClothBindingMessage;
	UPhysicsAsset* DebugPhysicsAsset = nullptr;
	float DebugMinBoneSize = 20.0f;
	FString DebugMessage;
};
