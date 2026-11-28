#include "EnginePCH.h"
#include "BoneHierarchyPanel.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Render/SkeletalMesh.h"
#include "Runtime/Render/SkeletalMeshData.h"

FBoneHierarchyPanel::FBoneHierarchyPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Skeleton Tree")
{
}

void FBoneHierarchyPanel::DrawContent()
{
	EditorIcons::SearchBox("##SkeletonTreeSearch", SearchBuffer, sizeof(SearchBuffer), "Search Skeleton Tree...");

	// UE5처럼 열 이름 머리줄을 둔다
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const float HeaderHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 2.0f;
	const ImVec2 Max(Min.x + ImGui::GetContentRegionAvail().x, Min.y + HeaderHeight);
	ImGui::GetWindowDrawList()->AddRectFilled(Min, Max, EditorColor::BgHeader);
	ImGui::SetCursorScreenPos(ImVec2(Min.x + ImGui::GetStyle().FramePadding.x, Min.y + ImGui::GetStyle().FramePadding.y));
	ImGui::TextUnformatted("Name");
	ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y + ImGui::GetStyle().ItemSpacing.y * 0.5f));

	DrawTree(Editor, SearchBuffer);
}

void FBoneHierarchyPanel::DrawTree(FSkeletalPreviewEditor& InEditor, const char* Filter)
{
	const USkeletalMesh* Mesh = InEditor.GetMesh();
	if (!Mesh)
	{
		return;
	}

	// 이름이 맞는 본과 그 조상을 표시 대상으로 표시한다. 부모가 항상 앞 인덱스이므로 뒤에서부터 올린다.
	TArray<uint8> Visible;
	const bool bFiltering = Filter && *Filter;
	if (bFiltering)
	{
		const auto& Bones = Mesh->GetRefSkeleton();
		const auto& Items = InEditor.GetBoneItems();
		Visible.Reserve(Items.Num());
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			Visible.Add(EditorStyle::ContainsIgnoreCase(Items[Index].Name.c_str(), Filter) ? 1 : 0);
		}
		for (int32 Index = Visible.Num() - 1; Index >= 0; --Index)
		{
			const int32 Parent = Index < Bones.Num() ? Bones[Index].ParentIndex : -1;
			if (Visible[Index] && Parent >= 0 && Parent < Visible.Num())
			{
				Visible[Parent] = 1;
			}
		}
	}

	if (ImGui::BeginChild("BoneHierarchyChild", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12.0f);

		for (int32 RootBoneIndex : InEditor.GetRootBoneIndices())
		{
			DrawBoneHierarchy(InEditor, RootBoneIndex, bFiltering ? &Visible : nullptr);
		}

		ImGui::PopStyleVar();
	}

	ImGui::EndChild();
}

void FBoneHierarchyPanel::DrawBoneHierarchy(FSkeletalPreviewEditor& InEditor, int32 BoneIndex, const TArray<uint8>* Visible)
{
	if (Visible && !(*Visible)[BoneIndex])
	{
		return;
	}

	ImGuiTreeNodeFlags Flag =
		ImGuiTreeNodeFlags_DefaultOpen |
		ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_SpanAvailWidth;

	const TArray<FEditorBoneItem>& BoneItems = InEditor.GetBoneItems();

	int32 ChildBoneCount = BoneItems[BoneIndex].ChildBoneIndices.Num();
	if (ChildBoneCount == 0)
	{
		Flag |= ImGuiTreeNodeFlags_Leaf;
	}

	if (InEditor.GetSelectedBoneIndex() == BoneIndex)
	{
		Flag |= ImGuiTreeNodeFlags_Selected;
	}

	ImGui::PushID(BoneIndex);

	// 검색 중에는 찾은 본이 보이도록 모두 펼친다
	if (Visible)
	{
		ImGui::SetNextItemOpen(true);
	}

	// 표시 이름과 ID를 분리하여 축약된 이름이 겹쳐도 구분되게 함
	const FString& BoneName = BoneItems[BoneIndex].Name;

	// 접힌 노드도 선택할 수 있도록 펼침 상태와 선택 처리를 분리
	const bool bOpen = ImGui::TreeNodeEx("Bone", Flag, "%s", BoneName.c_str());

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		InEditor.SelectBone(BoneIndex);
	}

	if (bOpen)
	{
		for (int32 ChildIndex : BoneItems[BoneIndex].ChildBoneIndices)
		{
			DrawBoneHierarchy(InEditor, ChildIndex, Visible);
		}

		ImGui::TreePop();
	}

	ImGui::PopID();
}
