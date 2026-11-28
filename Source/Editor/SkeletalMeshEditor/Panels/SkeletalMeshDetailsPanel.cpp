#include "EnginePCH.h"
#include "SkeletalMeshDetailsPanel.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Math/Transform.h"
#include "Render/SkeletalMesh.h"

namespace
{
	// 이름 칸에 라벨을 쓰고 값 칸으로 넘어간다. 값 위젯은 칸 너비를 꽉 채운다.
	void PropertyLabel(const char* Label)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Label);
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-FLT_MIN);
	}
}

FSkeletalMeshDetailsPanel::FSkeletalMeshDetailsPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Details")
{
}

void FSkeletalMeshDetailsPanel::DrawContent()
{
	const int32 BoneIndex = Editor.GetSelectedBoneIndex();
	FTransform Transform;
	if (!Editor.GetSelectedBoneLocalTransform(Transform))
	{
		ImGui::Spacing();
		ImGui::TextDisabled("Select a bone in the Skeleton Tree to see its details.");
		return;
	}

	const USkeletalMesh* Mesh = Editor.GetMesh();
	const auto& Bones = Mesh->GetRefSkeleton();
	const int32 ParentIndex = Bones[BoneIndex].ParentIndex;

	// 선택이 바뀌면 이전 본의 편집 상태가 새 본에 이어지지 않도록 ID를 구분합니다.
	ImGui::PushID(BoneIndex);

	ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	if (ImGui::CollapsingHeader("Bone"))
	{
		if (ImGui::BeginTable("BoneInfo", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.35f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.65f);
			PropertyLabel("Bone Name");
			ImGui::TextUnformatted(Bones[BoneIndex].Name.c_str());
			PropertyLabel("Bone Index");
			ImGui::Text("%d", BoneIndex);
			PropertyLabel("Parent");
			ImGui::TextUnformatted(ParentIndex >= 0 ? Bones[ParentIndex].Name.c_str() : "(root)");
			ImGui::EndTable();
		}
	}

	bool bChanged = false;
	ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	if (ImGui::CollapsingHeader("Transform"))
	{
		if (ImGui::BeginTable("BoneTransform", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.35f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.65f);

			PropertyLabel("Location");
			bChanged |= ImGui::DragFloat3("##Location", Transform.Location.V, 0.1f);

			// 드래그 중 값이 튀지 않도록 마지막 오일러 값을 기억한다
			static FRotationConversionCache RotationCache;
			FRotator Euler = RotationCache.QuatToRotator(Transform.Rotation);
			PropertyLabel("Rotation");
			if (ImGui::DragFloat3("##Rotation", Euler.V, 0.5f))
			{
				Transform.Rotation = RotationCache.RotatorToQuat(Euler);
				bChanged = true;
			}

			PropertyLabel("Scale");
			bChanged |= ImGui::DragFloat3("##Scale", Transform.Scale.V, 0.01f);
			ImGui::EndTable();
		}

		const float Spacing = ImGui::GetStyle().ItemSpacing.x;
		const float HalfWidth = (ImGui::GetContentRegionAvail().x - Spacing) * 0.5f;
		if (ImGui::Button("Reset Bone", ImVec2(HalfWidth, 0.0f)))
		{
			Editor.PushPoseUndo();
			Editor.ResetBone(BoneIndex);
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset All Bones", ImVec2(-FLT_MIN, 0.0f)))
		{
			Editor.PushPoseUndo();
			Editor.ResetPose();
		}
	}
	ImGui::PopID();

	if (bChanged)
	{
		Editor.SetSelectedBoneLocalTransform(Transform);
	}
}
