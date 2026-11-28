#include "EnginePCH.h"
#include "SkeletalMeshEditingToolsPanel.h"
#include "Core/Misc/Paths.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetFile.h"
#include "Physics/PhysicsAssetUtils.h"
#include "Render/SkeletalMesh.h"
#include "UObject/UObjectGlobals.h"
#include <filesystem>

namespace
{
	// Content/Physics/<메시 이름>.physicsasset
	std::filesystem::path DebugPhysicsAssetPath(const FString& MeshAssetPath)
	{
		const FString Stem = std::filesystem::path(MeshAssetPath).stem().string();
		return FPaths::ProjectContentDir() / "Physics" / (Stem + ".physicsasset");
	}

	// 직렬화 왕복 확인용: 바디 2개, 관절 1개, 충돌 끄기 1쌍을 손으로 만든 에셋
	UPhysicsAsset* MakeTestPhysicsAsset()
	{
		UPhysicsAsset* Asset = NewObject<UPhysicsAsset>();
		for (const char* Bone : { "pelvis", "spine_01" })
		{
			USkeletalBodySetup* Setup = NewObject<USkeletalBodySetup>(Asset);
			Setup->BoneName = FName(Bone);
			FKSphylElem Capsule;
			Capsule.Radius = 15.f;
			Capsule.Length = 20.f;
			Setup->AggGeom.SphylElems.Add(Capsule);
			Asset->SkeletalBodySetups.Add(Setup);
		}

		UPhysicsConstraintTemplate* Joint = NewObject<UPhysicsConstraintTemplate>(Asset);
		Joint->DefaultInstance.ConstraintBone1 = FName("spine_01");
		Joint->DefaultInstance.ConstraintBone2 = FName("pelvis");
		Joint->DefaultInstance.Pos2 = FVector(0, 0, 10);
		Asset->ConstraintSetup.Add(Joint);

		Asset->DisableCollision(0, 1);
		Asset->UpdateBodySetupIndexMap();
		return Asset;
	}

	// 저장 → 새 객체로 다시 읽기 → 개수 비교
	FString SaveAndReload(UPhysicsAsset& Asset, const std::filesystem::path& Path)
	{
		FString Error;
		if (!PhysicsAssetFile::Write(Path, Asset, Error)) return "Write failed: " + Error;

		UPhysicsAsset* Loaded = NewObject<UPhysicsAsset>();
		if (!PhysicsAssetFile::Read(Path, *Loaded, Error)) return "Read failed: " + Error;

		const bool bSame = Loaded->SkeletalBodySetups.Num() == Asset.SkeletalBodySetups.Num()
			&& Loaded->ConstraintSetup.Num() == Asset.ConstraintSetup.Num()
			&& Loaded->CollisionDisableTable.Num() == Asset.CollisionDisableTable.Num();
		return std::format("{} {} (bodies {}, constraints {}, disabled pairs {})",
			bSame ? "OK" : "MISMATCH", Path.generic_string(),
			Loaded->SkeletalBodySetups.Num(), Loaded->ConstraintSetup.Num(),
			Loaded->CollisionDisableTable.Num());
	}

