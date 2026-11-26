#include "EnginePCH.h"
#include "PhysicsAssetUtils.h"
#include "Physics/PhysicsAsset.h"
#include "Render/SkeletalMesh.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	bool IsValidBox(const FBox& B)
	{
		return B.Min.X <= B.Max.X && B.Min.Y <= B.Max.Y && B.Min.Z <= B.Max.Z;
	}

	// AABB를 행렬로 옮긴 뒤 다시 축 정렬로 감싼다 (8개 꼭짓점을 변환)
	FBox TransformBox(const FBox& Box, const FMatrix& M)
	{
		FBox Out{ FVector(FLT_MAX,FLT_MAX,FLT_MAX), FVector(-FLT_MAX,-FLT_MAX,-FLT_MAX) };
		for (int32 c = 0; c < 8; ++c)
		{
			const FVector Corner((c & 1) ? Box.Max.X : Box.Min.X,
				(c & 2) ? Box.Max.Y : Box.Min.Y,
				(c & 4) ? Box.Max.Z : Box.Min.Z);
			const FVector P = M.TransformPosition(Corner);
			for (int32 a = 0; a < 3; ++a)
			{
				Out.Min[a] = std::min(Out.Min[a], P[a]);
				Out.Max[a] = std::max(Out.Max[a], P[a]);
			}
		}
		return Out;
	}

	// 본 로컬 AABB -> 컴포넌트 공간 바인드 AABB (ToBoneLocal의 반대)
	FBox ToComponent(const FBox& LocalBox, const FMatrix& InverseBindPose)
	{
		return TransformBox(LocalBox, InverseBindPose.Inverse());
	}

	// 로컬 Z를 Axis(0=X,1=Y,2=Z) 방향으로 돌리는 회전. 순환 치환이라 좌표계 손잡이가 유지된다
	FRotator RotationZTo(int32 Axis)
	{
		const FVector E[3] = { FVector(1,0,0), FVector(0,1,0), FVector(0,0,1) };
		FMatrix M = FMatrix::Identity;
		M.SetAxis(0, E[(Axis + 1) % 3]);
		M.SetAxis(1, E[(Axis + 2) % 3]);
		M.SetAxis(2, E[Axis]);
		return MatrixToRotator(M);
	}

	bool BoxesOverlap(const FBox& A, const FBox& B)
	{
		return A.Min.X <= B.Max.X && B.Min.X <= A.Max.X
			&& A.Min.Y <= B.Max.Y && B.Min.Y <= A.Max.Y
			&& A.Min.Z <= B.Max.Z && B.Min.Z <= A.Max.Z;
	}

	// 박스의 가장 긴 축을 캡슐 축으로 두고, 나머지 두 축 중 큰 반크기를 반지름으로 쓴다
	void AddSphylFromLocalBox(UBodySetup* BodySetup, const FBox& Local)
	{
		const FVector Ext = (Local.Max - Local.Min) * 0.5f;
		const int32 Axis = (Ext.X >= Ext.Y && Ext.X >= Ext.Z) ? 0 : (Ext.Y >= Ext.Z ? 1 : 2);
		FKSphylElem E;
		E.Center = (Local.Max + Local.Min) * 0.5f;
		E.Rotation = RotationZTo(Axis);
		E.Radius = std::max(Ext[(Axis + 1) % 3], Ext[(Axis + 2) % 3]);
		E.Length = std::max(0.f, Ext[Axis] * 2.f - E.Radius * 2.f);
		BodySetup->AggGeom.SphylElems.Add(E);
	}

	void AddDefaultConstraint(UPhysicsAsset* PhysAsset, const TArray<FBoneInfo>& Bones, int32 Child, int32 Parent, const FPhysAssetCreateParams& Params)
	{
		const int32 Index = FPhysicsAssetUtils::CreateNewConstraint(PhysAsset, FName(Bones[Child].Name));
		FPhysicsAssetUtils::InitConstraintFromBones(PhysAsset->ConstraintSetup[Index]->DefaultInstance, Bones, Child, Parent, Params);
	}
}

