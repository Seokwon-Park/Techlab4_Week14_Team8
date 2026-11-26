#pragma once

#include "Core/NameTypes.h"
#include "Physics/ConstraintInstance.h"
#include "Math/Box.h"

class UPhysicsAsset;
class UPhysicsConstraintTemplate;
class USkeletalMesh;
class UBodySetup;
struct FBoneInfo;

enum EPhysAssetFitGeomType :uint8
{
	EFG_Box,
	EFG_Sphyl,
	EFG_Sphere,
	EFG_SingleConvexHull
};

enum EPhysAssetFitVertWeight : uint8
{
	EVW_AnyWeight,
	EVW_DominantWeight
};

struct FPhysAssetCreateParams
{
	float MinBoneSize = 20.0f;
	EPhysAssetFitGeomType GeomType = EFG_Sphyl;
	bool bCreateConstraints = true;
	bool bWalkPastSmall = true;
	EAngularConstraintMotion AngularConstraintMode = ACM_Limited;
	EPhysAssetFitVertWeight VertWeight = EVW_DominantWeight;
};

namespace FPhysicsAssetUtils
{
	bool CreateFromSkeletalMesh(UPhysicsAsset* PhysicsAsset, USkeletalMesh* SkelMesh, const FPhysAssetCreateParams& Params, FString& OutErrorMessage, bool bSetToMesh = true);
	int32 CreateNewBody(UPhysicsAsset* PhysAsset, FName InBodyName, const FPhysAssetCreateParams& Params);
	void  DestroyBody(UPhysicsAsset* PhysAsset, int32 BodyIndex);		  // 연결된 Constraint도 제거
	int32 CreateNewConstraint(UPhysicsAsset* PhysAsset, FName InConstraintName,UPhysicsConstraintTemplate* InConstraintSetup = nullptr);
	void  DestroyConstraint(UPhysicsAsset* PhysAsset, int32 ConstraintIndex);

	// 본마다 그 본에 속한 정점을 본 로컬로 옮겨 감싼 AABB. 정점이 없는 본은 Min > Max
	TArray<FBox> CalcBoneLocalBoxes(const USkeletalMesh* SkelMesh, EPhysAssetFitVertWeight VertWeight);
	// 정점이 없으면 false
	bool GetBoneLocalBox(const USkeletalMesh* SkelMesh, int32 BoneIndex, FBox& OutLocalBox, EPhysAssetFitVertWeight VertWeight = EVW_DominantWeight);
	// 본 정점을 감싸는 캡슐을 BodySetup에 추가한다. 정점이 없으면 false
	bool CreateCollisionFromBone(UBodySetup* BodySetup, USkeletalMesh* SkelMesh, int32 BoneIndex, EPhysAssetFitVertWeight VertWeight = EVW_DominantWeight);
	// Child 본 원점에 놓이는 Constraint의 본 이름, 두 프레임, 각도 제한 모드를 채운다
	void InitConstraintFromBones(FConstraintInstance& CI, const TArray<FBoneInfo>& Bones, int32 Child, int32 Parent, const FPhysAssetCreateParams& Params);
}