	// 왼쪽 세로 줄의 분류 버튼: 아이콘 위, 이름 아래. 선택이면 강조색으로 칠한다.
	bool CategoryItem(const char* Label, EEditorIcon Icon, bool bSelected, float Width)
	{
		// 아이콘 + 글자 한 줄 + 위아래 여백. 글꼴 크기가 바뀌어도 겹치지 않게 글자 높이로 잡는다
		const float IconSize = EditorStyle::Scale(20.0f);
		const float Gap = EditorStyle::Scale(6.0f);
		const float Height = Gap * 2.0f + IconSize + EditorStyle::Scale(4.0f) + ImGui::GetTextLineHeight();
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const bool bClicked = ImGui::InvisibleButton(Label, ImVec2(Width, Height));
		const bool bHovered = ImGui::IsItemHovered();

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImVec2 Min(Pos.x + EditorStyle::Scale(4.0f), Pos.y + EditorStyle::Scale(2.0f));
		const ImVec2 Max(Pos.x + Width - EditorStyle::Scale(4.0f), Pos.y + Height - EditorStyle::Scale(2.0f));
		if (bSelected)
		{
			DrawList->AddRectFilled(Min, Max, EditorColor::Accent, EditorStyle::Scale(3.0f));
		}
		else if (bHovered)
		{
			DrawList->AddRectFilled(Min, Max, EditorColor::BgHover, EditorStyle::Scale(3.0f));
		}

		const ImU32 Color = bSelected ? EditorColor::Ink : (bHovered ? EditorColor::TextPrimary : EditorColor::TextSecondary);
		EditorIcons::Draw(DrawList, Icon, ImVec2(Pos.x + Width * 0.5f, Pos.y + Gap + IconSize * 0.5f), IconSize, Color);
		const ImVec2 TextSize = ImGui::CalcTextSize(Label);
		DrawList->AddText(ImVec2(Pos.x + (Width - TextSize.x) * 0.5f, Pos.y + Gap + IconSize + EditorStyle::Scale(4.0f)), Color, Label);
		return bClicked;
	}

	// 도구 영역의 굵은 제목과 아래 여백
	void ToolSection(const char* Title)
	{
		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, EditorColor::ToVec4(EditorColor::TextMuted));
		ImGui::TextUnformatted(Title);
		ImGui::PopStyleColor();
		ImGui::Separator();
	}

	// 반 칸에 가장 긴 도구 이름이 들어가면 두 칸, 아니면 한 칸 격자로 그린다.
	// 버튼마다 따로 정하면 줄마다 모양이 달라지므로 패널 너비 하나로 정한다.
	bool UseTwoColumns()
	{
		const ImGuiStyle& Style = ImGui::GetStyle();
		const float Half = (ImGui::GetContentRegionAvail().x - Style.ItemSpacing.x) * 0.5f;
		return Half >= ImGui::CalcTextSize("Weight Wireframe").x + Style.FramePadding.x * 2.0f;
	}

	// 격자의 도구 버튼. Column이 0이면 새 줄, 1이면 두 칸일 때 같은 줄 오른쪽에 둔다.
	bool GridButton(const char* Label, int32 Column, bool bSelected = false)
	{
		const bool bTwoColumns = UseTwoColumns();
		const float Spacing = ImGui::GetStyle().ItemSpacing.x;
		float Width = -FLT_MIN;
		if (bTwoColumns)
		{
			if (Column == 1)
			{
				ImGui::SameLine();
				Width = ImGui::GetContentRegionAvail().x;
			}
			else
			{
				Width = (ImGui::GetContentRegionAvail().x - Spacing) * 0.5f;
			}
		}
		if (bSelected)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, EditorColor::ToVec4(EditorColor::Accent));
			ImGui::PushStyleColor(ImGuiCol_Text, EditorColor::ToVec4(EditorColor::Ink));
		}
		const bool bClicked = ImGui::Button(Label, ImVec2(Width < 0.0f ? Width : FMath::Max(Width, 1.0f), 0.0f));
		if (bSelected)
		{
			ImGui::PopStyleColor(2);
		}
		// 그래도 잘리면 전체 이름을 툴팁으로 보여 준다
		if (ImGui::GetItemRectSize().x < ImGui::CalcTextSize(Label).x + ImGui::GetStyle().FramePadding.x * 2.0f
			&& ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%s", Label);
		}
		return bClicked;
	}

	// 흐린 안내 문장. 패널이 좁으면 줄을 바꾼다
	void Hint(const char* Text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("%s", Text);
		ImGui::PopStyleColor();
	}
}

FSkeletalMeshEditingToolsPanel::FSkeletalMeshEditingToolsPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Editing Tools")
{
}

