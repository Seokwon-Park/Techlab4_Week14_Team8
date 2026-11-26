#pragma once

#include <memory>
#include <vector>

#include "Editor/AssetEditor/AssetEditorTab.h"

class FInputAssetEditor;
class FInputModifier;
class FInputTrigger;
class UInputAction;
struct FEnhancedActionKeyMapping;

// 입력 에셋 에디터 탭. 패널 하나짜리라 도킹 없이 탭 영역에 바로 그린다.
class FInputAssetEditorTab : public FAssetEditorTab
{
public:
	explicit FInputAssetEditorTab(FInputAssetEditor& InEditor);

	void DrawMenuBar() override;
	void OnRender(bool bActive) override;

private:
	void DrawToolbar();
	void DrawActionEditor(UInputAction& Action);
	void DrawContextEditor();
	// 반환값: 이 매핑을 지워 달라는 요청
	bool DrawMapping(FEnhancedActionKeyMapping& Mapping, int32 Index);
	void DrawActionPicker(FEnhancedActionKeyMapping& Mapping);
	void DrawKeyPicker(FEnhancedActionKeyMapping& Mapping, int32 Index);
	void UpdateKeyCapture();

	// 목록을 그리고 바꿨으면 true
	bool DrawModifierList(std::vector<std::shared_ptr<FInputModifier>>& Modifiers);
	bool DrawTriggerList(std::vector<std::shared_ptr<FInputTrigger>>& Triggers);

	FInputAssetEditor& InputEditor;

	// "Listen" 중인 매핑 번호. -1이면 아님. 누른 프레임의 클릭은 무시하려고 한 프레임 늦게 받는다
	int32 CapturingMapping = -1;
	bool bCaptureArmed = false;
	char KeyFilter[32] = {};
};