void FPhysicsAssetUtils::InitConstraintFromBones(FConstraintInstance& CI, const TArray<FBoneInfo>& Bones, int32 Child, int32 Parent, const FPhysAssetCreateParams& Params)
{
	CI.ConstraintBone1 = FName(Bones[Child].Name);
	CI.ConstraintBone2 = FName(Bones[Parent].Name);

	// Twist 축은 자식 본이 뻗은 방향(그 본의 자식들 쪽 평균)이다.
	// 본 로컬 X를 그대로 쓰면 본이 Y나 Z로 뻗는 스켈레톤(Mixamo 등)에서 Twist와 Swing이 뒤바뀐다
	FVector Twist = FVector::ZeroVector;
	for (int32 b = 0; b < Bones.Num(); ++b)
	{
		if (Bones[b].ParentIndex == Child && Bones[b].LocalBindPose.Location.SizeSquared() > 1e-8f)
			Twist = Twist + Bones[b].LocalBindPose.Location.GetSafeNormal();
	}
	if (Twist.SizeSquared() < 1e-6f)
	{
		// 끝 본은 부모에서 자기로 오는 방향을 자기 로컬 공간으로 옮겨 쓴다
		const FVector FromParent = Bones[Child].LocalBindPose.Location;
		const FMatrix ToChild = Bones[Child].LocalBindPose.GetLocalMatrix().Inverse();
		Twist = FVector(
			FromParent.X * ToChild[0][0] + FromParent.Y * ToChild[1][0] + FromParent.Z * ToChild[2][0],
			FromParent.X * ToChild[0][1] + FromParent.Y * ToChild[1][1] + FromParent.Z * ToChild[2][1],
			FromParent.X * ToChild[0][2] + FromParent.Y * ToChild[1][2] + FromParent.Z * ToChild[2][2]);
	}
	Twist = Twist.SizeSquared() > 1e-6f ? Twist.GetSafeNormal() : FVector(1, 0, 0);

	// Twist와 가장 덜 나란한 로컬 축을 직교화해 두 번째 축으로 쓴다
	const FVector Candidates[3] = { FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1) };
	FVector Sec = Candidates[0];
	for (const FVector& Candidate : Candidates)
		if (std::abs(Candidate.Dot(Twist)) < std::abs(Sec.Dot(Twist))) Sec = Candidate;
	Sec = (Sec - Twist * Sec.Dot(Twist)).GetSafeNormal();

	CI.Pos1 = FVector::ZeroVector;
	CI.PriAxis1 = Twist;
	CI.SecAxis1 = Sec;

	// 같은 프레임을 부모 본 공간으로 옮겨 바인드 포즈에서 두 프레임이 겹치게 한다
	const FMatrix ChildComp = Bones[Child].InverseBindPose.Inverse();
	const FMatrix ParentComp = Bones[Parent].InverseBindPose.Inverse();
	const FMatrix ChildInParent = ChildComp * ParentComp.Inverse();
	const auto ToParent = [&ChildInParent](const FVector& V)
		{
			return FVector(
				V.X * ChildInParent[0][0] + V.Y * ChildInParent[1][0] + V.Z * ChildInParent[2][0],
				V.X * ChildInParent[0][1] + V.Y * ChildInParent[1][1] + V.Z * ChildInParent[2][1],
				V.X * ChildInParent[0][2] + V.Y * ChildInParent[1][2] + V.Z * ChildInParent[2][2]).GetSafeNormal();
		};
	CI.Pos2 = FVector(ChildInParent[3][0], ChildInParent[3][1], ChildInParent[3][2]);
	CI.PriAxis2 = ToParent(Twist);
	CI.SecAxis2 = ToParent(Sec);

	CI.ProfileInstance.TwistLimit.TwistMotion = Params.AngularConstraintMode;
	CI.ProfileInstance.ConeLimit.Swing1Motion = Params.AngularConstraintMode;
	CI.ProfileInstance.ConeLimit.Swing2Motion = Params.AngularConstraintMode;
}

