#include "EnginePCH.h"
#include "Input/EnhancedInputSubsystem.h"
#include "Input/InputAction.h"
#include "Input/InputMappingContext.h"
#include "Input/EnhancedInputComponent.h"

#include <algorithm>
#include <cstring>

namespace
{
	ETriggerState MaxState(ETriggerState A, ETriggerState B) { return static_cast<uint8>(A) > static_cast<uint8>(B) ? A : B; }
	ETriggerState MinState(ETriggerState A, ETriggerState B) { return static_cast<uint8>(A) < static_cast<uint8>(B) ? A : B; }

	// 트리거가 하나도 없으면 값이 0이 아닐 때 Triggered (UE와 같은 암시적 Down)
	ETriggerState EvaluateTriggers(const std::vector<std::shared_ptr<FInputTrigger>>& Triggers, const FInputActionValue& Value, float DeltaTime)
	{
		if (Triggers.empty())
			return Value.IsNonZero() ? ETriggerState::Triggered : ETriggerState::None;

		ETriggerState Result = ETriggerState::None;
		// 모든 트리거가 내부 상태를 갱신해야 하므로 중간에 멈추지 않는다
		for (const auto& Trigger : Triggers)
		{
			if (Trigger) Result = MaxState(Result, Trigger->UpdateState(Value, DeltaTime));
		}
		return Result;
	}

	FInputActionValue ApplyModifiers(const std::vector<std::shared_ptr<FInputModifier>>& Modifiers, FInputActionValue Value,
		EInputActionValueType ValueType, float DeltaTime)
	{
		for (const auto& Modifier : Modifiers)
		{
			if (!Modifier) continue;
			Value = Modifier->ModifyRaw(Value, DeltaTime);
			Value.ConvertTo(ValueType);
		}
		return Value;
	}

	void ResetTriggers(const std::vector<std::shared_ptr<FInputTrigger>>& Triggers)
	{
		for (const auto& Trigger : Triggers)
			if (Trigger) Trigger->Reset();
	}

	void ResetContextTriggers(UInputMappingContext* Context)
	{
		if (!Context) return;
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
			ResetTriggers(Mapping.Triggers);
	}

	// 이전/현재 트리거 상태 → 이번 프레임 이벤트
	ETriggerEvent GetTriggerEvents(ETriggerState Prev, ETriggerState Curr)
	{
		using S = ETriggerState;
		using E = ETriggerEvent;
		switch (Prev)
		{
		case S::None:
			if (Curr == S::Ongoing) return E::Started;
			if (Curr == S::Triggered) return E::Started | E::Triggered;
			return E::None;
		case S::Ongoing:
			if (Curr == S::Ongoing) return E::Ongoing;
			if (Curr == S::Triggered) return E::Triggered;
			return E::Canceled;
		case S::Triggered:
			if (Curr == S::Triggered) return E::Triggered;
			if (Curr == S::Ongoing) return E::Ongoing;
			return E::Completed;
		}
		return E::None;
	}

	void Dispatch(const std::vector<UEnhancedInputComponent*>& InputStack, const UInputAction* Action, ETriggerEvent Events, const FInputActionValue& Value)
	{
		if (Events == ETriggerEvent::None) return;
		for (UEnhancedInputComponent* Component : InputStack)
		{
			if (Component) Component->DispatchActionEvent(Action, Events, Value);
		}
	}
}

void FEnhancedInputSubsystem::AddMappingContext(UInputMappingContext* Context, int32 Priority)
{
	if (!Context) return;

	// 이미 있으면 우선순위만 바꾼다
	const auto It = std::find_if(AppliedContexts.begin(), AppliedContexts.end(),
		[Context](const FAppliedContext& A) { return A.Context == Context; });
	if (It != AppliedContexts.end())
	{
		It->Priority = Priority;
	}
	else
	{
		ResetContextTriggers(Context);
		AppliedContexts.push_back({ Context, Priority });
	}

	// 높은 우선순위가 앞. 같으면 먼저 추가한 쪽이 앞
	std::stable_sort(AppliedContexts.begin(), AppliedContexts.end(),
		[](const FAppliedContext& A, const FAppliedContext& B) { return A.Priority > B.Priority; });
}

