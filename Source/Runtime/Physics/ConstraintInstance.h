#pragma once

#include "Core/NameTypes.h"
#include "Math/Transform.h"

struct FBodyInstance;

namespace physx
{
	class PxD6Joint;
	class PxScene;
}

enum EAngularConstraintMotion : uint8
{
	ACM_Free, // 그 축으로 마음대로 돈다
	ACM_Limited, // 정해진 각도까지만 돈다
	ACM_Locked // 그 축으로는 돌지 않는다
};

//Swing1 = Z축 회전(XY 평면) Swing2 = Y축 회전(XZ평면) 
//PhysX와 번호가 엇갈림
//스윙 두 개를 한 구조체에 묶고 둘이 합쳐서 가동 범위 원뿔(cone) 하나를 이루게 한다.
struct FConeConstraint
{
	float Swing1LimitDegrees = 45.f;
	float Swing2LimitDegrees = 45.f;
	EAngularConstraintMotion Swing1Motion = ACM_Limited;
	EAngularConstraintMotion Swing2Motion = ACM_Limited;
};

// 관절에는 자기만의 좌표축이 있고, 회전은 모두 이 축을 기준으로 정의
// Twist: X축을 중심으로 비틀기(보통 뼈가 뻗는 방향)
// Swing1 : Z축을 중심으로 회전
// Swing2 : Y축을 중심으로 회전
struct FTwistConstraint
{
	float TwistLimitDegrees = 45.0f;
	EAngularConstraintMotion TwistMotion = ACM_Limited;
};

struct FConstraintProfileProperties
{
	FConeConstraint ConeLimit;
	FTwistConstraint TwistLimit;
	bool bDisableCollision = true;
};

// 데이터와 런타임 조인트를 한 구조체에 담는다.
// 관절을 처음 만들 때 자동으로 계산
// 관절은 자식 뼈의 원점(피벗)에 둔다. 같은 점을 자식 쪽에서 표현한 값이 Pos1, 부모 쪽에서 표현한 값이 Pos2
struct FConstraintInstance
{
	FName ConstraintBone1;
	FName ConstraintBone2;

	FVector Pos1 = FVector::ZeroVector;
	FVector PriAxis1 = FVector(1, 0, 0);
	FVector SecAxis1 = FVector(0, 1, 0);

	FVector Pos2 = FVector::ZeroVector;
	FVector PriAxis2 = FVector(1, 0, 0);
	FVector SecAxis2 = FVector(0, 1, 0);

	FConstraintProfileProperties ProfileInstance;

	FTransform GetRefFrame(int32 Frame) const;

	bool InitConstraint(FBodyInstance* Body1, FBodyInstance* Body2, float Scale);
	void TermConstraint();
	bool IsValidConstraintInstance() const { return ConstraintHandle != nullptr; }

private:
	physx::PxD6Joint* ConstraintHandle = nullptr;
	void ApplyAngularLimits(physx::PxD6Joint& Joint) const;
	physx::PxScene* ConstraintScene = nullptr;
};
