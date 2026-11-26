#pragma once

#include <unordered_map>
#include <vector>

#include "Core/Types.h"
#include "Input/InputCoreTypes.h"
#include "Input/InputActionValue.h"
#include "Input/InputTriggers.h"

class UInputAction;
class UInputMappingContext;
class UEnhancedInputComponent;

// Action 하나의 프레임 간 상태
struct FInputActionInstance
{
	FInputActionValue Value;
	ETriggerState TriggerState = ETriggerState::None;
	ETriggerEvent LastEvents = ETriggerEvent::None;
	float ElapsedTriggeredTime = 0.0f;	// Triggered가 이어진 시간
};

// UE의 UEnhancedInputLocalPlayerSubsystem + UEnhancedPlayerInput 역할.
// PlayerController 하나가 하나씩 갖는다.
//  - 원시 키/축 값을 받아 두고 (InputKey / InputAxis)
//  - 적용된 Mapping Context를 우선순위 순으로 평가해 Action 값과 이벤트를 만든 뒤
//  - 입력 스택(UEnhancedInputComponent들)의 바인딩을 호출한다
class FEnhancedInputSubsystem
{
public:
	// Priority가 높은 Context가 먼저 평가되고, bConsumeInput Action이 쓴 키는 낮은 Context에 전달되지 않는다
	void AddMappingContext(UInputMappingContext* Context, int32 Priority = 0);
	void RemoveMappingContext(UInputMappingContext* Context);
	void ClearAllMappings();
	bool HasMappingContext(const UInputMappingContext* Context) const;

	// 원시 입력
	void InputKey(FKey Key, bool bDown);
	void InputAxis(FKey Key, float Delta);
	bool IsKeyDown(FKey Key) const;
	// 포커스를 잃었을 때: 모든 키를 뗀 상태로 만든다. 다음 ProcessInput에서 Completed가 한 번 나간다
	void FlushPressedKeys();

	// 매 프레임 PlayerController::Tick에서 호출. InputStack의 앞쪽부터 바인딩을 호출한다
	void ProcessInput(float DeltaTime, const std::vector<UEnhancedInputComponent*>& InputStack);

	// 바인딩 없이 현재 값을 읽고 싶을 때 (UE의 GetActionValue)
	FInputActionValue GetActionValue(const UInputAction* Action) const;
	const FInputActionInstance* FindActionInstance(const UInputAction* Action) const;

private:
	struct FAppliedContext
	{
		UInputMappingContext* Context = nullptr;
		int32 Priority = 0;
	};

	float RawValues[EKeys::NumKeys] = {};
	std::vector<FAppliedContext> AppliedContexts;
	std::unordered_map<const UInputAction*, FInputActionInstance> ActionInstances;
	// 이벤트를 결정적인 순서로 보내기 위해 처음 본 순서를 기억한다
	std::vector<const UInputAction*> ActionOrder;
};