void FSkeletalMeshEditingToolsPanel::DrawContent()
{
	// 툴바에서 Cloth Paint를 켜면 Cloth 분류로 넘어간다
	const bool bPaintMode = Editor.IsClothPaintMode();
	if (bPaintMode && !bWasClothPaintMode)
	{
		Category = ECategory::Cloth;
	}
	bWasClothPaintMode = bPaintMode;

	const float SettingsHeight = ImGui::GetFrameHeightWithSpacing() * 3.0f + ImGui::GetTextLineHeightWithSpacing() * 2.0f;
	const float BodyHeight = FMath::Max(ImGui::GetContentRegionAvail().y - SettingsHeight, EditorStyle::Scale(80.0f));

	DrawCategoryBar(BodyHeight);
	ImGui::SameLine();

	if (ImGui::BeginChild("SKMTools", ImVec2(0.0f, BodyHeight), ImGuiChildFlags_None))
	{
		switch (Category)
		{
		case ECategory::Skin:    DrawSkinTools();    break;
		case ECategory::Pose:    DrawPoseTools();    break;
		case ECategory::Cloth:   DrawClothTools();   break;
		case ECategory::Physics: DrawPhysicsTools(); break;
		}
	}
	ImGui::EndChild();

	DrawAssetEditingSettings();
}

