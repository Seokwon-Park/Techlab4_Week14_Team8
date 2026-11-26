#include "EnginePCH.h"
#include "Physics/CollisionProfile.h"

ECollisionResponse FCollisionResponseContainer::GetResponse(ECollisionCategory Category) const
{
	switch (Category)
	{
	case ECollisionCategory::WorldStatic:  return WorldStatic;
	case ECollisionCategory::WorldDynamic: return WorldDynamic;
	case ECollisionCategory::Pawn:         return Pawn;
	case ECollisionCategory::PhysicsBody:  return PhysicsBody;
	case ECollisionCategory::Vehicle:      return Vehicle;
	default:                               return ECollisionResponse::Ignore;
	}
}

void FCollisionResponseContainer::SetResponse(ECollisionCategory Category, ECollisionResponse Response)
{
	switch (Category)
	{
	case ECollisionCategory::WorldStatic:  WorldStatic = Response; break;
	case ECollisionCategory::WorldDynamic: WorldDynamic = Response; break;
	case ECollisionCategory::Pawn:         Pawn = Response; break;
	case ECollisionCategory::PhysicsBody:  PhysicsBody = Response; break;
	case ECollisionCategory::Vehicle:      Vehicle = Response; break;
	default: break;
	}
}

void FCollisionResponseContainer::SetAllResponses(ECollisionResponse Response)
{
	for (uint32 Index = 0; Index < static_cast<uint32>(ECollisionCategory::Count); ++Index)
		SetResponse(static_cast<ECollisionCategory>(Index), Response);
}

uint32 FCollisionResponseContainer::GetMask(ECollisionResponse Response) const
{
	uint32 Mask = 0;
	for (uint32 Index = 0; Index < static_cast<uint32>(ECollisionCategory::Count); ++Index)
	{
		const ECollisionCategory Category = static_cast<ECollisionCategory>(Index);
		if (GetResponse(Category) == Response)
			Mask |= CategoryBit(Category);
	}
	return Mask;
}

namespace
{
	FCollisionPresetInfo MakePreset(ECollisionEnabled Enabled, ECollisionCategory ObjectType, ECollisionResponse All, const char* Description)
	{
		FCollisionPresetInfo Info;
		Info.CollisionEnabled = Enabled;
		Info.ObjectType = ObjectType;
		Info.Responses.SetAllResponses(All);
		Info.Description = Description;
		return Info;
	}

	struct FPresetTable
	{
		FCollisionPresetInfo Presets[static_cast<int32>(ECollisionPreset::Trigger) + 1];

		FPresetTable()
		{
			using enum ECollisionPreset;
			auto Set = [this](ECollisionPreset Preset, const FCollisionPresetInfo& Info)
			{
				Presets[static_cast<int32>(Preset)] = Info;
			};

			Set(NoCollision, MakePreset(ECollisionEnabled::NoCollision, ECollisionCategory::WorldStatic, ECollisionResponse::Ignore,
				"No collision."));
			Set(BlockAll, MakePreset(ECollisionEnabled::QueryAndPhysics, ECollisionCategory::WorldStatic, ECollisionResponse::Block,
				"WorldStatic object that blocks all actors by default."));
			Set(OverlapAll, MakePreset(ECollisionEnabled::QueryOnly, ECollisionCategory::WorldStatic, ECollisionResponse::Overlap,
				"WorldStatic object that overlaps all actors by default."));
			Set(BlockAllDynamic, MakePreset(ECollisionEnabled::QueryAndPhysics, ECollisionCategory::WorldDynamic, ECollisionResponse::Block,
				"WorldDynamic object that blocks all actors by default."));
			Set(OverlapAllDynamic, MakePreset(ECollisionEnabled::QueryOnly, ECollisionCategory::WorldDynamic, ECollisionResponse::Overlap,
				"WorldDynamic object that overlaps all actors by default."));
			Set(Pawn, MakePreset(ECollisionEnabled::QueryAndPhysics, ECollisionCategory::Pawn, ECollisionResponse::Block,
				"Pawn object. Can be used for capsule of any playable character or AI."));
			Set(PhysicsActor, MakePreset(ECollisionEnabled::QueryAndPhysics, ECollisionCategory::PhysicsBody, ECollisionResponse::Block,
				"Simulating actors."));

			FCollisionPresetInfo RagdollInfo = MakePreset(ECollisionEnabled::QueryAndPhysics, ECollisionCategory::PhysicsBody, ECollisionResponse::Block,
				"Simulating skeletal mesh bodies. Ignores pawn capsules.");
			RagdollInfo.Responses.Pawn = ECollisionResponse::Ignore;
			Set(ECollisionPreset::Ragdoll, RagdollInfo);

			Set(Vehicle, MakePreset(ECollisionEnabled::QueryAndPhysics, ECollisionCategory::Vehicle, ECollisionResponse::Block,
				"Vehicle object that blocks everything."));
			Set(Trigger, MakePreset(ECollisionEnabled::QueryOnly, ECollisionCategory::WorldDynamic, ECollisionResponse::Overlap,
				"WorldDynamic object used for trigger volumes. Overlaps everything."));
		}
	};
}

const FCollisionPresetInfo* CollisionProfile::FindPreset(ECollisionPreset Preset)
{
	static const FPresetTable Table;
	const int32 Index = static_cast<int32>(Preset);
	if (Preset == ECollisionPreset::Custom || Index < 0 || Index > static_cast<int32>(ECollisionPreset::Trigger))
		return nullptr;
	return &Table.Presets[Index];
}
