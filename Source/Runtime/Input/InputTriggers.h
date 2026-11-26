#pragma once

#include "Input/InputActionValue.h"

// 트리거 하나가 이번 프레임에 내린 판정
enum class ETriggerState : uint8
{
	None,		// 조건 불충족
	Ongoing,	// 진행 중이지만 아직 발동 전 (Hold 대기 등)
	Triggered,	// 발동
};

// 바인딩이 받는 이벤트. 이전/현재 ETriggerState 전이로 결정된다
//   None      -> Ongoing   : Started
//   None      -> Triggered : Started, Triggered
//   Ongoing   -> Ongoing   : Ongoing
//   *         -> Triggered : Triggered
//   Triggered -> Ongoing   : Ongoing
//   Ongoing   -> None      : Canceled
//   Triggered -> None      : Completed
enum class ETriggerEvent : uint8
{
	None = 0,
	Started = 1 << 0,
	Ongoing = 1 << 1,
	Triggered = 1 << 2,
	Completed = 1 << 3,
	Canceled = 1 << 4,
};

inline ETriggerEvent operator|(ETriggerEvent A, ETriggerEvent B) { return static_cast<ETriggerEvent>(static_cast<uint8>(A) | static_cast<uint8>(B)); }
inline bool HasTriggerEvent(ETriggerEvent Events, ETriggerEvent Flag) { return (static_cast<uint8>(Events) & static_cast<uint8>(Flag)) != 0; }

// UE의 UInputTrigger. 값이 "발동"인지 판정한다. 프레임 사이 상태(누른 시간 등)를 직접 들고 있으므로
// 매핑 하나(또는 Action 하나)마다 각자의 인스턴스를 가진다.
class FInputTrigger
{
public:
	virtual ~FInputTrigger() = default;

	ETriggerState UpdateState(const FInputActionValue& ModifiedValue, float DeltaTime)
	{
		const bool bActuated = IsActuated(ModifiedValue);
		const ETriggerState State = UpdateState_Implementation(bActuated, DeltaTime);
		bWasActuated = bActuated;
		return State;
	}

	virtual void Reset() { bWasActuated = false; }

	// 값의 크기가 이 이상이면 "눌림"으로 본다
	float ActuationThreshold = 0.5f;

protected:
	virtual ETriggerState UpdateState_Implementation(bool bActuated, float DeltaTime) = 0;

	bool IsActuated(const FInputActionValue& Value) const
	{
		// 마우스 축처럼 작은 값도 들어오므로 Threshold가 0이면 0이 아니기만 하면 눌림
		if (ActuationThreshold <= 0.0f) return Value.IsNonZero();
		return Value.GetMagnitudeSq() >= ActuationThreshold * ActuationThreshold;
	}

	bool bWasActuated = false;
};

// 누르고 있는 동안 매 프레임 Triggered. 트리거를 하나도 안 붙이면 이것이 기본으로 쓰인다
class FInputTriggerDown : public FInputTrigger
{
public:
	explicit FInputTriggerDown(float InThreshold = 0.5f) { ActuationThreshold = InThreshold; }
protected:
	ETriggerState UpdateState_Implementation(bool bActuated, float) override
	{
		return bActuated ? ETriggerState::Triggered : ETriggerState::None;
	}
};

// 눌린 프레임에 한 번 Triggered
class FInputTriggerPressed : public FInputTrigger
{
protected:
	ETriggerState UpdateState_Implementation(bool bActuated, float) override
	{
		return (bActuated && !bWasActuated) ? ETriggerState::Triggered : ETriggerState::None;
	}
};

// 누르는 동안 Ongoing, 뗀 프레임에 Triggered
class FInputTriggerReleased : public FInputTrigger
{
protected:
	ETriggerState UpdateState_Implementation(bool bActuated, float) override
	{
		if (bActuated) return ETriggerState::Ongoing;
		return bWasActuated ? ETriggerState::Triggered : ETriggerState::None;
	}
};

// HoldTimeThreshold초 이상 누르면 Triggered. 그 전에 떼면 Canceled가 된다.
// bIsOneShot이면 한 번만 발동하고, 아니면 그 뒤로 누르는 동안 계속 Triggered
class FInputTriggerHold : public FInputTrigger
{
public:
	explicit FInputTriggerHold(float InHoldTime = 0.5f, bool bInOneShot = false)
		: HoldTimeThreshold(InHoldTime), bIsOneShot(bInOneShot) {}

	void Reset() override
	{
		FInputTrigger::Reset();
		HeldDuration = 0.0f;
		bFired = false;
	}

	float HoldTimeThreshold;
	bool bIsOneShot;

protected:
	ETriggerState UpdateState_Implementation(bool bActuated, float DeltaTime) override
	{
		if (!bActuated)
		{
			HeldDuration = 0.0f;
			bFired = false;
			return ETriggerState::None;
		}

		HeldDuration += DeltaTime;
		if (HeldDuration < HoldTimeThreshold) return ETriggerState::Ongoing;
		if (bIsOneShot && bFired) return ETriggerState::None;
		bFired = true;
		return ETriggerState::Triggered;
	}

	float HeldDuration = 0.0f;
	bool bFired = false;
};