bool FPhysicsAssetUtils::CreateCollisionFromBone(UBodySetup* BodySetup, USkeletalMesh* SkelMesh, int32 BoneIndex, EPhysAssetFitVertWeight VertWeight)
{
	FBox Local;
	if (!BodySetup || !SkelMesh || !GetBoneLocalBox(SkelMesh, BoneIndex, Local, VertWeight)) return false;
	AddSphylFromLocalBox(BodySetup, Local);
	return true;
}

// 렌더용 BoneBounds는 가중치가 있는 모든 본에 정점을 넣고 컴포넌트 공간 AABB라서, 본 로컬로 다시 감싸면 기울어진 본이 부푼다.
// 그래서 정점마다 본 로컬로 먼저 옮긴 뒤 감싼다
TArray<FBox> FPhysicsAssetUtils::CalcBoneLocalBoxes(const USkeletalMesh* SkelMesh, EPhysAssetFitVertWeight VertWeight)
{
	const TArray<FBoneInfo>& Bones = SkelMesh->GetRefSkeleton();
	const FSkeletalMeshLODRenderData& LOD = SkelMesh->GetRenderData().LODRenderData[0];
	TArray<FBox> Boxes;
	Boxes.Init(FBox{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX) }, Bones.Num());

	const auto AddVertex = [&](int32 BoneIndex, const FVector& Position)
	{
		if (BoneIndex < 0 || BoneIndex >= Bones.Num()) return;
		const FVector Local = Bones[BoneIndex].InverseBindPose.TransformPosition(Position);
		FBox& Box = Boxes[BoneIndex];
		for (int32 a = 0; a < 3; ++a)
		{
			Box.Min[a] = std::min(Box.Min[a], Local[a]);
			Box.Max[a] = std::max(Box.Max[a], Local[a]);
		}
	};

	const int32 NumVertices = std::min(LOD.BindVertices.Num(), LOD.SkinWeights.Num());
	for (int32 v = 0; v < NumVertices; ++v)
	{
		const FVector& Position = LOD.BindVertices[v].Position;
		const FSkinWeight& Weight = LOD.SkinWeights[v];
		if (VertWeight == EVW_AnyWeight)
		{
			for (int32 k = 0; k < MaxBoneInfluences; ++k)
			{
				if (Weight.Weight[k] > 0.0f) AddVertex(Weight.BoneIndex[k], Position);
			}
			continue;
		}

		int32 Dominant = 0;
		for (int32 k = 1; k < MaxBoneInfluences; ++k)
		{
			if (Weight.Weight[k] > Weight.Weight[Dominant]) Dominant = k;
		}
		if (Weight.Weight[Dominant] > 0.0f) AddVertex(Weight.BoneIndex[Dominant], Position);
	}
	return Boxes;
}

bool FPhysicsAssetUtils::GetBoneLocalBox(const USkeletalMesh* SkelMesh, int32 BoneIndex, FBox& OutLocalBox, EPhysAssetFitVertWeight VertWeight)
{
	if (!SkelMesh || BoneIndex < 0 || BoneIndex >= SkelMesh->GetRefSkeleton().Num()) return false;
	const TArray<FBox> Boxes = CalcBoneLocalBoxes(SkelMesh, VertWeight);
	if (!IsValidBox(Boxes[BoneIndex])) return false;
	OutLocalBox = Boxes[BoneIndex];
	return true;
}

