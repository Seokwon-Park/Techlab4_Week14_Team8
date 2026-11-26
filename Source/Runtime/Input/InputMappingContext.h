#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Input/InputCoreTypes.h"
#include "Input/InputModifiers.h"
#include "Input/InputTriggers.h"
#include "InputMappingContext.generated.h"

class UInputAction;

// UE의 FEnhancedActionKeyMapping. "이 키가 이 Action을 일으킨다" 한 줄.
// 키마다 Modifier(값 가공)와 Trigger(발동 조건)를 따로 붙일 수 있다.
struct FEnhancedActionKeyMapping
{
	UInputAction* Action = nullptr;
	FKey Key;

	std::vector<std::shared_ptr<FInputModifier>> Modifiers;
	std::vector<std::shared_ptr<FInputTrigger>> Triggers;

	template <typename T, typename... Args>
	FEnhancedActionKeyMapping& AddModifier(Args&&... InArgs)
	{
		Modifiers.push_back(std::make_shared<T>(std::forward<Args>(InArgs)...));
		return *this;
	}

	template <typename T, typename... Args>
	FEnhancedActionKeyMapping& AddTrigger(Args&&... InArgs)
	{
		Triggers.push_back(std::make_shared<T>(std::forward<Args>(InArgs)...));
		return *this;
	}
};

// UE의 UInputMappingContext. 상황(걷기, 차량, 메뉴...)별 키 매핑 묶음.
// PlayerController의 FEnhancedInputSubsystem에 우선순위와 함께 추가/제거해서 키 배치를 바꾼다.
UCLASS()
class UInputMappingContext : public UObject
{
	GENERATED_BODY()
public:
	// 매핑을 추가하고 참조를 돌려준다. 체이닝으로 Modifier/Trigger를 붙인다
	//   Context->MapKey(MoveAction, EKeyCode::S).AddModifier<FInputModifierSwizzleAxis>().AddModifier<FInputModifierNegate>();
	FEnhancedActionKeyMapping& MapKey(UInputAction* Action, FKey Key);

	// Action+Key가 같은 매핑을 모두 제거한다
	void UnmapKey(UInputAction* Action, FKey Key);
	void UnmapAction(UInputAction* Action);
	void UnmapAll() { Mappings.clear(); }

	const std::vector<FEnhancedActionKeyMapping>& GetMappings() const { return Mappings; }
	std::vector<FEnhancedActionKeyMapping>& GetMappings() { return Mappings; }

	static UInputMappingContext* Create(UObject* Outer, const char* Name);

	// 에셋에서 읽었다면 그 경로(Assets 상대 키)
	FString AssetPath;

private:
	std::vector<FEnhancedActionKeyMapping> Mappings;
};
