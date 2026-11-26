#include "EnginePCH.h"
#include "PhysicsAssetTreePanel.h"
#include "Physics/PhysicsAsset.h"
#include "Render/SkeletalMesh.h"

namespace
{
	const ImVec4 BodyTextColor(0.3f, 0.8f, 1.0f, 1.0f);

	FName GetBoneName(const FPhysicsAssetEditor& Editor, int32 BoneIndex)
	{
		return FName(Editor.GetMesh()->GetRefSkeleton()[BoneIndex].Name);
	}

	const char* GetBoneDisplayName(const FPhysicsAssetEditor& Editor, FName BoneName)
	{
		const int32 BoneIndex = Editor.FindBoneIndexByName(BoneName);
		return BoneIndex >= 0 ? Editor.GetBoneItems()[BoneIndex].Name.c_str() : "?";
	}

	const ImGuiTreeNodeFlags BranchFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
}

void FPhysicsAssetTreePanel::DrawContent()
{
	if (!Editor.GetMesh() || !Editor.GetPhysicsAsset())
	{
		return;
	}

	ImGui::Checkbox("Show All Bones", &bShowAllBones);
	ImGui::Separator();

	if (ImGui::BeginChild("PhysicsAssetTreeChild", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12.0f);
		for (int32 RootBoneIndex : Editor.GetRootBoneIndices())
		{
			DrawBone(RootBoneIndex);
		}
		ImGui::PopStyleVar();
	}
	ImGui::EndChild();

	switch (PendingAction)
	{
	case EPendingAction::AddBody:          Editor.MakeNewBody(PendingIndex); break;
	case EPendingAction::DeleteBody:       Editor.DeleteBody(PendingIndex); break;
	case EPendingAction::DeleteConstraint: Editor.DeleteConstraint(PendingIndex); break;
	case EPendingAction::AddShape:         Editor.AddShape(PendingIndex, PendingShapeType); break;
	case EPendingAction::DeleteShape:      Editor.DeleteShape(PendingIndex, PendingShapeType, PendingShapeIndex); break;
	case EPendingAction::RegenerateBody:   Editor.RegenerateBody(PendingIndex); break;
	case EPendingAction::MakeConstraint:   Editor.MakeNewConstraint(PendingIndex, PendingTargetBodyIndex); break;
	default: break;
	}
	PendingAction = EPendingAction::None;
	PendingIndex = INDEX_NONE;
	PendingShapeType = EAggCollisionShape::Unknown;
	PendingShapeIndex = INDEX_NONE;
	PendingTargetBodyIndex = INDEX_NONE;
}

int32 FPhysicsAssetTreePanel::FindBody(int32 BoneIndex) const
{
	return Editor.GetPhysicsAsset()->FindBodyIndex(GetBoneName(Editor, BoneIndex));
}

bool FPhysicsAssetTreePanel::HasConstraints(int32 BoneIndex) const
{
	const FName BoneName = GetBoneName(Editor, BoneIndex);
	for (const UPhysicsConstraintTemplate* Template : Editor.GetPhysicsAsset()->ConstraintSetup)
	{
		if (Template && Template->DefaultInstance.ConstraintBone1 == BoneName)
		{
			return true;
		}
	}
	return false;
}

bool FPhysicsAssetTreePanel::HasBodyDescendant(int32 BoneIndex) const
{
	for (int32 ChildIndex : Editor.GetBoneItems()[BoneIndex].ChildBoneIndices)
	{
		if (FindBody(ChildIndex) != INDEX_NONE || HasBodyDescendant(ChildIndex))
		{
			return true;
		}
	}
	return false;
}

