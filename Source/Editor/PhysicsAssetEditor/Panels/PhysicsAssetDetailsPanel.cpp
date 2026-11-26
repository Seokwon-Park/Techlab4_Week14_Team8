#include "EnginePCH.h"
#include "PhysicsAssetDetailsPanel.h"
#include "Physics/PhysicsAsset.h"

namespace
{
	// 본 로컬 단위가 메시마다 달라(cm, m) 드래그 속도를 값 크기에 맞춘다
	float GetDragSpeed(float Magnitude)
	{
		return std::max(std::abs(Magnitude) * 0.01f, 0.0001f);
	}

	bool DragVector(const char* Label, FVector& Value)
	{
		float V[3] = { Value.X, Value.Y, Value.Z };
		const float Speed = GetDragSpeed(std::max({ std::abs(V[0]), std::abs(V[1]), std::abs(V[2]), 1.0f }));
		if (!ImGui::DragFloat3(Label, V, Speed))
		{
			return false;
		}
		Value = FVector(V[0], V[1], V[2]);
		return true;
	}

	bool DragRotator(const char* Label, FRotator& Value)
	{
		float V[3] = { Value.Pitch, Value.Yaw, Value.Roll };
		if (!ImGui::DragFloat3(Label, V, 0.5f))
		{
			return false;
		}
		Value = FRotator(V[0], V[1], V[2]);
		return true;
	}

	bool DragSize(const char* Label, float& Value)
	{
		return ImGui::DragFloat(Label, &Value, GetDragSpeed(Value), 0.0f, FLT_MAX, "%.4g", ImGuiSliderFlags_AlwaysClamp);
	}

	bool DrawConstraintFrame(const char* Label, const FConstraintInstance& Constraint, int32 Frame,
		FVector& Pos, FVector& PriAxis, FVector& SecAxis)
	{
		ImGui::PushID(Label);
		ImGui::TextUnformatted(Label);
		bool bEdited = DragVector("Position", Pos);
		FRotator Rotation = Constraint.GetRefFrame(Frame).Rotation.ToFRotator();
		if (DragRotator("Rotation", Rotation))
		{
			const FQuat Quat = Rotation.Quaternion();
			PriAxis = Quat.RotateVector(FVector(1, 0, 0));
			SecAxis = Quat.RotateVector(FVector(0, 1, 0));
			bEdited = true;
		}
		ImGui::PopID();
		return bEdited;
	}

	bool DrawAngularLimit(const char* Label, EAngularConstraintMotion& Motion, float& LimitDegrees)
	{
		static const char* MotionNames[] = { "Free", "Limited", "Locked" };   // EAngularConstraintMotion 순서

		ImGui::PushID(Label);
		ImGui::SeparatorText(Label);
		bool bEdited = false;
		int32 MotionIndex = static_cast<int32>(Motion);
		if (ImGui::Combo("Motion", &MotionIndex, MotionNames, IM_ARRAYSIZE(MotionNames)))
		{
			Motion = static_cast<EAngularConstraintMotion>(MotionIndex);
			bEdited = true;
		}
		ImGui::BeginDisabled(Motion != ACM_Limited);
		bEdited |= ImGui::SliderFloat("Limit", &LimitDegrees, 0.0f, 180.0f, "%.1f deg");
		ImGui::EndDisabled();
		ImGui::PopID();
		return bEdited;
	}
}

void FPhysicsAssetDetailsPanel::DrawContent()
{
	UPhysicsAsset* PhysicsAsset = Editor.GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		return;
	}

	if (Editor.IsSimulating())
	{
		ImGui::TextDisabled("Stop the simulation to edit.");
	}
	ImGui::BeginDisabled(Editor.IsSimulating());

	const int32 BodyIndex = Editor.GetSelectedBodyIndex();
	const int32 ConstraintIndex = Editor.GetSelectedConstraintIndex();
	bool bEdited = false;
	if (BodyIndex != INDEX_NONE && PhysicsAsset->SkeletalBodySetups[BodyIndex])
	{
		bEdited = DrawBody(*PhysicsAsset->SkeletalBodySetups[BodyIndex]);
	}
	else if (ConstraintIndex != INDEX_NONE && PhysicsAsset->ConstraintSetup[ConstraintIndex])
	{
		bEdited = DrawConstraint(*PhysicsAsset->ConstraintSetup[ConstraintIndex]);
	}
	else
	{
		ImGui::TextDisabled("Select a body or constraint.");
	}
	ImGui::EndDisabled();

	if (bEdited)
	{
		Editor.MarkDirty();
	}
}

