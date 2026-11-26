#include "EnginePCH.h"
#include "PhysicsAssetEditor.h"
#include "PhysicsAssetEditorTab.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetFile.h"
#include "Physics/PhysicsAssetUtils.h"
#include "Physics/PhysicsDebugDraw.h"
#include "Render/SkeletalMesh.h"
#include "Render/LineBatcher.h"
#include "Collision/Ray.h"
#include "Engine/World.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysXConversion.h"

#include <PxSceneLock.h>

namespace
{
	// Shape 값은 본 로컬 단위라 메시마다 크기가 달라서 최소값을 배율로 둔다
	constexpr float MinShapeScale = 0.05f;

	FVector ClampShapeScale(const FVector& Scale)
	{
		return FVector(std::max(Scale.X, MinShapeScale), std::max(Scale.Y, MinShapeScale), std::max(Scale.Z, MinShapeScale));
	}

	// 컴포넌트 스케일과 본 스케일이 곱해지면 행렬식 자체가 아주 작아지므로 축 길이 곱에 대한 비율로 본다
	bool IsInvertibleFrame(const FMatrix& M)
	{
		const float AxisProduct = FVector(M.M[0][0], M.M[0][1], M.M[0][2]).Size() *
			FVector(M.M[1][0], M.M[1][1], M.M[1][2]).Size() * FVector(M.M[2][0], M.M[2][1], M.M[2][2]).Size();
		return AxisProduct > 0.0f && std::abs(M.Determinant()) > AxisProduct * 1e-4f;
	}

	// 축 하나만 끌면 그 축만 1에서 벗어나므로 1에서 가장 먼 값을 쓴다
	float DominantScale(float A, float B)
	{
		return std::abs(A - 1.0f) >= std::abs(B - 1.0f) ? A : B;
	}

	// 본 스케일이 섞인 행렬일 수 있으므로 축은 정규화한다
	void SetRefFrame(FConstraintInstance& Constraint, int32 Frame, const FMatrix& BoneLocalFrame)
	{
		const FVector Pos(BoneLocalFrame.M[3][0], BoneLocalFrame.M[3][1], BoneLocalFrame.M[3][2]);
		const FVector Pri = FVector(BoneLocalFrame.M[0][0], BoneLocalFrame.M[0][1], BoneLocalFrame.M[0][2]).GetSafeNormal();
		const FVector Sec = FVector(BoneLocalFrame.M[1][0], BoneLocalFrame.M[1][1], BoneLocalFrame.M[1][2]).GetSafeNormal();
		if (Frame == 0)
		{
			Constraint.Pos1 = Pos;
			Constraint.PriAxis1 = Pri;
			Constraint.SecAxis1 = Sec;
		}
		else
		{
			Constraint.Pos2 = Pos;
			Constraint.PriAxis2 = Pri;
			Constraint.SecAxis2 = Sec;
		}
	}
}

bool FPhysicsAssetEditor::InitializeResources()
{
	// cm 값으로 들어온 메시는 레벨에서처럼 0.01로 줄여야 중력(m/s²)과 크기 비율이 맞는다.
	// 이미 m 크기인 메시(레벨에 스케일 1 근처로 놓는 메시)는 그대로 둔다
	const auto& LODs = Mesh->GetRenderData().LODRenderData;
	const float MeshSize = LODs.IsEmpty() ? 0.0f : (LODs[0].BindAABB.Max - LODs[0].BindAABB.Min).Size();
	PreviewScale = MeshSize > CentimeterMeshMinSize ? CmToWorld : 1.0f;

	if (!InitializePreview())
	{
		return false;
	}

	GizmoTarget = Cast<USceneComponent>(PreviewScene.AddComponent(USceneComponent::StaticClass()));
	if (!GizmoTarget)
	{
		return false;
	}
	GizmoTarget->DetachFromParent();

	if (StandaloneAsset)
	{
		PhysicsAsset = StandaloneAsset;
		// 미리보기 시뮬레이션도 메시 기본 에셋이 아니라 이 에셋의 바디와 관절을 쓴다
		if (MeshComponent) MeshComponent->SetPhysicsAsset(StandaloneAsset);
		return true;
	}

	PhysicsAsset = Mesh->GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		UPhysicsAsset* NewAsset = NewObject<UPhysicsAsset>(Mesh);
		FString Error;
		if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(NewAsset, Mesh, MakeCreateParams(), Error, false))
		{
			HTR_LOG(Warning, "Physics asset generation failed: {} ({})", GetAssetPath(), Error);
		}
		// 실패해도 연결해 둬서 열 때마다 재생성하지 않는다
		Mesh->PhysicsAsset = NewAsset;
		PhysicsAsset = NewAsset;
		bDirty = true;
	}

	return true;
}

FPhysAssetCreateParams FPhysicsAssetEditor::MakeCreateParams() const
{
	FPhysAssetCreateParams Params;
	// 20cm = 사람 메시 대각선(약 256)의 8%. 작게 import된 메시는 같은 비율로 낮춘다
	const FBox& BindBox = Mesh->GetRenderData().LODRenderData[0].BindAABB;
	Params.MinBoneSize = std::min(Params.MinBoneSize, (BindBox.Max - BindBox.Min).Size() * 0.08f);
	return Params;
}

FString FPhysicsAssetEditor::GetSaveFilePath() const
{
	if (StandaloneAsset)
	{
		return StandaloneAssetPath;
	}
	if (!Mesh->PhysicsAssetPath.empty())
	{
		return Mesh->PhysicsAssetPath;
	}
	// USkeletalMesh::GetPhysicsAsset이 읽는 기본 위치와 같아야 다음에 열 때 로드된다
	return (std::filesystem::path("Assets/Physics") /
		(std::filesystem::path(Mesh->GetPath()).stem().string() + ".physicsasset")).generic_string();
}

bool FPhysicsAssetEditor::Save()
{
	if (bSimulating || !PhysicsAsset)
	{
		return false;
	}

	const FString FilePath = GetSaveFilePath();
	PhysicsAsset->PreviewMeshPath = MeshPath;
	FString Error;
	if (!PhysicsAssetFile::Write(FilePath, *PhysicsAsset, Error))
	{
		HTR_LOG(Warning, "Failed to save physics asset: {} ({})", FilePath, Error);
		return false;
	}
	if (!StandaloneAsset)
	{
		Mesh->PhysicsAssetPath = FilePath;
	}
	bDirty = false;
	HTR_LOG(Info, "Saved physics asset: {}", FilePath);
	return true;
}

bool FPhysicsAssetEditor::ConfirmClose()
{
	// RevertFromDisk가 바디 배열을 바꾸기 전에 인스턴스와 짝이 끊기지 않게 먼저 멈춘다
	StopSimulation();
	if (!bDirty)
	{
		return true;
	}

	const int Result = MessageBoxW(GetActiveWindow(), L"Save changes to this physics asset?",
		L"Physics Asset", MB_YESNOCANCEL | MB_ICONQUESTION);
	if (Result == IDCANCEL)
	{
		return false;
	}
	if (Result == IDYES)
	{
		return Save();
	}

	// 메시와 공유하는 객체라 저장 안 한 편집이 남으면 레벨 시뮬레이션에 그대로 쓰인다
	RevertFromDisk();
	return true;
}

