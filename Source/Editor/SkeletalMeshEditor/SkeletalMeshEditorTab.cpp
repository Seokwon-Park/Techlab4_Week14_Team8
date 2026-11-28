#include "EnginePCH.h"
#include "SkeletalMeshEditorTab.h"
#include "SkeletalMeshEditor.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Panels/SkeletalMeshViewportPanel.h"
#include "Panels/SkeletalMeshEditingToolsPanel.h"
#include "Panels/SkeletalMeshInfoPanel.h"
#include "Panels/SkeletalMeshDetailsPanel.h"
#include "Panels/SkeletalMeshSettingsPanel.h"
#include "Panels/BoneHierarchyPanel.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"

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
}

FSkeletalMeshEditorTab::FSkeletalMeshEditorTab(FSkeletalMeshEditor& InEditor)
	: FAssetEditorTab(InEditor)
	, MeshEditor(InEditor)
{
	// 탭의 부모 창이 달라져도 애셋별 도킹 ID를 유지합니다.
	const ImGuiID AssetId = ImHashStr(InEditor.GetAssetPath().c_str());
	DockSpaceId = ImHashStr("SkeletalMeshDockSpace", 0, AssetId);
	PanelClassId = ImHashStr("SkeletalMeshPanelClass", 0, AssetId);

	// EPanel 순서와 같아야 한다
	Panels.Add(MakeUnique<FSkeletalMeshViewportPanel>(InEditor));
	Panels.Add(MakeUnique<FSkeletalMeshEditingToolsPanel>(InEditor));
	Panels.Add(MakeUnique<FBoneHierarchyPanel>(InEditor));
	Panels.Add(MakeUnique<FSkeletalMeshInfoPanel>(InEditor));
	Panels.Add(MakeUnique<FSkeletalMeshDetailsPanel>(InEditor));
	Panels.Add(MakeUnique<FSkeletalMeshSettingsPanel>(InEditor));
	for (auto& Panel : Panels)
	{
		Panel->SetDockClass(PanelClassId);
		Panel->Init();
	}
}

