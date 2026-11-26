#include "EnginePCH.h"
#include "Editor/MaterialEditor/Panels/MaterialViewportPanel.h"
#include "Editor/MaterialEditor/MaterialEditor.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/Viewports/EditorViewportToolbar.h"
#include "Engine/Viewport.h"

FMaterialViewportPanel::FMaterialViewportPanel(FMaterialEditor& InEditor)
	: FMaterialEditorPanel(InEditor, "Preview", "MaterialEditorPanel")
{
}

void FMaterialViewportPanel::DrawContent()
{
	FEditorViewportClient* Client = Editor.GetViewportClient();
	if (!Client)
		return;

	// 상단 툴바: 저장 / 프리뷰 도형 전환 / 뷰포트 모드 전환
	{
		const bool bDirty = Editor.IsDirty();
		if (bDirty)
			ImGui::PushStyleColor(ImGuiCol_Button, EditorColor::Accent);
		if (ImGui::Button(bDirty ? "Save*" : "Save"))
			Editor.Save();
		if (bDirty)
			ImGui::PopStyleColor();

		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();

		auto DrawShapeButton = [this](const char* Label, EMaterialPreviewShape Shape)
		{
			const bool bSelected = Editor.GetPreviewShape() == Shape;
			if (bSelected)
				ImGui::PushStyleColor(ImGuiCol_Button, EditorColor::Accent);
			if (ImGui::Button(Label))
				Editor.SetPreviewShape(Shape);
			if (bSelected)
				ImGui::PopStyleColor();
		};

		DrawShapeButton("Sphere", EMaterialPreviewShape::Sphere);
		ImGui::SameLine();
		DrawShapeButton("Cube", EMaterialPreviewShape::Cube);
		ImGui::SameLine();
		DrawShapeButton("Plane", EMaterialPreviewShape::Plane);

		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();
		if (ImGui::Button("Focus (F)"))
			Editor.FocusPreviewMesh();
	}

	ImGui::Separator();

	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if (Size.x < 1.0f || Size.y < 1.0f)
		return;

	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	const ImVec2 ViewMax(Origin.x + Size.x, Origin.y + Size.y);
	if (FTexture2D* Target = Client->Viewport ? Client->Viewport->GetRenderTargetTexture() : nullptr; Target && Target->GetSRV())
	{
		ImGui::GetWindowDrawList()->AddImage(Target->GetSRV(), Origin, ViewMax);
	}

	const bool bToolbarBusy = EditorViewportUI::DrawAssetViewportToolbar(Client, Origin, ViewMax,
		UE::Widget::WM_None, {}, Editor.GetMutableCameraSpeed());

	ImGui::SetCursorScreenPos(Origin);
	ImGui::InvisibleButton("##MaterialPreviewViewport", Size,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

	const bool bHovered = ImGui::IsItemHovered() && !bToolbarBusy;
	if (bHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
		ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
	{
		ImGui::SetWindowFocus();
	}

	const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (bFocused && bHovered)
		EditorViewportUI::HandleViewTypeShortcuts(Client);
	if (bFocused && bHovered && ImGui::IsKeyPressed(ImGuiKey_F, false))
		Editor.FocusPreviewMesh();

	const ImGuiIO& IO = ImGui::GetIO();
	const bool bCameraDragging = ImGui::IsMouseDown(ImGuiMouseButton_Right);
	const bool bLeftDrag = bFocused && ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)
		&& !bCameraDragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f);
	FViewportCameraInput Input
	{
		.bCaptured   = bFocused && ImGui::IsItemActive() && bCameraDragging,
		.bLeftDrag   = bLeftDrag,
		.bAllowWheel = bHovered,
		.bForward    = ImGui::IsKeyDown(ImGuiKey_W),
		.bBackward   = ImGui::IsKeyDown(ImGuiKey_S),
		.bRight      = ImGui::IsKeyDown(ImGuiKey_D),
		.bLeft       = ImGui::IsKeyDown(ImGuiKey_A),
		.bUp         = ImGui::IsKeyDown(ImGuiKey_E),
		.bDown       = ImGui::IsKeyDown(ImGuiKey_Q),
		.MouseX      = IO.MouseDelta.x,
		.MouseY      = IO.MouseDelta.y,
		.Wheel       = IO.MouseWheel * 120.0f,
	};

	Editor.SetViewportState(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y), Input);
}
