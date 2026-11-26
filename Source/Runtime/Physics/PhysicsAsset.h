#pragma once

#include "UObject/Object.h"
#include "Physics/BodySetup.h"
#include "Physics/ConstraintInstance.h"
#include "PhysicsAsset.generated.h"

// 바디 두 개를 가리키는 인덱스 한 쌍. "이 두 바디끼리는 충돌하지 마"라는 목록(CollisionDisableTable)의 키
struct FRigidBodyIndexPair
{
	int32 Indices[2] = { INDEX_NONE, INDEX_NONE };

	FRigidBodyIndexPair() = default;
	FRigidBodyIndexPair(int32 A, int32 B)
	{
		Indices[0] = std::min(A, B);
		Indices[1] = std::max(A, B);
	}

	bool operator==(const FRigidBodyIndexPair& Other) const
	{
		return Indices[0] == Other.Indices[0] && Indices[1] == Other.Indices[1];
	}
};

struct FRigidBodyIndexPairHash
{
	size_t operator()(const FRigidBodyIndexPair& P)const
	{
		const uint64 Key = (static_cast<uint64>(static_cast<uint32>(P.Indices[0])) << 32)
			| static_cast<uint32>(P.Indices[1]);

		return std::hash<uint64>{}(Key);
	}
};

// UE PhysicsConstraintTemplate.h — 에셋에 저장되는 조인트 기본값. 컴포넌트가 복사해서 InitConstraint
UCLASS()
class UPhysicsConstraintTemplate : public UObject
{
	GENERATED_BODY()

public:
	FConstraintInstance DefaultInstance;

	using Super::Serialize;
	void Serialize(FStructuredArchive::FRecord Record) override;
};

UCLASS()
class UPhysicsAsset : public UObject
{
	GENERATED_BODY()

public:
	TArray<USkeletalBodySetup*> SkeletalBodySetups;
	TArray<UPhysicsConstraintTemplate*> ConstraintSetup;
	FString PreviewMeshPath;

	TMap<FRigidBodyIndexPair, bool, FRigidBodyIndexPairHash> CollisionDisableTable;
	void DisableCollision(int32 BodyIndexA, int32 BodyIndexB);
	void EnableCollision(int32 BodyIndexA, int32 BodyIndexB);
	bool IsCollisionEnabled(int32 BodyIndexA, int32 BodyIndexB) const;

	int32 FindBodyIndex(FName BodyName) const;
	int32 FindConstraintIndex(FName Bone1Name, FName Bone2Name) const;
	// 이 바디의 본을 Bone1이나 Bone2로 쓰는 Constraint 번호들
	void BodyFindConstraints(int32 BodyIndex, TArray<int32>& Constraints) const;
	void UpdateBodySetupIndexMap();

	using Super::Serialize;
	void Serialize(FStructuredArchive::FRecord Record) override;

private:
	TMap<FName, int32, FNameHash> BodySetupIndexMap;
};
