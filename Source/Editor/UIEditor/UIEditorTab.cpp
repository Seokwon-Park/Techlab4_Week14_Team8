#include "EnginePCH.h"
#include "Editor/UIEditor/UIEditorTab.h"
#include "Editor/UIEditor/UIEditor.h"
#include "Editor/UIEditor/Panels/UIEditorPanels.h"
#include "Editor/EditorUI/EditorIcons.h"

#include "UI/UserWidget.h"
#include "UObject/UObjectHash.h"

#include <format>

namespace
{
	// BuildDefaultLayout이 쓰는 패널 순서
	enum EUIEditorPanelIndex { Panel_Canvas, Panel_Palette, Panel_Hierarchy, Panel_Details };
}

FUIEditorTab::FUIEditorTab(FUIEditor& InEditor)
	: FAssetEditorTab(InEditor)
	, UIEditor(InEditor)
{
	// 같은 위젯을 다시 열어도 배치가 유지되고, 다른 에셋 창과 섞이지 않도록 에셋 경로로 ID를 만든다
	const ImGuiID AssetId = ImHashStr(InEditor.GetAssetPath().c_str());
	DockSpaceId = ImHashStr("UIEditorDockSpace", 0, AssetId);
	PanelClassId = ImHashStr("UIEditorPanelClass", 0, AssetId);

	Panels.Add(MakeUnique<FUICanvasPanel>(InEditor));      // Panel_Canvas
	Panels.Add(MakeUnique<FUIPalettePanel>(InEditor));     // Panel_Palette
	Panels.Add(MakeUnique<FUIHierarchyPanel>(InEditor));   // Panel_Hierarchy
	Panels.Add(MakeUnique<FUIDetailsPanel>(InEditor));     // Panel_Details
	for (auto& Panel : Panels)
	{
		Panel->SetDockClass(PanelClassId);
		Panel->Init();
	}
}

void FUIEditorTab::DrawMenuBar()
{
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save", "Ctrl+S")) UIEditor.Save();
		ImGui::Separator();
		if (ImGui::MenuItem("Close Editor")) UIEditor.Close();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Edit"))
	{
		if (ImGui::MenuItem("Undo", "Ctrl+Z", false, UIEditor.CanUndo())) UIEditor.Undo();
		if (ImGui::MenuItem("Redo", "Ctrl+Y", false, UIEditor.CanRedo())) UIEditor.Redo();
		ImGui::Separator();
		if (ImGui::MenuItem("Delete", "Del", false, UIEditor.GetSelected() && UIEditor.GetSelected() != UIEditor.GetRoot()))
			UIEditor.DeleteSelected();
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
		ImGui::EndMenu();
	}

	ImGui::TextDisabled("%s%s", UIEditor.GetFilePath().c_str(), UIEditor.IsDirty() ? " *" : "");
}

