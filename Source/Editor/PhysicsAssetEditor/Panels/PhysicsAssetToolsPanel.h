#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"

// UE5 Physics Asset 에디터 오른쪽 아래 Tools 패널.
// Body Creation 설정을 고르고, 아래 고정 버튼으로 모든 바디를 다시 만든다.
class FPhysicsAssetToolsPanel : public TAssetEditorPanel<FPhysicsAssetEditor>
{
public:
	explicit FPhysicsAssetToolsPanel(FPhysicsAssetEditor& InEditor)
		: TAssetEditorPanel<FPhysicsAssetEditor>(InEditor, "Tools", "PhysicsAssetPanel")
	{
	}

protected:
	void DrawContent() override;
};
