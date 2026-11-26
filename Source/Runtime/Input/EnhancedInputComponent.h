#pragma once

#include <functional>
#include <vector>

#include "Components/ActorComponent.h"
#include "Input/InputActionValue.h"
#include "Input/InputTriggers.h"
#include "EnhancedInputComponent.generated.h"

class UInputAction;

using FEnhancedInputActionHandlerSignature = std::function<void(const FInputActionValue&)>;

// BindAction 한 건
struct FEnhancedInputActionEventBinding
{
	const UInputAction* Action = nullptr;
	ETriggerEvent TriggerEvent = ETriggerEvent::Triggered;
	FEnhancedInputActionHandlerSignature Handler;
	const void* Owner = nullptr;	// RemoveBindingsForObject용
	uint32 Handle = 0;
};

// UE의 UEnhancedInputComponent. Action의 이벤트(Started/Triggered/Completed...)에 함수를 묶는다.
// PlayerController가 자기 것 하나, 빙의한 Pawn용 하나를 만들어 입력 스택으로 사용한다.
//   Input->BindAction(JumpAction, ETriggerEvent::Started, this, &AMyCharacter::OnJump);
UCLASS()
class UEnhancedInputComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	// void Func(const FInputActionValue&)
	template <typename T>
	uint32 BindAction(const UInputAction* Action, ETriggerEvent TriggerEvent, T* Object, void (T::*Func)(const FInputActionValue&))
	{
		if (!Object || !Func) return 0;
		return AddBinding(Action, TriggerEvent, [Object, Func](const FInputActionValue& Value) { (Object->*Func)(Value); }, Object);
	}

	// void Func() - 값이 필요 없는 버튼용
	template <typename T>
	uint32 BindAction(const UInputAction* Action, ETriggerEvent TriggerEvent, T* Object, void (T::*Func)())
	{
		if (!Object || !Func) return 0;
		return AddBinding(Action, TriggerEvent, [Object, Func](const FInputActionValue&) { (Object->*Func)(); }, Object);
	}

	// 람다 / 일반 함수
	uint32 BindAction(const UInputAction* Action, ETriggerEvent TriggerEvent, FEnhancedInputActionHandlerSignature Handler, const void* Owner = nullptr)
	{
		return AddBinding(Action, TriggerEvent, std::move(Handler), Owner);
	}

	bool RemoveBindingByHandle(uint32 Handle);
	void RemoveBindingsForObject(const void* Owner);
	void ClearActionBindings() { Bindings.clear(); }
	bool HasBindings() const { return !Bindings.empty(); }

	// FEnhancedInputSubsystem이 Action 이벤트가 생겼을 때 호출한다
	void DispatchActionEvent(const UInputAction* Action, ETriggerEvent Events, const FInputActionValue& Value);

	const std::vector<FEnhancedInputActionEventBinding>& GetActionEventBindings() const { return Bindings; }

private:
	uint32 AddBinding(const UInputAction* Action, ETriggerEvent TriggerEvent, FEnhancedInputActionHandlerSignature Handler, const void* Owner);

	std::vector<FEnhancedInputActionEventBinding> Bindings;
	uint32 NextHandle = 1;
};
