#pragma once

#include "Editor/AssetEditor/AssetEditorTab.h"
#include "Panels/SkeletalMeshEditorPanel.h"

class FSkeletalMeshEditor;

class FSkeletalMeshEditorTab : public FAssetEditorTab
{
public:
	explicit FSkeletalMeshEditorTab(FSkeletalMeshEditor& InEditor);

	// 탭의 메뉴 바를 그립니다.
	void DrawMenuBar() override;

	// 탭의 렌더링을 수행합니다.
	void OnRender(bool bActive) override;

private:
	// 패널 순서. BuildDefaultLayout과 툴바가 이 순서로 패널을 찾는다.
	enum EPanel : int32
	{
		Panel_Viewport,
		Panel_EditingTools,
		Panel_SkeletonTree,
		Panel_AssetDetails,
		Panel_Details,
		Panel_PreviewSettings,
	};

	void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size);

	// UE5 스켈레탈 메시 에디터 상단 툴바 (편집 도구, Cloth Paint, 에디터 모드 전환)
	void DrawToolbar();

	FSkeletalMeshEditor& MeshEditor;
	TArray<TUniquePtr<FSkeletalMeshEditorPanel>> Panels;
	ImGuiID DockSpaceId = 0;
	ImGuiID PanelClassId = 0;
	bool bLayoutInitialized = false;
};
