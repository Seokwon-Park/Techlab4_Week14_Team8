#include "EnginePCH.h"
#include "Input/InputMappingContext.h"
#include "UObject/UObjectGlobals.h"

#include <algorithm>

FEnhancedActionKeyMapping& UInputMappingContext::MapKey(UInputAction* Action, FKey Key)
{
	FEnhancedActionKeyMapping& Mapping = Mappings.emplace_back();
	Mapping.Action = Action;
	Mapping.Key = Key;
	return Mapping;
}

void UInputMappingContext::UnmapKey(UInputAction* Action, FKey Key)
{
	Mappings.erase(std::remove_if(Mappings.begin(), Mappings.end(),
		[Action, Key](const FEnhancedActionKeyMapping& M) { return M.Action == Action && M.Key == Key; }),
		Mappings.end());
}

void UInputMappingContext::UnmapAction(UInputAction* Action)
{
	Mappings.erase(std::remove_if(Mappings.begin(), Mappings.end(),
		[Action](const FEnhancedActionKeyMapping& M) { return M.Action == Action; }),
		Mappings.end());
}

UInputMappingContext* UInputMappingContext::Create(UObject* Outer, const char* Name)
{
	const FName UniqueName = MakeUniqueObjectName(Outer, StaticClass(), FName(Name));
	return NewObject<UInputMappingContext>(Outer, UniqueName);
}
