#include "EnginePCH.h"
#include "SkeletalMeshInfoPanel.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Render/SkeletalMesh.h"

#include <filesystem>

namespace
{
	bool BeginPropertyTable(const char* Id)
	{
		if (!ImGui::BeginTable(Id, 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
		{
			return false;
		}
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.38f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.62f);
		return true;
	}

	// 이름 칸에 라벨을 쓰고 값 칸으로 넘어간다.
	void PropertyLabel(const char* Label)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Label);
		ImGui::TableSetColumnIndex(1);
	}

	void PropertyText(const char* Label, const char* Value)
	{
		PropertyLabel(Label);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Value);
	}

	void PropertyInt(const char* Label, int32 Value)
	{
		PropertyLabel(Label);
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%d", Value);
	}

	bool Category(const char* Title)
	{
		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		return ImGui::CollapsingHeader(Title);
	}
}

FSkeletalMeshInfoPanel::FSkeletalMeshInfoPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Asset Details")
{
}

void FSkeletalMeshInfoPanel::DrawContent()
{
	USkeletalMesh* Mesh = Editor.GetMesh();
	if (!Mesh || Mesh->GetRenderData().LODRenderData.IsEmpty())
	{
		return;
	}

	EditorIcons::SearchBox("##AssetDetailsSearch", SearchBuffer, sizeof(SearchBuffer), "Search");
	const auto Visible = [this](const char* Title) { return EditorStyle::ContainsIgnoreCase(Title, SearchBuffer); };

	const FSkeletalMeshLODRenderData& LOD = Mesh->GetRenderData().LODRenderData[0];
	const auto& Slots = Mesh->GetMaterialSlots();

	// Material Slots: 슬롯마다 색 견본, 이름, 텍스처 경로
	if (Visible("Material Slots") && Category("Material Slots"))
	{
		if (BeginPropertyTable("MaterialSlots"))
		{
			PropertyText("Material Slots", std::format("{} Material Slots", Slots.Num()).c_str());
			for (int32 Index = 0; Index < Slots.Num(); ++Index)
			{
				const FSkeletalMaterialSlot& Slot = Slots[Index];
				ImGui::PushID(Index);
				PropertyLabel(std::format("Element {}", Index).c_str());
				const float Swatch = ImGui::GetFrameHeight() * 1.8f;
				const ImVec4 Color(Slot.BaseColor.X, Slot.BaseColor.Y, Slot.BaseColor.Z, 1.0f);
				ImGui::ColorButton("##Swatch", Color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha,
					ImVec2(Swatch, Swatch));
				ImGui::SameLine();
				ImGui::BeginGroup();
				ImGui::TextUnformatted(Slot.Name.empty() ? "(unnamed)" : Slot.Name.c_str());
				const FString Texture = Slot.DiffuseTexturePath.empty()
					? FString("No texture")
					: std::filesystem::path(Slot.DiffuseTexturePath).filename().string();
				ImGui::TextDisabled("%s", Texture.c_str());
				ImGui::EndGroup();
				if (!Slot.DiffuseTexturePath.empty() && ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", Slot.DiffuseTexturePath.c_str());
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}

	// LOD Picker: 지금은 LOD0 하나만 있다
	if (Visible("LOD Picker") && Category("LOD Picker"))
	{
		if (BeginPropertyTable("LODPicker"))
		{
			PropertyLabel("LOD");
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::BeginCombo("##LOD", "Auto (LOD 0)"))
			{
				ImGui::Selectable("Auto (LOD 0)", true);
				ImGui::Selectable("LOD 0", false);
				ImGui::EndCombo();
			}
			ImGui::EndTable();
		}
	}

	// LOD 0: 섹션별 머티리얼과 삼각형 수
	if (Visible("LOD 0") && Category("LOD 0"))
	{
		if (BeginPropertyTable("LOD0"))
		{
			for (int32 Index = 0; Index < LOD.Sections.Num(); ++Index)
			{
				const FSkeletalMeshSection& Section = LOD.Sections[Index];
				bool bCloth = false;
				for (const FClothSectionBinding& Binding : Mesh->GetClothSectionBindings())
				{
					bCloth |= Binding.SectionIndex == Index;
				}
				const char* SlotName = Slots.IsValidIndex(static_cast<int32>(Section.MaterialSlotIndex))
					? Slots[Section.MaterialSlotIndex].Name.c_str() : "?";
				PropertyLabel(std::format("Section {}", Index).c_str());
				ImGui::AlignTextToFramePadding();
				ImGui::Text("%s  (%u tris)%s", SlotName, Section.IndexCount / 3, bCloth ? "  [Cloth]" : "");
			}
			ImGui::EndTable();
		}
	}

	// 원본 애셋 정보와 LOD0 통계
	if (Visible("Mesh Info") && Category("Mesh Info"))
	{
		if (BeginPropertyTable("MeshInfo"))
		{
			const FString Name = std::filesystem::path(Editor.GetAssetPath()).stem().string();
			PropertyText("Asset Name", Name.c_str());
			PropertyLabel("Asset Path");
			ImGui::TextWrapped("%s", Editor.GetAssetPath().c_str());
			PropertyInt("Vertices", static_cast<int32>(LOD.BindVertices.Num()));
			PropertyInt("Triangles", static_cast<int32>(LOD.Indices.Num() / 3));
			PropertyInt("Sections", static_cast<int32>(LOD.Sections.Num()));
			PropertyInt("Bones", Mesh->GetNumBones());
			const FVector Size = LOD.BindAABB.Max - LOD.BindAABB.Min;
			PropertyText("Approx Size", std::format("{:.0f} x {:.0f} x {:.0f}", Size.X, Size.Y, Size.Z).c_str());
			ImGui::EndTable();
		}
	}

	// Physics: 연결된 Physics Asset
	if (Visible("Physics") && Category("Physics"))
	{
		if (BeginPropertyTable("Physics"))
		{
			PropertyLabel("Physics Asset");
			const UPhysicsAsset* PhysicsAsset = Mesh->GetPhysicsAsset();
			const FString Path = PhysicsAsset
				? (Mesh->PhysicsAssetPath.empty() ? FString("(default path)") : Mesh->PhysicsAssetPath)
				: FString("None");
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(Path.c_str());
			PropertyLabel("");
			if (ImGui::Button("Open Physics Asset", ImVec2(-FLT_MIN, 0.0f)))
			{
				Editor.OpenPhysicsAssetEditor();
			}
			ImGui::EndTable();
		}
	}
}
