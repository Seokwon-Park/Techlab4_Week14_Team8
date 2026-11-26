#include "EnginePCH.h"
#include "Editor/StaticMeshEditor/Panels/StaticMeshDetailsPanel.h"
#include "Editor/StaticMeshEditor/StaticMeshEditor.h"
#include "Physics/BodySetup.h"
#include "Render/Mesh.h"
#include <imgui.h>

FStaticMeshDetailsPanel::FStaticMeshDetailsPanel(FStaticMeshEditor& InEditor)
	: FStaticMeshEditorPanel(InEditor, "Details")
{
}

void FStaticMeshDetailsPanel::DrawContent()
{
	UStaticMesh* Mesh = Editor.GetMesh();
	if (!Mesh)
	{
		ImGui::TextDisabled("No StaticMesh loaded.");
		return;
	}

	// 14주차 다중 LOD 요약
	if (ImGui::CollapsingHeader("LOD Summary", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const uint32 NumLODs = Mesh->GetLODCount();
		ImGui::Text("LOD Count: %u", NumLODs);
		for (uint32 LOD = 0; LOD < NumLODs; ++LOD)
		{
			const FStaticMeshData& Data = Mesh->GetMeshData(LOD);
			ImGui::TextDisabled("  LOD %u: %d verts, %d tris",
				LOD, Data.Vertices.Num(), Data.Indices.Num() / 3);
		}
	}

	ImGui::Separator();

	if (ImGui::CollapsingHeader("Collision (BodySetup)", ImGuiTreeNodeFlags_DefaultOpen))
	{
		bool bShow = Editor.GetShowCollision();
		if (ImGui::Checkbox("Show Collision", &bShow))
			Editor.SetShowCollision(bShow);

		ImGui::SameLine();
		if (ImGui::Button(Editor.IsDirty() ? "Save .bodysetup*" : "Save .bodysetup"))
			Editor.SaveBodySetup();

		ImGui::BeginDisabled(!Editor.CanUndoCollision());
		if (ImGui::Button("Undo")) Editor.UndoCollision();
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!Editor.CanRedoCollision());
		if (ImGui::Button("Redo")) Editor.RedoCollision();
		ImGui::EndDisabled();

		ImGui::SeparatorText("Auto-Fit Primitives");
		if (ImGui::Button("+ Box"))     Editor.AddAutoFitBox();
		ImGui::SameLine();
		if (ImGui::Button("+ Sphere"))  Editor.AddAutoFitSphere();
		ImGui::SameLine();
		if (ImGui::Button("+ Capsule")) Editor.AddAutoFitCapsule();
		ImGui::SameLine();
		if (ImGui::Button("+ Convex"))  Editor.AddAutoFitConvex();
		ImGui::SameLine();
		if (ImGui::Button("Clear All")) Editor.ClearCollision();

		UBodySetup* Setup = Mesh->GetBodySetup();
		if (!Setup)
		{
			ImGui::TextDisabled("No BodySetup.");
			return;
		}

		ImGui::SeparatorText("Physics Properties");
		bool bPropChanged = false;
		bPropChanged |= ImGui::DragFloat("Friction", &Setup->Friction, 0.01f, 0.0f, 10.0f);
		bPropChanged |= ImGui::DragFloat("Restitution", &Setup->Restitution, 0.01f, 0.0f, 1.0f);
		bPropChanged |= ImGui::Checkbox("Override Mass", &Setup->bOverrideMass);
		if (Setup->bOverrideMass)
			bPropChanged |= ImGui::DragFloat("Mass (kg)", &Setup->MassInKgOverride, 0.1f, 0.01f, 100000.0f);
		if (bPropChanged) Editor.MarkDirty();

		// Box Elements
		auto& Boxes = Setup->AggGeom.BoxElems;
		if (ImGui::TreeNodeEx("Boxes", ImGuiTreeNodeFlags_DefaultOpen, "Boxes (%d)", Boxes.Num()))
		{
			int32 RemoveIdx = INDEX_NONE;
			for (int32 i = 0; i < Boxes.Num(); ++i)
			{
				ImGui::PushID(i);
				FKBoxElem& E = Boxes[i];
				if (ImGui::TreeNode("##Box", "Box %d", i))
				{
					bool bEdited = false;
					bEdited |= ImGui::DragFloat3("Center", E.Center.V, 0.02f);
					float Rot[3] = { E.Rotation.Roll, E.Rotation.Pitch, E.Rotation.Yaw };
					if (ImGui::DragFloat3("Rotation", Rot, 0.5f))
					{
						E.Rotation = FRotator(Rot[1], Rot[2], Rot[0]);
						bEdited = true;
					}
					float Ext[3] = { E.X, E.Y, E.Z };
					if (ImGui::DragFloat3("Size (XYZ)", Ext, 0.02f, 0.01f, 1000.0f))
					{
						E.X = Ext[0]; E.Y = Ext[1]; E.Z = Ext[2];
						bEdited = true;
					}
					if (bEdited) Editor.MarkDirty();
					if (ImGui::Button("Delete Box")) RemoveIdx = i;
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			if (RemoveIdx != INDEX_NONE)
			{
				Editor.PushCollisionUndo();
				Boxes.RemoveAt(static_cast<uint32>(RemoveIdx));
				Editor.MarkDirty();
			}
			ImGui::TreePop();
		}

		// Sphere Elements
		auto& Spheres = Setup->AggGeom.SphereElems;
		if (ImGui::TreeNodeEx("Spheres", ImGuiTreeNodeFlags_DefaultOpen, "Spheres (%d)", Spheres.Num()))
		{
			int32 RemoveIdx = INDEX_NONE;
			for (int32 i = 0; i < Spheres.Num(); ++i)
			{
				ImGui::PushID(i);
				FKSphereElem& E = Spheres[i];
				if (ImGui::TreeNode("##Sphere", "Sphere %d", i))
				{
					bool bEdited = false;
					bEdited |= ImGui::DragFloat3("Center", E.Center.V, 0.02f);
					bEdited |= ImGui::DragFloat("Radius", &E.Radius, 0.02f, 0.01f, 1000.0f);
					if (bEdited) Editor.MarkDirty();
					if (ImGui::Button("Delete Sphere")) RemoveIdx = i;
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			if (RemoveIdx != INDEX_NONE)
			{
				Editor.PushCollisionUndo();
				Spheres.RemoveAt(static_cast<uint32>(RemoveIdx));
				Editor.MarkDirty();
			}
			ImGui::TreePop();
		}

		// Capsule Elements
		auto& Sphyls = Setup->AggGeom.SphylElems;
		if (ImGui::TreeNodeEx("Capsules", ImGuiTreeNodeFlags_DefaultOpen, "Capsules (%d)", Sphyls.Num()))
		{
			int32 RemoveIdx = INDEX_NONE;
			for (int32 i = 0; i < Sphyls.Num(); ++i)
			{
				ImGui::PushID(i);
				FKSphylElem& E = Sphyls[i];
				if (ImGui::TreeNode("##Capsule", "Capsule %d", i))
				{
					bool bEdited = false;
					bEdited |= ImGui::DragFloat3("Center", E.Center.V, 0.02f);
					float Rot[3] = { E.Rotation.Roll, E.Rotation.Pitch, E.Rotation.Yaw };
					if (ImGui::DragFloat3("Rotation", Rot, 0.5f))
					{
						E.Rotation = FRotator(Rot[1], Rot[2], Rot[0]);
						bEdited = true;
					}
					bEdited |= ImGui::DragFloat("Radius", &E.Radius, 0.02f, 0.01f, 1000.0f);
					bEdited |= ImGui::DragFloat("Length", &E.Length, 0.02f, 0.01f, 1000.0f);
					if (bEdited) Editor.MarkDirty();
					if (ImGui::Button("Delete Capsule")) RemoveIdx = i;
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			if (RemoveIdx != INDEX_NONE)
			{
				Editor.PushCollisionUndo();
				Sphyls.RemoveAt(static_cast<uint32>(RemoveIdx));
				Editor.MarkDirty();
			}
			ImGui::TreePop();
		}

		// Convex Elements
		auto& Convexes = Setup->AggGeom.ConvexElems;
		if (ImGui::TreeNodeEx("ConvexHulls", ImGuiTreeNodeFlags_DefaultOpen, "Convex Hulls (%d)", Convexes.Num()))
		{
			int32 RemoveIdx = INDEX_NONE;
			for (int32 i = 0; i < Convexes.Num(); ++i)
			{
				ImGui::PushID(i);
				ImGui::Text("Convex %d (%d verts)", i, Convexes[i].VertexData.Num());
				ImGui::SameLine();
				if (ImGui::SmallButton("Delete")) RemoveIdx = i;
				ImGui::PopID();
			}
			if (RemoveIdx != INDEX_NONE)
			{
				Editor.PushCollisionUndo();
				Convexes.RemoveAt(static_cast<uint32>(RemoveIdx));
				Editor.MarkDirty();
			}
			ImGui::TreePop();
		}
	}
}
