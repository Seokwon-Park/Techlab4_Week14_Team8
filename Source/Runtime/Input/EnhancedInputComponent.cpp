#include "EnginePCH.h"
#include "Input/EnhancedInputComponent.h"

#include <algorithm>

uint32 UEnhancedInputComponent::AddBinding(const UInputAction* Action, ETriggerEvent TriggerEvent,
	FEnhancedInputActionHandlerSignature Handler, const void* Owner)
{
	if (!Action || !Handler || TriggerEvent == ETriggerEvent::None) return 0;

	FEnhancedInputActionEventBinding& Binding = Bindings.emplace_back();
	Binding.Action = Action;
	Binding.TriggerEvent = TriggerEvent;
	Binding.Handler = std::move(Handler);
	Binding.Owner = Owner;
	Binding.Handle = NextHandle++;
	return Binding.Handle;
}

bool UEnhancedInputComponent::RemoveBindingByHandle(uint32 Handle)
{
	const auto It = std::find_if(Bindings.begin(), Bindings.end(),
		[Handle](const FEnhancedInputActionEventBinding& B) { return B.Handle == Handle; });
	if (It == Bindings.end()) return false;
	Bindings.erase(It);
	return true;
}

void UEnhancedInputComponent::RemoveBindingsForObject(const void* Owner)
{
	Bindings.erase(std::remove_if(Bindings.begin(), Bindings.end(),
		[Owner](const FEnhancedInputActionEventBinding& B) { return B.Owner == Owner; }),
		Bindings.end());
}

void UEnhancedInputComponent::DispatchActionEvent(const UInputAction* Action, ETriggerEvent Events, const FInputActionValue& Value)
{
	if (Bindings.empty()) return;

	// 핸들러 안에서 바인딩을 추가/제거할 수 있으므로 복사본을 순회한다
	const std::vector<FEnhancedInputActionEventBinding> Snapshot = Bindings;
	for (const FEnhancedInputActionEventBinding& Binding : Snapshot)
	{
		if (Binding.Action == Action && HasTriggerEvent(Events, Binding.TriggerEvent))
		{
			Binding.Handler(Value);
		}
	}
}
