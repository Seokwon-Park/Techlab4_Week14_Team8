#pragma once

#include "Core/Types.h"
#include "Physics/PhysicsFiltering.h"
#include "CollisionProfile.generated.h"

// UE ECollisionResponse: 상대 카테고리(Object Type)에 대한 이 바디의 반응.
UENUM()
enum class ECollisionResponse : uint8
{
	Ignore,    // 무시
	Overlap,   // 밀어내지 않고 겹침(Begin/End Overlap)만 알림
	Block,     // 서로 밀어냄 (Hit). 양쪽 모두 Block일 때만 막음
};

// 채널별 응답 매트릭스 (UE FCollisionResponseContainer 대응)
USTRUCT()
struct FCollisionResponseContainer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	ECollisionResponse WorldStatic = ECollisionResponse::Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse WorldDynamic = ECollisionResponse::Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse Pawn = ECollisionResponse::Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse PhysicsBody = ECollisionResponse::Block;
	UPROPERTY(EditAnywhere)
	ECollisionResponse Vehicle = ECollisionResponse::Block;

	ECollisionResponse GetResponse(ECollisionCategory Category) const;
	void SetResponse(ECollisionCategory Category, ECollisionResponse Response);
	void SetAllResponses(ECollisionResponse Response);
	uint32 GetMask(ECollisionResponse Response) const;
};

// UE Collision Presets
UENUM()
enum class ECollisionPreset : uint8
{
	Custom,
	NoCollision,
	BlockAll,
	OverlapAll,
	BlockAllDynamic,
	OverlapAllDynamic,
	Pawn,
	PhysicsActor,
	Ragdoll,
	Vehicle,
	Trigger,
};

struct FCollisionPresetInfo
{
	ECollisionEnabled CollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	ECollisionCategory ObjectType = ECollisionCategory::WorldDynamic;
	FCollisionResponseContainer Responses;
	const char* Description = "";
};

namespace CollisionProfile
{
	// Custom이면 nullptr 반환
	const FCollisionPresetInfo* FindPreset(ECollisionPreset Preset);
}

// 필터 데이터 word3의 비트 8..15에 Overlap 마스크를 담는다 (상위 비트는 ContactReportBit / QueryOnlyShapeQueryBit 보존).
namespace CollisionFilterBits
{
	constexpr uint32 OverlapShift = 8;
	constexpr uint32 OverlapWidth = 8;
	constexpr uint32 OverlapFieldMask = ((1u << OverlapWidth) - 1u) << OverlapShift;

	constexpr uint32 PackOverlap(uint32 ExistingWord3, uint32 OverlapMask)
	{
		return (ExistingWord3 & ~OverlapFieldMask) | ((OverlapMask & ((1u << OverlapWidth) - 1u)) << OverlapShift);
	}

	constexpr uint32 ExtractOverlapMask(uint32 Word3)
	{
		return (Word3 & OverlapFieldMask) >> OverlapShift;
	}
}