bool FPhysicsAssetUtils::CreateFromSkeletalMesh(UPhysicsAsset* PhysicsAsset, USkeletalMesh* SkelMesh, const FPhysAssetCreateParams& Params, FString& OutErrorMessage, bool bSetToMesh)
{
	if (!PhysicsAsset || !SkelMesh)
	{
		OutErrorMessage = "PhysicsAsset 또는 SkeletalMesh가 없습니다";
		return false;
	}

	const TArray<FBoneInfo>& Bones = SkelMesh->GetRefSkeleton();
	const TArray<FBox> LocalBoxes = CalcBoneLocalBoxes(SkelMesh, Params.VertWeight);
	TArray<int32> BodyOfBone;
	BodyOfBone.Init(INDEX_NONE, Bones.Num());
	TArray<FBox> BodyCompBox;   // 바디 번호 순서. 3단계 겹침 검사용 (컴포넌트 공간)

	// 1) 본마다 캡슐 하나
	for (int32 b = 0; b < Bones.Num(); ++b)
	{
		// UE CreateFromSkeletalMeshInternal: 정점이 없는 본은 크기가 0이라 MinBoneSize를 못 넘어 바디가 생기지 않는다.
		// (예: root 본. 선분으로 대신 만들면 발끝~골반을 덮는 캡슐이 바닥에 박혀 래그돌이 튀어 오른다)
		const FBox& Local = LocalBoxes[b];
		if (!IsValidBox(Local)) continue;

		const FVector Ext = (Local.Max - Local.Min) * 0.5f;   // half size
		const int32 Axis = (Ext.X >= Ext.Y && Ext.X >= Ext.Z) ? 0 : (Ext.Y >= Ext.Z ? 1 : 2);
		// 기준치보다 작은 뼈는 거른다
		// 본 로컬 크기에 본 스케일을 곱해 컴포넌트 공간(cm) 기준으로 비교한다
		const FMatrix BoneComp = Bones[b].InverseBindPose.Inverse();
		const float BoneScale = FVector(BoneComp[0][0], BoneComp[0][1], BoneComp[0][2]).Size();
		if (Ext[Axis] * 2.f * BoneScale < Params.MinBoneSize) continue;

		const int32 BodyIndex = CreateNewBody(PhysicsAsset, FName(Bones[b].Name), Params);
		AddSphylFromLocalBox(PhysicsAsset->SkeletalBodySetups[BodyIndex], Local);
		BodyOfBone[b] = BodyIndex;

		// 캡슐을 만든 박스 그대로 보관 (3단계 겹침 검사용)
		if (BodyIndex >= BodyCompBox.Num()) BodyCompBox.SetNum(BodyIndex + 1);
		BodyCompBox[BodyIndex] = ToComponent(Local, Bones[b].InverseBindPose);
	}

	// 2) 바디가 있는 가장 가까운 조상과 조인트
	if (Params.bCreateConstraints)
	{
		for (int32 b = 0; b < Bones.Num(); ++b)
		{
			if (BodyOfBone[b] == INDEX_NONE) continue;
			int32 Parent = Bones[b].ParentIndex;
			while (Params.bWalkPastSmall && Parent >= 0 && BodyOfBone[Parent] == INDEX_NONE)
				Parent = Bones[Parent].ParentIndex;
			if (Parent < 0 || BodyOfBone[Parent] == INDEX_NONE) continue;
			AddDefaultConstraint(PhysicsAsset, Bones, b, Parent, Params);
		}
	}

	// 3) 바인드 포즈에서 겹치는 바디 쌍은 충돌 끄기 (조인트로 안 이어진 쌍 포함)
	//	1단계에서 캡슐을 만든 박스(컴포넌트 공간)로 비교한다.
	//	UE PhysicsAssetUtils의 기본 정책은 확인 못 함. 처음엔 전부 꺼도 시연엔 충분
	for (int32 i = 0; i < BodyCompBox.Num(); ++i)
		for (int32 j = i + 1; j < BodyCompBox.Num(); ++j)
			if (BoxesOverlap(BodyCompBox[i], BodyCompBox[j]))
				PhysicsAsset->DisableCollision(i, j);

	PhysicsAsset->UpdateBodySetupIndexMap();
	if (bSetToMesh) SkelMesh->PhysicsAsset = PhysicsAsset;
	return PhysicsAsset->SkeletalBodySetups.Num() > 0;
}