void FPhysicsAssetEditor::RevertFromDisk()
{
	const FString FilePath = GetSaveFilePath();
	if (!PhysicsAsset || !std::filesystem::exists(FilePath))
	{
		return;
	}

	FString Error;
	if (!PhysicsAssetFile::Read(FilePath, *PhysicsAsset, Error))
	{
		HTR_LOG(Warning, "Failed to revert physics asset: {} ({})", FilePath, Error);
	}
	SelectionType = EPhysicsAssetSelection::None;
	SelectionIndex = INDEX_NONE;
	bDirty = false;
}

void FPhysicsAssetEditor::ShutdownResources()
{
	StopSimulation();
	PhysicsGizmo.EndDrag();
	PhysicsGizmo.SetTarget(nullptr);
	GizmoTarget = nullptr;
	PhysicsAsset = nullptr;
	SelectionType = EPhysicsAssetSelection::None;
	SelectionIndex = INDEX_NONE;
	FSkeletalPreviewEditor::ShutdownResources();
}

TUniquePtr<IEditorTab> FPhysicsAssetEditor::CreateTab()
{
	return MakeUnique<FPhysicsAssetEditorTab>(*this);
}

int32 FPhysicsAssetEditor::FindBoneIndexByName(FName BoneName) const
{
	return Mesh ? Mesh->FindBoneIndex(BoneName.ToString()) : INDEX_NONE;
}

void FPhysicsAssetEditor::SelectBody(int32 BodyIndex)
{
	if (!PhysicsAsset || BodyIndex < 0 || BodyIndex >= PhysicsAsset->SkeletalBodySetups.Num())
	{
		return;
	}

	SelectBone(FindBoneIndexByName(PhysicsAsset->SkeletalBodySetups[BodyIndex]->BoneName));
	SelectionType = EPhysicsAssetSelection::Body;
	SelectionIndex = BodyIndex;
	SelectedShapeType = EAggCollisionShape::Unknown;
	SelectedShapeIndex = INDEX_NONE;
}

void FPhysicsAssetEditor::SelectShape(int32 BodyIndex, EAggCollisionShape::Type ShapeType, int32 ShapeIndex)
{
	SelectBody(BodyIndex);
	if (GetSelectedBodyIndex() != BodyIndex)
	{
		return;
	}
	SelectedShapeType = ShapeType;
	SelectedShapeIndex = ShapeIndex;
}

void FPhysicsAssetEditor::SelectConstraint(int32 ConstraintIndex)
{
	if (!PhysicsAsset || ConstraintIndex < 0 || ConstraintIndex >= PhysicsAsset->ConstraintSetup.Num())
	{
		return;
	}

	// SelectBone -> OnBoneSelected가 Body 선택으로 바꾸므로 그 뒤에 덮어쓴다
	SelectBone(FindBoneIndexByName(PhysicsAsset->ConstraintSetup[ConstraintIndex]->DefaultInstance.ConstraintBone1));
	SelectionType = EPhysicsAssetSelection::Constraint;
	SelectionIndex = ConstraintIndex;
}

void FPhysicsAssetEditor::ClearSelection()
{
	SelectionType = EPhysicsAssetSelection::None;
	SelectionIndex = INDEX_NONE;
}

bool FPhysicsAssetEditor::IsBoneChildOf(int32 BoneIndex, int32 AncestorBoneIndex) const
{
	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
	for (int32 Parent = Bones[BoneIndex].ParentIndex; Parent >= 0; Parent = Bones[Parent].ParentIndex)
	{
		if (Parent == AncestorBoneIndex)
		{
			return true;
		}
	}
	return false;
}

int32 FPhysicsAssetEditor::FindControllingBodyIndex(int32 BoneIndex) const
{
	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
	for (int32 Bone = BoneIndex; Bone >= 0; Bone = Bones[Bone].ParentIndex)
	{
		const int32 BodyIndex = PhysicsAsset->FindBodyIndex(FName(Bones[Bone].Name));
		if (BodyIndex != INDEX_NONE)
		{
			return BodyIndex;
		}
	}
	return INDEX_NONE;
}

