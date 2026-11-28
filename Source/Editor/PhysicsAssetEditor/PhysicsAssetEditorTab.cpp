#include "EnginePCH.h"
#include "PhysicsAssetEditorTab.h"
#include "PhysicsAssetEditor.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/SkeletalPreviewEditor/SkeletalPreviewViewportPanel.h"
#include "Panels/PhysicsAssetTreePanel.h"
#include "Panels/PhysicsAssetDetailsPanel.h"
#include "Panels/PhysicsAssetSettingsPanel.h"
#include "Panels/PhysicsAssetGraphPanel.h"
#include "Panels/PhysicsAssetToolsPanel.h"

namespace
{
	// 툴바 버튼 묶음 사이의 세로 구분선
	void ToolbarSeparator()
	{
		ImGui::SameLine();
		const float Height = ImGui::GetFrameHeight() + EditorStyle::Scale(4.0f);
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const float X = Pos.x + EditorStyle::Scale(4.0f);
		ImGui::GetWindowDrawList()->AddLine(ImVec2(X, Pos.y + EditorStyle::Scale(4.0f)),
			ImVec2(X, Pos.y + Height), EditorColor::Border, 1.0f);
		ImGui::Dummy(ImVec2(EditorStyle::Scale(9.0f), Height));
		ImGui::SameLine();
	}

	void ItemTooltip(const char* Text)
	{
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%s", Text);
		}
	}
}