void FEnhancedInputSubsystem::RemoveMappingContext(UInputMappingContext* Context)
{
	const auto It = std::remove_if(AppliedContexts.begin(), AppliedContexts.end(),
		[Context](const FAppliedContext& A) { return A.Context == Context; });
	if (It == AppliedContexts.end()) return;
	AppliedContexts.erase(It, AppliedContexts.end());
	ResetContextTriggers(Context);
}

void FEnhancedInputSubsystem::ClearAllMappings()
{
	for (const FAppliedContext& Applied : AppliedContexts)
		ResetContextTriggers(Applied.Context);
	AppliedContexts.clear();
}

bool FEnhancedInputSubsystem::HasMappingContext(const UInputMappingContext* Context) const
{
	return std::any_of(AppliedContexts.begin(), AppliedContexts.end(),
		[Context](const FAppliedContext& A) { return A.Context == Context; });
}

void FEnhancedInputSubsystem::InputKey(FKey Key, bool bDown)
{
	if (Key.Code <= 0 || Key.Code >= FKey::AxisKeyBegin) return;
	RawValues[Key.Code] = bDown ? 1.0f : 0.0f;
}

void FEnhancedInputSubsystem::InputAxis(FKey Key, float Delta)
{
	if (!Key.IsAxis() || Key.Code >= EKeys::NumKeys) return;
	// 한 프레임에 여러 번 들어와도 합산한다. ProcessInput이 끝나면 0으로 돌아간다
	RawValues[Key.Code] += Delta;
}

bool FEnhancedInputSubsystem::IsKeyDown(FKey Key) const
{
	if (Key.Code <= 0 || Key.Code >= EKeys::NumKeys) return false;
	return RawValues[Key.Code] != 0.0f;
}

void FEnhancedInputSubsystem::FlushPressedKeys()
{
	std::memset(RawValues, 0, sizeof(RawValues));
}