int32 FPhysicsAssetEditor::FindChildConstraintIndex(FName ChildBoneName) const
{
	for (int32 i = 0; i < PhysicsAsset->ConstraintSetup.Num(); ++i)
	{
		if (PhysicsAsset->ConstraintSetup[i]->DefaultInstance.ConstraintBone1 == ChildBoneName)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void FPhysicsAssetEditor::InitConstraintSetup(UPhysicsConstraintTemplate* Template, int32 ChildBodyIndex, int32 ParentBodyIndex, bool bKeepProfile)
{
	const int32 ChildBone = FindBoneIndexByName(PhysicsAsset->SkeletalBodySetups[ChildBodyIndex]->BoneName);
	const int32 ParentBone = FindBoneIndexByName(PhysicsAsset->SkeletalBodySetups[ParentBodyIndex]->BoneName);
	if (ChildBone == INDEX_NONE || ParentBone == INDEX_NONE)
	{
		return;
	}

	FConstraintInstance& Constraint = Template->DefaultInstance;
	const FConstraintProfileProperties Profile = Constraint.ProfileInstance;
	FPhysicsAssetUtils::InitConstraintFromBones(Constraint, Mesh->GetRefSkeleton(), ChildBone, ParentBone, FPhysAssetCreateParams());
	if (bKeepProfile)
	{
		Constraint.ProfileInstance = Profile;
	}
	PhysicsAsset->DisableCollision(ChildBodyIndex, ParentBodyIndex);
}

void FPhysicsAssetEditor::MakeNewBody(int32 BoneIndex)
{
	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
	if (bSimulating || !PhysicsAsset || BoneIndex < 0 || BoneIndex >= Bones.Num())
	{
		return;
	}
	const FName BoneName(Bones[BoneIndex].Name);
	if (PhysicsAsset->FindBodyIndex(BoneName) != INDEX_NONE)
	{
		return;
	}

	const int32 ParentBodyIndex = FindControllingBodyIndex(BoneIndex);
	const int32 NewBodyIndex = FPhysicsAssetUtils::CreateNewBody(PhysicsAsset, BoneName, FPhysAssetCreateParams());
	if (!FPhysicsAssetUtils::CreateCollisionFromBone(PhysicsAsset->SkeletalBodySetups[NewBodyIndex], Mesh, BoneIndex))
	{
		FPhysicsAssetUtils::DestroyBody(PhysicsAsset, NewBodyIndex);
		HTR_LOG(Warning, "Cannot add a body: bone has no skinned vertices: {}", Bones[BoneIndex].Name);
		return;
	}

	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		if (!IsBoneChildOf(i, BoneIndex))
		{
			continue;
		}
		const int32 ChildBodyIndex = PhysicsAsset->FindBodyIndex(FName(Bones[i].Name));
		if (ChildBodyIndex == INDEX_NONE)
		{
			continue;
		}

		int32 ConstraintIndex = FindChildConstraintIndex(FName(Bones[i].Name));
		const bool bNewConstraint = ConstraintIndex == INDEX_NONE;
		if (bNewConstraint)
		{
			ConstraintIndex = FPhysicsAssetUtils::CreateNewConstraint(PhysicsAsset, FName(Bones[i].Name));
		}
		else
		{
			const FName OldParentName = PhysicsAsset->ConstraintSetup[ConstraintIndex]->DefaultInstance.ConstraintBone2;
			const int32 OldParentBone = FindBoneIndexByName(OldParentName);
			if (OldParentBone != INDEX_NONE && IsBoneChildOf(OldParentBone, BoneIndex))
			{
				continue;
			}
			const int32 OldParentBody = PhysicsAsset->FindBodyIndex(OldParentName);
			if (OldParentBody != INDEX_NONE && OldParentBody == ParentBodyIndex)
			{
				PhysicsAsset->EnableCollision(ChildBodyIndex, OldParentBody);
			}
		}
		InitConstraintSetup(PhysicsAsset->ConstraintSetup[ConstraintIndex], ChildBodyIndex, NewBodyIndex, !bNewConstraint);
	}

	if (ParentBodyIndex != INDEX_NONE)
	{
		const int32 ConstraintIndex = FPhysicsAssetUtils::CreateNewConstraint(PhysicsAsset, BoneName);
		InitConstraintSetup(PhysicsAsset->ConstraintSetup[ConstraintIndex], NewBodyIndex, ParentBodyIndex, false);
	}

	SelectBody(NewBodyIndex);
	MarkDirty();
}

void FPhysicsAssetEditor::DeleteBody(int32 BodyIndex)
{
	if (bSimulating || !PhysicsAsset || BodyIndex < 0 || BodyIndex >= PhysicsAsset->SkeletalBodySetups.Num())
	{
		return;
	}

	const FName BoneName = PhysicsAsset->SkeletalBodySetups[BodyIndex]->BoneName;
	const int32 BoneIndex = FindBoneIndexByName(BoneName);
	const int32 ParentBone = BoneIndex != INDEX_NONE ? Mesh->GetRefSkeleton()[BoneIndex].ParentIndex : INDEX_NONE;
	const int32 ParentBodyIndex = ParentBone >= 0 ? FindControllingBodyIndex(ParentBone) : INDEX_NONE;
	if (ParentBodyIndex != INDEX_NONE)
	{
		TArray<int32> Constraints;
		PhysicsAsset->BodyFindConstraints(BodyIndex, Constraints);
		for (int32 ConstraintIndex : Constraints)
		{
			UPhysicsConstraintTemplate* Constraint = PhysicsAsset->ConstraintSetup[ConstraintIndex];
			if (!(Constraint->DefaultInstance.ConstraintBone2 == BoneName))
			{
				continue;
			}
			const FName ChildName = Constraint->DefaultInstance.ConstraintBone1;
			const int32 ChildBodyIndex = PhysicsAsset->FindBodyIndex(ChildName);
			if (ChildBodyIndex == INDEX_NONE)
			{
				continue;
			}
			const int32 NewConstraintIndex = FPhysicsAssetUtils::CreateNewConstraint(PhysicsAsset, ChildName, Constraint);
			InitConstraintSetup(PhysicsAsset->ConstraintSetup[NewConstraintIndex], ChildBodyIndex, ParentBodyIndex, true);
		}
	}

	FPhysicsAssetUtils::DestroyBody(PhysicsAsset, BodyIndex);
	ClearSelection();
	MarkDirty();
}

void FPhysicsAssetEditor::DeleteConstraint(int32 ConstraintIndex)
{
	if (bSimulating || !PhysicsAsset || ConstraintIndex < 0 || ConstraintIndex >= PhysicsAsset->ConstraintSetup.Num())
	{
		return;
	}

	FPhysicsAssetUtils::DestroyConstraint(PhysicsAsset, ConstraintIndex);
	ClearSelection();
	MarkDirty();
}

void FPhysicsAssetEditor::RegenerateBody(int32 BodyIndex)
{
	if (bSimulating || !PhysicsAsset || BodyIndex < 0 || BodyIndex >= PhysicsAsset->SkeletalBodySetups.Num())
	{
		return;
	}

	USkeletalBodySetup* BodySetup = PhysicsAsset->SkeletalBodySetups[BodyIndex];
	const int32 BoneIndex = FindBoneIndexByName(BodySetup->BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return;
	}

	const FKAggregateGeom OldGeom = BodySetup->AggGeom;
	BodySetup->AggGeom = FKAggregateGeom();
	if (!FPhysicsAssetUtils::CreateCollisionFromBone(BodySetup, Mesh, BoneIndex, MakeCreateParams().VertWeight))
	{
		BodySetup->AggGeom = OldGeom;
		HTR_LOG(Warning, "Cannot regenerate a body: bone has no skinned vertices: {}", Mesh->GetRefSkeleton()[BoneIndex].Name);
		return;
	}

	SelectBody(BodyIndex);
	MarkDirty();
}

void FPhysicsAssetEditor::RegenerateAllBodies()
{
	if (bSimulating || !PhysicsAsset)
	{
		return;
	}

	const int Result = MessageBoxW(GetActiveWindow(),
		L"Delete all bodies and constraints and generate them again?\nShape, constraint and collision edits will be lost.",
		L"Physics Asset", MB_OKCANCEL | MB_ICONWARNING);
	if (Result != IDOK)
	{
		return;
	}

	ClearSelection();
	for (int32 BodyIndex = PhysicsAsset->SkeletalBodySetups.Num() - 1; BodyIndex >= 0; --BodyIndex)
	{
		FPhysicsAssetUtils::DestroyBody(PhysicsAsset, BodyIndex);
	}
	for (int32 ConstraintIndex = PhysicsAsset->ConstraintSetup.Num() - 1; ConstraintIndex >= 0; --ConstraintIndex)
	{
		FPhysicsAssetUtils::DestroyConstraint(PhysicsAsset, ConstraintIndex);
	}
	PhysicsAsset->CollisionDisableTable.Empty();

	FString Error;
	if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(PhysicsAsset, Mesh, MakeCreateParams(), Error, false))
	{
		HTR_LOG(Warning, "Physics asset generation failed: {} ({})", GetAssetPath(), Error);
	}
	MarkDirty();
}

bool FPhysicsAssetEditor::HasConstraintBetween(int32 BodyIndexA, int32 BodyIndexB) const
{
	const FName BoneA = PhysicsAsset->SkeletalBodySetups[BodyIndexA]->BoneName;
	const FName BoneB = PhysicsAsset->SkeletalBodySetups[BodyIndexB]->BoneName;
	return PhysicsAsset->FindConstraintIndex(BoneA, BoneB) != INDEX_NONE ||
		PhysicsAsset->FindConstraintIndex(BoneB, BoneA) != INDEX_NONE;
}

void FPhysicsAssetEditor::MakeNewConstraint(int32 ChildBodyIndex, int32 ParentBodyIndex)
{
	const int32 BodyCount = PhysicsAsset ? PhysicsAsset->SkeletalBodySetups.Num() : 0;
	if (bSimulating || ChildBodyIndex < 0 || ChildBodyIndex >= BodyCount || ParentBodyIndex < 0 ||
		ParentBodyIndex >= BodyCount || ChildBodyIndex == ParentBodyIndex || HasConstraintBetween(ChildBodyIndex, ParentBodyIndex))
	{
		return;
	}

	const int32 ConstraintIndex = FPhysicsAssetUtils::CreateNewConstraint(PhysicsAsset, PhysicsAsset->SkeletalBodySetups[ChildBodyIndex]->BoneName);
	InitConstraintSetup(PhysicsAsset->ConstraintSetup[ConstraintIndex], ChildBodyIndex, ParentBodyIndex, false);
	SelectConstraint(ConstraintIndex);
	MarkDirty();
}