void FPhysicsAssetTreePanel::DrawBone(int32 BoneIndex)
{
	const TArray<FEditorBoneItem>& BoneItems = Editor.GetBoneItems();
	const int32 BodyIndex = FindBody(BoneIndex);

	ImGui::PushID(BoneIndex);

	if (!bShowAllBones)
	{
		// 바디가 없는 본은 행 없이 자식만 같은 깊이로 그린다
		if (BodyIndex != INDEX_NONE)
		{
			DrawBody(BoneIndex, BodyIndex, true);
		}
		else
		{
			for (int32 ChildIndex : BoneItems[BoneIndex].ChildBoneIndices)
			{
				DrawBone(ChildIndex);
			}
		}
		ImGui::PopID();
		return;
	}

	ImGuiTreeNodeFlags Flags = BranchFlags;
	if (BodyIndex == INDEX_NONE && BoneItems[BoneIndex].ChildBoneIndices.IsEmpty())
	{
		Flags |= ImGuiTreeNodeFlags_Leaf;
	}
	if (Editor.GetSelectionType() == EPhysicsAssetSelection::None && Editor.GetSelectedBoneIndex() == BoneIndex)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	const bool bOpen = ImGui::TreeNodeEx("Bone", Flags, "%s", BoneItems[BoneIndex].Name.c_str());
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		Editor.SelectBone(BoneIndex);
	}
	if (ImGui::BeginPopupContextItem("BoneContext"))
	{
		if (ImGui::MenuItem("Add Body", nullptr, false, BodyIndex == INDEX_NONE))
		{
			PendingAction = EPendingAction::AddBody;
			PendingIndex = BoneIndex;
		}
		ImGui::EndPopup();
	}

	if (bOpen)
	{
		if (BodyIndex != INDEX_NONE)
		{
			DrawBody(BoneIndex, BodyIndex, false);
		}
		for (int32 ChildIndex : BoneItems[BoneIndex].ChildBoneIndices)
		{
			DrawBone(ChildIndex);
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void FPhysicsAssetTreePanel::DrawBody(int32 BoneIndex, int32 BodyIndex, bool bNestChildBones)
{
	ImGuiTreeNodeFlags Flags = BranchFlags;
	if (Editor.GetPhysicsAsset()->SkeletalBodySetups[BodyIndex]->AggGeom.GetElementCount() == 0 &&
		!HasConstraints(BoneIndex) && !(bNestChildBones && HasBodyDescendant(BoneIndex)))
	{
		Flags |= ImGuiTreeNodeFlags_Leaf;
	}
	if (Editor.GetSelectedBodyIndex() == BodyIndex && Editor.GetSelectedShapeType() == EAggCollisionShape::Unknown)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	ImGui::PushStyleColor(ImGuiCol_Text, BodyTextColor);
	const bool bOpen = ImGui::TreeNodeEx("Body", Flags, "%s", Editor.GetBoneItems()[BoneIndex].Name.c_str());
	ImGui::PopStyleColor();
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		Editor.SelectBody(BodyIndex);
	}
	if (ImGui::BeginPopupContextItem("BodyContext"))
	{
		const auto AddShapeItem = [this, BodyIndex](const char* Label, EAggCollisionShape::Type ShapeType)
		{
			if (ImGui::MenuItem(Label))
			{
				PendingAction = EPendingAction::AddShape;
				PendingIndex = BodyIndex;
				PendingShapeType = ShapeType;
			}
		};
		AddShapeItem("Add Sphere", EAggCollisionShape::Sphere);
		AddShapeItem("Add Box", EAggCollisionShape::Box);
		AddShapeItem("Add Capsule", EAggCollisionShape::Sphyl);
		ImGui::Separator();
		if (ImGui::MenuItem("Regenerate Body"))
		{
			PendingAction = EPendingAction::RegenerateBody;
			PendingIndex = BodyIndex;
		}
		DrawConstrainToMenu(BodyIndex);
		ImGui::Separator();
		if (ImGui::MenuItem("Delete Body"))
		{
			PendingAction = EPendingAction::DeleteBody;
			PendingIndex = BodyIndex;
		}
		ImGui::EndPopup();
	}

	if (bOpen)
	{
		DrawShapes(BodyIndex);
		DrawConstraints(BoneIndex);
		if (bNestChildBones)
		{
			for (int32 ChildIndex : Editor.GetBoneItems()[BoneIndex].ChildBoneIndices)
			{
				DrawBone(ChildIndex);
			}
		}
		ImGui::TreePop();
	}
}

void FPhysicsAssetTreePanel::DrawConstrainToMenu(int32 BodyIndex)
{
	const UPhysicsAsset* PhysicsAsset = Editor.GetPhysicsAsset();
	if (!ImGui::BeginMenu("Constrain To", PhysicsAsset->SkeletalBodySetups.Num() > 1))
	{
		return;
	}
	for (int32 TargetIndex = 0; TargetIndex < PhysicsAsset->SkeletalBodySetups.Num(); ++TargetIndex)
	{
		if (TargetIndex == BodyIndex)
		{
			continue;
		}
		const int32 TargetBone = Editor.FindBoneIndexByName(PhysicsAsset->SkeletalBodySetups[TargetIndex]->BoneName);
		const char* TargetName = TargetBone >= 0 ? Editor.GetBoneItems()[TargetBone].Name.c_str() : "?";
		ImGui::PushID(TargetIndex);
		if (ImGui::MenuItem(TargetName, nullptr, false, !Editor.HasConstraintBetween(BodyIndex, TargetIndex)))
		{
			PendingAction = EPendingAction::MakeConstraint;
			PendingIndex = BodyIndex;
			PendingTargetBodyIndex = TargetIndex;
		}
		ImGui::PopID();
	}
	ImGui::EndMenu();
}

void FPhysicsAssetTreePanel::DrawShapes(int32 BodyIndex)
{
	const FKAggregateGeom& Geom = Editor.GetPhysicsAsset()->SkeletalBodySetups[BodyIndex]->AggGeom;
	for (int32 i = 0; i < Geom.SphereElems.Num(); ++i)
	{
		DrawShape(BodyIndex, EAggCollisionShape::Sphere, i, "Sphere");
	}
	for (int32 i = 0; i < Geom.BoxElems.Num(); ++i)
	{
		DrawShape(BodyIndex, EAggCollisionShape::Box, i, "Box");
	}
	for (int32 i = 0; i < Geom.SphylElems.Num(); ++i)
	{
		DrawShape(BodyIndex, EAggCollisionShape::Sphyl, i, "Capsule");
	}
	for (int32 i = 0; i < Geom.ConvexElems.Num(); ++i)
	{
		DrawShape(BodyIndex, EAggCollisionShape::Convex, i, "Convex");
	}
}

void FPhysicsAssetTreePanel::DrawShape(int32 BodyIndex, EAggCollisionShape::Type ShapeType, int32 ShapeIndex, const char* Label)
{
	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (Editor.GetSelectedBodyIndex() == BodyIndex && Editor.GetSelectedShapeType() == ShapeType &&
		Editor.GetSelectedShapeIndex() == ShapeIndex)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	ImGui::PushID(static_cast<int32>(ShapeType) * 1000 + ShapeIndex);
	ImGui::TreeNodeEx("Shape", Flags, "%s %d", Label, ShapeIndex);
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
	{
		Editor.SelectShape(BodyIndex, ShapeType, ShapeIndex);
	}
	if (ImGui::BeginPopupContextItem("ShapeContext"))
	{
		if (ImGui::MenuItem("Delete Shape"))
		{
			PendingAction = EPendingAction::DeleteShape;
			PendingIndex = BodyIndex;
			PendingShapeType = ShapeType;
			PendingShapeIndex = ShapeIndex;
		}
		ImGui::EndPopup();
	}
	ImGui::PopID();
}

void FPhysicsAssetTreePanel::DrawConstraints(int32 BoneIndex)
{
	const UPhysicsAsset* PhysicsAsset = Editor.GetPhysicsAsset();
	const FName BoneName = GetBoneName(Editor, BoneIndex);
	const ImGuiTreeNodeFlags LeafFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;

	for (int32 ConstraintIndex = 0; ConstraintIndex < PhysicsAsset->ConstraintSetup.Num(); ++ConstraintIndex)
	{
		const FConstraintInstance& Constraint = PhysicsAsset->ConstraintSetup[ConstraintIndex]->DefaultInstance;
		if (!(Constraint.ConstraintBone1 == BoneName))
		{
			continue;
		}

		ImGuiTreeNodeFlags Flags = LeafFlags;
		if (Editor.GetSelectedConstraintIndex() == ConstraintIndex)
		{
			Flags |= ImGuiTreeNodeFlags_Selected;
		}
		ImGui::PushID(ConstraintIndex);
		ImGui::TreeNodeEx("Constraint", Flags, "[ %s -> %s ] Constraint",
			GetBoneDisplayName(Editor, Constraint.ConstraintBone2), GetBoneDisplayName(Editor, Constraint.ConstraintBone1));
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			Editor.SelectConstraint(ConstraintIndex);
		}
		if (ImGui::BeginPopupContextItem("ConstraintContext"))
		{
			if (ImGui::MenuItem("Delete Constraint"))
			{
				PendingAction = EPendingAction::DeleteConstraint;
				PendingIndex = ConstraintIndex;
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
}