int32 FPhysicsAssetUtils::CreateNewBody(UPhysicsAsset* PhysAsset, FName InBodyName, const FPhysAssetCreateParams& Params)
{
	const int32 Existing = PhysAsset->FindBodyIndex(InBodyName);
	if (Existing != INDEX_NONE) return Existing;

	USkeletalBodySetup* Setup = NewObject<USkeletalBodySetup>(PhysAsset);
	Setup->BoneName = InBodyName;
	PhysAsset->SkeletalBodySetups.Add(Setup);
	PhysAsset->UpdateBodySetupIndexMap();
	return PhysAsset->SkeletalBodySetups.Num() - 1;
}


int32 FPhysicsAssetUtils::CreateNewConstraint(UPhysicsAsset* PhysAsset, FName InConstraintName, UPhysicsConstraintTemplate* InConstraintSetup)
{
	UPhysicsConstraintTemplate* Template = NewObject<UPhysicsConstraintTemplate>(PhysAsset);
	if (InConstraintSetup) Template->DefaultInstance = InConstraintSetup->DefaultInstance;
	PhysAsset->ConstraintSetup.Add(Template);
	return PhysAsset->ConstraintSetup.Num() - 1;
}

// UE DestroyConstraint
void FPhysicsAssetUtils::DestroyConstraint(UPhysicsAsset* PhysAsset, int32 ConstraintIndex)
{
	if (!PhysAsset || !PhysAsset->ConstraintSetup.IsValidIndex(ConstraintIndex)) return;
	PhysAsset->ConstraintSetup.RemoveAt(ConstraintIndex);   // 객체는 GC가 정리 (UE와 같음)
}

namespace
{
	// UE RemoveBodyFromCollisionTableInternal: 지울 바디가 들어간 쌍은 빼고, 뒤 번호는 하나씩 당긴다
	void RemoveBodyFromCollisionTableInternal(UPhysicsAsset* PhysAsset, int32 BodyIndex)
	{
		TMap<FRigidBodyIndexPair, bool, FRigidBodyIndexPairHash> NewCDT;
		for (int32 i = 1; i < PhysAsset->SkeletalBodySetups.Num(); ++i)
		{
			for (int32 j = 0; j < i; ++j)
			{
				if (!PhysAsset->CollisionDisableTable.Contains(FRigidBodyIndexPair(j, i))) continue;
				if (i == BodyIndex || j == BodyIndex) continue;

				const int32 NewI = i > BodyIndex ? i - 1 : i;
				const int32 NewJ = j > BodyIndex ? j - 1 : j;
				NewCDT.Add(FRigidBodyIndexPair(NewJ, NewI), false);
			}
		}
		PhysAsset->CollisionDisableTable = NewCDT;
	}

	// UE RemoveBodyConstraintsInternal: 지울 때마다 번호가 바뀌므로 매번 다시 찾는다
	void RemoveBodyConstraintsInternal(UPhysicsAsset* PhysAsset, int32 BodyIndex)
	{
		TArray<int32> Constraints;
		PhysAsset->BodyFindConstraints(BodyIndex, Constraints);
		while (Constraints.Num() > 0)
		{
			FPhysicsAssetUtils::DestroyConstraint(PhysAsset, Constraints[0]);
			PhysAsset->BodyFindConstraints(BodyIndex, Constraints);
		}
	}
}

// UE DestroyBody: 충돌 표 정리 -> 연결된 Constraint 제거 -> 바디 제거 -> 본 이름 캐시 갱신.
// 순서가 중요하다. Constraint는 BoneName으로 찾으므로 바디를 지우기 전에 지운다
void FPhysicsAssetUtils::DestroyBody(UPhysicsAsset* PhysAsset, int32 BodyIndex)
{
	if (!PhysAsset || !PhysAsset->SkeletalBodySetups.IsValidIndex(BodyIndex)) return;

	RemoveBodyFromCollisionTableInternal(PhysAsset, BodyIndex);
	RemoveBodyConstraintsInternal(PhysAsset, BodyIndex);

	PhysAsset->SkeletalBodySetups.RemoveAt(BodyIndex);   // 객체는 GC가 정리 (UE와 같음)
	PhysAsset->UpdateBodySetupIndexMap();
}