void FPhysicsAssetEditor::AddShape(int32 BodyIndex, EAggCollisionShape::Type ShapeType)
{
	if (bSimulating || !PhysicsAsset || BodyIndex < 0 || BodyIndex >= PhysicsAsset->SkeletalBodySetups.Num())
	{
		return;
	}

	const int32 BoneIndex = FindBoneIndexByName(PhysicsAsset->SkeletalBodySetups[BodyIndex]->BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return;
	}

	// 15cm = 사람 메시 대각선(약 256)의 6%. 컴포넌트 공간 크기를 InverseBindPose의 스케일로 본 로컬 단위로 바꾼다
	const FBox& BindBox = Mesh->GetRenderData().LODRenderData[0].BindAABB;
	const FMatrix& InverseBindPose = Mesh->GetRefSkeleton()[BoneIndex].InverseBindPose;
	const float ComponentToBoneScale = FVector(InverseBindPose.M[0][0], InverseBindPose.M[0][1], InverseBindPose.M[0][2]).Size();
	const float Size = (BindBox.Max - BindBox.Min).Size() * 0.06f * ComponentToBoneScale;

	FKAggregateGeom& Geom = PhysicsAsset->SkeletalBodySetups[BodyIndex]->AggGeom;
	int32 NewIndex = INDEX_NONE;
	switch (ShapeType)
	{
	case EAggCollisionShape::Sphere:
	{
		FKSphereElem Elem;
		Elem.Radius = Size;
		NewIndex = Geom.SphereElems.Num();
		Geom.SphereElems.Add(Elem);
		break;
	}
	case EAggCollisionShape::Box:
	{
		FKBoxElem Elem;
		Elem.X = Elem.Y = Elem.Z = Size * 0.5f;
		NewIndex = Geom.BoxElems.Num();
		Geom.BoxElems.Add(Elem);
		break;
	}
	case EAggCollisionShape::Sphyl:
	{
		FKSphylElem Elem;
		Elem.Radius = Size;
		Elem.Length = Size;
		NewIndex = Geom.SphylElems.Num();
		Geom.SphylElems.Add(Elem);
		break;
	}
	default:
		return;
	}

	SelectShape(BodyIndex, ShapeType, NewIndex);
	MarkDirty();
}

void FPhysicsAssetEditor::DeleteShape(int32 BodyIndex, EAggCollisionShape::Type ShapeType, int32 ShapeIndex)
{
	if (bSimulating || !PhysicsAsset || BodyIndex < 0 || BodyIndex >= PhysicsAsset->SkeletalBodySetups.Num())
	{
		return;
	}

	FKAggregateGeom& Geom = PhysicsAsset->SkeletalBodySetups[BodyIndex]->AggGeom;
	const auto RemoveAt = [ShapeIndex](auto& Elems)
	{
		if (ShapeIndex < 0 || ShapeIndex >= Elems.Num())
		{
			return false;
		}
		Elems.RemoveAt(ShapeIndex);
		return true;
	};

	bool bRemoved = false;
	switch (ShapeType)
	{
	case EAggCollisionShape::Sphere: bRemoved = RemoveAt(Geom.SphereElems); break;
	case EAggCollisionShape::Box:    bRemoved = RemoveAt(Geom.BoxElems); break;
	case EAggCollisionShape::Sphyl:  bRemoved = RemoveAt(Geom.SphylElems); break;
	case EAggCollisionShape::Convex: bRemoved = RemoveAt(Geom.ConvexElems); break;
	default: break;
	}
	if (!bRemoved)
	{
		return;
	}

	if (Geom.GetElementCount() == 0)
	{
		DeleteBody(BodyIndex);
		return;
	}
	SelectBody(BodyIndex);
	MarkDirty();
}

void FPhysicsAssetEditor::OnBoneSelected()
{
	PhysicsGizmo.EndDrag();
	SelectionType = EPhysicsAssetSelection::None;
	SelectionIndex = INDEX_NONE;
	SelectedShapeType = EAggCollisionShape::Unknown;
	SelectedShapeIndex = INDEX_NONE;
	if (!PhysicsAsset || !Mesh || SelectedBoneIndex < 0)
	{
		return;
	}

	const int32 BodyIndex = PhysicsAsset->FindBodyIndex(FName(Mesh->GetRefSkeleton()[SelectedBoneIndex].Name));
	if (BodyIndex != INDEX_NONE)
	{
		SelectionType = EPhysicsAssetSelection::Body;
		SelectionIndex = BodyIndex;
	}
}

void FPhysicsAssetEditor::OnRenderLine(FLineBatcher& LineBatcher)
{
	FSkeletalPreviewEditor::OnRenderLine(LineBatcher);

	if (!PhysicsAsset || !MeshComponent)
	{
		return;
	}

	FPhysicsDebugDraw DebugDraw;
	if (bShowBodies)
	{
		DebugDraw.DrawPhysicsAssetBodies(&LineBatcher, MeshComponent, *PhysicsAsset, false, GetSelectedBodyIndex(), true);
	}
	if (bShowConstraints)
	{
		DebugDraw.DrawPhysicsAssetConstraints(&LineBatcher, MeshComponent, *PhysicsAsset, GetSelectedConstraintIndex(), ConstraintDrawSize);
	}

	if (bSimulating)
	{
		if (MouseHandle.IsGrabbing())
		{
			const FVector4 GrabColor(1.0f, 0.2f, 0.2f, 1.0f);
			const FBox Bounds = GetPreviewBounds();
			LineBatcher.AddLine(MouseHandle.GetGrabbedLocation(), MouseHandle.GetTargetLocation(), GrabColor);
			LineBatcher.AddSphere(MouseHandle.GetTargetLocation(), (Bounds.Max - Bounds.Min).Size() * 0.005f, GrabColor, 8);
		}
	}
}

void FPhysicsAssetEditor::ToggleSimulation()
{
	bSimulating ? StopSimulation() : StartSimulation();
}

void FPhysicsAssetEditor::StartSimulation()
{
	UWorld* World = PreviewScene.GetWorld();
	FPhysicsScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	if (bSimulating || !MeshComponent || !PhysicsAsset || !Scene)
	{
		return;
	}

	PhysicsGizmo.EndDrag();
	PreSimulationComponentWorld = MeshComponent->GetWorldMatrix();

	// 프리뷰 액터는 충돌이 꺼진 채 만들어진다. 켜지 않으면 바디 Shape가 시뮬레이션에서 빠져 바닥을 뚫는다
	if (AActor* Owner = MeshComponent->GetOwner())
		Owner->SetActorEnableCollision(true);

	// 인스턴스는 에디터를 열 때 만들어져 그 뒤의 에셋 편집이 빠져 있다
	MeshComponent->RecreatePhysicsState();
	if (MeshComponent->Bodies.IsEmpty())
	{
		HTR_LOG(Warning, "Cannot simulate: no physics bodies were created for {}", GetAssetPath());
		if (AActor* Owner = MeshComponent->GetOwner())
			Owner->SetActorEnableCollision(false);
		return;
	}

	CreateSimulationFloor(*Scene);
	MeshComponent->SetSimulatePhysics(true);
	Scene->ResetStepTime();
	World->bShouldSimulatePhysics = true;
	bSimulating = true;
}

