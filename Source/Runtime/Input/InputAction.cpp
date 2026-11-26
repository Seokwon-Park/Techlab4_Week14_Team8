#include "EnginePCH.h"
#include "Input/InputAction.h"
#include "UObject/UObjectGlobals.h"

UInputAction* UInputAction::Create(UObject* Outer, const char* Name, EInputActionValueType InValueType)
{
	// 같은 Outer 아래 같은 이름이면 NewObject가 기존 객체를 덮어쓰므로 고유 이름을 만든다
	const FName UniqueName = MakeUniqueObjectName(Outer, StaticClass(), FName(Name));
	UInputAction* Action = NewObject<UInputAction>(Outer, UniqueName);
	if (Action) Action->ValueType = InValueType;
	return Action;
}
