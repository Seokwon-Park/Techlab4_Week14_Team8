#include "EnginePCH.h"
#include "Editor/MaterialEditor/Panels/MaterialDetailsPanel.h"
#include "Editor/MaterialEditor/MaterialEditor.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/HitoriEd/EditorDragDrop.h"
#include "Asset/AssetManager.h"
#include "Render/Material.h"
#include "Render/Texture2D.h"
#include "UObject/UObjectIterator.h"

#include <filesystem>

namespace
{
	FString GetTextureDisplayName(const FString& Path)
	{
		if (Path.empty()) return "None";
		std::filesystem::path P(Path);
		return P.stem().string();
	}

	bool BeginMaterialPropTable(const char* TableId)
	{
		if (!ImGui::BeginTable(TableId, 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
			return false;
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.42f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.58f);
		return true;
	}

	void DrawPropRowLabel(const char* Label)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Label);
		ImGui::TableSetColumnIndex(1);
	}

	bool DrawMaterialTextureRow(const char* Label, const char* Id, UMaterial* Material, int32 SlotIndex)
	{
		bool bChanged = false;
		UTexture2D* Current = Material->GetTexture(SlotIndex);
		const float ThumbSize = EditorStyle::Scale(44.0f);
		const float ItemThumbSize = EditorStyle::Scale(22.0f);

		DrawPropRowLabel(Label);
		ImGui::PushID(Id);

		if (Current && Current->GetResource() && Current->GetResource()->GetSRV())
			ImGui::Image((ImTextureID)Current->GetResource()->GetSRV(), ImVec2(ThumbSize, ThumbSize));
		else
			ImGui::Button("None", ImVec2(ThumbSize, ThumbSize));

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Texture))
			{
				UTexture2D* Dropped = *static_cast<UTexture2D**>(Payload->Data);
				if (Dropped != Current)
				{
					Material->SetTexture(SlotIndex, Dropped);
					Current = Dropped;
					bChanged = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine();
		ImGui::BeginGroup();

		const FString CurrentPath = Current ? Current->GetPath() : "";
		const FString DisplayName = Current ? GetTextureDisplayName(CurrentPath) : "None";
		ImGui::SetNextItemWidth(-EditorStyle::Scale(28.0f));
		if (ImGui::BeginCombo("##TexCombo", DisplayName.c_str()))
		{
			if (ImGui::Selectable("None", Current == nullptr) && Current != nullptr)
			{
				Material->SetTexture(SlotIndex, nullptr);
				Current = nullptr;
				bChanged = true;
			}

			for (TObjectIterator<UTexture2D> It; It; ++It)
			{
				UTexture2D* Tex = *It;
				if (!Tex->GetResource() || !Tex->GetResource()->GetSRV() || Tex->GetPath().empty())
					continue;
				const FString TexPath = Tex->GetPath();
				if (TexPath == "WhiteTexture" || TexPath == "FlatNormalTexture")
					continue;
				const bool bSelected = (Tex == Current);
				ImGui::PushID(Tex);
				ImGui::Image((ImTextureID)Tex->GetResource()->GetSRV(), ImVec2(ItemThumbSize, ItemThumbSize));
				ImGui::SameLine();
				if (ImGui::Selectable(GetTextureDisplayName(TexPath).c_str(), bSelected, ImGuiSelectableFlags_None, ImVec2(0.0f, ItemThumbSize)) && !bSelected)
				{
					Material->SetTexture(SlotIndex, Tex);
					Current = Tex;
					bChanged = true;
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", TexPath.c_str());
				ImGui::PopID();
			}

			for (const FString& UnloadedPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::Texture))
			{
				ImGui::PushID(UnloadedPath.c_str());
				ImGui::Dummy(ImVec2(ItemThumbSize, ItemThumbSize));
				ImGui::SameLine();
				if (ImGui::Selectable(GetTextureDisplayName(UnloadedPath).c_str(), false, ImGuiSelectableFlags_None, ImVec2(0.0f, ItemThumbSize)))
				{
					if (UTexture2D* Loaded = UAssetManager::GetAssetByPath<UTexture2D>(UnloadedPath))
					{
						Material->SetTexture(SlotIndex, Loaded);
						Current = Loaded;
						bChanged = true;
					}
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", UnloadedPath.c_str());
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		if (ImGui::IsItemHovered() && Current)
			ImGui::SetTooltip("%s", CurrentPath.c_str());

		ImGui::SameLine();
		ImGui::BeginDisabled(Current == nullptr);
		if (ImGui::Button("X", ImVec2(EditorStyle::Scale(22.0f), 0.0f)))
		{
			Material->SetTexture(SlotIndex, nullptr);
			bChanged = true;
		}
		ImGui::EndDisabled();

		if (Current)
			ImGui::TextDisabled("%s", CurrentPath.c_str());
		else
			ImGui::TextDisabled("Drop texture here or pick above");

		ImGui::EndGroup();
		ImGui::PopID();
		return bChanged;
	}
}

FMaterialDetailsPanel::FMaterialDetailsPanel(FMaterialEditor& InEditor)
	: FMaterialEditorPanel(InEditor, "Details & Parameters", "MaterialEditorPanel")
{
}

void FMaterialDetailsPanel::DrawContent()
{
	UMaterial* Material = Editor.GetMaterial();
	if (!Material)
	{
		ImGui::TextDisabled("No material loaded.");
		return;
	}

	ImGui::TextDisabled("Asset: %s", Editor.GetAssetPath().c_str());
	ImGui::Separator();

	bool bModified = false;

	if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (BeginMaterialPropTable("##MatDetailsTable"))
		{
			DrawPropRowLabel("Material Domain");
			ImGui::SetNextItemWidth(-1.0f);
			const char* DomainNames[] = { "Surface", "Particle", "Decal", "UI" };
			int32 DomainIdx = static_cast<int32>(Material->MaterialDomain);
			if (ImGui::Combo("##MaterialDomain", &DomainIdx, DomainNames, IM_ARRAYSIZE(DomainNames)))
			{
				Material->MaterialDomain = static_cast<EMaterialDomain>(DomainIdx);
				bModified = true;
			}

			DrawPropRowLabel("Blend Mode");
			ImGui::SetNextItemWidth(-1.0f);
			const char* BlendNames[] = { "Opaque", "Masked", "Translucent", "Additive" };
			int32 BlendIdx = static_cast<int32>(Material->BlendMode);
			if (ImGui::Combo("##BlendMode", &BlendIdx, BlendNames, IM_ARRAYSIZE(BlendNames)))
			{
				Material->BlendMode = static_cast<EMaterialBlendMode>(BlendIdx);
				bModified = true;
			}

			DrawPropRowLabel("Shading Model");
			ImGui::SetNextItemWidth(-1.0f);
			const char* ShadingNames[] = { "Default Lit", "Unlit" };
			int32 ShadingIdx = static_cast<int32>(Material->ShadingModel);
			if (ImGui::Combo("##ShadingModel", &ShadingIdx, ShadingNames, IM_ARRAYSIZE(ShadingNames)))
			{
				Material->ShadingModel = static_cast<EMaterialShadingModel>(ShadingIdx);
				bModified = true;
			}

			DrawPropRowLabel("Two Sided");
			if (ImGui::Checkbox("##TwoSided", &Material->bTwoSided))
				bModified = true;

			if (Material->IsTranslucent() || Material->BlendMode == EMaterialBlendMode::Additive)
			{
				DrawPropRowLabel("Before DOF");
				if (ImGui::Checkbox("##BeforeDOF", &Material->bRenderBeforeDOF))
					bModified = true;
			}

			DrawPropRowLabel("Opacity Mask Clip Value");
			ImGui::BeginDisabled(!Material->IsMasked());
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::DragFloat("##MaskClipValue", &Material->OpacityMaskClipValue, 0.01f, 0.0f, 1.0f, "%.3f"))
				bModified = true;
			ImGui::EndDisabled();

			DrawPropRowLabel("Sampler State");
			ImGui::SetNextItemWidth(-1.0f);
			const char* SamplerNames[] = { "Linear Clamp", "Linear Wrap", "Point Clamp", "Linear Wrap U / Clamp V" };
			const ESamplerState SamplerValues[] = {
				ESamplerState::LinearClamp,
				ESamplerState::LinearWrap,
				ESamplerState::PointClamp,
				ESamplerState::LinearWrapUClampV
			};
			int32 SamplerIdx = 0;
			for (int32 i = 0; i < IM_ARRAYSIZE(SamplerValues); ++i)
			{
				if (Material->SamplerState == SamplerValues[i]) { SamplerIdx = i; break; }
			}
			if (ImGui::Combo("##SamplerState", &SamplerIdx, SamplerNames, IM_ARRAYSIZE(SamplerNames)))
			{
				Material->SamplerState = SamplerValues[SamplerIdx];
				bModified = true;
			}

			ImGui::EndTable();
		}
	}

	if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (BeginMaterialPropTable("##MatParamsTable"))
		{
			DrawPropRowLabel("Base Color");
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::ColorEdit4("##BaseColor", &Material->BaseColor.X))
				bModified = true;

			bModified |= DrawMaterialTextureRow("Base Color Texture", "BaseColorTex", Material, 0);
			bModified |= DrawMaterialTextureRow("Normal Map", "NormalTex", Material, 1);

			DrawPropRowLabel("Flip Normal Y");
			if (ImGui::Checkbox("##FlipNormalY", &Material->bFlipNormalY))
				bModified = true;

			ImGui::BeginDisabled(Material->IsUnlit());
			DrawPropRowLabel("Specular Color");
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::ColorEdit3("##SpecularColor", &Material->SpecularColor.X))
				bModified = true;

			DrawPropRowLabel("Shininess");
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::DragFloat("##Shininess", &Material->Shininess, 0.5f, 1.0f, 256.0f))
				bModified = true;
			ImGui::EndDisabled();

			DrawPropRowLabel("UV Scroll Speed");
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::DragFloat2("##UVScrollSpeed", &Material->UVScrollSpeed.X, 0.01f))
				bModified = true;

			ImGui::EndTable();
		}
	}

	if (bModified)
		Editor.MarkDirty();
}