FPhysicsAssetEditorTab::FPhysicsAssetEditorTab(FPhysicsAssetEditor& InEditor)
	: FDockedAssetEditorTab(InEditor, "PhysicsAssetDockSpace", "PhysicsAssetPanelClass")
	, PhysicsEditor(InEditor)
{
	// EPanel 순서와 같아야 한다
	RegisterPanel(MakeUnique<FSkeletalPreviewViewportPanel>(InEditor));
	RegisterPanel(MakeUnique<FPhysicsAssetTreePanel>(InEditor));
	RegisterPanel(MakeUnique<FPhysicsAssetDetailsPanel>(InEditor));
	RegisterPanel(MakeUnique<FPhysicsAssetSettingsPanel>(InEditor));
	RegisterPanel(MakeUnique<FPhysicsAssetGraphPanel>(InEditor));
	RegisterPanel(MakeUnique<FPhysicsAssetToolsPanel>(InEditor));

	SaveCommand.Label = "Save";
	SaveCommand.Tooltip = "Save";
	SaveCommand.IconId = EEditorIcon::Save;
	SaveCommand.Shortcut = { EKeyCode::S, true, false, false };
	SaveCommand.Execute = [this] { PhysicsEditor.Save(); };
	SaveCommand.CanExecute = [this] { return PhysicsEditor.IsDirty() && !PhysicsEditor.IsSimulating(); };
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
	const ImU32 IconColor = EditorColor::TextPrimary;
	const bool bSimulating = PhysicsEditor.IsSimulating();
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + EditorStyle::Scale(4.0f));
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + EditorStyle::Scale(2.0f));

	EditorCommandUI::ToolbarButton("PhysicsAssetSave", SaveCommand);

	ToolbarSeparator();

	// 프리뷰 포즈와 시뮬레이션
	ImGui::BeginDisabled(bSimulating);
	if (EditorIcons::Button("PAReferencePose", EEditorIcon::Possess, IconColor, "Reference Pose"))
	{
		PhysicsEditor.ResetPreviewPose();
	}
	ImGui::EndDisabled();
	ItemTooltip("Reset the preview mesh to the reference pose");
	ImGui::SameLine();

	if (EditorIcons::Button("PASimulate", bSimulating ? EEditorIcon::Stop : EEditorIcon::Play,
		bSimulating ? EditorColor::Error : EditorIcons::PlayGreen, bSimulating ? "Stop" : "Simulate", false, bSimulating))
	{
		PhysicsEditor.ToggleSimulation();
	}
	ItemTooltip(bSimulating ? "Stop simulation" : "Simulate (ragdoll test)");

	ToolbarSeparator();

	// 선택한 바디와 나머지 모든 바디 사이의 충돌
	const bool bCanCollision = PhysicsEditor.CanEditSelectedBodyCollision();
	ImGui::BeginDisabled(!bCanCollision);
	if (EditorIcons::Button("PAEnableCollision", EEditorIcon::Check, IconColor, "Enable Collision"))
	{
		PhysicsEditor.SetSelectedBodyCollisionWithAll(true);
	}
	ImGui::EndDisabled();
	ItemTooltip("Enable collision between the selected body and all other bodies");
	ImGui::SameLine();
	ImGui::BeginDisabled(!bCanCollision);
	if (EditorIcons::Button("PADisableCollision", EEditorIcon::EyeClosed, IconColor, "Disable Collision"))
	{
		PhysicsEditor.SetSelectedBodyCollisionWithAll(false);
	}
	ImGui::EndDisabled();
	ItemTooltip("Disable collision between the selected body and all other bodies");

	ToolbarSeparator();

	ImGui::BeginDisabled(bSimulating);
	if (EditorIcons::Button("PARegenerateAll", EEditorIcon::Reset, IconColor, "Regenerate All Bodies"))
	{
		PhysicsEditor.RegenerateAllBodies();
	}
	ImGui::EndDisabled();
	ItemTooltip("Delete all bodies and constraints and generate them again with the Tools settings");

	// 오른쪽 끝: 에디터 모드 전환 (UE5의 Skeleton / Mesh / Animation / Physics)
	const float ModeWidth = EditorIcons::CalcButtonWidth(true, nullptr, false) * 2.0f
		+ ImGui::GetStyle().ItemSpacing.x + EditorStyle::Scale(8.0f);
	const float RightX = ImGui::GetWindowContentRegionMax().x - ModeWidth;
	ImGui::SameLine();
	if (ImGui::GetCursorPosX() < RightX)
	{
		ImGui::SetCursorPosX(RightX);
	}
	if (EditorIcons::Button("PAModeMesh", EEditorIcon::SkeletalMesh, IconColor))
	{
		PhysicsEditor.OpenMeshEditor();
	}
	ItemTooltip("Skeletal Mesh Editor");
	ImGui::SameLine();
	EditorIcons::Button("PAModePhysics", EEditorIcon::ClassPawn, IconColor, nullptr, false, true);
	ItemTooltip("Physics Asset Editor");

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + EditorStyle::Scale(2.0f));
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

	// UE5 Physics Asset 에디터 배치:
	// 왼쪽 위 Skeleton Tree, 왼쪽 아래 Graph, 가운데 뷰포트,
	// 오른쪽 위 Details / Preview Scene Settings 탭 묶음, 오른쪽 아래 Tools
	ImGuiID ViewportId = InDockSpaceId;
	ImGuiID TreeId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Left, 0.20f, nullptr, &ViewportId);
	const ImGuiID GraphId = ImGui::DockBuilderSplitNode(TreeId, ImGuiDir_Down, 0.38f, nullptr, &TreeId);
	ImGuiID DetailsId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.25f, nullptr, &ViewportId);
	const ImGuiID ToolsId = ImGui::DockBuilderSplitNode(DetailsId, ImGuiDir_Down, 0.40f, nullptr, &DetailsId);

	ImGui::DockBuilderDockWindow(Panels[Panel_Viewport]->GetWindowName(), ViewportId);
	ImGui::DockBuilderDockWindow(Panels[Panel_SkeletonTree]->GetWindowName(), TreeId);
	ImGui::DockBuilderDockWindow(Panels[Panel_Graph]->GetWindowName(), GraphId);
	ImGui::DockBuilderDockWindow(Panels[Panel_Details]->GetWindowName(), DetailsId);
	ImGui::DockBuilderDockWindow(Panels[Panel_PreviewSettings]->GetWindowName(), DetailsId);
	ImGui::DockBuilderDockWindow(Panels[Panel_Tools]->GetWindowName(), ToolsId);

	// 오른쪽 위 탭 묶음에서는 Details를 먼저 보여 준다
	if (ImGuiDockNode* DetailsNode = ImGui::DockBuilderGetNode(DetailsId))
	{
		DetailsNode->SelectedTabId = ImHashStr(Panels[Panel_Details]->GetWindowName());
	}
	ImGui::DockBuilderFinish(InDockSpaceId);
}
