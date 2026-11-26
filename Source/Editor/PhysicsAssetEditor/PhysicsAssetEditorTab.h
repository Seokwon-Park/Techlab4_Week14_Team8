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
	void DrawToolbar();

	void BeforeRender() override;
	void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size) override;

	FPhysicsAssetEditor& PhysicsEditor;
	FEditorCommand SaveCommand;
	FEditorCommand SimulateCommand;
	FEditorCommand StopSimulationCommand;
	FEditorCommand RegenerateAllBodiesCommand;
};
