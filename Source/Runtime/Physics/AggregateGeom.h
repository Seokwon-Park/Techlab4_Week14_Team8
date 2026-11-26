#pragma once

#include "Containers/Array.h"
#include "Core/NameTypes.h"
#include "Math/Transform.h"

namespace EAggCollisionShape
{
	enum Type : uint8
	{
		Sphere,
		Box,
		Sphyl,
		Convex,
		Unknown
	};
}

struct FKShapeElem
{
	FName Name;
	EAggCollisionShape::Type ShapeType = EAggCollisionShape::Unknown;
	bool bContributeToMass = true;

protected:
	explicit FKShapeElem(EAggCollisionShape::Type InShapeType) : ShapeType(InShapeType) {}
};

struct FKSphereElem : public FKShapeElem
{
	FKSphereElem() : FKShapeElem(EAggCollisionShape::Sphere) {}

	FVector Center = FVector::ZeroVector;
	float Radius = 1.0f;
};

struct FKBoxElem : public FKShapeElem
{
	FKBoxElem() : FKShapeElem(EAggCollisionShape::Box) {}

	FVector Center = FVector::ZeroVector;
	FRotator Rotation = FRotator(0, 0, 0);
	float X = 1.0f;
	float Y = 1.0f;
	float Z = 1.0f;
};

//Capsule
struct FKSphylElem : public FKShapeElem
{
	FKSphylElem() : FKShapeElem(EAggCollisionShape::Sphyl) {}

	FVector Center = FVector::ZeroVector;
	FRotator Rotation = FRotator(0, 0, 0);
	float Radius = 1.0f;
	float Length = 1.0f;
};

struct FKConvexElem : public FKShapeElem
{
	FKConvexElem() : FKShapeElem(EAggCollisionShape::Convex) {}

	TArray<FVector> VertexData;
	FTransform Transform;
};

struct FKAggregateGeom
{
	TArray<FKSphereElem> SphereElems;
	TArray<FKBoxElem> BoxElems;
	TArray<FKSphylElem> SphylElems;
	TArray<FKConvexElem> ConvexElems;

	int32 GetElementCount() const
	{
		return SphereElems.Num() + BoxElems.Num() + SphylElems.Num() + ConvexElems.Num();
	}

	void EmptyElements()
	{
		SphereElems.Empty();
		BoxElems.Empty();
		SphylElems.Empty();
		ConvexElems.Empty();
	}
};