void FPhysicsAssetEditor::StopSimulation()
{
	if (!bSimulating)
	{
		return;
	}
	bSimulating = false;
	MouseHandle.ReleaseComponent();

	if (UWorld* World = PreviewScene.GetWorld())
	{
		World->bShouldSimulatePhysics = false;
	}
	FloorBody.TermBody();
	if (FloorMeshComponent)
		FloorMeshComponent->SetVisibility(false);

	if (MeshComponent)
	{
		// SetSimulatePhysics(false)가 컴포넌트를 세워 옮기므로 그 뒤에 원위치로 덮어쓴다
		MeshComponent->SetSimulatePhysics(false);
		if (AActor* Owner = MeshComponent->GetOwner())
			Owner->SetActorEnableCollision(false);
		MeshComponent->SetWorldTransform(PreSimulationComponentWorld, false);
		MeshComponent->ResetPose();
		MeshComponent->RefreshBoneTransformsNow();
	}
}

void FPhysicsAssetEditor::CreateSimulationFloor(FPhysicsScene& Scene)
{
	if (!FloorBodySetup)
	{
		FloorBodySetup = NewObject<UBodySetup>(Mesh);
		FloorBodySetup->AggGeom.BoxElems.Add(FKBoxElem());
	}

	// 바디 형상의 가장 낮은 면에 메시보다 훨씬 넓은 판을 깐다.
	// CalcBounds는 컬링용이라 스키닝 바운드에 25% 여유를 더해 발보다 한참 아래까지 내려간다
	FBox Bounds{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX) };
	for (const FBodyInstance* Body : MeshComponent->Bodies)
	{
		if (!Body || !Body->IsValidBodyInstance()) continue;
		const FBox BodyBounds = Body->GetBodyBounds();
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Bounds.Min[Axis] = std::min(Bounds.Min[Axis], BodyBounds.Min[Axis]);
			Bounds.Max[Axis] = std::max(Bounds.Max[Axis], BodyBounds.Max[Axis]);
		}
	}
	if (Bounds.Min.X > Bounds.Max.X)
		Bounds = MeshComponent->CalcBounds();
	const float MeshSize = (Bounds.Max - Bounds.Min).Size();
	FKBoxElem& Box = FloorBodySetup->AggGeom.BoxElems[0];
	Box.X = MeshSize * 10.0f;
	Box.Y = MeshSize * 10.0f;
	Box.Z = MeshSize * 0.1f;

	const FVector HalfExtent(Box.X * 0.5f, Box.Y * 0.5f, Box.Z * 0.5f);
	const FVector Center((Bounds.Min.X + Bounds.Max.X) * 0.5f, (Bounds.Min.Y + Bounds.Max.Y) * 0.5f, Bounds.Min.Z - HalfExtent.Z);
	if (!FloorBody.InitBody(*FloorBodySetup, FTransform(FRotator(0, 0, 0), Center), nullptr, Scene, EPhysicsBodyType::Static))
	{
		HTR_LOG(Warning, "Failed to create the simulation floor");
	}

	// 충돌은 FloorBody가 맡고, 메시는 같은 크기의 Cube(한 변 1)로 보여주기만 한다
	if (!FloorMeshComponent)
		FloorMeshComponent = Cast<UStaticMeshComponent>(PreviewScene.AddComponent(UStaticMeshComponent::StaticClass()));
	if (FloorMeshComponent)
	{
		FloorMeshComponent->SetWorldTransform(FTransform(FRotator(0, 0, 0), Center, FVector(Box.X, Box.Y, Box.Z)).GetLocalMatrix(), false);
		FloorMeshComponent->SetVisibility(true);
	}
}

void FPhysicsAssetEditor::TickAfterWorld(float DeltaTime)
{
	if (bSimulating)
	{
		PhysicsGizmo.EndDrag();
		PhysicsGizmo.SetTarget(nullptr);
		UpdateGrab();
		return;
	}

	if (!UpdatePhysicsGizmo())
	{
		UpdatePhysicsPicking();
	}
	SyncPhysicsGizmo();
}

void FPhysicsAssetEditor::UpdateGrab()
{
	if (MouseHandle.IsGrabbing())
	{
		if (!PointerInput.bRightMouseDown || PointerInput.bCancel || !CanProcessViewportInput())
		{
			MouseHandle.ReleaseComponent();
			return;
		}

		if (CameraInput.Wheel != 0.0f)
		{
			const FBox Bounds = GetPreviewBounds();
			const float Step = (Bounds.Max - Bounds.Min).Size() * 0.02f;
			GrabDepth = std::max(GrabDepth + CameraInput.Wheel / 120.0f * Step, Step);
		}
		MoveGrab();
		return;
	}

	const bool bGrabPressed = CanProcessViewportInput() && MeshComponent && PhysicsAsset &&
		PointerInput.bRightMousePressed && (PointerInput.bCtrlDown || PointerInput.bShiftDown);
	if (!bGrabPressed)
	{
		return;
	}

	UWorld* World = PreviewScene.GetWorld();
	FPhysicsScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	physx::PxScene* NativeScene = Scene ? Scene->GetPxScene() : nullptr;
	if (!NativeScene)
	{
		return;
	}

	const FVector2 MousePosition(PointerInput.MouseUV.X * ViewportWidth, PointerInput.MouseUV.Y * ViewportHeight);
	const FRay Ray = ViewportClient->DeprojectScreenToWorld(MousePosition);
	const physx::PxVec3 Origin = U2PVector(Ray.Origin);
	const physx::PxVec3 UnitDir = U2PVector(Ray.Direction.GetSafeNormal());
	if (!Origin.isFinite() || !UnitDir.isNormalized())
	{
		return;
	}

	// 바닥은 정적 액터라 eDYNAMIC만 검사하면 바디만 맞는다
	physx::PxRaycastBuffer Hits;
	{
		const physx::PxSceneReadLock ReadLock(*NativeScene);
		if (!NativeScene->raycast(Origin, UnitDir, 1.0e6f, Hits, physx::PxHitFlag::eDEFAULT,
			physx::PxQueryFilterData(physx::PxQueryFlag::eDYNAMIC)) || !Hits.hasBlock)
		{
			return;
		}
	}

	int32 BodyIndex = INDEX_NONE;
	for (int32 i = 0; i < MeshComponent->Bodies.Num(); ++i)
	{
		if (MeshComponent->Bodies[i] && MeshComponent->Bodies[i]->GetRigidDynamic() == Hits.block.actor)
		{
			BodyIndex = i;
			break;
		}
	}
	const FVector HitLocation = P2UVector(Hits.block.position);
	if (BodyIndex != INDEX_NONE && MouseHandle.GrabComponentAtLocation(*Scene, *MeshComponent->Bodies[BodyIndex], HitLocation))
	{
		const FVector Forward = ViewportClient->GetViewRotation().Quaternion().GetForwardVector();
		GrabDepth = (HitLocation - ViewportClient->GetViewLocation()).Dot(Forward);
	}
}