void FSkeletalMeshEditingToolsPanel::DrawCategoryBar(float Height)
{
	// 가장 긴 분류 이름("Physics")이 잘리지 않는 너비
	const float Width = FMath::Max(EditorStyle::Scale(56.0f),
		ImGui::CalcTextSize("Physics").x + EditorStyle::Scale(20.0f));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorColor::ToVec4(EditorColor::BgDeepest));
	if (ImGui::BeginChild("SKMCategories", ImVec2(Width, Height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar))
	{
		struct FItem { const char* Label; EEditorIcon Icon; ECategory Category; };
		static constexpr FItem Items[] = {
			{ "Skin",    EEditorIcon::SkeletalMesh, ECategory::Skin },
			{ "Pose",    EEditorIcon::Translate,    ECategory::Pose },
			{ "Cloth",   EEditorIcon::VisualEffects, ECategory::Cloth },
			{ "Physics", EEditorIcon::Shapes,       ECategory::Physics },
		};
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + EditorStyle::Scale(4.0f));
		for (const FItem& Item : Items)
		{
			if (CategoryItem(Item.Label, Item.Icon, Category == Item.Category, Width))
			{
				Category = Item.Category;
			}
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();
}

void FSkeletalMeshEditingToolsPanel::DrawSkinTools()
{
	ToolSection("Skin Weights");

	bool bShaded = Editor.IsWeightHeatmapEnabled();
	if (GridButton("Weight Heatmap", 0, bShaded))
	{
		Editor.SetWeightHeatmap(!bShaded);
	}
	bool bWire = Editor.GetShowBoneWeightHeatmap();
	if (GridButton("Weight Wireframe", 1, bWire))
	{
		Editor.SetShowBoneWeightHeatmap(!bWire);
	}

	const int32 BoneIndex = Editor.GetSelectedBoneIndex();
	const auto& Bones = Editor.GetBoneItems();
	if (BoneIndex >= 0 && BoneIndex < Bones.Num())
	{
		ImGui::TextWrapped("Bone: %s", Bones[BoneIndex].Name.c_str());
	}
	else
	{
		Hint("Select a bone in the Skeleton Tree to view its weights.");
	}

	ToolSection("Bone Visualizer");
	bool bShowBones = Editor.IsBoneVisualizerVisible();
	if (ImGui::Checkbox("Show Bones", &bShowBones))
	{
		Editor.SetBoneVisualizerVisible(bShowBones);
	}
	FVector4 BoneColor = Editor.GetBoneVisualizerColor();
	// 좁은 패널에서 RGB 입력칸이 뭉개지지 않게 견본만 두고, 누르면 색 선택 창을 연다
	if (ImGui::ColorEdit3("Bone Color", &BoneColor.X, ImGuiColorEditFlags_NoInputs))
	{
		Editor.SetBoneVisualizerColor(BoneColor);
	}
}

void FSkeletalMeshEditingToolsPanel::DrawPoseTools()
{
	ToolSection("Gizmo");
	const UE::Widget::EWidgetMode Mode = Editor.GetGizmoMode();
	if (GridButton("Translate (W)", 0, Mode == UE::Widget::WM_Translate)) Editor.SetGizmoMode(UE::Widget::WM_Translate);
	if (GridButton("Rotate (E)", 1, Mode == UE::Widget::WM_Rotate))       Editor.SetGizmoMode(UE::Widget::WM_Rotate);
	if (GridButton("Scale (R)", 0, Mode == UE::Widget::WM_Scale))         Editor.SetGizmoMode(UE::Widget::WM_Scale);
	const bool bWorld = Editor.GetGizmoSpace() == COORD_World;
	if (GridButton(bWorld ? "Space: World" : "Space: Local", 1))
	{
		Editor.SetGizmoSpace(bWorld ? COORD_Local : COORD_World);
	}

	ToolSection("Preview Pose");
	const int32 BoneIndex = Editor.GetSelectedBoneIndex();
	ImGui::BeginDisabled(BoneIndex < 0);
	if (GridButton("Reset Bone", 0))
	{
		Editor.PushPoseUndo();
		Editor.ResetBone(BoneIndex);
	}
	ImGui::EndDisabled();
	if (GridButton("Reset All Bones", 1))
	{
		Editor.PushPoseUndo();
		Editor.ResetPose();
	}
	Hint("Pose edits only affect the preview.");
}

// 선택한 렌더 섹션의 Cloth 브러시 조작과 바인딩 적용
void FSkeletalMeshEditingToolsPanel::DrawClothTools()
{
	USkeletalMesh* Mesh = Editor.GetMesh();
	if (!Mesh || Mesh->GetRenderData().LODRenderData.IsEmpty())
	{
		return;
	}

	ToolSection("Cloth Section");
	const auto& Sections = Mesh->GetRenderData().LODRenderData[0].Sections;
	const auto& MaterialSlots = Mesh->GetMaterialSlots();
	const int32 ClothSectionIndex = Editor.GetClothSectionIndex();
	const FString Preview = Sections.IsValidIndex(ClothSectionIndex)
		? std::format("{} - {}", ClothSectionIndex, MaterialSlots[Sections[ClothSectionIndex].MaterialSlotIndex].Name)
		: FString("Select a section");

	// 현재 메시의 섹션 번호와 머티리얼 이름 표시
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::BeginCombo("##ClothSection", Preview.c_str()))
	{
		for (int32 Index = 0; Index < Sections.Num(); ++Index)
		{
			const FString Label = std::format("{} - {}",
				Index, MaterialSlots[Sections[Index].MaterialSlotIndex].Name);
			const bool bSelected = Index == ClothSectionIndex;
			if (ImGui::Selectable(Label.c_str(), bSelected) && !bSelected)
			{
				Editor.SelectClothSection(Index);
				ClothBindingMessage.clear();
			}
		}
		ImGui::EndCombo();
	}

	const int32 VertexCount = Editor.GetClothSectionVertexCount();
	const int32 PinCount = Editor.GetClothPinnedVertexCount();

	ToolSection("Paint Max Distance");
	ImGui::BeginDisabled(VertexCount == 0);
	const bool bPaintMaxDistance = Editor.IsClothPaintMode();
	if (GridButton(bPaintMaxDistance ? "Stop Painting" : "Paint", 0, bPaintMaxDistance))
	{
		Editor.SetClothPaintMode(!bPaintMaxDistance);
	}
	if (GridButton("Fill Section", 1)) Editor.FillClothPaint();
	if (GridButton("Clear Paint", 0))  Editor.ClearClothPaint();

	float Radius = Editor.GetClothBrushRadius();
	ImGui::TextUnformatted("Brush Radius");
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::SliderFloat("##BrushRadius", &Radius, 4.0f, 64.0f, "%.0f px"))
	{
		Editor.SetClothBrushRadius(Radius);
	}
	float MaxDistance = Editor.GetClothBrushMaxDistance();
	ImGui::TextUnformatted("Max Distance");
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::DragFloat("##MaxDistance", &MaxDistance, 0.01f, 0.0f, FLT_MAX, "%.4g m", ImGuiSliderFlags_AlwaysClamp))
	{
		Editor.SetClothBrushMaxDistance(MaxDistance);
	}
	Hint("Drag to paint. 0 m pins. Shift + drag erases.");
	ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.1f, 1.0f), "Pinned: %d / %d", PinCount, VertexCount);
	ImGui::TextColored(ImVec4(0.2f, 0.75f, 1.0f, 1.0f), "Distance-limited: %d", Editor.GetClothCappedVertexCount());
	const bool bApply = ImGui::Button("Apply & Save Cloth Binding", ImVec2(-FLT_MIN, 0.0f));
	ImGui::EndDisabled();

	ToolSection("Preview");
	USkeletalMeshComponent* PreviewComponent = Editor.GetMeshComponent();
	// Cloth 프리뷰에 사용할 PhysicsAsset 바디 선택
	if (PreviewComponent)
	{
		FString& CollisionBody = PreviewComponent->ClothCollisionBody;
		const char* BodyPreview = CollisionBody.empty() ? "None" : CollisionBody.c_str();
		ImGui::TextUnformatted("Collision Body");
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##CollisionBody", BodyPreview))
		{
			if (ImGui::Selectable("None", CollisionBody.empty()))
			{
				CollisionBody.clear();
			}
			if (const UPhysicsAsset* PhysicsAsset = PreviewComponent->GetPhysicsAsset())
			{
				for (const USkeletalBodySetup* Setup : PhysicsAsset->SkeletalBodySetups)
				{
					if (!Setup || (Setup->AggGeom.SphereElems.IsEmpty()
						&& Setup->AggGeom.SphylElems.IsEmpty() && Setup->AggGeom.BoxElems.IsEmpty()))
					{
						continue;
					}
					const FString BoneName = Setup->BoneName.ToString();
					if (ImGui::Selectable(BoneName.c_str(), CollisionBody == BoneName))
					{
						CollisionBody = BoneName;
					}
				}
			}
			ImGui::EndCombo();
		}
	}

	// 적용된 Cloth의 프리뷰 실행·정지·초기화 버튼
	const bool bRunning = PreviewComponent && PreviewComponent->HasClothSimulation();

	ImGui::BeginDisabled(!PreviewComponent || Mesh->GetClothSectionBindings().IsEmpty());
	const bool bToggle = GridButton(bRunning ? "Stop" : "Simulate", 0, bRunning);
	ImGui::BeginDisabled(!bRunning);
	const bool bReset = GridButton("Reset", 1);
	ImGui::EndDisabled();
	ImGui::EndDisabled();

	if (bToggle && bRunning)
	{
		PreviewComponent->StopClothPreview();
		ClothBindingMessage.clear();
	}
	else if (bToggle || bReset)
	{
		if (PreviewComponent->RestartClothPreview())
		{
			Editor.SetClothPaintMode(false);
			ClothBindingMessage.clear();
		}
		else
		{
			ClothBindingMessage = "Cloth preview failed. Check the Output Log.";
		}
	}

	if (!ClothBindingMessage.empty())
	{
		ImGui::TextWrapped("%s", ClothBindingMessage.c_str());
	}

	if (!bApply)
	{
		return;
	}

	if (PinCount == VertexCount)
	{
		ClothBindingMessage = "Leave at least one vertex unpinned.";
		return;
	}

	FString SaveError;
	const bool bSaved = Editor.ApplyClothBinding(SaveError);
	ClothBindingMessage = bSaved ? "Cloth binding saved. Click Simulate to preview." : std::move(SaveError);
}

