#pragma once

#include "Editor/AssetEditor/DockedAssetEditorTab.h"
#include "Editor/EditorUI/EditorCommand.h"

class FPhysicsAssetEditor;

class FPhysicsAssetEditorTab : public FDockedAssetEditorTab
{
public:
	explicit FPhysicsAssetEditorTab(FPhysicsAssetEditor& InEditor);

	void OnRender(bool bActive) override;

private:
	// 패널 순서. 생성자의 RegisterPanel 순서와 같아야 한다
	enum EPanel : int32
	{
		Panel_Viewport,
		Panel_SkeletonTree,
		Panel_Details,
		Panel_PreviewSettings,
		Panel_Graph,
		Panel_Tools,
	};

	// UE5 Physics Asset 에디터 상단 툴바 (저장, 레퍼런스 포즈, 시뮬레이션, 충돌 켜기/끄기, 모드 전환)
	void DrawToolbar();

	void BeforeRender() override;
	void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size) override;

	FPhysicsAssetEditor& PhysicsEditor;
	FEditorCommand SaveCommand;
};
