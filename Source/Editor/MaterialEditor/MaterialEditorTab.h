#pragma once

#include "Editor/AssetEditor/DockedAssetEditorTab.h"

class FMaterialEditor;

class FMaterialEditorTab : public FDockedAssetEditorTab
{
public:
	explicit FMaterialEditorTab(FMaterialEditor& InEditor);

	void DrawMenuBar() override;
	void OnRender(bool bActive) override;

protected:
	void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size) override;

private:
	void DrawUnsavedPromptModal();

	FMaterialEditor& MaterialEditor;
};
