#include "EnginePCH.h"
#include "BodySetup.h"
#include "Serialization/MathSerialization.h"

void UBodySetupCore::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FArchive& Ar = Record.GetUnderlyingArchive();

	// FName은 문자열로 저장
	FString Bone = Ar.IsSaving() ? BoneName.ToString() : FString{};
	int32 Type = static_cast<int32>(PhysicsType);
	Record << SA_VALUE("BoneName", Bone);
	Record << SA_VALUE("PhysicsType", Type);

	if (Ar.IsLoading())
	{
		BoneName = FName(Bone);
		PhysicsType = static_cast<EPhysicsType>(Type);
	}
}

void UBodySetup::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FArchive& Ar = Record.GetUnderlyingArchive();

	Record << SA_VALUE("bOverrideMass", bOverrideMass);
	Record << SA_VALUE("MassInKgOverride", MassInKgOverride);
	Record << SA_VALUE("Friction", Friction);
	Record << SA_VALUE("Restitution", Restitution);

	// 기존 에셋에서 생략된 형상의 기본값은 질량 계산에 포함
	const auto SerializeMassContribution = [](FStructuredArchive::FRecord Shape, FKShapeElem& Elem)
	{
		if (auto Field = Shape.TryEnterField("bContributeToMass", !Elem.bContributeToMass))
		{
			Field.GetValue() << Elem.bContributeToMass;
		}
		else if (Shape.GetUnderlyingArchive().IsLoading())
		{
			Elem.bContributeToMass = true;
		}
	};

	int32 NumSpheres = Ar.IsSaving() ? AggGeom.SphereElems.Num() : 0;
	auto Spheres = Record.EnterArray("SphereElems", NumSpheres);
	if (Ar.IsLoading()) AggGeom.SphereElems.SetNum(NumSpheres);
	for (int32 i = 0; i < NumSpheres; ++i)
	{
		FKSphereElem& E = AggGeom.SphereElems[i];
		auto R = Spheres.EnterElement().EnterRecord();
		R << SA_VALUE("Center", E.Center);
		R << SA_VALUE("Radius", E.Radius);
		SerializeMassContribution(R, E);
	}

	int32 NumBoxes = Ar.IsSaving() ? AggGeom.BoxElems.Num() : 0;
	auto Boxes = Record.EnterArray("BoxElems", NumBoxes);
	if (Ar.IsLoading()) AggGeom.BoxElems.SetNum(NumBoxes);
	for (int32 i = 0; i < NumBoxes; ++i)
	{
		FKBoxElem& E = AggGeom.BoxElems[i];
		auto R = Boxes.EnterElement().EnterRecord();
		R << SA_VALUE("Center", E.Center);
		R << SA_VALUE("Rotation", E.Rotation);
		R << SA_VALUE("X", E.X);
		R << SA_VALUE("Y", E.Y);
		R << SA_VALUE("Z", E.Z);
		SerializeMassContribution(R, E);
	}

	int32 NumSphyls = Ar.IsSaving() ? AggGeom.SphylElems.Num() : 0;
	auto Sphyls = Record.EnterArray("SphylElems", NumSphyls);
	if (Ar.IsLoading()) AggGeom.SphylElems.SetNum(NumSphyls);
	for (int32 i = 0; i < NumSphyls; ++i)
	{
		FKSphylElem& E = AggGeom.SphylElems[i];
		auto R = Sphyls.EnterElement().EnterRecord();
		R << SA_VALUE("Center", E.Center);
		R << SA_VALUE("Rotation", E.Rotation);
		R << SA_VALUE("Radius", E.Radius);
		R << SA_VALUE("Length", E.Length);
		SerializeMassContribution(R, E);
	}
	// 컨벡스 형상의 편집 데이터와 질량 기여 여부 보존
	if (auto Field = Record.TryEnterField("ConvexElems", !AggGeom.ConvexElems.IsEmpty()))
	{
		int32 Count = AggGeom.ConvexElems.Num();
		auto Array = Field.GetValue().EnterArray(Count);
		AggGeom.ConvexElems.SetNum(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FKConvexElem& Elem = AggGeom.ConvexElems[Index];
			auto Shape = Array.EnterElement().EnterRecord();
			SerializeMassContribution(Shape, Elem);
			Shape << SA_VALUE("Transform", Elem.Transform);
			int32 NumVertices = Elem.VertexData.Num();
			auto Vertices = Shape.EnterArray("VertexData", NumVertices);
			Elem.VertexData.SetNum(NumVertices);
			for (FVector& Vertex : Elem.VertexData)
			{
				Vertices.EnterElement() << Vertex;
			}
		}
	}
	else if (Ar.IsLoading())
	{
		AggGeom.ConvexElems.Reset();
	}
}
