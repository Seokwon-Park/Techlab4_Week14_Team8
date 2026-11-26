#include "EnginePCH.h"
#include "Physics/PhysicsFiltering.h"
#include "Physics/CollisionProfile.h"
#include <PxFiltering.h>

namespace PhysicsFiltering
{
	physx::PxFilterData ToPxFilterData(const FMaskFilter& Filter)
	{
		return physx::PxFilterData(Filter.Category, Filter.CollidesWith, Filter.OwnerId, Filter.Flags);
	}

	FMaskFilter FromPxFilterData(const physx::PxFilterData& Data)
	{
		return FMaskFilter{ Data.word0, Data.word1, Data.word2, Data.word3 };
	}

	bool IsOverlapOnlyPair(const physx::PxFilterData& Filter0, const physx::PxFilterData& Filter1)
	{
		const bool bBlock = (Filter0.word0 & Filter1.word1) != 0 && (Filter1.word0 & Filter0.word1) != 0;
		return !bBlock;
	}
}
