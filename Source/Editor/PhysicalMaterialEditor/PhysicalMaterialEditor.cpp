#include "EnginePCH.h"
#include "Editor/PhysicalMaterialEditor/PhysicalMaterialEditor.h"
#include "Editor/Details/PropertyEditor.h"
#include "Asset/AssetUtils.h"
#include "Core/Misc/Paths.h"
#include "Physics/PhysicalMaterial.h"
#include "Physics/PhysicalMaterialFile.h"
#include "imgui.h"
#include "imgui_internal.h"

FPhysicalMaterialEditor::FPhysicalMaterialEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager,
	const FString& InAssetPath, UPhysicalMaterial& InMaterial)
	: FAssetEditor(InEditorEngine, InEditorManager, InAssetPath)
	, Material(&InMaterial)
{
}

TUniquePtr<IEditorTab> FPhysicalMaterialEditor::CreateTab()
{
	return MakeUnique<FPhysicalMaterialEditorTab>(*this);
}

bool FPhysicalMaterialEditor::Save()
{
	if (!Material)
		return false;

	const FString FilePath = FAssetUtils::ResolveAssetFilePath(GetAssetPath());
	FString Error;
	if (!PhysicalMaterialFile::Write(FPaths::FromUtf8(FilePath.empty() ? GetAssetPath() : FilePath), *Material, Error))
	{
		HTR_LOG(Error, "Failed to save physical material '{}': {}", GetAssetPath(), Error);
		return false;
	}

	bDirty = false;
	HTR_LOG(Info, "Saved physical material: {}", GetAssetPath());
	return true;
}

FPhysicalMaterialEditorTab::FPhysicalMaterialEditorTab(FPhysicalMaterialEditor& InEditor)
	: FAssetEditorTab(InEditor)
	, PhysicalMaterialEditor(InEditor)
{
}

void FPhysicalMaterialEditorTab::DrawMenuBar()
{
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save", "Ctrl+S"))
			PhysicalMaterialEditor.Save();
		ImGui::Separator();
		if (ImGui::MenuItem("Close Editor"))
			PhysicalMaterialEditor.Close();
		ImGui::EndMenu();
	}
}

void FPhysicalMaterialEditorTab::OnRender(bool bActive)
{
	if (PhysicalMaterialEditor.IsCloseRequested() || !bActive)
		return;

	if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
		PhysicalMaterialEditor.Save();

	UPhysicalMaterial* Material = PhysicalMaterialEditor.GetMaterial();
	if (!Material)
		return;

	ImGui::BeginChild("##PhysicalMaterialProperties", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
	if (ImGui::Button(PhysicalMaterialEditor.IsDirty() ? "Save*" : "Save"))
		PhysicalMaterialEditor.Save();
	ImGui::SameLine();
	ImGui::TextDisabled("%s", PhysicalMaterialEditor.GetAssetPath().c_str());
	ImGui::Separator();

	FPropertyEditor::DrawObjectProperties(Material);
	if (ImGui::GetCurrentContext() && ImGui::GetCurrentContext()->ActiveIdHasBeenEditedThisFrame)
		PhysicalMaterialEditor.MarkDirty();

	ImGui::EndChild();
}