void FSkeletalMeshEditorTab::DrawMenuBar()
{
	if (ImGui::BeginMenu("Viewer"))
	{
		if (ImGui::MenuItem("Open Physics Asset"))
		{
			MeshEditor.OpenPhysicsAssetEditor();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Close Viewer"))
		{
			MeshEditor.Close();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Window"))
	{
		for (auto& Panel : Panels)
		{
			if (ImGui::MenuItem(Panel->GetPanelName(), nullptr, Panel->IsOpen()))
			{
				Panel->SetOpen(!Panel->IsOpen());
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Reset Layout"))
		{
			for (auto& Panel : Panels)
			{
				Panel->SetOpen(true);
			}
			bLayoutInitialized = false;
		}
		ImGui::EndMenu();
	}
}

void FSkeletalMeshEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);

	// UE5 스켈레탈 메시 에디터 배치:
	// 왼쪽 Editing Tools, 가운데 뷰포트, 오른쪽 위 Skeleton Tree,
	// 오른쪽 아래 Asset Details / Details / Preview Scene Settings 탭 묶음
	ImGuiID ViewportId = InDockSpaceId;
	const ImGuiID ToolsId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Left, 0.20f, nullptr, &ViewportId);
	ImGuiID RightId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.25f, nullptr, &ViewportId);
	const ImGuiID TreeId = ImGui::DockBuilderSplitNode(RightId, ImGuiDir_Up, 0.45f, nullptr, &RightId);

	ImGui::DockBuilderDockWindow(Panels[Panel_Viewport]->GetWindowName(), ViewportId);
	ImGui::DockBuilderDockWindow(Panels[Panel_EditingTools]->GetWindowName(), ToolsId);
	ImGui::DockBuilderDockWindow(Panels[Panel_SkeletonTree]->GetWindowName(), TreeId);
	ImGui::DockBuilderDockWindow(Panels[Panel_AssetDetails]->GetWindowName(), RightId);
	ImGui::DockBuilderDockWindow(Panels[Panel_Details]->GetWindowName(), RightId);
	ImGui::DockBuilderDockWindow(Panels[Panel_PreviewSettings]->GetWindowName(), RightId);

	// 탭 묶음에서는 Asset Details를 먼저 보여 준다
	if (ImGuiDockNode* RightNode = ImGui::DockBuilderGetNode(RightId))
	{
		RightNode->SelectedTabId = ImHashStr(Panels[Panel_AssetDetails]->GetWindowName());
	}
	ImGui::DockBuilderFinish(InDockSpaceId);
}

void FSkeletalMeshEditorTab::DrawToolbar()
{
	const ImU32 IconColor = EditorColor::TextPrimary;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + EditorStyle::Scale(4.0f));
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + EditorStyle::Scale(2.0f));

	// 프리뷰 포즈
	if (EditorIcons::Button("SKMResetPose", EEditorIcon::Reset, IconColor, "Reset Pose"))
	{
		MeshEditor.PushPoseUndo();
		MeshEditor.ResetPose();
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset all bones to the reference pose");
	ImGui::SameLine();
	if (EditorIcons::Button("SKMFocus", EEditorIcon::ZoomFit, IconColor, "Focus"))
	{
		MeshEditor.FocusMesh();
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Frame the mesh in the viewport (F)");

	ToolbarSeparator();

	// 편집 도구 패널 열기·닫기
	IEditorPanel& ToolsPanel = *Panels[Panel_EditingTools];
	if (EditorIcons::Button("SKMEditingTools", EEditorIcon::Wrench, IconColor, "Editing Tools",
		false, ToolsPanel.IsOpen()))
	{
		ToolsPanel.SetOpen(!ToolsPanel.IsOpen());
	}
	ImGui::SameLine();

	// Cloth Paint: 섹션을 고르지 않았으면 첫 섹션으로 시작한다
	const USkeletalMesh* Mesh = MeshEditor.GetMesh();
	const bool bHasSections = Mesh && !Mesh->GetRenderData().LODRenderData.IsEmpty()
		&& !Mesh->GetRenderData().LODRenderData[0].Sections.IsEmpty();
	const bool bPainting = MeshEditor.IsClothPaintMode();
	ImGui::BeginDisabled(!bHasSections);
	if (EditorIcons::Button("SKMClothPaint", EEditorIcon::VisualEffects, IconColor,
		bPainting ? "Deactivate Cloth Paint" : "Activate Cloth Paint", false, bPainting))
	{
		if (bPainting)
		{
			MeshEditor.SetClothPaintMode(false);
		}
		else
		{
			if (MeshEditor.GetClothSectionIndex() == INDEX_NONE)
			{
				MeshEditor.SelectClothSection(0);
			}
			if (MeshEditor.GetClothSectionVertexCount() > 0)
			{
				MeshEditor.SetClothPaintMode(true);
				ToolsPanel.SetOpen(true);
			}
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();

	bool bHeatmap = MeshEditor.IsWeightHeatmapEnabled();
	if (EditorIcons::Button("SKMWeights", EEditorIcon::Lit, IconColor, "Weight Heatmap", false, bHeatmap))
	{
		MeshEditor.SetWeightHeatmap(!bHeatmap);
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shade the mesh by the selected bone's skin weights");

	ToolbarSeparator();

	if (EditorIcons::Button("SKMPhysicsAsset", EEditorIcon::Shapes, IconColor, "Physics Asset"))
	{
		MeshEditor.OpenPhysicsAssetEditor();
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open the Physics Asset editor for this mesh");

	// 오른쪽 끝: 에디터 모드 전환 (UE5의 Skeleton / Mesh / Animation / Physics)
	const float ModeWidth = EditorIcons::CalcButtonWidth(true, nullptr, false) * 2.0f
		+ ImGui::GetStyle().ItemSpacing.x + EditorStyle::Scale(8.0f);
	const float RightX = ImGui::GetWindowContentRegionMax().x - ModeWidth;
	ImGui::SameLine();
	if (ImGui::GetCursorPosX() < RightX)
	{
		ImGui::SetCursorPosX(RightX);
	}
	EditorIcons::Button("SKMModeMesh", EEditorIcon::SkeletalMesh, IconColor, nullptr, false, true);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Skeletal Mesh Editor");
	ImGui::SameLine();
	if (EditorIcons::Button("SKMModePhysics", EEditorIcon::ClassPawn, IconColor))
	{
		MeshEditor.OpenPhysicsAssetEditor();
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Physics Asset Editor");

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + EditorStyle::Scale(2.0f));
	ImGui::Separator();
}

void FSkeletalMeshEditorTab::OnRender(bool bActive)
{
	MeshEditor.SetViewportState(0, 0, {});
	if (MeshEditor.IsCloseRequested())
	{
		return;
	}

	// 독스페이스가 남은 영역을 쓰므로 툴바를 먼저 그린다
	if (bActive)
	{
		DrawToolbar();
	}

	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		const ImVec2 Size = ImGui::GetContentRegionAvail();
		if (bActive && Size.x > 0.0f && Size.y > 0.0f &&
			(!bLayoutInitialized || !ImGui::DockBuilderGetNode(DockSpaceId)))
		{
			// 탭을 새로 열면 저장된 배치를 제거하고 기본 레이아웃으로 시작합니다.
			ImGui::DockBuilderRemoveNode(DockSpaceId);
			BuildDefaultLayout(DockSpaceId, Size);
			bLayoutInitialized = true;
		}

		ImGuiWindowClass WindowClass;
		WindowClass.ClassId = PanelClassId;
		WindowClass.DockingAllowUnclassed = false;
		const ImGuiDockNodeFlags Flags = bActive ? ImGuiDockNodeFlags_None : ImGuiDockNodeFlags_KeepAliveOnly;
		if (bActive || ImGui::DockBuilderGetNode(DockSpaceId))
		{
			EditorPanelUI::DockSpace(DockSpaceId, ImVec2(0.0f, 0.0f), Flags, &WindowClass);
		}
	}

	for (auto& Panel : Panels)
	{
		// 분리된 패널은 부모 탭이 가려져도 독립적으로 표시합니다.
		if (Panel->IsOpen() && (bActive || Panel->IsDetached(DockSpaceId)))
		{
			Panel->OnRender();
		}
	}
}