void FPhysicsAssetEditor::MoveGrab()
{
	const FVector2 MousePosition(PointerInput.MouseUV.X * ViewportWidth, PointerInput.MouseUV.Y * ViewportHeight);
	const FRay Ray = ViewportClient->DeprojectScreenToWorld(MousePosition);
	const FVector Forward = ViewportClient->GetViewRotation().Quaternion().GetForwardVector();
	const float RayDepth = Ray.Direction.Dot(Forward);
	if (RayDepth < 1e-4f)
	{
		return;
	}

	// 잡은 점과 같은 깊이의 카메라 평면에 마우스 레이를 투영한다 (직교 뷰도 같은 식)
	const float T = (GrabDepth - (Ray.Origin - ViewportClient->GetViewLocation()).Dot(Forward)) / RayDepth;
	MouseHandle.SetTargetLocation(Ray.PointAt(T));
}

void FPhysicsAssetEditor::UpdatePhysicsPicking()
{
	const bool bClicked = CanProcessViewportInput() && MeshComponent && PhysicsAsset &&
		PointerInput.bHovered && PointerInput.bMousePressed && !PointerInput.bCancel &&
		!CameraInput.bCaptured && ViewportWidth > 0 && ViewportHeight > 0;
	if (bClicked)
	{
		const FVector2 MousePosition(PointerInput.MouseUV.X * ViewportWidth, PointerInput.MouseUV.Y * ViewportHeight);
		EPhysicsAssetSelection HitType = EPhysicsAssetSelection::None;
		int32 HitIndex = INDEX_NONE;
		EAggCollisionShape::Type HitShapeType = EAggCollisionShape::Unknown;
		int32 HitShapeIndex = INDEX_NONE;
		if (PickPhysics(ViewportClient->DeprojectScreenToWorld(MousePosition), HitType, HitIndex, HitShapeType, HitShapeIndex))
		{
			HitType == EPhysicsAssetSelection::Body ? SelectShape(HitIndex, HitShapeType, HitShapeIndex) : SelectConstraint(HitIndex);
			return;
		}
	}

	// 아무것도 맞지 않으면 본을 고른다
	UpdateBonePicking();
}

void FPhysicsAssetEditor::SetGizmoMode(UE::Widget::EWidgetMode Mode)
{
	PhysicsGizmo.EndDrag();
	ModeTools.SetWidgetMode(Mode);
	SyncPhysicsGizmo();
}

USkeletalBodySetup* FPhysicsAssetEditor::GetSelectedBodySetup() const
{
	const int32 BodyIndex = GetSelectedBodyIndex();
	if (!PhysicsAsset || BodyIndex < 0 || BodyIndex >= PhysicsAsset->SkeletalBodySetups.Num())
	{
		return nullptr;
	}
	return PhysicsAsset->SkeletalBodySetups[BodyIndex];
}

bool FPhysicsAssetEditor::ResolveSelectedShape(EAggCollisionShape::Type& OutType, int32& OutIndex) const
{
	const USkeletalBodySetup* BodySetup = GetSelectedBodySetup();
	if (!BodySetup)
	{
		return false;
	}

	const FKAggregateGeom& Geom = BodySetup->AggGeom;
	const int32 Index = GetSelectedShapeIndex();
	switch (GetSelectedShapeType())
	{
	case EAggCollisionShape::Sphere: OutType = EAggCollisionShape::Sphere; OutIndex = Index; return Index >= 0 && Index < Geom.SphereElems.Num();
	case EAggCollisionShape::Box:    OutType = EAggCollisionShape::Box;    OutIndex = Index; return Index >= 0 && Index < Geom.BoxElems.Num();
	case EAggCollisionShape::Sphyl:  OutType = EAggCollisionShape::Sphyl;  OutIndex = Index; return Index >= 0 && Index < Geom.SphylElems.Num();
	default: break;
	}

	OutIndex = 0;
	if (!Geom.SphylElems.IsEmpty())
	{
		OutType = EAggCollisionShape::Sphyl;
		return true;
	}
	if (!Geom.BoxElems.IsEmpty())
	{
		OutType = EAggCollisionShape::Box;
		return true;
	}
	if (!Geom.SphereElems.IsEmpty())
	{
		OutType = EAggCollisionShape::Sphere;
		return true;
	}
	return false;
}

bool FPhysicsAssetEditor::GetSelectedShapeLocal(FVector& OutCenter, FQuat& OutRotation) const
{
	EAggCollisionShape::Type ShapeType;
	int32 ShapeIndex;
	if (!ResolveSelectedShape(ShapeType, ShapeIndex))
	{
		return false;
	}

	const FKAggregateGeom& Geom = GetSelectedBodySetup()->AggGeom;
	switch (ShapeType)
	{
	case EAggCollisionShape::Sphyl:
		OutCenter = Geom.SphylElems[ShapeIndex].Center;
		OutRotation = Geom.SphylElems[ShapeIndex].Rotation.Quaternion();
		return true;
	case EAggCollisionShape::Box:
		OutCenter = Geom.BoxElems[ShapeIndex].Center;
		OutRotation = Geom.BoxElems[ShapeIndex].Rotation.Quaternion();
		return true;
	case EAggCollisionShape::Sphere:
		OutCenter = Geom.SphereElems[ShapeIndex].Center;
		OutRotation = FQuat::Identity();
		return true;
	default:
		return false;
	}
}

UPhysicsConstraintTemplate* FPhysicsAssetEditor::GetSelectedConstraintTemplate() const
{
	const int32 ConstraintIndex = GetSelectedConstraintIndex();
	if (!PhysicsAsset || ConstraintIndex < 0 || ConstraintIndex >= PhysicsAsset->ConstraintSetup.Num())
	{
		return nullptr;
	}
	return PhysicsAsset->ConstraintSetup[ConstraintIndex];
}

bool FPhysicsAssetEditor::GetShapeGizmoTransform(FTransform& OutTransform) const
{
	const USkeletalBodySetup* BodySetup = GetSelectedBodySetup();
	FTransform BoneWorld;
	FVector Center;
	FQuat Rotation;
	if (!BodySetup ||
		!FPhysicsDebugDraw().GetBoneWorldTransform(MeshComponent, BodySetup->BoneName.ToString(), BoneWorld) ||
		!GetSelectedShapeLocal(Center, Rotation))
	{
		return false;
	}

	OutTransform = FTransform((BoneWorld.Rotation * Rotation).Normalized(), BoneWorld.GetLocalMatrix().TransformPosition(Center));
	return true;
}

bool FPhysicsAssetEditor::GetConstraintGizmoTransform(FTransform& OutTransform) const
{
	const UPhysicsConstraintTemplate* Template = GetSelectedConstraintTemplate();
	FMatrix ParentFrameWorld, ChildFrameWorld;
	if (!Template || ModeTools.GetWidgetMode() == UE::Widget::WM_Scale ||
		!FPhysicsDebugDraw().GetConstraintFrames(MeshComponent, Template->DefaultInstance, ParentFrameWorld, ChildFrameWorld))
	{
		return false;
	}

	// 본 스케일이 프레임 축에 섞이지 않도록 위치와 회전만 쓴다
	const FTransform ParentFrame = FTransform::FromMatrix(ParentFrameWorld);
	OutTransform = FTransform(ParentFrame.Rotation, ParentFrame.Location);
	return true;
}

