#pragma once

#include "Core/Types.h"
#include "UObject/ObjectMacros.h"
#include "PhysicsFiltering.generated.h"

// 바디의 충돌 채널(카테고리). 한 비트씩 사용해 word0(내 종류)·word1(차단 대상)에 OR로 담는다.
UENUM()
enum class ECollisionCategory : uint8
{
	WorldStatic,    // 바닥, 벽
	WorldDynamic,   // 상자 등 일반 물체
	Pawn,           // 캐릭터 캡슐
	PhysicsBody,    // 랙돌 본 / 시뮬레이션 바디
	Vehicle,        // 차체
	Count,
};

constexpr uint32 CategoryBit(ECollisionCategory Category) { return 1u << static_cast<uint32>(Category); }
constexpr uint32 AllCategoriesMask = (1u << static_cast<uint32>(ECollisionCategory::Count)) - 1u;

// word3 하위 플래그 비트
namespace ECollisionFlags
{
	constexpr uint32 Drivable = 1u << 0;   // 차량 서스펜션 레이캐스트가 바퀴를 올릴 수 있는 표면
}

// UE ECollisionEnabled: 이 바디가 어떤 충돌에 참여하는지 지정한다.
UENUM()
enum class ECollisionEnabled : uint8
{
	NoCollision,       // 바디 충돌/쿼리 모두 비활성화
	QueryOnly,         // 레이캐스트·스윕·오버랩만 참여 (물리 충돌 없음)
	PhysicsOnly,       // 물리 충돌만 참여 (쿼리 제외)
	QueryAndPhysics,   // 쿼리 + 물리 모두 참여 (기본)
};

// PxFilterData 대응 구조체
struct FMaskFilter
{
	uint32 Category = 0;      // word0: 이 바디의 종류 (CategoryBit 하나)
	uint32 CollidesWith = 0;  // word1: Block 대상 카테고리 비트 OR
	uint32 OwnerId = 0;       // word2: 0이 아니고 서로 같으면 같은 랙돌 소유자
	uint32 Flags = 0;         // word3: 하위 플래그 + 상위 Overlap 마스크
};
