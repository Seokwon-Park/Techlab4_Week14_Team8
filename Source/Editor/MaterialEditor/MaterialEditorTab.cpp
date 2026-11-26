#include "EnginePCH.h"
#include "Editor/MaterialEditor/MaterialEditorTab.h"
#include "Editor/MaterialEditor/MaterialEditor.h"
#include "Editor/MaterialEditor/Panels/MaterialViewportPanel.h"
#include "Editor/MaterialEditor/Panels/MaterialDetailsPanel.h"
#include "imgui_internal.h"

FMaterialEditorTab::FMaterialEditorTab(FMaterialEditor& InEditor)
	: FDockedAssetEditorTab(InEditor, "MaterialDockSpace", "MaterialPanelClass")
	, MaterialEditor(InEditor)
{
	RegisterPanel(MakeUnique<FMaterialViewportPanel>(InEditor));
	RegisterPanel(MakeUnique<FMaterialDetailsPanel>(InEditor));
}

void FMaterialEditorTab::DrawMenuBar()
{
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save", "Ctrl+S"))
			MaterialEditor.Save();
		ImGui::Separator();
		if (ImGui::MenuItem("Close Editor"))
			MaterialEditor.Close();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Window"))
	{
		for (auto& Panel : Panels)
		{
			bool bOpen = Panel->IsOpen();
			if (ImGui::MenuItem(Panel->GetPanelName(), nullptr, bOpen))
				Panel->SetOpen(!bOpen);
		}
		ImGui::EndMenu();
	}
}

void FMaterialEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);

	ImGuiID ViewportId = InDockSpaceId;
	const ImGuiID DetailsId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.36f, nullptr, &ViewportId);

	ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), ViewportId);
	ImGui::DockBuilderDockWindow(Panels[1]->GetWindowName(), DetailsId);
	ImGui::DockBuilderFinish(InDockSpaceId);
}

void FMaterialEditorTab::DrawUnsavedPromptModal()
{
	const FString PopupId = "Unsaved Material Changes###MatUnsaved_" + MaterialEditor.GetAssetPath();
	if (MaterialEditor.ConsumeUnsavedPromptRequest())
		ImGui::OpenPopup(PopupId.c_str());

	if (ImGui::BeginPopupModal(PopupId.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Material '%s' has unsaved changes.", MaterialEditor.GetAssetPath().c_str());
		ImGui::TextUnformatted("Do you want to save before closing?");
		ImGui::Spacing();

		if (ImGui::Button("Save", ImVec2(90.0f, 0.0f)))
		{
			if (MaterialEditor.Save())
				MaterialEditor.CloseWithoutPrompt();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Don't Save", ImVec2(90.0f, 0.0f)))
		{
			MaterialEditor.CloseWithoutPrompt();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void FMaterialEditorTab::OnRender(bool bActive)
{
	MaterialEditor.SetViewportState(0, 0, {});
	if (MaterialEditor.IsCloseRequested())
		return;

	if (bActive && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
		MaterialEditor.Save();

	FDockedAssetEditorTab::OnRender(bActive);
	if (bActive)
		DrawUnsavedPromptModal();
}
