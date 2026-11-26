#include "EnginePCH.h"
#include "PhysicsAsset.h"
#include "Physics/PhysicalMaterial.h"

void UPhysicsAsset::DisableCollision(int32 BodyIndexA, int32 BodyIndexB)
{
	if (BodyIndexA == BodyIndexB) return;
	CollisionDisableTable.Add(FRigidBodyIndexPair(BodyIndexA, BodyIndexB), false);
}

void UPhysicsAsset::EnableCollision(int32 BodyIndexA, int32 BodyIndexB)
{
	CollisionDisableTable.Remove(FRigidBodyIndexPair(BodyIndexA, BodyIndexB));
}

bool UPhysicsAsset::IsCollisionEnabled(int32 BodyIndexA, int32 BodyIndexB) const
{
	return !CollisionDisableTable.Contains(FRigidBodyIndexPair(BodyIndexA, BodyIndexB));
}

int32 UPhysicsAsset::FindBodyIndex(FName BodyName) const
{
	const int32* Found = BodySetupIndexMap.Find(BodyName);
	return Found ? *Found : INDEX_NONE;
}

int32 UPhysicsAsset::FindConstraintIndex(FName Bone1Name, FName Bone2Name) const
{
	for (int32 i = 0; i < ConstraintSetup.Num(); ++i)
	{
		const FConstraintInstance& CI = ConstraintSetup[i]->DefaultInstance;
		if (CI.ConstraintBone1 == Bone1Name && CI.ConstraintBone2 == Bone2Name)
			return i;
	}
	return INDEX_NONE;
}

// UE UPhysicsAsset::BodyFindConstraints
void UPhysicsAsset::BodyFindConstraints(int32 BodyIndex, TArray<int32>& Constraints) const
{
	Constraints.Empty();
	const FName BodyName = SkeletalBodySetups[BodyIndex]->BoneName;
	for (int32 ConIdx = 0; ConIdx < ConstraintSetup.Num(); ++ConIdx)
	{
		const FConstraintInstance& CI = ConstraintSetup[ConIdx]->DefaultInstance;
		if (CI.ConstraintBone1 == BodyName || CI.ConstraintBone2 == BodyName)
			Constraints.Add(ConIdx);
	}
}

void UPhysicsAsset::UpdateBodySetupIndexMap()
{
	BodySetupIndexMap.Empty();
	for (int32 i = 0; i < SkeletalBodySetups.Num(); ++i)
		BodySetupIndexMap.Add(SkeletalBodySetups[i]->BoneName, i);
}

