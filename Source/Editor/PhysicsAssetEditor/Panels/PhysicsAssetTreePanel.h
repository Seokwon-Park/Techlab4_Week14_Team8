#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"

class FPhysicsAssetTreePanel : public TAssetEditorPanel<FPhysicsAssetEditor>
{
public:
	explicit FPhysicsAssetTreePanel(FPhysicsAssetEditor& InEditor)
		: TAssetEditorPanel<FPhysicsAssetEditor>(InEditor, "Skeleton Tree", "PhysicsAssetPanel")
	{
	}

protected:
	void DrawContent() override;

private:
	void DrawBone(int32 BoneIndex);
	void DrawBody(int32 BoneIndex, int32 BodyIndex, bool bNestChildBones);
	void DrawShapes(int32 BodyIndex);
	void DrawShape(int32 BodyIndex, EAggCollisionShape::Type ShapeType, int32 ShapeIndex, const char* Label);
	void DrawConstraints(int32 BoneIndex);
	bool HasConstraints(int32 BoneIndex) const;
	bool HasBodyDescendant(int32 BoneIndex) const;
	int32 FindBody(int32 BoneIndex) const;

	void DrawConstrainToMenu(int32 BodyIndex);

	enum class EPendingAction : uint8 { None, AddBody, DeleteBody, DeleteConstraint, AddShape, DeleteShape, RegenerateBody, MakeConstraint };
	// 트리를 그리는 도중에 배열을 바꾸면 순회가 깨지므로 다 그린 뒤 실행한다
	EPendingAction PendingAction = EPendingAction::None;
	int32 PendingIndex = INDEX_NONE;
	EAggCollisionShape::Type PendingShapeType = EAggCollisionShape::Unknown;
	int32 PendingShapeIndex = INDEX_NONE;
	int32 PendingTargetBodyIndex = INDEX_NONE;

	bool bShowAllBones = false;
};
