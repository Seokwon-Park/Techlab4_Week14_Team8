#pragma once

#include "Math/Transform.h"

class FLineBatcher;
class UWorld;
class AActor;
class UStaticMeshComponent;
class USkeletalMeshComponent;
struct FKAggregateGeom;
struct FKSphereElem;
struct FKBoxElem;
struct FKSphylElem;
struct FKConvexElem;
struct FConstraintInstance;
class UPhysicsAsset;


class FPhysicsDebugDraw
{
public:
	// 바디 하나의 Shape 전부를 BodyWorld 위치에 그린다
	// FillAlpha > 0이면 면도 반투명으로 채운다 (Convex 제외)
	void DrawAggregateGeom(FLineBatcher* LineBatcher, const FKAggregateGeom& Geom,
		const FTransform& BodyWorld, const FVector4& Color, float FillAlpha = 0.0f);
	// 월드의 StaticMesh, SkeletalMesh 물리 바디를 전부 그린다. Selected 액터의 바디는 강조색
	// Shape 컴포넌트(Box/Sphere/Capsule)는 DrawCollisionShapes가 그리므로 제외한다
	void DrawPhysicsBodies(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected);
	void DrawConstraintLimit(FLineBatcher* LineBatcher, const FConstraintInstance& Constraint,
		const FMatrix& ParentFrameWorld, const FMatrix& ChildFrameWorld, float DrawSize) const;
	void DrawConstraintLimits(UWorld* World, FLineBatcher* LineBatcher);
	void DrawPhysicsAssetBodies(FLineBatcher* LineBatcher, const USkeletalMeshComponent* Component,
		const UPhysicsAsset& PhysicsAsset, bool bSelected, int32 SelectedBody = INDEX_NONE, bool bSolid = false);
	void DrawPhysicsAssetConstraints(FLineBatcher* LineBatcher, const USkeletalMeshComponent* Component,
		const UPhysicsAsset& PhysicsAsset, int32 SelectedConstraint = INDEX_NONE, float ConstraintDrawSize = 1.0f);

	float GetConstraintDrawSize(const USkeletalMeshComponent* Component, float ConstraintDrawSize) const;
	bool GetConstraintFrames(const USkeletalMeshComponent* Component, const FConstraintInstance& Constraint,
		FMatrix& OutParentFrameWorld, FMatrix& OutChildFrameWorld) const;
	bool GetBoneWorldTransform(const USkeletalMeshComponent* Component, const FString& BoneName, FTransform& OutBoneWorld) const;
	FMatrix MakeElemMatrix(const FVector& Center, const FRotator& Rotation) const;

private:
	// 바디 기본색(하늘색)과 선택된 액터의 강조색(주황)
	const FVector4 BodyColor = FVector4(0.3f, 0.8f, 1.0f, 1.0f);
	const FVector4 SelectedBodyColor = FVector4(1.0f, 0.6f, 0.1f, 1.0f);
	const FVector4 SolidBodyColor = FVector4(0.6f, 0.45f, 1.0f, 1.0f);
	const float BodyFillAlpha = 0.25f;

	const FVector4 SwingColor = FVector4(1.0f, 0.3f, 0.3f, 1.0f);
	const FVector4 TwistColor = FVector4(0.3f, 1.0f, 0.3f, 1.0f);
	const float ConstraintFillAlpha = 0.35f;
	const float ConstraintSizeRatio = 0.016f;
	const float SelectedConstraintSizeScale = 2.5f;

	const FKAggregateGeom* FindStaticMeshGeom(UStaticMeshComponent* Component) const;
	const UPhysicsAsset* FindPhysicsAsset(const USkeletalMeshComponent* Component) const;
	
	void DrawSphereElem(FLineBatcher* LineBatcher, const FKSphereElem& Elem, const FTransform& BodyWorld, const FVector4& Color, float FillAlpha = 0.0f) const;
	void DrawBoxElem(FLineBatcher* LineBatcher, const FKBoxElem& Elem, const FTransform& BodyWorld, const FVector4& Color, float FillAlpha = 0.0f) const;
	void DrawSphylElem(FLineBatcher* LineBatcher, const FKSphylElem& Elem, const FTransform& BodyWorld, const FVector4& Color, float FillAlpha = 0.0f) const;
	void DrawConvexElem(FLineBatcher* LineBatcher, const FKConvexElem& Elem, const FTransform& BodyWorld, const FVector4& Color, float FillAlpha = 0.0f) const;
	
	bool			ShouldDrawComponent(const UPrimitiveComponent* Component, const UWorld* World);
	const FVector4&	PickBodyColor(const UPrimitiveComponent* Component, const AActor* Selected);
};