void FPhysicsAssetEditor::SyncPhysicsGizmo()
{
	FTransform GizmoTransform;
	const bool bHasTarget = MeshComponent && GizmoTarget &&
		(SelectionType == EPhysicsAssetSelection::Constraint
			? GetConstraintGizmoTransform(GizmoTransform)
			: GetShapeGizmoTransform(GizmoTransform));
	if (!bHasTarget)
	{
		PhysicsGizmo.SetTarget(nullptr);
		return;
	}

	if (PhysicsGizmo.IsUsing())
	{
		return;
	}

	// 스케일은 1로 두고 드래그 동안 늘어난 배율만 읽는다
	GizmoTarget->SetTransform(GizmoTransform);
	PhysicsGizmo.SetTarget(GizmoTarget);
}

bool FPhysicsAssetEditor::BeginShapeDrag()
{
	USkeletalBodySetup* BodySetup = GetSelectedBodySetup();
	FTransform BoneWorld;
	if (!BodySetup || !FPhysicsDebugDraw().GetBoneWorldTransform(MeshComponent, BodySetup->BoneName.ToString(), BoneWorld))
	{
		return false;
	}

	const FMatrix BoneMatrix = BoneWorld.GetLocalMatrix();
	if (!IsInvertibleFrame(BoneMatrix))
	{
		return false;
	}

	DragStartGeom = BodySetup->AggGeom;
	DragBoneWorldInverse = BoneMatrix.Inverse();
	DragBoneWorldRotation = BoneWorld.Rotation;
	return true;
}

bool FPhysicsAssetEditor::BeginConstraintDrag()
{
	UPhysicsConstraintTemplate* Template = GetSelectedConstraintTemplate();
	FTransform ParentFrame;
	FMatrix ParentFrameWorld, ChildFrameWorld;
	if (!Template || !GetConstraintGizmoTransform(ParentFrame) ||
		!FPhysicsDebugDraw().GetConstraintFrames(MeshComponent, Template->DefaultInstance, ParentFrameWorld, ChildFrameWorld))
	{
		return false;
	}

	const int32 ParentBone = FindBoneIndexByName(Template->DefaultInstance.ConstraintBone2);
	const int32 ChildBone = FindBoneIndexByName(Template->DefaultInstance.ConstraintBone1);
	if (ParentBone == INDEX_NONE || ChildBone == INDEX_NONE)
	{
		return false;
	}

	const FMatrix ParentBoneWorld = MeshComponent->GetBoneWorldMatrix(ParentBone);
	const FMatrix ChildBoneWorld = MeshComponent->GetBoneWorldMatrix(ChildBone);
	if (!IsInvertibleFrame(ParentBoneWorld) || !IsInvertibleFrame(ChildBoneWorld))
	{
		return false;
	}

	DragStartConstraint = Template->DefaultInstance;
	DragBoneWorldInverse = ParentBoneWorld.Inverse();
	DragChildBoneWorldInverse = ChildBoneWorld.Inverse();
	DragChildFrameRelative = ChildFrameWorld * ParentFrame.GetLocalMatrix().Inverse();
	return true;
}

bool FPhysicsAssetEditor::UpdatePhysicsGizmo()
{
	if (!CanProcessViewportInput() || !PhysicsGizmo.GetTarget())
	{
		PhysicsGizmo.EndDrag();
		return false;
	}

	const bool bConstraint = SelectionType == EPhysicsAssetSelection::Constraint;
	if (PointerInput.bCancel)
	{
		if (PhysicsGizmo.IsUsing())
		{
			if (UPhysicsConstraintTemplate* Template = bConstraint ? GetSelectedConstraintTemplate() : nullptr)
			{
				Template->DefaultInstance = DragStartConstraint;
			}
			else if (USkeletalBodySetup* BodySetup = GetSelectedBodySetup())
			{
				BodySetup->AggGeom = DragStartGeom;
			}
		}
		PhysicsGizmo.EndDrag();
		return false;
	}

	const bool bWasUsing = PhysicsGizmo.IsUsing();
	if ((!PointerInput.bHovered && !bWasUsing) || CameraInput.bCaptured)
	{
		PhysicsGizmo.EndDrag();
		return false;
	}

	SyncPhysicsGizmo();
	const int32 Width = static_cast<int32>(ViewportClient->GetWidth());
	const int32 Height = static_cast<int32>(ViewportClient->GetHeight());
	const FVector2 MousePosition(PointerInput.MouseUV.X * Width, PointerInput.MouseUV.Y * Height);
	const FRay MouseRay = ViewportClient->DeprojectScreenToWorld(MousePosition);

	const bool bUpdateDrag = bWasUsing || (PointerInput.bMousePressed && PointerInput.bMouseDown);
	PhysicsGizmo.Update(MouseRay, MousePosition, ViewportClient->GetViewProjectionMatrix(),
		Width, Height, bUpdateDrag, ViewportClient->GetViewLocation(), !ViewportClient->IsPerspective());

	if (!bWasUsing && PhysicsGizmo.IsUsing() && !(bConstraint ? BeginConstraintDrag() : BeginShapeDrag()))
	{
		PhysicsGizmo.EndDrag();
		return true;
	}

	if (PhysicsGizmo.IsUsing())
	{
		bConstraint ? ApplyConstraintTransform() : ApplyShapeTransform();
		if (!PointerInput.bMouseDown)
		{
			PhysicsGizmo.EndDrag();
		}
	}

	return bWasUsing || PhysicsGizmo.IsUsing() || PhysicsGizmo.GetHoveredAxis() >= 0;
}

void FPhysicsAssetEditor::ApplyConstraintTransform()
{
	UPhysicsConstraintTemplate* Template = GetSelectedConstraintTemplate();
	if (!Template)
	{
		return;
	}

	// 부모 프레임을 기즈모로 옮기고, 자식 프레임은 드래그 시작 때의 상대 위치를 유지한다
	const FMatrix ParentFrameWorld = GizmoTarget->GetWorldMatrix();
	const FMatrix ChildFrameWorld = DragChildFrameRelative * ParentFrameWorld;
	SetRefFrame(Template->DefaultInstance, 1, ParentFrameWorld * DragBoneWorldInverse);
	SetRefFrame(Template->DefaultInstance, 0, ChildFrameWorld * DragChildBoneWorldInverse);
	MarkDirty();
}

