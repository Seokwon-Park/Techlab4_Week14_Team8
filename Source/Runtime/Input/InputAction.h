#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Input/InputActionValue.h"
#include "Input/InputModifiers.h"
#include "Input/InputTriggers.h"
#include "InputAction.generated.h"

// UE의 UInputAction. "점프", "이동"처럼 키와 무관한 동작 하나를 정의한다.
// 어떤 키가 이 동작을 일으키는지는 UInputMappingContext가 정한다.
UCLASS()
class UInputAction : public UObject
{
	GENERATED_BODY()
public:
	// 바인딩에 전달되는 값의 모양
	EInputActionValueType ValueType = EInputActionValueType::Boolean;

	// true면 이 Action이 쓴 키는 우선순위가 낮은 Context의 매핑에 전달되지 않는다
	bool bConsumeInput = true;

	// 매핑 값을 모두 합친 뒤 적용되는 Modifier / Trigger. 비어 있으면 Down 트리거로 동작한다
	std::vector<std::shared_ptr<FInputModifier>> Modifiers;
	std::vector<std::shared_ptr<FInputTrigger>> Triggers;

	template <typename T, typename... Args>
	T& AddModifier(Args&&... InArgs)
	{
		auto Modifier = std::make_shared<T>(std::forward<Args>(InArgs)...);
		T& Ref = *Modifier;
		Modifiers.push_back(std::move(Modifier));
		return Ref;
	}

	template <typename T, typename... Args>
	T& AddTrigger(Args&&... InArgs)
	{
		auto Trigger = std::make_shared<T>(std::forward<Args>(InArgs)...);
		T& Ref = *Trigger;
		Triggers.push_back(std::move(Trigger));
		return Ref;
	}

	// 에셋에서 읽었다면 그 경로(Assets 상대 키). 코드로 만든 Action은 비어 있다
	FString AssetPath;

	// 코드에서 Action을 만들 때 쓰는 헬퍼. 반환된 객체는 호출한 쪽이 delete로 해제한다
	static UInputAction* Create(UObject* Outer, const char* Name, EInputActionValueType InValueType);
};