void FEnhancedInputSubsystem::ProcessInput(float DeltaTime, const std::vector<UEnhancedInputComponent*>& InputStack)
{
	// 이번 프레임에 Action별로 모은 값
	struct FFrameAction
	{
		FInputActionValue Value;
		ETriggerState MappingState = ETriggerState::None;	// 모든 매핑의 최대 상태 (트리거 없는 매핑 포함)
		ETriggerState ExplicitState = ETriggerState::None;	// 트리거를 직접 붙인 매핑들만의 최대 상태
		bool bHasExplicitTriggers = false;
	};
	std::unordered_map<const UInputAction*, FFrameAction> Frame;

	bool Consumed[EKeys::NumKeys] = {};

	// 1. 우선순위 높은 Context부터 매핑 평가
	for (const FAppliedContext& Applied : AppliedContexts)
	{
		if (!Applied.Context) continue;

		// 같은 Context 안의 매핑끼리는 키를 공유할 수 있어야 하므로 소비는 Context 단위로 반영한다
		std::vector<int32> KeysToConsume;
		for (const FEnhancedActionKeyMapping& Mapping : Applied.Context->GetMappings())
		{
			const UInputAction* Action = Mapping.Action;
			const int32 Code = Mapping.Key.Code;
			if (!Action || Code <= 0 || Code >= EKeys::NumKeys) continue;

			auto [It, bInserted] = Frame.try_emplace(Action);
			FFrameAction& FrameAction = It->second;
			if (bInserted)
			{
				FrameAction.Value = FInputActionValue(Action->ValueType, FVector(0.0f, 0.0f, 0.0f));
				if (std::find(ActionOrder.begin(), ActionOrder.end(), Action) == ActionOrder.end())
					ActionOrder.push_back(Action);
			}

			// 상위 Context가 가져간 키는 0으로 평가해서 트리거 상태가 자연스럽게 풀리게 한다
			const float Raw = Consumed[Code] ? 0.0f : RawValues[Code];
			FInputActionValue Value(Action->ValueType, FVector(Raw, 0.0f, 0.0f));
			Value = ApplyModifiers(Mapping.Modifiers, Value, Action->ValueType, DeltaTime);

			const ETriggerState State = EvaluateTriggers(Mapping.Triggers, Value, DeltaTime);
			FrameAction.Value += Value;
			FrameAction.MappingState = MaxState(FrameAction.MappingState, State);
			if (!Mapping.Triggers.empty())
			{
				FrameAction.bHasExplicitTriggers = true;
				FrameAction.ExplicitState = MaxState(FrameAction.ExplicitState, State);
			}

			if (Action->bConsumeInput) KeysToConsume.push_back(Code);
		}

		for (int32 Code : KeysToConsume) Consumed[Code] = true;
	}

	// 2. Action 단위로 최종 상태를 정하고 이벤트를 보낸다
	std::vector<const UInputAction*> StaleActions;
	const std::vector<const UInputAction*> Order = ActionOrder;
	for (const UInputAction* Action : Order)
	{
		FInputActionInstance& Instance = ActionInstances[Action];
		const ETriggerState PrevState = Instance.TriggerState;

		const auto FrameIt = Frame.find(Action);
		if (FrameIt == Frame.end())
		{
			// 이 Action을 가진 Context가 빠졌다. Action 객체가 이미 지워졌을 수 있으므로 역참조하지 않는다
			Instance.Value.Reset();
			Instance.TriggerState = ETriggerState::None;
			Instance.LastEvents = GetTriggerEvents(PrevState, ETriggerState::None);
			Dispatch(InputStack, Action, Instance.LastEvents, Instance.Value);
			StaleActions.push_back(Action);
			continue;
		}

		const FFrameAction& FrameAction = FrameIt->second;
		const FInputActionValue Value = ApplyModifiers(Action->Modifiers, FrameAction.Value, Action->ValueType, DeltaTime);

		ETriggerState State = FrameAction.MappingState;
		if (!Action->Triggers.empty())
		{
			// Action에 트리거가 있으면 합산 값으로 판정하고, 매핑에 직접 붙인 트리거가 있다면 둘 다 만족해야 한다
			State = EvaluateTriggers(Action->Triggers, Value, DeltaTime);
			if (FrameAction.bHasExplicitTriggers) State = MinState(State, FrameAction.ExplicitState);
		}

		Instance.Value = Value;
		Instance.TriggerState = State;
		Instance.LastEvents = GetTriggerEvents(PrevState, State);
		Instance.ElapsedTriggeredTime = (State == ETriggerState::Triggered) ? Instance.ElapsedTriggeredTime + DeltaTime : 0.0f;

		Dispatch(InputStack, Action, Instance.LastEvents, Value);
	}

	for (const UInputAction* Action : StaleActions)
	{
		ActionInstances.erase(Action);
		ActionOrder.erase(std::remove(ActionOrder.begin(), ActionOrder.end(), Action), ActionOrder.end());
	}

	// 3. 마우스 축은 프레임 이동량이므로 매 프레임 비운다
	for (int32 Code = FKey::AxisKeyBegin; Code < EKeys::NumKeys; ++Code)
		RawValues[Code] = 0.0f;
}

FInputActionValue FEnhancedInputSubsystem::GetActionValue(const UInputAction* Action) const
{
	const auto It = ActionInstances.find(Action);
	return It != ActionInstances.end() ? It->second.Value : FInputActionValue();
}

const FInputActionInstance* FEnhancedInputSubsystem::FindActionInstance(const UInputAction* Action) const
{
	const auto It = ActionInstances.find(Action);
	return It != ActionInstances.end() ? &It->second : nullptr;
}