void FPhysicsAssetEditor::ApplyShapeTransform()
{
	USkeletalBodySetup* BodySetup = GetSelectedBodySetup();
	EAggCollisionShape::Type ShapeType;
	int32 ShapeIndex;
	if (!BodySetup || !ResolveSelectedShape(ShapeType, ShapeIndex))
	{
		return;
	}

	// 기즈모 배율은 드래그 시작부터 누적이므로 매번 시작 값에서 다시 계산한다
	FKAggregateGeom& Geom = BodySetup->AggGeom;
	Geom = DragStartGeom;

	const UE::Widget::EWidgetMode Mode = ModeTools.GetWidgetMode();
	const FVector Center = DragBoneWorldInverse.TransformPosition(GizmoTarget->GetWorldLocation());
	const FRotator Rotation = (DragBoneWorldRotation.Inverse() *
		GizmoTarget->GetRelativeRotation().Quaternion()).Normalized().ToFRotator();
	const FVector Scale = ClampShapeScale(GizmoTarget->GetRelativeScale3D());

	if (ShapeType == EAggCollisionShape::Sphyl)
	{
		FKSphylElem& Elem = Geom.SphylElems[ShapeIndex];
		if (Mode == UE::Widget::WM_Translate)
		{
			Elem.Center = Center;
		}
		else if (Mode == UE::Widget::WM_Rotate)
		{
			Elem.Rotation = Rotation;
		}
		else if (Mode == UE::Widget::WM_Scale)
		{
			const float TotalHeight = (Elem.Length + Elem.Radius * 2.0f) * Scale.Z;
			Elem.Radius *= DominantScale(Scale.X, Scale.Y);
			Elem.Length = std::max(TotalHeight - Elem.Radius * 2.0f, 0.0f);
		}
	}
	else if (ShapeType == EAggCollisionShape::Box)
	{
		FKBoxElem& Elem = Geom.BoxElems[ShapeIndex];
		if (Mode == UE::Widget::WM_Translate)
		{
			Elem.Center = Center;
		}
		else if (Mode == UE::Widget::WM_Rotate)
		{
			Elem.Rotation = Rotation;
		}
		else if (Mode == UE::Widget::WM_Scale)
		{
			Elem.X *= Scale.X;
			Elem.Y *= Scale.Y;
			Elem.Z *= Scale.Z;
		}
	}
	else if (ShapeType == EAggCollisionShape::Sphere)
	{
		FKSphereElem& Elem = Geom.SphereElems[ShapeIndex];
		if (Mode == UE::Widget::WM_Translate)
		{
			Elem.Center = Center;
		}
		else if (Mode == UE::Widget::WM_Scale)
		{
			const float SphereScale = DominantScale(DominantScale(Scale.X, Scale.Y), Scale.Z);
			Elem.Radius *= SphereScale;
		}
	}

	MarkDirty();
}

bool FPhysicsAssetEditor::PickPhysics(const FRay& Ray, EPhysicsAssetSelection& OutType, int32& OutIndex,
	EAggCollisionShape::Type& OutShapeType, int32& OutShapeIndex) const
{
	FPhysicsDebugDraw DebugDraw;
	float NearestT = FLT_MAX;

	if (bShowConstraints)
	{
		const float Radius = DebugDraw.GetConstraintDrawSize(MeshComponent, ConstraintDrawSize);
		for (int32 i = 0; i < PhysicsAsset->ConstraintSetup.Num(); ++i)
		{
			FMatrix ParentFrameWorld, ChildFrameWorld;
			float T = 0.0f;
			if (PhysicsAsset->ConstraintSetup[i] &&
				DebugDraw.GetConstraintFrames(MeshComponent, PhysicsAsset->ConstraintSetup[i]->DefaultInstance, ParentFrameWorld, ChildFrameWorld) &&
				RayIntersectsSphere(Ray, ParentFrameWorld.TransformPosition(FVector::ZeroVector), Radius, T) && T < NearestT)
			{
				NearestT = T;
				OutType = EPhysicsAssetSelection::Constraint;
				OutIndex = i;
			}
		}
		// 관절 구는 Body 캡슐 안쪽에 묻히므로 맞았으면 Body보다 우선한다
		if (OutType == EPhysicsAssetSelection::Constraint)
		{
			return true;
		}
	}

	float BodyT = 0.0f;
	if (bShowBodies && PickBody(Ray, OutIndex, OutShapeType, OutShapeIndex, BodyT))
	{
		OutType = EPhysicsAssetSelection::Body;
		return true;
	}
	return false;
}

bool FPhysicsAssetEditor::PickBody(const FRay& Ray, int32& OutBodyIndex, EAggCollisionShape::Type& OutShapeType,
	int32& OutShapeIndex, float& OutT) const
{
	FPhysicsDebugDraw DebugDraw;
	float NearestT = FLT_MAX;
	OutBodyIndex = INDEX_NONE;

	for (int32 BodyIndex = 0; BodyIndex < PhysicsAsset->SkeletalBodySetups.Num(); ++BodyIndex)
	{
		const USkeletalBodySetup* BodySetup = PhysicsAsset->SkeletalBodySetups[BodyIndex];
		FTransform BoneWorld;
		if (!BodySetup || !DebugDraw.GetBoneWorldTransform(MeshComponent, BodySetup->BoneName.ToString(), BoneWorld))
		{
			continue;
		}

		const FMatrix BodyMatrix = BoneWorld.GetLocalMatrix();
		const FVector& Scale = BoneWorld.Scale;
		const auto Consider = [&](bool bHit, float T, EAggCollisionShape::Type ShapeType, int32 ShapeIndex)
		{
			if (bHit && T < NearestT)
			{
				NearestT = T;
				OutBodyIndex = BodyIndex;
				OutShapeType = ShapeType;
				OutShapeIndex = ShapeIndex;
			}
		};

		const FKAggregateGeom& Geom = BodySetup->AggGeom;
		for (int32 i = 0; i < Geom.SphereElems.Num(); ++i)
		{
			const FKSphereElem& Elem = Geom.SphereElems[i];
			const float Radius = Elem.Radius * std::max({ std::abs(Scale.X), std::abs(Scale.Y), std::abs(Scale.Z) });
			float T = 0.0f;
			Consider(RayIntersectsSphere(Ray, BodyMatrix.TransformPosition(Elem.Center), Radius, T), T, EAggCollisionShape::Sphere, i);
		}

		for (int32 i = 0; i < Geom.SphylElems.Num(); ++i)
		{
			const FKSphylElem& Elem = Geom.SphylElems[i];
			const FMatrix ElemWorld = DebugDraw.MakeElemMatrix(Elem.Center, Elem.Rotation) * BodyMatrix;
			const FVector Center = ElemWorld.TransformPosition(FVector::ZeroVector);
			const FVector Up = FVector(ElemWorld.M[2][0], ElemWorld.M[2][1], ElemWorld.M[2][2]).GetSafeNormal();
			const float Radius = Elem.Radius * std::max(std::abs(Scale.X), std::abs(Scale.Y));
			const float HalfLength = Elem.Length * 0.5f * std::abs(Scale.Z);
			float T = 0.0f;
			Consider(RayIntersectsCapsule(Ray, Center - Up * HalfLength, Center + Up * HalfLength, Radius, T), T, EAggCollisionShape::Sphyl, i);
		}

		for (int32 i = 0; i < Geom.BoxElems.Num(); ++i)
		{
			const FKBoxElem& Elem = Geom.BoxElems[i];
			const FMatrix ElemWorld = DebugDraw.MakeElemMatrix(Elem.Center, Elem.Rotation) * BodyMatrix;
			const FVector HalfExtent(Elem.X * 0.5f, Elem.Y * 0.5f, Elem.Z * 0.5f);
			float T = 0.0f;
			Consider(RayIntersectsAABB(ToLocalRay(Ray, ElemWorld), HalfExtent * -1.0f, HalfExtent, T), T, EAggCollisionShape::Box, i);
		}
	}

	OutT = NearestT;
	return OutBodyIndex != INDEX_NONE;
}