void FUIEditorTab::DrawToolbar()
{
	// UE5 Widget Blueprint 에디터 툴바: 왼쪽 저장·편집 명령, 오른쪽 Designer 모드
	const float S = EditorIcons::GetScale();
	const float ButtonHeight = 28.0f * S;
	const float BarHeight = ButtonHeight + 10.0f * S;
	const ImU32 IconGray = ImGui::GetColorU32(ImGuiCol_Text, 0.78f);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * S, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f * S, 4.0f * S));
	if (ImGui::BeginChild("##UIEditorToolbar", ImVec2(0.0f, BarHeight), ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
	{
		ImGui::SetCursorPosY((BarHeight - ButtonHeight) * 0.5f);
		auto Gap = [S]()
		{
			ImGui::SameLine(0.0f, 8.0f * S);
			const ImVec2 P = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddLine(ImVec2(P.x, P.y + 4.0f * S), ImVec2(P.x, P.y + 24.0f * S),
				ImGui::GetColorU32(ImGuiCol_Separator));
			ImGui::Dummy(ImVec2(1.0f, 0.0f));
			ImGui::SameLine(0.0f, 8.0f * S);
		};

		ImGui::BeginDisabled(!UIEditor.IsDirty());
		if (EditorIcons::Button("##UISave", EEditorIcon::Save, IconGray, nullptr, false, false, ButtonHeight))
			UIEditor.Save();
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Save (Ctrl+S)");

		Gap();
		ImGui::BeginDisabled(!UIEditor.CanUndo());
		if (EditorIcons::Button("##UIUndo", EEditorIcon::Undo, IconGray, nullptr, false, false, ButtonHeight))
			UIEditor.Undo();
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Undo (Ctrl+Z)");
		ImGui::SameLine();
		ImGui::BeginDisabled(!UIEditor.CanRedo());
		if (EditorIcons::Button("##UIRedo", EEditorIcon::Redo, IconGray, nullptr, false, false, ButtonHeight))
			UIEditor.Redo();
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Redo (Ctrl+Y)");

		// 이 에셋으로 만들 C++ 클래스 (UUserWidget 자손)
		Gap();
		const FString ParentLabel = "Parent: " + UIEditor.GetParentClassName();
		if (EditorIcons::Button("##UIParentClass", EEditorIcon::Blueprint, IconGray, ParentLabel.c_str(), true, false, ButtonHeight))
		{
			ImGui::OpenPopup("##UIParentClassMenu");
			ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 2.0f));
		}
		ImGui::SetItemTooltip("C++ class this widget is created as");
		if (ImGui::BeginPopup("##UIParentClassMenu"))
		{
			EditorIcons::MenuSection("Parent Class");
			TArray<UClass*> Classes;
			GetDerivedClasses(UUserWidget::StaticClass(), Classes);
			Classes.Add(UUserWidget::StaticClass());
			for (UClass* Class : Classes)
				if (EditorIcons::MenuRow(Class->Name.c_str(), EEditorIcon::ClassActor, Class->Name == UIEditor.GetParentClassName(), true, false, nullptr, true))
					UIEditor.SetParentClassName(Class->Name);
			ImGui::EndPopup();
		}

		// 미리보기 해상도
		ImGui::SameLine();
		const FVector2& PreviewSize = UIEditor.GetPreviewSize();
		const FString SizeLabel = std::format("{} x {}", (int)PreviewSize.X, (int)PreviewSize.Y);
		if (EditorIcons::Button("##UIPreviewSize", EEditorIcon::Viewport, IconGray, SizeLabel.c_str(), true, false, ButtonHeight))
		{
			ImGui::OpenPopup("##UIPreviewSizeMenu");
			ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 2.0f));
		}
		ImGui::SetItemTooltip("Preview screen size (check anchors at other aspect ratios)");
		if (ImGui::BeginPopup("##UIPreviewSizeMenu"))
		{
			static const FVector2 Sizes[] = { {1920, 1080}, {1280, 720}, {2560, 1440}, {2560, 1080}, {1080, 1920}, {750, 1334} };
			EditorIcons::MenuSection("Screen Size");
			for (const FVector2& Size : Sizes)
				if (EditorIcons::MenuRow(std::format("{} x {}", (int)Size.X, (int)Size.Y).c_str(), Size.X >= Size.Y ? EEditorIcon::Viewport : EEditorIcon::LayoutSingle,
					Size.X == PreviewSize.X && Size.Y == PreviewSize.Y, true, false, nullptr, true))
					UIEditor.SetPreviewSize(Size);
			ImGui::EndPopup();
		}

		// 오른쪽: Designer 모드 (그래프 모드는 아직 없어 항상 켜짐)
		const float DesignerW = EditorIcons::CalcButtonWidth(true, "Designer", false, ButtonHeight);
		ImGui::SameLine();
		const float RightX = ImGui::GetWindowContentRegionMax().x - DesignerW - 4.0f * S;
		if (RightX > ImGui::GetCursorPosX() + 8.0f * S)
		{
			ImGui::SetCursorPosX(RightX);
			const ImVec2 Min = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddRectFilled(Min, ImVec2(Min.x + DesignerW, Min.y + ButtonHeight), IM_COL32(15, 110, 230, 255), 3.0f * S);
			EditorIcons::Button("##UIDesigner", EEditorIcon::Designer, IM_COL32_WHITE, "Designer", false, false, ButtonHeight);
			ImGui::SetItemTooltip("Designer");
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
}

void FUIEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);

	// 왼쪽에 Palette(위)와 Hierarchy(아래), 가운데 Canvas, 오른쪽에 Details를 둔다.
	ImGuiID CanvasId = InDockSpaceId;
	ImGuiID LeftId = ImGui::DockBuilderSplitNode(CanvasId, ImGuiDir_Left, 0.15f, nullptr, &CanvasId);
	const ImGuiID DetailsId = ImGui::DockBuilderSplitNode(CanvasId, ImGuiDir_Right, 0.22f, nullptr, &CanvasId);
	const ImGuiID PaletteId = ImGui::DockBuilderSplitNode(LeftId, ImGuiDir_Up, 0.52f, nullptr, &LeftId);

	ImGui::DockBuilderDockWindow(Panels[Panel_Canvas]->GetWindowName(), CanvasId);
	ImGui::DockBuilderDockWindow(Panels[Panel_Palette]->GetWindowName(), PaletteId);
	ImGui::DockBuilderDockWindow(Panels[Panel_Hierarchy]->GetWindowName(), LeftId);
	ImGui::DockBuilderDockWindow(Panels[Panel_Details]->GetWindowName(), DetailsId);
	ImGui::DockBuilderFinish(InDockSpaceId);
}

void FUIEditorTab::OnRender(bool bActive)
{
	if (UIEditor.IsCloseRequested())
	{
		return;
	}

	if (bActive)
		DrawToolbar();

	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		const ImVec2 Size = ImGui::GetContentRegionAvail();
		if (bActive && Size.x > 0.0f && Size.y > 0.0f &&
			(!bLayoutInitialized || !ImGui::DockBuilderGetNode(DockSpaceId)))
		{
			// 탭을 새로 열면 저장된 배치를 제거하고 기본 레이아웃으로 시작한다.
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

	bool bAnyRendered = false;
	for (auto& Panel : Panels)
	{
		// 분리된 패널은 부모 탭이 가려져도 독립적으로 표시한다.
		if (Panel->IsOpen() && (bActive || Panel->IsDetached(DockSpaceId)))
		{
			Panel->OnRender();
			bAnyRendered = true;
		}
	}

	if (bAnyRendered)
	{
		// 트리를 순회하는 도중에는 바꾸지 않고, 패널을 모두 그린 뒤 옮긴다.
		UIEditor.FlushPendingReparent();
		UIEditor.CommitUndoIfChanged();
	}
}
