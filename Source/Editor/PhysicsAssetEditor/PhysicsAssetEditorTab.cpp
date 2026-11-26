#include "EnginePCH.h"
#include "PhysicsAssetEditorTab.h"
#include "PhysicsAssetEditor.h"
#include "Editor/SkeletalPreviewEditor/SkeletalPreviewViewportPanel.h"
#include "Panels/PhysicsAssetTreePanel.h"
#include "Panels/PhysicsAssetDetailsPanel.h"
#include "Panels/PhysicsAssetSettingsPanel.h"
#include "Panels/PhysicsAssetGraphPanel.h"

FPhysicsAssetEditorTab::FPhysicsAssetEditorTab(FPhysicsAssetEditor& InEditor)
	: FDockedAssetEditorTab(InEditor, "PhysicsAssetDockSpace", "PhysicsAssetPanelClass")
	, PhysicsEditor(InEditor)
{
	RegisterPanel(MakeUnique<FSkeletalPreviewViewportPanel>(InEditor));
	RegisterPanel(MakeUnique<FPhysicsAssetTreePanel>(InEditor));
	RegisterPanel(MakeUnique<FPhysicsAssetDetailsPanel>(InEditor));
	RegisterPanel(MakeUnique<FPhysicsAssetSettingsPanel>(InEditor));
	RegisterPanel(MakeUnique<FPhysicsAssetGraphPanel>(InEditor));

	SaveCommand.Label = "Save";
	SaveCommand.Tooltip = "Save";
	SaveCommand.IconId = EEditorIcon::Save;
	SaveCommand.Shortcut = { EKeyCode::S, true, false, false };
	SaveCommand.Execute = [this] { PhysicsEditor.Save(); };
	SaveCommand.CanExecute = [this] { return PhysicsEditor.IsDirty() && !PhysicsEditor.IsSimulating(); };

	// 같은 자리에 Simulate / Stop 중 지금 쓸 수 있는 하나만 보인다
	SimulateCommand.Label = "Simulate";
	SimulateCommand.Tooltip = "Simulate (ragdoll test)";
	SimulateCommand.IconId = EEditorIcon::Play;
	SimulateCommand.IconTint = { 0.40f, 0.80f, 0.30f, 1.0f };
	SimulateCommand.Execute = [this] { PhysicsEditor.ToggleSimulation(); };
	SimulateCommand.IsVisible = [this] { return !PhysicsEditor.IsSimulating(); };

	StopSimulationCommand.Label = "Stop";
	StopSimulationCommand.Tooltip = "Stop simulation";
	StopSimulationCommand.IconId = EEditorIcon::Stop;
	StopSimulationCommand.IconTint = { 0.95f, 0.42f, 0.48f, 1.0f };
	StopSimulationCommand.Execute = [this] { PhysicsEditor.ToggleSimulation(); };
	StopSimulationCommand.IsVisible = [this] { return PhysicsEditor.IsSimulating(); };

	RegenerateAllBodiesCommand.Label = "Regenerate All Bodies";
	RegenerateAllBodiesCommand.Tooltip = "Delete all bodies and constraints and generate them again";
	RegenerateAllBodiesCommand.Execute = [this] { PhysicsEditor.RegenerateAllBodies(); };
	RegenerateAllBodiesCommand.CanExecute = [this] { return !PhysicsEditor.IsSimulating(); };
}

void FPhysicsAssetEditorTab::OnRender(bool bActive)
{
	// 기반 OnRender가 남은 영역으로 독스페이스 크기를 잡으므로 툴바를 먼저 그린다
	if (bActive && !PhysicsEditor.IsCloseRequested())
	{
		DrawToolbar();
	}
	FDockedAssetEditorTab::OnRender(bActive);
}

void FPhysicsAssetEditorTab::DrawToolbar()
{
	EditorCommandUI::ToolbarButton("PhysicsAssetSave", SaveCommand);
	ImGui::SameLine();
	EditorCommandUI::ToolbarButton("PhysicsAssetSimulate", SimulateCommand);
	EditorCommandUI::ToolbarButton("PhysicsAssetStopSimulation", StopSimulationCommand);
	ImGui::SameLine();
	EditorCommandUI::ToolbarButton("PhysicsAssetRegenerateAll", RegenerateAllBodiesCommand);
	ImGui::Separator();

	const ImGuiFocusedFlags FocusFlags = ImGuiFocusedFlags_RootAndChildWindows | ImGuiFocusedFlags_DockHierarchy;
	if (ImGui::IsWindowFocused(FocusFlags) && !ImGui::GetIO().WantTextInput && SaveCommand.Shortcut.IsPressedThisFrame())
	{
		SaveCommand.TryExecute();
	}
}

void FPhysicsAssetEditorTab::BeforeRender()
{
	// 패널이 그려지지 않은 프레임에는 프리뷰 입력을 비운다
	PhysicsEditor.SetViewportState(0, 0, {});
}

void FPhysicsAssetEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);
	ImGuiID ViewportId = InDockSpaceId;
	ImGuiID TreeId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Left, 0.20f, nullptr, &ViewportId);
	const ImGuiID GraphId = ImGui::DockBuilderSplitNode(TreeId, ImGuiDir_Down, 0.35f, nullptr, &TreeId);
	ImGuiID DetailsId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.30f, nullptr, &ViewportId);
	const ImGuiID SettingsId = ImGui::DockBuilderSplitNode(DetailsId, ImGuiDir_Up, 0.20f, nullptr, &DetailsId);
	ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), ViewportId);
	ImGui::DockBuilderDockWindow(Panels[1]->GetWindowName(), TreeId);
	ImGui::DockBuilderDockWindow(Panels[2]->GetWindowName(), DetailsId);
	ImGui::DockBuilderDockWindow(Panels[3]->GetWindowName(), SettingsId);
	ImGui::DockBuilderDockWindow(Panels[4]->GetWindowName(), GraphId);
	ImGui::DockBuilderFinish(InDockSpaceId);
}
