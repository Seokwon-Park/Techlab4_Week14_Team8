#include "EnginePCH.h"
#include "PhysicsAssetToolsPanel.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Physics/PhysicsAsset.h"

namespace
{
	// 이름 칸에 라벨을 쓰고 값 칸으로 넘어간다. 값 위젯은 칸 너비를 꽉 채운다.
	void PropertyLabel(const char* Label, const char* Tooltip = nullptr)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Label);
		if (Tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tooltip);
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-FLT_MIN);
	}

	template <typename TEnum>
	void EnumCombo(const char* Id, TEnum& Value, const char* const* Names, int32 Count)
	{
		int Index = static_cast<int>(Value);
		if (ImGui::Combo(Id, &Index, Names, Count))
		{
			Value = static_cast<TEnum>(Index);
		}
	}
}

void FPhysicsAssetToolsPanel::DrawContent()
{
	FPhysAssetCreateParams& Params = Editor.CreateParams;
	const float ButtonHeight = ImGui::GetFrameHeight() + EditorStyle::Scale(6.0f);
	const float BodyHeight = ImGui::GetContentRegionAvail().y - ButtonHeight - ImGui::GetStyle().ItemSpacing.y * 2.0f;

	if (ImGui::BeginChild("PhysicsToolsBody", ImVec2(0.0f, FMath::Max(BodyHeight, 1.0f))))
	{
		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Body Creation")
			&& ImGui::BeginTable("BodyCreation", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.5f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.5f);

			PropertyLabel("Min Bone Size", "Bones smaller than this get no body of their own");
			ImGui::DragFloat("##MinBoneSize", &Params.MinBoneSize, 0.1f, 0.0f, 1000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);

			// 지금 바디 생성기가 만드는 모양만 고른다 (Convex Hull은 아직 없다)
			static const char* GeomNames[] = { "Box", "Capsule", "Sphere" };
			PropertyLabel("Primitive Type");
			if (Params.GeomType > EFG_Sphere) Params.GeomType = EFG_Sphyl;
			EnumCombo("##PrimitiveType", Params.GeomType, GeomNames, IM_ARRAYSIZE(GeomNames));

			static const char* WeightNames[] = { "Any Weight", "Dominant Weight" };
			PropertyLabel("Vertex Weighting Type", "Which skinned vertices count as part of a bone");
			EnumCombo("##VertWeight", Params.VertWeight, WeightNames, IM_ARRAYSIZE(WeightNames));

			PropertyLabel("Walk Past Small Bones", "Give small bones' vertices to the next bone up");
			ImGui::Checkbox("##WalkPastSmall", &Params.bWalkPastSmall);

			ImGui::EndTable();
		}

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Constraint Creation")
			&& ImGui::BeginTable("ConstraintCreation", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.5f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.5f);

			PropertyLabel("Create Constraints");
			ImGui::Checkbox("##CreateConstraints", &Params.bCreateConstraints);

			static const char* MotionNames[] = { "Free", "Limited", "Locked" };
			PropertyLabel("Angular Constraint Mode");
			ImGui::BeginDisabled(!Params.bCreateConstraints);
			EnumCombo("##AngularMode", Params.AngularConstraintMode, MotionNames, IM_ARRAYSIZE(MotionNames));
			ImGui::EndDisabled();

			ImGui::EndTable();
		}
	}
	ImGui::EndChild();

	// 오른쪽 아래 고정 버튼 (UE5의 파란 Generate All Bodies)
	const float Width = ImGui::CalcTextSize("Generate All Bodies").x + EditorStyle::Scale(32.0f);
	ImGui::SetCursorPosX(FMath::Max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - Width));
	ImGui::BeginDisabled(Editor.IsSimulating() || !Editor.GetPhysicsAsset());
	ImGui::PushStyleColor(ImGuiCol_Button, EditorColor::ToVec4(EditorColor::Accent));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorColor::ToVec4(EditorColor::AccentHover));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, EditorColor::ToVec4(EditorColor::AccentActive));
	ImGui::PushStyleColor(ImGuiCol_Text, EditorColor::ToVec4(EditorColor::Ink));
	if (ImGui::Button("Generate All Bodies", ImVec2(Width, ButtonHeight)))
	{
		// 바디가 없으면 지울 것도 없으니 묻지 않는다
		Editor.RegenerateAllBodies(!Editor.GetPhysicsAsset()->SkeletalBodySetups.IsEmpty());
	}
	ImGui::PopStyleColor(4);
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::SetTooltip("Delete all bodies and constraints and generate them with these settings");
	}
}