void FSkeletalMeshEditingToolsPanel::DrawPhysicsTools()
{
	ToolSection("Physics Asset");
	if (ImGui::Button("Open Physics Asset Editor", ImVec2(-FLT_MIN, 0.0f)))
	{
		Editor.OpenPhysicsAssetEditor();
	}

	ToolSection("Generate Bodies");
	ImGui::TextUnformatted("Min Bone Size");
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::DragFloat("##MinBoneSize", &DebugMinBoneSize, 0.1f, 0.f, 1000.f);
	if (GridButton("Generate", 0) && Editor.GetMesh())
	{
		FPhysAssetCreateParams Params;
		Params.MinBoneSize = DebugMinBoneSize;
		FString Error;
		DebugPhysicsAsset = NewObject<UPhysicsAsset>();
		if (FPhysicsAssetUtils::CreateFromSkeletalMesh(DebugPhysicsAsset, Editor.GetMesh(), Params, Error))
			DebugMessage = SaveAndReload(*DebugPhysicsAsset, DebugPhysicsAssetPath(Editor.GetAssetPath()));
		else
			DebugMessage = "Generate failed: " + (Error.empty() ? FString("no bodies (MinBoneSize too big?)") : Error);
		HTR_LOG(Info, "{}", DebugMessage);
	}
	// 직렬화 왕복 확인
	if (GridButton("Save/Load Test", 1))
	{
		DebugMessage = SaveAndReload(*MakeTestPhysicsAsset(), FPaths::ProjectContentDir() / "Physics" / "_Test.physicsasset");
		HTR_LOG(Info, "{}", DebugMessage);
	}

	if (!DebugMessage.empty()) ImGui::TextWrapped("%s", DebugMessage.c_str());

	if (DebugPhysicsAsset && ImGui::TreeNode("Bodies"))
	{
		for (USkeletalBodySetup* Setup : DebugPhysicsAsset->SkeletalBodySetups)
		{
			const FString Bone = Setup->BoneName.ToString();
			if (Setup->AggGeom.SphylElems.Num() > 0)
			{
				const FKSphylElem& E = Setup->AggGeom.SphylElems[0];
				ImGui::Text("%s  R %.1f  L %.1f", Bone.c_str(), E.Radius, E.Length);
			}
			else
			{
				ImGui::Text("%s  (no capsule)", Bone.c_str());
			}
		}
		ImGui::TreePop();
	}
}

void FSkeletalMeshEditingToolsPanel::DrawAssetEditingSettings()
{
	ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	if (!ImGui::CollapsingHeader("Asset Editing Settings"))
	{
		return;
	}

	// 지금은 LOD0만 편집한다
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Editing LOD");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::BeginCombo("##EditingLOD", "LOD0"))
	{
		ImGui::Selectable("LOD0", true);
		ImGui::EndCombo();
	}

	// 지금 분류의 실행 취소 기록 (Cloth는 페인트, 나머지는 프리뷰 포즈)
	const bool bCloth = Category == ECategory::Cloth;
	const bool bCanUndo = bCloth ? Editor.CanUndoCloth() : Editor.CanUndoPose();
	const bool bCanRedo = bCloth ? Editor.CanRedoCloth() : Editor.CanRedoPose();
	ImGui::BeginDisabled(!bCanUndo);
	if (GridButton("Undo", 0))
	{
		bCloth ? Editor.UndoCloth() : Editor.UndoPose();
	}
	ImGui::EndDisabled();
	ImGui::BeginDisabled(!bCanRedo);
	if (GridButton("Redo", 1))
	{
		bCloth ? Editor.RedoCloth() : Editor.RedoPose();
	}
	ImGui::EndDisabled();
}
