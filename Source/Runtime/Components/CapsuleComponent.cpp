#include "EnginePCH.h"
#include "CapsuleComponent.h"
#include "Physics/AggregateGeom.h"
#include "Physics/Collision.h"
#include "Render/LineBatcher.h"

float UCapsuleComponent::GetScaledCapsuleHalfHeight() const
{
	const float Half = CapsuleHalfHeight * std::abs(GetWorldScale3D().Z); 
	return std::max(Half, GetScaledCapsuleRadius()); // 반높이가 반지름보다 작으면 구가 된다
}

float UCapsuleComponent::GetScaledCapsuleRadius() const
{
	const FVector S = GetWorldScale3D().GetAbs(); 
	return CapsuleRadius * std::max(S.X, S.Y); // 반지름은 축에 수직인 X·Y 중 큰 쪽
}

FBox UCapsuleComponent::CalcLocalBounds() const
{
	const FVector E(CapsuleRadius, CapsuleRadius, CapsuleHalfHeight);
	return FBox{ E * -1.0f, E };
}

void UCapsuleComponent::DrawDebug(FLineBatcher* LineBatcher, const FVector4& Color) const
{
	const FWorldCapsule Cap = FCollision::ToWorld(this);   // A = 위쪽 반구 중심, B = 아래쪽
	const FMatrix M = GetWorldMatrix();
	const FVector Up = (Cap.A - Cap.B).Length() > 1e-4f ? (Cap.A - Cap.B).GetSafeNormal()
		: FVector(M.M[2][0], M.M[2][1], M.M[2][2]).GetSafeNormal();
	const FVector Right = FVector(M.M[0][0], M.M[0][1], M.M[0][2]).GetSafeNormal();

	LineBatcher->AddCapsule((Cap.A + Cap.B) * 0.5f, Up, Right,
		Cap.Radius, GetScaledCapsuleHalfHeight(), Color);
}

void UCapsuleComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "CapsuleHalfHeight" || Property.Name == "CapsuleRadius")
		RecreatePhysicsState();
}

void UCapsuleComponent::UpdateBodySetup(FKAggregateGeom& AggGeom) const
{
	// CapsuleHalfHeight는 반구 끝까지, Length는 두 반구 중심 사이 거리다
	FKSphylElem Elem;
	Elem.Radius = CapsuleRadius;
	Elem.Length = std::max(0.0f, 2.0f * (CapsuleHalfHeight - CapsuleRadius));
	AggGeom.SphylElems.Add(Elem);
}