void UPhysicsAsset::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FArchive& Ar = Record.GetUnderlyingArchive();

	Record << SA_VALUE("PreviewMesh", PreviewMeshPath);

	// 한 에셋에서 공유하는 재질의 중복 저장 방지
	TArray<UPhysicalMaterial*> Materials;
	if (Ar.IsSaving())
	{
		for (const USkeletalBodySetup* Body : SkeletalBodySetups)
		{
			if (Body->PhysMaterial)
			{
				Materials.AddUnique(Body->PhysMaterial);
			}
		}
	}
	if (auto Field = Record.TryEnterField("PhysicalMaterials", !Materials.IsEmpty()))
	{
		int32 Count = Materials.Num();
		auto Array = Field.GetValue().EnterArray(Count);
		if (Ar.IsLoading())
		{
			Materials.SetNum(Count);
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Ar.IsLoading())
			{
				// 같은 에셋을 다시 읽을 때 같은 재질 슬롯 재사용
				Materials[Index] = NewObject<UPhysicalMaterial>(this, FName("EmbeddedPhysicalMaterial", Index + 1));
				if (!Materials[Index])
				{
					Ar.SetError();
					return;
				}
			}
			UPhysicalMaterial::StaticClass()->SerializeTaggedProperties(Array.EnterElement(), Materials[Index]);
		}
	}

	int32 NumBodies = Ar.IsSaving() ? SkeletalBodySetups.Num() : 0;
	auto Bodies = Record.EnterArray("SkeletalBodySetups", NumBodies);
	if (Ar.IsLoading()) SkeletalBodySetups.SetNum(NumBodies);
	for (int32 i = 0; i < NumBodies; ++i)
	{
		if (Ar.IsLoading()) SkeletalBodySetups[i] = NewObject<USkeletalBodySetup>(this);
		auto BodyRecord = Bodies.EnterElement().EnterRecord();
		USkeletalBodySetup* Body = SkeletalBodySetups[i];
		Body->Serialize(BodyRecord);
		// 재질 필드가 없는 기존 바디는 자체 마찰·반발값 사용
		int32 MaterialIndex = Ar.IsSaving() ? Materials.Find(Body->PhysMaterial) : INDEX_NONE;
		if (auto Field = BodyRecord.TryEnterField("PhysMaterialIndex", MaterialIndex != INDEX_NONE))
		{
			Field.GetValue() << MaterialIndex;
			if (Ar.IsLoading())
			{
				if (!Materials.IsValidIndex(MaterialIndex))
				{
					Ar.SetError();
				}
				else
				{
					Body->PhysMaterial = Materials[MaterialIndex];
				}
			}
		}
	}

	int NumConstraints = Ar.IsSaving() ? ConstraintSetup.Num() : 0;
	auto Constraints = Record.EnterArray("ConstraintSetup", NumConstraints);
	if (Ar.IsLoading()) ConstraintSetup.SetNum(NumConstraints);
	for (int32 i = 0; i < NumConstraints; ++i)
	{
		if (Ar.IsLoading()) ConstraintSetup[i] = NewObject<UPhysicsConstraintTemplate>(this);
		ConstraintSetup[i]->Serialize(Constraints.EnterElement().EnterRecord());
	}

	// 충돌 끄기 테이블: [{ "A": 0, "B": 2 }, ...]
	TArray<FRigidBodyIndexPair> Pairs;
	if(Ar.IsSaving())
		for(const auto&[Pair, bEnabled] : CollisionDisableTable)
			Pairs.Add(Pair);

	int32 NumPairs = Pairs.Num();
	auto PairArray = Record.EnterArray("CollisionDisableTable", NumPairs);
	if (Ar.IsLoading()) Pairs.SetNum(NumPairs);
	for (int32 i = 0; i < NumPairs; ++i)
	{
		auto R = PairArray.EnterElement().EnterRecord();
		R << SA_VALUE("A", Pairs[i].Indices[0]);
		R << SA_VALUE("B", Pairs[i].Indices[1]);
	}

	if (Ar.IsLoading())
	{
		CollisionDisableTable.Empty();
		for (const FRigidBodyIndexPair& Pair : Pairs)
			DisableCollision(Pair.Indices[0], Pair.Indices[1]);
		UpdateBodySetupIndexMap();
	}
}

void UPhysicsConstraintTemplate::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FArchive& Ar = Record.GetUnderlyingArchive();
	FConstraintInstance& CI = DefaultInstance;
	FConstraintProfileProperties& P = CI.ProfileInstance;

	FString Bone1 = Ar.IsSaving() ? CI.ConstraintBone1.ToString() : FString{};
	FString Bone2 = Ar.IsSaving() ? CI.ConstraintBone2.ToString() : FString{};
	Record << SA_VALUE("ConstraintBone1", Bone1);
	Record << SA_VALUE("ConstraintBone2", Bone2);

	Record << SA_VALUE("Pos1", CI.Pos1);
	Record << SA_VALUE("PriAxis1", CI.PriAxis1);
	Record << SA_VALUE("SecAxis1", CI.SecAxis1);
	Record << SA_VALUE("Pos2", CI.Pos2);
	Record << SA_VALUE("PriAxis2", CI.PriAxis2);
	Record << SA_VALUE("SecAxis2", CI.SecAxis2);

	int32 TwistMotion = P.TwistLimit.TwistMotion;
	int32 Swing1Motion = P.ConeLimit.Swing1Motion;
	int32 Swing2Motion = P.ConeLimit.Swing2Motion;
	Record << SA_VALUE("TwistMotion", TwistMotion);
	Record << SA_VALUE("Swing1Motion", Swing1Motion);
	Record << SA_VALUE("Swing2Motion", Swing2Motion);
	Record << SA_VALUE("TwistLimitDegrees", P.TwistLimit.TwistLimitDegrees);
	Record << SA_VALUE("Swing1LimitDegrees", P.ConeLimit.Swing1LimitDegrees);
	Record << SA_VALUE("Swing2LimitDegrees", P.ConeLimit.Swing2LimitDegrees);
	Record << SA_VALUE("bDisableCollision", P.bDisableCollision);

	if (Ar.IsLoading())
	{
		CI.ConstraintBone1 = FName(Bone1);
		CI.ConstraintBone2 = FName(Bone2);
		P.TwistLimit.TwistMotion = static_cast<EAngularConstraintMotion>(TwistMotion);
		P.ConeLimit.Swing1Motion = static_cast<EAngularConstraintMotion>(Swing1Motion);
		P.ConeLimit.Swing2Motion = static_cast<EAngularConstraintMotion>(Swing2Motion);
	}
}