bool FPhysicsAssetDetailsPanel::DrawBody(USkeletalBodySetup& BodySetup)
{
	ImGui::Text("Body: %s", BodySetup.BoneName.ToString().c_str());
	bool bEdited = false;

	static const char* PhysicsTypeNames[] = { "Default", "Kinematic", "Simulated" };   // EPhysicsType 순서
	int32 PhysicsType = static_cast<int32>(BodySetup.PhysicsType);
	if (ImGui::Combo("Physics Type", &PhysicsType, PhysicsTypeNames, IM_ARRAYSIZE(PhysicsTypeNames)))
	{
		BodySetup.PhysicsType = static_cast<EPhysicsType>(PhysicsType);
		bEdited = true;
	}

	FKAggregateGeom& Geom = BodySetup.AggGeom;
	for (int32 i = 0; i < Geom.SphereElems.Num(); ++i)
	{
		FKSphereElem& Elem = Geom.SphereElems[i];
		ImGui::PushID(i);
		ImGui::SeparatorText("Sphere");
		bEdited |= ImGui::Checkbox("Contribute To Mass", &Elem.bContributeToMass);
		bEdited |= DragVector("Center", Elem.Center);
		bEdited |= DragSize("Radius", Elem.Radius);
		ImGui::PopID();
	}

	for (int32 i = 0; i < Geom.BoxElems.Num(); ++i)
	{
		FKBoxElem& Elem = Geom.BoxElems[i];
		ImGui::PushID(1000 + i);
		ImGui::SeparatorText("Box");
		bEdited |= ImGui::Checkbox("Contribute To Mass", &Elem.bContributeToMass);
		bEdited |= DragVector("Center", Elem.Center);
		bEdited |= DragRotator("Rotation", Elem.Rotation);
		bEdited |= DragSize("X", Elem.X);
		bEdited |= DragSize("Y", Elem.Y);
		bEdited |= DragSize("Z", Elem.Z);
		ImGui::PopID();
	}

	for (int32 i = 0; i < Geom.SphylElems.Num(); ++i)
	{
		FKSphylElem& Elem = Geom.SphylElems[i];
		ImGui::PushID(2000 + i);
		ImGui::SeparatorText("Capsule");
		bEdited |= ImGui::Checkbox("Contribute To Mass", &Elem.bContributeToMass);
		bEdited |= DragVector("Center", Elem.Center);
		bEdited |= DragRotator("Rotation", Elem.Rotation);
		bEdited |= DragSize("Radius", Elem.Radius);
		bEdited |= DragSize("Length", Elem.Length);
		ImGui::PopID();
	}

	for (int32 i = 0; i < Geom.ConvexElems.Num(); ++i)
	{
		ImGui::PushID(3000 + i);
		ImGui::SeparatorText("Convex");
		bEdited |= ImGui::Checkbox("Contribute To Mass", &Geom.ConvexElems[i].bContributeToMass);
		ImGui::Text("Vertices: %d", Geom.ConvexElems[i].VertexData.Num());
		ImGui::PopID();
	}

	ImGui::SeparatorText("Physics");
	bEdited |= ImGui::Checkbox("Override Mass", &BodySetup.bOverrideMass);
	ImGui::BeginDisabled(!BodySetup.bOverrideMass);
	bEdited |= ImGui::DragFloat("Mass (kg)", &BodySetup.MassInKgOverride, 0.1f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::EndDisabled();
	bEdited |= ImGui::DragFloat("Friction", &BodySetup.Friction, 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	bEdited |= ImGui::SliderFloat("Restitution", &BodySetup.Restitution, 0.0f, 1.0f, "%.3f");
	return bEdited;
}

bool FPhysicsAssetDetailsPanel::DrawConstraint(UPhysicsConstraintTemplate& Template)
{
	FConstraintInstance& Constraint = Template.DefaultInstance;
	FConstraintProfileProperties& Profile = Constraint.ProfileInstance;

	ImGui::Text("Bone 1 (Child): %s", Constraint.ConstraintBone1.ToString().c_str());
	ImGui::Text("Bone 2 (Parent): %s", Constraint.ConstraintBone2.ToString().c_str());

	// Frame1 = 자식 본 공간, Frame2 = 부모 본 공간
	ImGui::SeparatorText("Constraint Transforms");
	bool bEdited = DrawConstraintFrame("Child", Constraint, 0, Constraint.Pos1, Constraint.PriAxis1, Constraint.SecAxis1);
	bEdited |= DrawConstraintFrame("Parent", Constraint, 1, Constraint.Pos2, Constraint.PriAxis2, Constraint.SecAxis2);

	bEdited |= DrawAngularLimit("Swing 1", Profile.ConeLimit.Swing1Motion, Profile.ConeLimit.Swing1LimitDegrees);
	bEdited |= DrawAngularLimit("Swing 2", Profile.ConeLimit.Swing2Motion, Profile.ConeLimit.Swing2LimitDegrees);
	bEdited |= DrawAngularLimit("Twist", Profile.TwistLimit.TwistMotion, Profile.TwistLimit.TwistLimitDegrees);

	ImGui::SeparatorText("Collision");
	if (ImGui::Checkbox("Disable Collision", &Profile.bDisableCollision))
	{
		bEdited = true;
		UPhysicsAsset* PhysicsAsset = Editor.GetPhysicsAsset();
		const int32 BodyA = PhysicsAsset->FindBodyIndex(Constraint.ConstraintBone1);
		const int32 BodyB = PhysicsAsset->FindBodyIndex(Constraint.ConstraintBone2);
		if (BodyA != INDEX_NONE && BodyB != INDEX_NONE)
		{
			if (Profile.bDisableCollision)
			{
				PhysicsAsset->DisableCollision(BodyA, BodyB);
			}
			else
			{
				PhysicsAsset->EnableCollision(BodyA, BodyB);
			}
		}
	}
	return bEdited;
}
